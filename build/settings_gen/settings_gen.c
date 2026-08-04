#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

int main(void) {
    static const unsigned char data[] = {
        0x10, 0x00, 0x01, 0x00, /* InfoManager FILE_VERSION 0x00010010 LE */
        0x10, 0x00,             /* name length (16, NUL included) */
        't', 'w', '_', 's', 't', 'o', 'r', 'a', 'g', 'e', '_', 'p', 'a', 't', 'h', 0x00,
        0x0e, 0x00,             /* value length (14, NUL included) */
        '/', 'd', 'a', 't', 'a', '/', 'm', 'e', 'd', 'i', 'a', '/', '0', 0x00,
        /* never ask about keeping system read-only again, and leave it read-only */
        0x1d, 0x00,             /* name length (29, NUL included) */
        't', 'w', '_', 'n', 'e', 'v', 'e', 'r', '_', 's', 'h', 'o', 'w', '_', 's', 'y', 's', 't', 'e', 'm', '_', 'r', 'o', '_', 'p', 'a', 'g', 'e', 0x00,
        0x02, 0x00,             /* value length (2) */
        '1', 0x00,
        0x13, 0x00,             /* name length (19, NUL included) */
        't', 'w', '_', 'm', 'o', 'u', 'n', 't', '_', 's', 'y', 's', 't', 'e', 'm', '_', 'r', 'o', 0x00,
        0x02, 0x00,             /* value length (2) */
        '1', 0x00,
        /* vibration durations (ms) tuned to be closer to stock haptics */
        0x11, 0x00,             /* tw_button_vibrate (17 chars) */
        't', 'w', '_', 'b', 'u', 't', 't', 'o', 'n', '_', 'v', 'i', 'b', 'r', 'a', 't', 'e', 0x00,
        0x03, 0x00, '6', '0', 0x00,
        0x11, 0x00,             /* tw_action_vibrate */
        't', 'w', '_', 'a', 'c', 't', 'i', 'o', 'n', '_', 'v', 'i', 'b', 'r', 'a', 't', 'e', 0x00,
        0x03, 0x00, '1', '2', '0', 0x00,
        0x13, 0x00,             /* tw_keyboard_vibrate (19 chars) */
        't', 'w', '_', 'k', 'e', 'y', 'b', 'o', 'a', 'r', 'd', '_', 'v', 'i', 'b', 'r', 'a', 't', 'e', 0x00,
        0x03, 0x00, '4', '0', 0x00,
    };
    mkdir("/persist", 0777);
    mkdir("/persist/TWRP", 0777);
    /* Only create the file when it does not exist yet: TWRP persists its
       settings (e.g. tw_never_show_system_ro_page) here, and overwriting
       them on every boot would make the system_readonly prompt appear
       every time. */
    int fd = open("/persist/TWRP/.twrp_settings",
                  O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd < 0) return 0; /* already exists - keep whatever TWRP saved */
    if (write(fd, data, sizeof(data)) != (ssize_t)sizeof(data)) {
        close(fd);
        return 2;
    }
    close(fd);
    return 0;
}
