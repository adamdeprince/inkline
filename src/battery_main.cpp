#include "rmt/battery.hpp"
#include <QCoreApplication>
#include <cstdio>
#include <cstring>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    bool percentage = false;
    if (argc == 2 && (std::strcmp(argv[1], "--percentage") == 0 || std::strcmp(argv[1], "--short") == 0))
        percentage = true;
    else if (argc != 1) {
        std::fputs("Usage: inkline-battery [--percentage]\n", stderr);
        return 2;
    }
    const auto battery = rmt::read_battery();
    const auto output = (percentage ? battery.percentage() : battery.report()).toUtf8();
    std::printf("%s\n", output.constData());
    return percentage || battery.available() ? 0 : 1;
}
