#!/bin/sh
set -eu
utility_root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
test -x /home/root/.local/bin/emacs
test -x /home/root/.local/bin/pdflatex
test -r /home/root/.local/share/inkline-utilities/texlive/state/prefix
"$utility_root/bin/gs" --version
"$utility_root/bin/convert" -size 12x8 xc:white "$TMPDIR/check.png"
"$utility_root/bin/identify" -format '%wx%h\n' "$TMPDIR/check.png"
"$utility_root/bin/gs" -q -dSAFER -dNOPAUSE -dBATCH -sDEVICE=png16m -r72 \
  "-sOutputFile=$TMPDIR/render.png" \
  -c '<</PageSize [72 72]>> setpagedevice 0 setgray 10 10 52 52 rectfill showpage'
test "$("$utility_root/bin/identify" -format '%wx%h' "$TMPDIR/render.png")" = 72x72
/home/root/.local/bin/emacs -Q --batch -l "$utility_root/inkline-ghostty-tex.el" \
  --eval '(progn (require (quote doc-view)) (unless (and (featurep (quote ghostty-tex)) (featurep (quote tex-site)) (executable-find doc-view-ghostscript-program) (executable-find doc-view-dvipdfm-program)) (error "Missing TeX integration")) (princ (format "ghostty-tex, AUCTeX and DocView loaded in Emacs %s\n" emacs-version)))'
