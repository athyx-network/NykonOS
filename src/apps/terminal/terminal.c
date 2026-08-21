#include "../../sys/nykon_api.h"
#include <stdio.h>
#include <string.h>

// Simple terminal app implementation

static char input_buf[128];
static int input_len = 0;
static char history[20][128];
static int history_count = 0;

static void add_to_history(const char* str) {
    if (history_count == 20) {
        for (int i = 0; i < 19; i++) {
            int j = 0;
            while(history[i+1][j] != '\0') {
                history[i][j] = history[i+1][j];
                j++;
            }
            history[i][j] = '\0';
        }
        history_count = 19;
    }
    
    int j = 0;
    while(str[j] != '\0') {
        history[history_count][j] = str[j];
        j++;
    }
    history[history_count][j] = '\0';
    history_count++;
}

static void str_concat(char* dest, const char* src1, const char* src2) {
    int i = 0;
    while (src1[i] != '\0') {
        dest[i] = src1[i];
        i++;
    }
    int j = 0;
    while (src2[j] != '\0') {
        dest[i] = src2[j];
        i++;
        j++;
    }
    dest[i] = '\0';
}

static int str_cmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

static char* str_chr(char* str, int c) {
    while (*str) {
        if (*str == (char)c) return str;
        str++;
    }
    return 0;
}

static void term_init(void) {
    input_buf[0] = '\0';
    input_len = 0;
    history_count = 0;
    add_to_history("Nykon OS Terminal v1.0");
    add_to_history("Type 'help' for commands.");
    // No change
}

static void term_update(void) {
    char c;
    while (nykon_get_keyboard(&c)) {
        if (c == '\n' || c == '\r') {
            char prompt_buf[150];
            str_concat(prompt_buf, "> ", input_buf);
            add_to_history(prompt_buf);
            
            if (str_cmp(input_buf, "help") == 0) {
                add_to_history("Available commands: help, clear, lock");
            } else if (str_cmp(input_buf, "clear") == 0) {
                history_count = 0;
            } else if (str_cmp(input_buf, "lock") == 0) {
                nykon_lock_screen();
            } else if (input_len > 0) {
                // Check for 'rd' command
                if (input_buf[0] == 'r' && input_buf[1] == 'd' && input_buf[2] == ' ') {
                    const char *rel_path = input_buf + 3; // path after 'rd '
                    unsigned int size = 0;
                    char *content = nykon_file_read(rel_path, &size);
                    if (content) {
                        // split content by newline and add each line to history
                        char *ptr = content;
                        while (ptr && *ptr) {
                            // find newline
                            char *nl = str_chr(ptr, '\n');
                            if (nl) *nl = '\0';
                            add_to_history(ptr);
                            if (!nl) break;
                            ptr = nl + 1;
                        }
                        // assume nykon_file_read allocates memory that should be freed
                        // No explicit free function provided; if needed, ignore.
                    } else {
                        char err_msg[256];
                        str_concat(err_msg, "Unable to read ", rel_path);
                        add_to_history(err_msg);
                    }
                } else {
                    add_to_history("Command not found.");
                }
            }
            
            input_len = 0;
            input_buf[0] = '\0';
            nykon_request_redraw();
        } else if (c == '\b' && input_len > 0) {
            input_len--;
            input_buf[input_len] = '\0';
            nykon_request_redraw();
        } else if (c >= 32 && c <= 126 && input_len < 127) {
            input_buf[input_len++] = c;
            input_buf[input_len] = '\0';
            nykon_request_redraw();
        }
    }
}

static void term_draw(void) {
    int px, py, pw, ph;
    nykon_get_screen_bounds(&px, &py, &pw, &ph);
    
    // Draw background
    nykon_draw_rect(px, py, pw, ph, 0xFF000000); // Black background
    
    // Draw history
    int y = py + 20;
    for (int i = 0; i < history_count; i++) {
        nykon_draw_string(px + 10, y, history[i], 0xFFFFFFFF);
        y += 24;
    }
    
    // Draw current input
    char prompt_buf[150];
    str_concat(prompt_buf, "> ", input_buf);
    
    // Blink cursor
    if ((nykon_get_time() / 500) % 2 == 0) {
        int len = 0;
        while(prompt_buf[len] != '\0') len++;
        prompt_buf[len++] = '_';
        prompt_buf[len] = '\0';
    }
    
    nykon_draw_string(px + 10, y, prompt_buf, 0xFF00FF00); // Green input line
}

NykonApp terminal_app = {
    "Terminal",
    "apps/terminal/icon.png",
    0xFF111111, // Dark tile
    term_init,
    term_update,
    term_draw
};
