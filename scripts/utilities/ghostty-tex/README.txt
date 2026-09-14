ghostty-tex 0.1.4 for Inkline on reMarkable 2 (firmware 3.27)

Install Inkline's Emacs and compact TeX Live utilities first. This add-on bundles
the exact ghostty-tex 0.1.4 archive, AUCTeX 12.2, Ghostscript, ImageMagick, fonts,
private ARM libraries and their license notices. Existing TeX Live supplies
pdflatex and dvipdfmx. MuPDF and dvipng are not required for PDF/DVI page previews.

Installation: https://inkline.goblinreactor.com/install.html#ghostty-tex
Downloads and matching source: https://inkline.goblinreactor.com/utilities.html

Verify the outer archive's SHA-256, extract it in /tmp, and run:
  ./ghostty-tex/install-device.sh --check
  ./ghostty-tex/install-device.sh

The installer appends a small marked block to the active Emacs init and early
init files. It preserves existing settings and makes a backup before changing
an existing file. No stock system libraries are replaced. Already-running Emacs
sessions stay running; restart Emacs to load the add-on.

Open a document inside Inkline:
  emacs paper.tex

  C-c C-c          Save source and compile; PDF left, source right.
  C-u C-c C-c      Choose another AUCTeX command, such as BibTeX.
  C-c C-v          Show the current output.
  M-n / M-p        Next / previous page (or C-c n / C-c p).
  M-x inkline-ghostty-tex-save-pdf
                   Copy the finished PDF to a permanent location.
  M-x ghostty-tex-mode
                   Toggle the integration in this Emacs session.

Inkline's right Alt/Opt+V pastes its clipboard into Emacs. Emacs copies/kills
use OSC 52. Run directly inside Inkline; Kitty placeholder images in tmux are
not supported by this Inkline profile.

FLASH AND RAM

Normal LaTeX builds put PDFs, auxiliary files, logs, DocView page PNGs, TeX
caches, and TeX-buffer recovery files in RAM-backed /tmp. ImageMagick cannot
spill its pixel cache onto flash. Images travel through inline Kitty graphics.
The source .tex file is saved normally when requested or when compiling.
Installation and explicit PDF export are intentional persistent writes.

Each Emacs process has a separate private /tmp/inkline-ghostty-tex-* directory,
removed on normal Emacs exit. Its build results and recovery files are lost
on exit or reboot. Save a PDF explicitly if you want to retain it. Converter
font caches also use /tmp and disappear on reboot. RAM is shared with Inkline
and the rest of the tablet; very large documents can exhaust it. Custom TeX
commands, shell-escape programs and arbitrary document code can choose their
own output paths; the RAM policy is not an operating-system write sandbox.

DISABLE OR REMOVE

  ghostty-tex-configure --disable
  ghostty-tex-configure --enable

These change only the marked configuration blocks; restart Emacs afterward.
To remove the add-on and its configuration blocks:
  /home/root/.local/share/inkline-utilities/ghostty-tex/current/uninstall-device.sh

BUILD AND LICENSES

From the Inkline repository, first prepare the existing utility locale/license
inputs with scripts/utilities/debian.py, then:
  python3 scripts/package-ghostty-tex.py --package-file ghostty-tex-0.1.4.tar.gz --archive

Use --offline once all inputs are cached. All upstream binaries and sources
are SHA-256 pinned in scripts/utilities/ghostty-tex/debian.lock.json. The public
download includes the exact Lisp source archive, all corresponding Debian
source archives, and the Inkline packaging recipes. GPL texts and individual
dependency copyright notices are included in the installed bundle.
