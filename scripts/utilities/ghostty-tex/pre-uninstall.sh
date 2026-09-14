#!/bin/sh
set -eu
utility_root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
"$utility_root/bin/ghostty-tex-configure" --disable
