# Building the Inkline manual

`docs/manual.tex` is the authoritative LaTeX master. The nine `.tex` files in
this directory are chapters of **one document**, in the same languages as the
website. They are not separate downloadable manuals. `docs/Inkline Manual.pdf`
is the committed, installable artifact. LaTeX utility installation instructions
remain on the website; this manual describes Inkline itself.

Use Python 3 and TeX Live 2026 with LuaLaTeX. On macOS, MacTeX includes the
required packages; put `/Library/TeX/texbin` on `PATH`. A Linux TeX Live 2026
installation can use the same upstream packages:

```sh
tlmgr install collection-luatex collection-langjapanese collection-langchinese \
  collection-langenglish collection-langfrench collection-langgerman \
  collection-langitalian collection-langspanish collection-langportuguese \
  collection-latexrecommended collection-latexextra collection-fontsrecommended
python3 scripts/build-manual.py
```

With a distribution-managed TeX installation, use its package manager instead
of `tlmgr` (for example Debian's `texlive-luatex`, `texlive-lang-japanese`,
`texlive-lang-chinese`, `texlive-lang-european`, `texlive-lang-french`,
`texlive-lang-german`, `texlive-lang-spanish`, `texlive-latex-extra`, and
`texlive-fonts-recommended`). The tested build uses TeX Live 2026 on macOS.

The document uses TeX Gyre fonts and the project's unmodified, OFL-licensed
Noto CJK font, with Japanese, Simplified Chinese and Traditional Chinese
language features. Font subsets are embedded and the text is searchable.
There is no PyQt dependency and shell escape is disabled.

The builder runs LuaLaTeX three times to resolve the front language index,
page links and bookmarks. Auxiliary files and font caches live in the host's
temporary directory and are removed after success. On the tablet, `/tmp` is
RAM-backed. Only the completed PDF is copied into the repository. Failed
builds retain their temporary logs and print their location. `--output PATH`
writes a review copy. `SOURCE_DATE_EPOCH` defaults to 2026-09-15 UTC; override
it explicitly for a different release date. Identical PDF bytes also require
identical TeX packages, fonts and engine versions.

Before publishing, check the log for overfull boxes, missing glyphs and
unresolved links, inspect the rendered index and representative pages, and
verify that all nine language chapters extract as text. Keep the single PDF
in the terminal bundle; every website language should link to that same file.
