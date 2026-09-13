#!/bin/sh
exec python3 "$(dirname -- "$0")/cross.py" c++ "$@"
