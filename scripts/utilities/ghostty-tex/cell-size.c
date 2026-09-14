// Read Inkline's live PTY pixel geometry without sending terminal input replies.
#include <fcntl.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    const int fd = open(argv[1], O_RDONLY | O_NOCTTY | O_CLOEXEC);
    if (fd < 0) return 1;
    struct winsize size = {0};
    const int result = ioctl(fd, TIOCGWINSZ, &size);
    close(fd);
    if (result || !size.ws_col || !size.ws_row || !size.ws_xpixel || !size.ws_ypixel)
        return 1;
    const unsigned width = size.ws_xpixel / size.ws_col;
    const unsigned height = size.ws_ypixel / size.ws_row;
    if (!width || !height) return 1;
    printf("%u %u\n", width, height);
    return 0;
}
