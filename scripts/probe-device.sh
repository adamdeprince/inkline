#!/bin/sh
# Read system metadata only. Never open input event streams or display devices.
set -u

if [ "$#" -gt 0 ]; then
    case "$1" in
        -h|--help)
            printf '%s\n' 'Usage: ssh root@TABLET_IP sh -s < scripts/probe-device.sh'
            printf '%s\n' 'Reports firmware, display metadata, keyboard capabilities and memory.'
            exit 0
            ;;
        *)
            printf 'Unexpected argument: %s\n' "$1" >&2
            exit 2
            ;;
    esac
fi

if [ "$(uname -s)" != Linux ]; then
    printf '%s\n' 'Run this probe on the reMarkable over SSH, not on the development Mac.' >&2
    exit 1
fi

section() {
    printf '\n[%s]\n' "$1"
}

show() {
    if [ -r "$1" ]; then
        printf '%s: ' "$1"
        cat "$1" || printf '(read failed)\n'
        printf '\n'
    fi
}

section system
uname -srm
for model in /sys/firmware/devicetree/base/model /proc/device-tree/model; do
    if [ -r "$model" ]; then
        printf 'model: '
        tr -d '\000' < "$model"
        printf '\n'
        break
    fi
done
if [ -r /etc/os-release ]; then
    awk '/^(NAME|VERSION|VERSION_ID|ID|BUILD_ID|PRETTY_NAME)=/' /etc/os-release
fi
if [ -r /usr/share/remarkable/update.conf ]; then
    awk '/^REMARKABLE_RELEASE_VERSION=/' /usr/share/remarkable/update.conf
fi

section memory
if [ -r /proc/meminfo ]; then
    awk '/^(MemTotal|MemAvailable|SwapTotal|SwapFree):/' /proc/meminfo
fi
if [ -r /proc/mounts ]; then
    awk '$2 == "/dev/shm" || $2 == "/tmp" { print $2, $3, $4 }' /proc/mounts
fi

section display
show /proc/fb
for framebuffer in /sys/class/graphics/fb*; do
    [ -d "$framebuffer" ] || continue
    for property in name virtual_size bits_per_pixel; do
        show "$framebuffer/$property"
    done
done
for library in /usr/lib/plugins/platforms/*epaper* /usr/lib/plugins/scenegraph/*epaper*; do
    [ -f "$library" ] || continue
    printf 'epaper library: %s\n' "$library"
done
if command -v systemctl >/dev/null 2>&1; then
    printf 'xochitl state: '
    systemctl is-active xochitl 2>/dev/null || :
fi

section runtime_libraries
for library in /lib/libc.so.6 /lib/ld-linux-armhf.so.3 /usr/lib/libQt6Core.so.6 /usr/lib/libpng16.so.16 /usr/lib/libstdc++.so.6; do
    [ -e "$library" ] && ls -l "$library"
done

section input_devices
count=0
for event in /sys/class/input/event*; do
    [ -d "$event" ] || continue
    count=$((count + 1))
    printf '\n/dev/input/%s\n' "${event##*/}"
    for property in name id/bustype id/vendor id/product capabilities/ev capabilities/key; do
        show "$event/device/$property"
    done
done
[ "$count" -gt 0 ] || printf '%s\n' 'No input event metadata found.'

section usb_role
for property in /sys/class/usb_role/*/role /sys/class/typec/port*/data_role; do
    show "$property"
done

section network_addresses
if command -v ip >/dev/null 2>&1; then
    ip -o -4 addr show scope global 2>/dev/null | awk '{print $2, $4}'
fi

section available_tools
for program in rm2fb-client rm2fbctl qmake qmake6 cmake python3 gcc ssh; do
    command -v "$program" 2>/dev/null || :
done

printf '\n%s\n' 'Probe complete. No input events were read and no settings were changed.'
