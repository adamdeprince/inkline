# Body of the reMarkable launcher, sourced into the generated package wrapper.
export WP51_KITTY_MEDIUM="${WP51_KITTY_MEDIUM:-inline}"
case "${1-}" in
    '') set -- edit --display epaper ;;
    edit) shift; set -- edit --display epaper "$@" ;;
    new|create-demo|make-probes|save-copy|inspect|verify-roundtrip|export-latex|export-pdf|equation-latex|latex-wp|check-theme|doctor|help|--help|-h|--version|-V) ;;
    *)
        # Upstream also accepts one filename as shorthand for "edit FILE".
        if [ "$#" -eq 1 ]; then set -- edit --display epaper "$@"; fi
        ;;
esac
# Explicit --display or --theme later in the edit arguments overrides the default.
exec "$utility_root/libexec/goblin-purrfect" "$@"
