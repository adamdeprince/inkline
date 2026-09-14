#!/bin/sh
set -eu
utility_root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ ! -x /home/root/.local/bin/emacs ] || [ ! -x /home/root/.local/bin/pdflatex ] || \
   [ ! -r /home/root/.local/share/inkline-utilities/texlive/state/prefix ]; then
    echo 'Install Inkline Emacs and TeX Live, then run texlive-install before adding ghostty-tex.' >&2
    exit 1
fi
"$utility_root/bin/ghostty-tex-configure" --check
