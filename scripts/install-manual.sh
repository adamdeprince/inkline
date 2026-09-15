#!/bin/sh
# Use the tablet's USB document importer; never write xochitl metadata ourselves.
set -eu
umask 077
root=/home/root/.local/share/inkline
pdf="$root/current/docs/Inkline Manual.pdf"
test -f "$pdf"
exec 7>/run/inkline-manual.lock
flock -x 7
checksum=$(sha256sum "$pdf" | cut -d ' ' -f 1)
if [ -f "$root/manual-import.sha256" ] && [ "$(cat "$root/manual-import.sha256")" = "$checksum" ]; then
    echo 'This manual has already been imported into My files.'
    exit 0
fi
if ! systemctl is-active --quiet xochitl.service; then
    echo 'Manual bundled. To add it to My files, quit Inkline, enable the USB web interface, and run ~/inkline manual from SSH.'
    exit 0
fi
# No retries: a lost HTTP response after successful import could create a duplicate.
if "$root/current/inkline" --import-manual "$pdf" >/dev/null 2>&1; then
    printf '%s\n' "$checksum" > "$root/.manual-import.new"
    mv -f "$root/.manual-import.new" "$root/manual-import.sha256"
    echo 'Inkline Manual imported into My files.'
else
    echo 'Manual bundled; automatic import is unavailable. Enable USB web interface and run ~/inkline manual, or import the PDF from the project website.'
fi
