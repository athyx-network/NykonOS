#include "keyboard.h"

char key_states[256] = {0};

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
#elif defined(TARGET_BPI)
#define UART0_RBR (*(volatile unsigned int *)0x01C28000)
#define UART0_LSR (*(volatile unsigned int *)0x01C28014)

void keyboard_init() {
}

int keyboard_poll(char *ascii_char) {
    if (UART0_LSR & 1) {
        *ascii_char = (char)(UART0_RBR & 0xFF);
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
            if (extended) {
                if (code == 0x6B) { key_states['a'] = 0; key_states['A'] = 0; } // Left
                else if (code == 0x74) { key_states['d'] = 0; key_states['D'] = 0; } // Right
                else if (code == 0x75) { key_states['w'] = 0; key_states['W'] = 0; } // Up
                else if (code == 0x72) { key_states['s'] = 0; key_states['S'] = 0; } // Down
            } else if (code < 256) {
                char ascii = scancode_set2_to_ascii[code];
                if (ascii) {
                    key_states[(unsigned char)ascii] = 0;
                    if (ascii >= 'a' && ascii <= 'z') key_states[(unsigned char)(ascii - 32)] = 0;
                    if (ascii >= 'A' && ascii <= 'Z') key_states[(unsigned char)(ascii + 32)] = 0;
                }
            }
            break_code = 0;
            extended = 0;
            return 0; // Key released
        }
        
        // Key pressed
        if (extended) {
            if (code == 0x6B) { *ascii_char = 'a'; key_states['a'] = 1; key_states['A'] = 1; } // Left
            else if (code == 0x74) { *ascii_char = 'd'; key_states['d'] = 1; key_states['D'] = 1; } // Right
            else if (code == 0x75) { *ascii_char = 'w'; key_states['w'] = 1; key_states['W'] = 1; } // Up
            else if (code == 0x72) { *ascii_char = 's'; key_states['s'] = 1; key_states['S'] = 1; } // Down
        } else if (code < 256) {
            *ascii_char = scancode_set2_to_ascii[code];
            if (*ascii_char) {
                key_states[(unsigned char)*ascii_char] = 1;
                if (*ascii_char >= 'a' && *ascii_char <= 'z') key_states[(unsigned char)(*ascii_char - 32)] = 1;
                if (*ascii_char >= 'A' && *ascii_char <= 'Z') key_states[(unsigned char)(*ascii_char + 32)] = 1;
            }
        }
        
        extended = 0;
        return (*ascii_char != 0); // Key pressed event
    }
    return 0;
}
#endif
