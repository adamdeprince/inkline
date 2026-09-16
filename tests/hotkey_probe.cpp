// Manual on-device integration probe. Creates a temporary kernel keyboard and
// sends Ctrl+Opt+Alt+T (or a numeric evdev key code) through actual evdev.
// Intentionally not part of CTest: this switches the tablet's visible UI.
#include <linux/uinput.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <cstdlib>
int main(int argc, char **argv) {
    if (argc > 4) return 2;
    const bool folio = argc == 4 && std::strcmp(argv[3], "folio") == 0;
    if (argc == 4 && !folio) return 2;
    char *end = nullptr;
    const long code = argc > 1 ? std::strtol(argv[1], &end, 10) : KEY_T;
    if ((argc > 1 && (!end || *end)) || code < KEY_ESC || code > KEY_F12) return 2;
    const long hold_ms = argc > 2 ? std::strtol(argv[2], &end, 10) : 150;
    if ((argc > 2 && (!end || *end)) || hold_ms < 1 || hold_ms > 5000) return 2;
    int fd = open("/dev/uinput", O_WRONLY | O_CLOEXEC);
    if (fd < 0) return 1;
    if (ioctl(fd, UI_SET_EVBIT, EV_KEY) < 0) return 1;
    for (int key = KEY_ESC; key <= KEY_RIGHTMETA; ++key) if (ioctl(fd, UI_SET_KEYBIT, key) < 0) return 1;
    uinput_setup setup{};
    std::strcpy(setup.name, "Inkline launcher acceptance test");
    setup.id.bustype = BUS_USB; setup.id.vendor = 0x1; setup.id.product = 0x1;
    if (folio) {
        std::strcpy(setup.name, "rM_Keyboard");
        setup.id.bustype = BUS_HOST; setup.id.vendor = 0x2edd; setup.id.product = 1;
    }
    if (ioctl(fd, UI_DEV_SETUP, &setup) < 0 || ioctl(fd, UI_DEV_CREATE) < 0) return 1;
    usleep(1800000);
    auto send = [&](unsigned short type, unsigned short code, int value) {
        input_event event{}; event.type = type; event.code = code; event.value = value;
        return write(fd, &event, sizeof(event)) == sizeof(event);
    };
    bool ok = true;
    const int opt = folio ? KEY_END : KEY_LEFTMETA;
    for (int key : {KEY_LEFTCTRL, opt, KEY_LEFTALT, int(code)}) ok &= send(EV_KEY, key, 1);
    ok &= send(EV_SYN, SYN_REPORT, 0);
    usleep(static_cast<unsigned>(hold_ms) * 1000);
    for (int key : {int(code), KEY_LEFTALT, opt, KEY_LEFTCTRL}) ok &= send(EV_KEY, key, 0);
    ok &= send(EV_SYN, SYN_REPORT, 0);
    usleep(2000000);
    ioctl(fd, UI_DEV_DESTROY); close(fd);
    return ok ? 0 : 1;
}
