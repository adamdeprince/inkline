/*
 * Copyright (c) 2026 Adam DePrince <adam.deprince@gmail.com>
 *
 * Input methods. A method composes keystrokes into a preedit and commits
 * Unicode into the focused window, so entry is identical on every host
 * terminal (the point of doing it in the multiplexer).
 *
 *   Romaji          algorithmic romaji -> hiragana (no data table)
 *   US-International dead key (' ` ^ " ~) + letter -> accented latin
 *   Pinyin / Zhuyin a syllable -> hanzi candidates (share/pinyin.dict)
 *   Wubi            wubi86 shape code -> hanzi/words (share/wubi.dict)
 *
 * The pinyin and wubi tables are built by tools/gen-imdata.py from
 * permissively-licensed sources (mozillazg/pinyin-data, MIT, over Unicode
 * Unihan; KyleBing/rime-wubi86-jidian, Apache-2.0) -- see share/README.md.
 * They load at runtime, so the binary stays small and the data is
 * replaceable.
 */

// Adapted for Inkline: bounded, owned dictionaries; Qt input routing and
// composition display; no multiplexer or on-disk input history.
#include "rmt/input_method.hpp"
#include <QCoreApplication>
#include <QFile>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <stdexcept>
namespace rmt {
namespace {
using dv_buf = QByteArray;
void dv_buf_append(dv_buf *out, const void *data, size_t len) {
    out->append(static_cast<const char *>(data), qsizetype(len));
}
enum { DV_IM_OFF, DV_IM_ROMAJI, DV_IM_USINTL, DV_IM_PINYIN, DV_IM_ZHUYIN, DV_IM_WUBI };
struct dv_im {
    int method = 0;
    char pre[48]{};
    int prelen = 0;
    char cand[10][64]{};
    int ncand = 0, page = 0, dead = 0;
};
#define CANDS_PER_PAGE	9	/* selectable with keys 1-9 */

static void
commit_cp(dv_buf *out, uint32_t cp)
{
	unsigned char	u[4];
	size_t		n;

	if (cp < 0x80) {
		u[0] = (unsigned char)cp;
		n = 1;
	} else if (cp < 0x800) {
		u[0] = 0xc0 | (cp >> 6);
		u[1] = 0x80 | (cp & 0x3f);
		n = 2;
	} else if (cp < 0x10000) {
		u[0] = 0xe0 | (cp >> 12);
		u[1] = 0x80 | ((cp >> 6) & 0x3f);
		u[2] = 0x80 | (cp & 0x3f);
		n = 3;
	} else {
		u[0] = 0xf0 | (cp >> 18);
		u[1] = 0x80 | ((cp >> 12) & 0x3f);
		u[2] = 0x80 | ((cp >> 6) & 0x3f);
		u[3] = 0x80 | (cp & 0x3f);
		n = 4;
	}
	dv_buf_append(out, u, n);
}

static void
commit_str(dv_buf *out, const char *s)
{
	dv_buf_append(out, s, strlen(s));
}

void
dv_im_reset(struct dv_im *im)
{
	im->prelen = 0;
	im->pre[0] = '\0';
	im->ncand = 0;
	im->page = 0;
	im->dead = 0;
}

const char *
dv_im_name(int method)
{
	switch (method) {
	case DV_IM_ROMAJI:
		return ("Romaji");
	case DV_IM_USINTL:
		return ("US-Intl");
	case DV_IM_PINYIN:
		return ("Pinyin");
	case DV_IM_ZHUYIN:
		return ("Zhuyin");
	case DV_IM_WUBI:
		return ("Wubi");
	default:
		return ("Off");
	}
}

/* ---- Runtime dictionary: key -> space-separated candidate blob --------- */

struct immap {
    std::unordered_map<std::string, std::string> entries;
    bool loaded = false;
};
static immap py_map, wb_map;
static const char *im_get(immap *m, const char *key) {
    const auto found = m->entries.find(key);
    return found == m->entries.end() ? nullptr : found->second.c_str();
}
static void im_load(immap *m, const char *name, size_t) {
    if (m->loaded) return;
    QFile file(QCoreApplication::applicationDirPath() + "/assets/input-methods/" + name);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 4 * 1024 * 1024)
        throw std::runtime_error("Input dictionary is missing or too large; reinstall Inkline");
    std::unordered_map<std::string, std::string> entries;
    while (!file.atEnd()) {
        QByteArray line = file.readLine(16384);
        while (line.endsWith('\n') || line.endsWith('\r')) line.chop(1);
        if (line.startsWith('#') || line.isEmpty()) continue;
        const auto tab = line.indexOf('\t');
        if (tab < 1 || tab > 6 || line.size() >= 16383 || entries.size() >= 262144)
            throw std::runtime_error("Invalid input dictionary");
        if (tab + 1 == line.size()) continue;
        entries.emplace(line.left(tab).toStdString(), line.mid(tab + 1).toStdString());
    }
    m->entries = std::move(entries);
    m->loaded = true;
}

/* Fill cand[] with page `im->page` of the space-separated blob. */
static void
fill_page(struct dv_im *im, const char *blob)
{
	int		skip = im->page * CANDS_PER_PAGE;
	const char	*p = blob;

	im->ncand = 0;
	if (blob == nullptr)
		return;
	while (*p != '\0' && im->ncand < CANDS_PER_PAGE) {
		const char	*sp = strchr(p, ' ');
		const size_t full_len = sp ? (size_t)(sp - p) : strlen(p);
        size_t len = full_len;

		if (skip > 0)
			skip--;
		else {
			if (len >= sizeof im->cand[0]) {
				len = sizeof im->cand[0] - 1;
				while (len && ((unsigned char)p[len] & 0xc0) == 0x80) --len;
			}
			memcpy(im->cand[im->ncand], p, len);
			im->cand[im->ncand][len] = '\0';
			im->ncand++;
		}
		p = sp ? sp + 1 : p + full_len;
	}
}

static int
has_more_pages(struct dv_im *im, const char *blob)
{
	int		limit = (im->page + 1) * CANDS_PER_PAGE, n = 0;
	const char	*p = blob;

	if (blob == nullptr)
		return (0);
	while (*p != '\0') {
		const char	*sp = strchr(p, ' ');

		if (++n > limit)
			return (1);
		if (sp == nullptr)
			break;
		p = sp + 1;
	}
	return (0);
}

static void
lookup(struct dv_im *im, struct immap *m)
{
	fill_page(im, im_get(m, im->pre));
}

/* ---- Romaji -> Hiragana (algorithmic) ---------------------------------- */

struct romaji {
	const char	*r;
	const char	*kana;
};

static const struct romaji romaji_tab[] = {
	{ "kya", "きゃ" }, { "kyu", "きゅ" }, { "kyo", "きょ" },
	{ "sha", "しゃ" }, { "shu", "しゅ" }, { "sho", "しょ" }, { "shi", "し" },
	{ "cha", "ちゃ" }, { "chu", "ちゅ" }, { "cho", "ちょ" }, { "chi", "ち" },
	{ "tsu", "つ" },
	{ "nya", "にゃ" }, { "nyu", "にゅ" }, { "nyo", "にょ" },
	{ "hya", "ひゃ" }, { "hyu", "ひゅ" }, { "hyo", "ひょ" },
	{ "mya", "みゃ" }, { "myu", "みゅ" }, { "myo", "みょ" },
	{ "rya", "りゃ" }, { "ryu", "りゅ" }, { "ryo", "りょ" },
	{ "gya", "ぎゃ" }, { "gyu", "ぎゅ" }, { "gyo", "ぎょ" },
	{ "ja",  "じゃ" }, { "ju",  "じゅ" }, { "jo",  "じょ" }, { "ji", "じ" },
	{ "bya", "びゃ" }, { "byu", "びゅ" }, { "byo", "びょ" },
	{ "pya", "ぴゃ" }, { "pyu", "ぴゅ" }, { "pyo", "ぴょ" },
	{ "ka", "か" }, { "ki", "き" }, { "ku", "く" }, { "ke", "け" }, { "ko", "こ" },
	{ "sa", "さ" }, { "su", "す" }, { "se", "せ" }, { "so", "そ" },
	{ "ta", "た" }, { "te", "て" }, { "to", "と" },
	{ "na", "な" }, { "ni", "に" }, { "nu", "ぬ" }, { "ne", "ね" }, { "no", "の" },
	{ "ha", "は" }, { "hi", "ひ" }, { "fu", "ふ" }, { "he", "へ" }, { "ho", "ほ" },
	{ "ma", "ま" }, { "mi", "み" }, { "mu", "む" }, { "me", "め" }, { "mo", "も" },
	{ "ya", "や" }, { "yu", "ゆ" }, { "yo", "よ" },
	{ "ra", "ら" }, { "ri", "り" }, { "ru", "る" }, { "re", "れ" }, { "ro", "ろ" },
	{ "wa", "わ" }, { "wo", "を" },
	{ "ga", "が" }, { "gi", "ぎ" }, { "gu", "ぐ" }, { "ge", "げ" }, { "go", "ご" },
	{ "za", "ざ" }, { "zu", "ず" }, { "ze", "ぜ" }, { "zo", "ぞ" },
	{ "da", "だ" }, { "de", "で" }, { "do", "ど" },
	{ "ba", "ば" }, { "bi", "び" }, { "bu", "ぶ" }, { "be", "べ" }, { "bo", "ぼ" },
	{ "pa", "ぱ" }, { "pi", "ぴ" }, { "pu", "ぷ" }, { "pe", "ぺ" }, { "po", "ぽ" },
	{ "a", "あ" }, { "i", "い" }, { "u", "う" }, { "e", "え" }, { "o", "お" },
	{ "n", "ん" },
	{ nullptr, nullptr },
};

static const char *
romaji_lookup(const char *r, int len)
{
	int	i;

	for (i = 0; romaji_tab[i].r != nullptr; i++)
		if ((int)strlen(romaji_tab[i].r) == len &&
		    strncmp(romaji_tab[i].r, r, (size_t)len) == 0)
			return (romaji_tab[i].kana);
	return (nullptr);
}

static void
romaji_convert(struct dv_im *im, dv_buf *out, int flush)
{
	while (im->prelen > 0) {
		const char	*k;
		int		n;

		if (!flush) {
            bool prefix = false;
            for (int i = 0; romaji_tab[i].r; ++i)
                if (int(strlen(romaji_tab[i].r)) > im->prelen &&
                    strncmp(romaji_tab[i].r, im->pre, size_t(im->prelen)) == 0) prefix = true;
            if (prefix) break;
        }
		if (im->prelen >= 2 && im->pre[0] == im->pre[1] &&
		    im->pre[0] != 'a' && im->pre[0] != 'e' && im->pre[0] != 'i' &&
		    im->pre[0] != 'o' && im->pre[0] != 'u' && im->pre[0] != 'n') {
			commit_str(out, "っ");
			memmove(im->pre, im->pre + 1, (size_t)(--im->prelen));
			continue;
		}
		if (im->prelen >= 2 && im->pre[0] == 'n' &&
		    strchr("aiueoy", im->pre[1]) == nullptr) {
			commit_str(out, "ん");
			memmove(im->pre, im->pre + 1, (size_t)(--im->prelen));
			continue;
		}
		n = im->prelen < 3 ? im->prelen : 3;
		for (; n >= 1; n--) {
			k = romaji_lookup(im->pre, n);
			if (k != nullptr) {
				commit_str(out, k);
				memmove(im->pre, im->pre + n,
				    (size_t)(im->prelen - n));
				im->prelen -= n;
				break;
			}
		}
		if (n == 0)
			break;
	}
	im->pre[im->prelen] = '\0';
}

/* ---- US-International dead keys ---------------------------------------- */

struct deadmap {
	char		dead;
	char		base;
	uint32_t	cp;
};

static const struct deadmap deadmap[] = {
	{ '\'', 'a', 0xE1 }, { '\'', 'e', 0xE9 }, { '\'', 'i', 0xED },
	{ '\'', 'o', 0xF3 }, { '\'', 'u', 0xFA }, { '\'', 'y', 0xFD },
	{ '\'', 'c', 0xE7 }, { '\'', 'A', 0xC1 }, { '\'', 'E', 0xC9 },
	{ '\'', 'I', 0xCD }, { '\'', 'O', 0xD3 }, { '\'', 'U', 0xDA },
	{ '`', 'a', 0xE0 }, { '`', 'e', 0xE8 }, { '`', 'i', 0xEC },
	{ '`', 'o', 0xF2 }, { '`', 'u', 0xF9 },
	{ '^', 'a', 0xE2 }, { '^', 'e', 0xEA }, { '^', 'i', 0xEE },
	{ '^', 'o', 0xF4 }, { '^', 'u', 0xFB },
	{ '"', 'a', 0xE4 }, { '"', 'e', 0xEB }, { '"', 'i', 0xEF },
	{ '"', 'o', 0xF6 }, { '"', 'u', 0xFC }, { '"', 'y', 0xFF },
	{ '~', 'n', 0xF1 }, { '~', 'o', 0xF5 }, { '~', 'a', 0xE3 },
	{ '~', 'N', 0xD1 },
	{ 0, 0, 0 },
};

static int
is_dead(int ch)
{
	return (ch == '\'' || ch == '`' || ch == '^' || ch == '"' || ch == '~');
}

/* Bopomofo key -> pinyin letter (standard keyboard layout, subset). */
static char
zhuyin_key(int ch)
{
	switch (ch) {
	case '1': return ('b');
	case 'q': return ('p');
	case 'a': return ('m');
	case 'z': return ('f');
	case '2': return ('d');
	case 'w': return ('t');
	case 's': return ('n');
	case 'x': return ('l');
	case 'e': return ('g');
	case 'd': return ('k');
	case 'c': return ('h');
	case 'r': return ('j');
	case 'f': return ('q');
	case 'v': return ('x');
	case 'u': return ('i');
	case 'j': return ('u');
	case '8': return ('a');
	case 'i': return ('o');
	case 'o': return ('e');
	default:  return ((char)ch);
	}
}

static int
commit_candidate(struct dv_im *im, int pick, dv_buf *out)
{
	if (pick < 0 || pick >= im->ncand)
		return (0);
	commit_str(out, im->cand[pick]);
	dv_im_reset(im);
	return (1);
}

static void
commit_raw(struct dv_im *im, dv_buf *out)
{
	int	i;

	for (i = 0; i < im->prelen; i++)
		commit_cp(out, (unsigned char)im->pre[i]);
	dv_im_reset(im);
}

/* Table-backed methods: pinyin, zhuyin, wubi. */
static int
table_key(struct dv_im *im, struct immap *m, int maxlen, int ch,
    dv_buf *out)
{
	int	c = (im->method == DV_IM_ZHUYIN) ? zhuyin_key(ch) : ch;

	if (c >= 'a' && c <= 'z') {
		if (im->prelen < maxlen && im->prelen < (int)sizeof im->pre - 1)
			im->pre[im->prelen++] = (char)c;
		im->pre[im->prelen] = '\0';
		im->page = 0;
		lookup(im, m);
		return (1);
	}
	if (im->prelen == 0)
		return (0);
	if (ch == ' ') {
		if (!commit_candidate(im, 0, out))
			commit_raw(im, out);
		return (1);
	}
	if (ch >= '1' && ch <= '9') {
		commit_candidate(im, ch - '1', out);
		return (1);
	}
	if (ch == '.' || ch == '=' || ch == ']') {	/* next page */
		if (has_more_pages(im, im_get(m, im->pre))) {
			im->page++;
			lookup(im, m);
		}
		return (1);
	}
	if (ch == ',' || ch == '-' || ch == '[') {	/* previous page */
		if (im->page > 0) {
			im->page--;
			lookup(im, m);
		}
		return (1);
	}
	if (ch == '\r' || ch == '\n') {
		/* Enter confirms the top candidate (what most people expect),
		 * falling back to the raw letters only when nothing matched. */
		if (im->ncand > 0)
			commit_candidate(im, 0, out);
		else
			commit_raw(im, out);
		return (1);
	}
	return (0);
}

int
dv_im_key(struct dv_im *im, int ch, dv_buf *commit)
{
	if (im->method == DV_IM_OFF)
		return (0);
	if (im->method == DV_IM_PINYIN || im->method == DV_IM_ZHUYIN)
		im_load(&py_map, "pinyin.dict", 1024);
	else if (im->method == DV_IM_WUBI)
		im_load(&wb_map, "wubi.dict", 262144);

	if (ch == 0x7f || ch == 0x08) {
        if (im->dead) { im->dead = 0; return 1; }
		if (im->prelen > 0) {
			im->pre[--im->prelen] = '\0';
			im->page = 0;
			if (im->method == DV_IM_PINYIN || im->method == DV_IM_ZHUYIN)
				lookup(im, &py_map);
			else if (im->method == DV_IM_WUBI)
				lookup(im, &wb_map);
			return (1);
		}
		return (0);
	}
	if (ch == 0x1b) {			/* Esc drops the preedit */
		if (im->prelen > 0 || im->dead) {
			dv_im_reset(im);
			return (1);
		}
		return (0);
	}

	switch (im->method) {
	case DV_IM_ROMAJI:
		if (ch >= 'a' && ch <= 'z') {
			if (im->prelen < (int)sizeof im->pre - 1)
				im->pre[im->prelen++] = (char)ch;
			im->pre[im->prelen] = '\0';
			romaji_convert(im, commit, 0);
			return (1);
		}
		if (im->prelen > 0) {
			romaji_convert(im, commit, 1);
			commit_raw(im, commit);
		}
		return (0);		/* also let the key through */

	case DV_IM_USINTL:
		if (im->dead) {
			int	d = im->dead, i;

			im->dead = 0;
			for (i = 0; deadmap[i].dead != 0; i++)
				if (deadmap[i].dead == d && deadmap[i].base == ch) {
					commit_cp(commit, deadmap[i].cp);
					return (1);
				}
			commit_cp(commit, (unsigned char)d);
			if (is_dead(ch)) {
				im->dead = ch;
				return (1);
			}
			return (0);
		}
		if (is_dead(ch)) {
			im->dead = ch;
			return (1);
		}
		return (0);

	case DV_IM_PINYIN:
	case DV_IM_ZHUYIN:
		return (table_key(im, &py_map, 6, ch, commit));
	case DV_IM_WUBI:
		return (table_key(im, &wb_map, 4, ch, commit));
	default:
		return (0);
	}
}

} // namespace
class InputMethod::Private {
public:
    dv_im im;
    std::unordered_set<quint64> consumed;
};
InputMethod::InputMethod() : d_(std::make_unique<Private>()) {}
InputMethod::~InputMethod() = default;
int InputMethod::method() const { return d_->im.method; }
QString InputMethod::name(int method) { return QString::fromUtf8(dv_im_name(method)); }
void InputMethod::set_method(int method) {
    if (method < 0 || method >= COUNT) throw std::out_of_range("Input method");
    if (method == Pinyin || method == Zhuyin) im_load(&py_map, "pinyin.dict", 0);
    if (method == Wubi) im_load(&wb_map, "wubi.dict", 0);
    dv_im_reset(&d_->im); d_->im.method = method;
}
void InputMethod::reset() { dv_im_reset(&d_->im); }
bool InputMethod::pending() const { return d_->im.prelen || d_->im.dead; }
QString InputMethod::preedit() const {
    return d_->im.dead ? QString(QChar(d_->im.dead)) : QString::fromUtf8(d_->im.pre);
}
QStringList InputMethod::candidates() const {
    QStringList result;
    for (int i = 0; i < d_->im.ncand; ++i) result.append(QString::fromUtf8(d_->im.cand[i]));
    return result;
}
int InputMethod::page() const { return d_->im.page; }
QByteArray InputMethod::candidate(int index) {
    QByteArray result; commit_candidate(&d_->im, index, &result); return result;
}
InputMethod::Result InputMethod::key(const MappedInput &event) {
    Result result;
    const quint64 identity = event.scan ? event.scan : (quint64(1) << 32) | quint32(event.key);
    if (event.type == QEvent::KeyRelease) {
        result.consumed = d_->consumed.erase(identity) != 0;
        return result;
    }
    if (event.modifiers & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier | Qt::GroupSwitchModifier)) {
        // Modifier presses alone must not discard composition: Option+Tab
        // delivers Escape, and Option+Space temporarily opens Settings.
        if (event.key != Qt::Key_Control && event.key != Qt::Key_Alt && event.key != Qt::Key_AltGr) reset();
        return result;
    }
    int ch = 0;
    if (event.key == Qt::Key_Backspace) ch = 0x7f;
    else if (event.key == Qt::Key_Escape) ch = 0x1b;
    else if (event.key == Qt::Key_Return || event.key == Qt::Key_Enter) ch = '\r';
    else if (event.text.size() == 1 && event.text[0].unicode() < 128) ch = event.text[0].unicode();
    if (ch) result.consumed = dv_im_key(&d_->im, ch, &result.commit) != 0;
    else if (event.key != Qt::Key_Shift && event.key != Qt::Key_Control && event.key != Qt::Key_Alt && event.key != Qt::Key_AltGr) reset();
    if (result.consumed) d_->consumed.insert(identity);
    return result;
}
} // namespace rmt
