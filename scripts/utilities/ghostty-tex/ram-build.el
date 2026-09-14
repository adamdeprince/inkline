;;; ram-build.el --- RAM builds for AUCTeX 12.2 -*- lexical-binding: t; -*-

;; AUCTeX 12.2 predates TeX-output-dir. Keep its source and process identities
;; intact, but redirect output names and compiler/tool working directories.
(require 'tex-buf)
(defvar inkline-ghostty-tex--command nil)
(defvar inkline-ghostty-tex--directory nil)
(defconst inkline-ghostty-tex--output-tools
  '("BibTeX" "Biber" "Index" "MakeIndex" "Makeinfo" "DviPS" "Dvips" "Dvipdfmx" "Dvipdf" "Ps2Pdf"))
(defconst inkline-ghostty-tex--output-extensions
  '("pdf" "dvi" "xdv" "ps" "log" "aux" "bbl" "bcf" "blg" "run.xml"
    "idx" "ind" "ilg" "out" "toc" "lof" "lot" "nav" "snm" "synctex.gz"))

(defun inkline-ghostty-tex--source-buffer ()
  "Return the source buffer for the current AUCTeX operation."
  (cond ((and buffer-file-name (derived-mode-p 'tex-mode 'plain-tex-mode 'latex-mode))
         (current-buffer))
        ((and (boundp 'TeX-command-buffer) (buffer-live-p TeX-command-buffer))
         TeX-command-buffer)))

(defun inkline-ghostty-tex-build-directory ()
  "Return a private RAM directory unique to this document's master file."
  (let ((source (inkline-ghostty-tex--source-buffer)))
    (unless source (user-error "No TeX source buffer"))
    (with-current-buffer source
      (let* ((master (expand-file-name (TeX-master-file t)))
             (directory (expand-file-name
                         (concat "build/" (secure-hash 'sha256 master) "/")
                         inkline-ghostty-tex-cache)))
        (make-directory directory t)
        directory))))

(defun inkline-ghostty-tex--output-name (original &optional extension nondirectory ask)
  "Resolve AUCTeX's output files in RAM, retaining normal source paths."
  (let ((name (funcall original extension nondirectory ask)))
    (if (and (member extension inkline-ghostty-tex--output-extensions)
             (inkline-ghostty-tex--source-buffer))
        (if nondirectory (file-name-nondirectory name)
          (expand-file-name (file-name-nondirectory name)
                            (inkline-ghostty-tex-build-directory)))
      name)))

(advice-add 'TeX-master-file :around #'inkline-ghostty-tex--output-name)

(defun inkline-ghostty-tex--preview-output (original)
  "Use AUCTeX output paths with its lowercase major-mode names too."
  (if (and buffer-file-name (derived-mode-p 'latex-mode 'plain-tex-mode 'tex-mode))
      (expand-file-name (TeX-active-master (TeX-output-extension)))
    (funcall original)))

(advice-add 'ghostty-tex-output-file :around #'inkline-ghostty-tex--preview-output)

(defun inkline-ghostty-tex--expand-tool (original command file &optional list)
  "Give postprocessors the RAM job name instead of the source job name."
  (funcall original command
           (if (member inkline-ghostty-tex--command inkline-ghostty-tex--output-tools)
               (lambda (&optional extension nondirectory ask)
                 (let ((name (funcall file extension nondirectory ask)))
                   (if nondirectory (file-name-nondirectory name)
                     (expand-file-name (file-name-nondirectory name)
                                       inkline-ghostty-tex--directory))))
             file)
           list))

(advice-add 'TeX-command-expand :around #'inkline-ghostty-tex--expand-tool)

(defun inkline-ghostty-tex--run-tool (original name command file)
  "Run bibliography/index converters in RAM as well."
  (funcall original name
           (if (and inkline-ghostty-tex--directory
                    (member name inkline-ghostty-tex--output-tools))
               (concat "cd " (shell-quote-argument inkline-ghostty-tex--directory)
                       " && " command)
             command)
           file))

(advice-add 'TeX-run-command :around #'inkline-ghostty-tex--run-tool)

(defun inkline-ghostty-tex--compile (original name file &optional confirm)
  "Keep standard TeX builds and their temporary files on tmpfs."
  (let* ((inkline-ghostty-tex--command name)
         (inkline-ghostty-tex--directory (inkline-ghostty-tex-build-directory))
         (source-directory (expand-file-name (TeX-master-directory)))
         (process-environment (copy-sequence process-environment))
         (TeX-command-extra-options
          (if (member name '("TeX" "LaTeX" "AmSTeX"))
              (concat TeX-command-extra-options " -output-directory="
                      (shell-quote-argument inkline-ghostty-tex--directory))
            TeX-command-extra-options)))
    (dolist (variable '("TEXINPUTS" "BIBINPUTS" "BSTINPUTS"))
      (setenv variable (concat inkline-ghostty-tex--directory ":"
                              source-directory ":" (or (getenv variable) ""))))
    (setenv "TEXMFOUTPUT" inkline-ghostty-tex--directory)
    (funcall original name file confirm)))

(advice-add 'TeX-command :around #'inkline-ghostty-tex--compile)

(defun inkline-ghostty-tex--source-settings ()
  "Keep generated AUCTeX metadata and TeX editing recovery files in RAM."
  (let ((directory (expand-file-name "editing/" inkline-ghostty-tex-cache)))
    (make-directory directory t)
    (setq-local TeX-auto-save nil)
    (setq-local TeX-auto-local (expand-file-name "auctex/" directory))
    (setq-local backup-directory-alist `(("." . ,directory)))
    (setq-local auto-save-file-name-transforms `((".*" ,directory t)))
    (when buffer-auto-save-file-name
      (setq buffer-auto-save-file-name (make-auto-save-file-name)))))

(dolist (hook '(LaTeX-mode-hook plain-TeX-mode-hook))
  (add-hook hook #'inkline-ghostty-tex--source-settings))

(defun inkline-ghostty-tex-save-pdf (destination)
  "Save the current RAM-built PDF permanently at DESTINATION."
  (interactive
   (let ((source (inkline-ghostty-tex--source-buffer)))
     (unless source (user-error "Select the TeX source pane first"))
     (with-current-buffer source
       (list (read-file-name "Save PDF permanently: "
                             (file-name-directory (expand-file-name (TeX-master-file t)))
                             nil nil (concat (TeX-master-file nil t) ".pdf"))))))
  (let* ((source (inkline-ghostty-tex--source-buffer))
         (pdf (and source (with-current-buffer source (TeX-master-file "pdf")))))
    (unless (and pdf (file-readable-p pdf)) (user-error "Compile a PDF first with C-c C-c"))
    (copy-file pdf destination 1)
    (message "Saved %s" (expand-file-name destination))))

(provide 'inkline-ghostty-tex-ram-build)
;;; ram-build.el ends here
