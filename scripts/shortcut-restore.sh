#!/bin/sh
# No management locks here: this is called by systemd while stopping the app.
if systemctl is-active --quiet inkline.service; then
    systemctl kill --kill-whom=all --signal=CONT inkline.service 2>/dev/null || :
    /home/root/.local/share/inkline/current/inkline --redraw 2>/dev/null || :
else
    systemctl --no-block start xochitl.service
fi
