#!/bin/sh
# Install LuaLaTeX for Purrfect, or the full upstream collection on request.
set -eu
umask 077
prefix=/home/root/.local/share/inkline-texlive/2026
repository=https://mirror.ctan.org/systems/texlive/tlnet
docs=0
full=0
check=0
while [ "$#" -gt 0 ]; do
    case "$1" in
        --check) check=1; shift ;;
        --with-docs) docs=1; shift ;;
        --full) full=1; shift ;;
        --prefix) test "$#" -ge 2; prefix=$2; shift 2 ;;
        --repository) test "$#" -ge 2; repository=${2%/}; shift 2 ;;
        *) echo 'Usage: texlive-install [--check] [--full] [--with-docs] [--prefix /absolute/path] [--repository https://mirror/path]' >&2; exit 1 ;;
    esac
done
case "$prefix" in /*) ;; *) echo 'The TeX Live prefix must be an absolute path.' >&2; exit 1 ;; esac
case "$prefix" in /|*' '*|*'	'*) echo 'Choose a dedicated directory without whitespace.' >&2; exit 1 ;; esac
test "$(id -u)" = 0
case "$repository" in https://*) ;; *) echo 'Use an HTTPS TeX Live repository.' >&2; exit 1 ;; esac
utility_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
. "$utility_root/runtime.env"
export PATH="$utility_root/bin:$PATH"
export TEXLIVE_DOWNLOADER=curl
installer="$utility_root/runtime/texlive-installer/install-tl"
arch=$("$utility_root/bin/perl" "$installer" --print-arch)
test "$arch" = armhf-linux || { echo "Unexpected TeX Live architecture: $arch" >&2; exit 1; }
if [ -e "$prefix" ]; then
    test -d "$prefix" && test ! -L "$prefix" && test -f "$prefix/.inkline-texlive" || {
        echo "Refusing to use an unrelated $prefix." >&2; exit 1;
    }
fi
ancestor=$prefix
while [ ! -d "$ancestor" ]; do ancestor=$(dirname -- "$ancestor"); done
available=$(df -Pk "$ancestor" | awk 'END {print $4}')
# Includes headroom for generated formats and changes in the rolling mirror.
scheme=scheme-basic
needed=393216
if [ "$full" = 1 ]; then
    scheme=scheme-full
    needed=5242880
    test "$docs" = 0 || needed=11534336
elif [ "$docs" = 1 ]; then
    needed=1048576
fi
test "$available" -ge "$needed" || {
    echo "TeX Live needs at least $((needed / 1024)) MiB free at $prefix for these options." >&2
    exit 1
}
if [ "$check" = 1 ]; then
    printf 'TeX Live installer: ARMv7 and storage checks passed; nothing downloaded or installed.\n'
    exit 0
fi
state=${INKLINE_TEXLIVE_STATE_DIR:-/home/root/.local/share/inkline-utilities/texlive/state}
mkdir -p "$state"
exec 9>"$state/install.lock"
flock -x 9
scratch=$(mktemp -d /tmp/inkline-texlive.XXXXXX)
trap 'rm -rf -- "$scratch"' EXIT
trap 'exit 130' HUP INT TERM
"$utility_root/bin/curl" -fLsS -o "$scratch/texlive.tlpdb" "$repository/tlpkg/texlive.tlpdb"
year=$(awk '/^name 00texlive.config$/ { config=1; next } config && /^$/ { exit } config && /^depend release\// { split($2, value, "/"); print value[2]; exit }' "$scratch/texlive.tlpdb")
test "$year" = 2026 || {
    echo "This installer expects TeX Live 2026; the repository reports ${year:-an unknown version}." >&2
    echo 'Use an updated Inkline package or --repository with a TeX Live 2026 archive.' >&2
    exit 1
}
rm "$scratch/texlive.tlpdb"
mkdir -p "$prefix"
printf 'Inkline TeX Live 2026\n' > "$prefix/.inkline-texlive"
printf '%s\n' "$prefix" > "$state/.prefix.$$"
mv "$state/.prefix.$$" "$state/prefix"
cat > "$scratch/profile" <<EOF
selected_scheme $scheme
binary_armhf-linux 1
TEXDIR $prefix
TEXMFLOCAL $prefix/texmf-local
TEXMFSYSCONFIG $prefix/texmf-config
TEXMFSYSVAR $prefix/texmf-var
TEXMFHOME ${TEXMFHOME:-/home/root/texmf}
TEXMFCONFIG ${TEXMFCONFIG:-/home/root/.texlive2026/texmf-config}
TEXMFVAR ${TEXMFVAR:-/home/root/.texlive2026/texmf-var}
instopt_adjustpath 0
instopt_letter 0
instopt_portable 0
tlpdbopt_autobackup 0
tlpdbopt_install_docfiles $docs
tlpdbopt_install_srcfiles $docs
EOF
printf 'Installing TeX Live 2026 (%s) in %s.\n' "$scheme" "$prefix"
TMPDIR="$scratch" "$utility_root/bin/perl" "$installer" \
    --repository "$repository" \
    --profile "$scratch/profile" --no-gui --logfile "$scratch/install-tl.log"
test -x "$prefix/bin/armhf-linux/luatex"
if [ "$full" = 0 ]; then
    PATH="$prefix/bin/armhf-linux:$PATH" TMPDIR="$scratch" \
        "$utility_root/bin/perl" "$prefix/texmf-dist/scripts/texlive/tlmgr.pl" install \
        collection-luatex amsmath amsfonts xcolor graphics ulem fontspec ragged2e \
        setspace tabto-ltx fancyhdr endnotes tools geometry lm latexmk
fi
printf 'TeX Live installed. Use pdflatex, lualatex, latexmk, and tlmgr in Inkline.\n'
