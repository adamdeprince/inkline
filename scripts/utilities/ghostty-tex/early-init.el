;;; early-init.el --- Inkline terminal capabilities -*- lexical-binding: t; -*-
(load (expand-file-name "share/ghostty-tex-0.1.4/ghostty-tex-early.el"
                        (file-name-directory load-file-name)) nil t)
(defvar xterm-extra-capabilities)
(when (equal (getenv "TERM_PROGRAM") "inkline")
  (setq xterm-extra-capabilities '(setSelection modifyOtherKeys)))
