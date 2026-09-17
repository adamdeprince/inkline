#include "rmt/battery.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <cstdio>
#include <cstdlib>

#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); std::exit(1); } } while (0)
void write(const QString &path, const QByteArray &value) {
    QFile file(path); CHECK(file.open(QIODevice::WriteOnly)); CHECK(file.write(value) == value.size());
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir temporary; CHECK(temporary.isValid());
    const auto mains = temporary.filePath("usb"); CHECK(QDir().mkpath(mains));
    write(mains + "/type", "USB\n"); write(mains + "/capacity", "100\n");
    const auto battery = temporary.filePath("max77818_battery"); CHECK(QDir().mkpath(battery));
    write(battery + "/type", "Battery\n"); write(battery + "/present", "1\n");
    write(battery + "/capacity", "73\n"); write(battery + "/status", "Charging\n");
    const auto info = rmt::read_battery(temporary.path());
    CHECK(info.available() && info.percent == 73 && info.percentage() == "73%");
    CHECK(info.compact() == "Battery 73% · charging");
    CHECK(info.report() == "Battery: 73%\nStatus: Charging");
    write(battery + "/present", "0\n");
    const auto fallback = rmt::read_battery(temporary.path());
    CHECK(fallback.percent == 100 && fallback.compact() == "Battery 100%");
    CHECK(!rmt::read_battery(temporary.filePath("missing")).available());
    std::puts("Battery: sysfs selection, percentage, status and fallback passed.");
}
