#include "keyboard.h"

#ifdef LINUX_BUILD
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>

void keyboard_init() {
    struct termios t;
    tcgetattr(STDIN_FILENO, &t);
    t.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &t);
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
}

int keyboard_poll(char *ascii_char) {
    char c;
    if (read(STDIN_FILENO, &c, 1) == 1) {
        if (c == 127) c = '\b'; // Map DEL to backspace
        *ascii_char = c;
        return 1;
    }
    return 0;
}
#else
#define KMI0_CR     (*(volatile unsigned int *)0x10006000)
#define KMI0_STAT   (*(volatile unsigned int *)0x10006004)
#define KMI0_DATA   (*(volatile unsigned int *)0x10006008)

static const char scancode_set2_to_ascii[256] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '\t', '`', 0,
    0, 0, 0, 0, 0, 'q', '1', 0, 0, 0, 'z', 's', 'a', 'w', '2', 0,
    0, 'c', 'x', 'd', 'e', '4', '3', 0, 0, ' ', 'v', 'f', 't', 'r', '5', 0,
    0, 'n', 'b', 'h', 'g', 'y', '6', 0, 0, 0, 'm', 'j', 'u', '7', '8', 0,
    0, ',', 'k', 'i', 'o', '0', '9', 0, 0, '.', '/', 'l', ';', 'p', '-', 0,
    0, 0, '\'', 0, '[', '=', 0, 0, 0, 0, '\n', ']', 0, '\\', 0, 0,
    0, 0, 0, 0, 0, 0, '\b', 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static int extended = 0;
static int break_code = 0;

void keyboard_init() {
    KMI0_CR = 0x04; // Enable KMI
}

int keyboard_poll(char *ascii_char) {
    *ascii_char = 0;
    if (KMI0_STAT & 0x10) { // RX Full
        unsigned int code = KMI0_DATA;
        
        if (code == 0xE0) {
            extended = 1;
            return 0;
        }
        if (code == 0xF0) {
            break_code = 1;
            return 0;
        }
        
        if (break_code) {
            break_code = 0;
            extended = 0;
            return 0; // Key released
        }
        
        // Key pressed
        if (!extended && code < 256) {
            *ascii_char = scancode_set2_to_ascii[code];
        }
        
        extended = 0;
        return 1; // Key pressed event
    }
    return 0;
}
#endif
