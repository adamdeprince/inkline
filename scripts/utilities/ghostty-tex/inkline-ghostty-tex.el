;;; inkline-ghostty-tex.el --- Tablet configuration for ghostty-tex -*- lexical-binding: t; -*-

(require 'cl-lib)

(defconst inkline-ghostty-tex-root
  (file-name-directory (or load-file-name buffer-file-name)))
(defvar inkline-ghostty-tex-cache
  (make-temp-file "/tmp/inkline-ghostty-tex-" t))

;; This directory is on the tablet's tmpfs. Never inherit a persistent TMPDIR.
(setq temporary-file-directory (file-name-as-directory inkline-ghostty-tex-cache))
(setq auto-save-list-file-prefix (expand-file-name "autosave-" temporary-file-directory))
(setenv "TMPDIR" temporary-file-directory)
(setenv "XDG_CACHE_HOME" temporary-file-directory)
(setenv "TEXMFVAR" (expand-file-name "texmf-var" temporary-file-directory))
(setenv "TEXMFCONFIG" (expand-file-name "texmf-config" temporary-file-directory))
(setenv "TEXMFCACHE" (expand-file-name "texmf-var" temporary-file-directory))
(setq native-comp-deferred-compilation nil)
(when (fboundp 'startup-redirect-eln-cache)
  (startup-redirect-eln-cache (expand-file-name "eln-cache" temporary-file-directory)))

(add-to-list 'load-path (expand-file-name "share/ghostty-tex-0.1.4" inkline-ghostty-tex-root))
(add-to-list 'load-path (expand-file-name "runtime/debian/usr/share/emacs/site-lisp" inkline-ghostty-tex-root))
(add-to-list 'exec-path (expand-file-name "bin" inkline-ghostty-tex-root))
(setenv "PATH" (concat (expand-file-name "bin" inkline-ghostty-tex-root)
                       path-separator (or (getenv "PATH") "")))

;; Debian's AUCTeX normally gets this from site-start.el, which the portable
;; Emacs launcher intentionally does not load. Its extracted Lisp is relocatable.
(defvar debian-emacs-flavor 'emacs)
(setq TeX-lisp-directory (expand-file-name "runtime/debian/usr/share/auctex/" inkline-ghostty-tex-root)
      TeX-data-directory TeX-lisp-directory
      TeX-auto-global (expand-file-name "auctex/" inkline-ghostty-tex-cache))
(require 'tex-site)
(add-to-list 'load-path TeX-lisp-directory)
(require 'tex)

(setq doc-view-cache-directory (expand-file-name "docview/" inkline-ghostty-tex-cache)
      doc-view-pdfdraw-program nil
      doc-view-pdf->png-converter-function #'doc-view-pdf->png-converter-ghostscript
      doc-view-ghostscript-program (expand-file-name "bin/gs" inkline-ghostty-tex-root)
      doc-view-dvipdf-program nil
      doc-view-dvipdfm-program (expand-file-name "bin/dvipdfmx" inkline-ghostty-tex-root)
      kitty-graphics-preferred-protocol 'kitty
      kitty-graphics-kitty-placement-mode 'direct
      kitty-graphics-doc-view-resolution-scale 1.0
      kitty-graphics-base64-cache-bytes (* 8 1024 1024)
      kitty-graphics-cache-size 8
      kitty-graphics-render-delay 0.1
      kitty-graphics-process-timeout 60.0
      kitty-graphics-debug nil
      kitty-graphics-enable-video nil
      kitty-graphics-enable-browser nil)

(require 'kitty-graphics)

(defun inkline-ghostty-tex--cell-size (original &optional force)
  "Read local Inkline geometry without replies entering Emacs's input queue."
  (if (not (equal (kitty-graphics--frame-getenv "TERM_PROGRAM") "inkline"))
      (funcall original force)
    (with-temp-buffer
      (when (and (= 0 (call-process (expand-file-name "libexec/cell-size" inkline-ghostty-tex-root)
                                    nil t nil (terminal-name)))
                 (progn (goto-char (point-min)) (looking-at "\\([0-9]+\\) \\([0-9]+\\)")))
        (let ((width (string-to-number (match-string 1)))
              (height (string-to-number (match-string 2))))
          (setq kitty-graphics--cell-pixel-width width
                kitty-graphics--cell-pixel-height height)
          (set-terminal-parameter nil 'kitty-graphics-cell-w width)
          (set-terminal-parameter nil 'kitty-graphics-cell-h height)
          (cons width height))))))

(advice-add 'kitty-graphics--query-cell-size :around #'inkline-ghostty-tex--cell-size)

(defun inkline-ghostty-tex--detect (original)
  "Recognize Inkline without changing its terminal identity."
  (or (and (equal (kitty-graphics--frame-getenv "TERM_PROGRAM") "inkline")
           (not (kitty-graphics--frame-getenv "TMUX")))
      (funcall original)))

(advice-add 'kitty-graphics--kitty-detect :around #'inkline-ghostty-tex--detect)
(require 'ghostty-tex)
(load (expand-file-name "ram-build.el" inkline-ghostty-tex-root) nil t)

(defun inkline-ghostty-tex--cleanup ()
  "Remove this Emacs process's private RAM builds and preview cache."
  (ignore-errors (delete-directory inkline-ghostty-tex-cache t)))

(add-hook 'kill-emacs-hook #'inkline-ghostty-tex--cleanup)
(unless noninteractive
  (ghostty-tex-mode 1))

(provide 'inkline-ghostty-tex)
;;; inkline-ghostty-tex.el ends here
