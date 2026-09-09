#include "../../sys/nykon_api.h"
#include <stdio.h>
#include <string.h>

#define MAX_HISTORY 64
#define MAX_LINE_LEN 128

static char input_buf[MAX_LINE_LEN];
static int input_len = 0;
static char history[MAX_HISTORY][MAX_LINE_LEN];
static unsigned int history_colors[MAX_HISTORY];
static int history_count = 0;

static void add_line(const char* str, unsigned int color) {
    if (history_count >= MAX_HISTORY) {
        for (int i = 0; i < MAX_HISTORY - 1; i++) {
            int j = 0;
            while (history[i+1][j] != '\0') {
                history[i][j] = history[i+1][j];
                j++;
            }
            history[i][j] = '\0';
            history_colors[i] = history_colors[i+1];
        }
        history_count = MAX_HISTORY - 1;
    }
    
    int j = 0;
    while (str[j] != '\0' && j < MAX_LINE_LEN - 1) {
        history[history_count][j] = str[j];
        j++;
    }
    history[history_count][j] = '\0';
    history_colors[history_count] = color;
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

static void print_neofetch(void) {
    // Read hardware CPU ID via CP15
    unsigned int midr = 0;
#ifndef LINUX_BUILD
    __asm__ volatile("mrc p15, 0, %0, c0, c0, 0" : "=r"(midr));
#endif
    unsigned int part = (midr >> 4) & 0xFFF;
    unsigned int var = (midr >> 20) & 0xF;
    unsigned int rev = midr & 0xF;

    const char *cpu_str;
    if (part == 0xC07) {
        cpu_str = "Allwinner H2+ Cortex-A7 (Quad-Core)";
    } else if (part == 0x926) {
        cpu_str = "ARM926EJ-S (ARMv5TE)";
    } else {
        cpu_str = "ARM Generic Core";
    }

    add_line("Nykon OS v1.0 (Bare-Metal ARM)", 0xFF00E5FF);
    add_line("--------------------------------------", 0xFF555555);
    
#if defined(TARGET_BPI)
    add_line("Host:     Banana Pi M2 Zero", 0xFFFFFFFF);
#else
    add_line("Host:     ARM VersatilePB (QEMU)", 0xFFFFFFFF);
#endif
    
    char cpu_line[64];
    str_concat(cpu_line, "CPU:      ", cpu_str);
    add_line(cpu_line, 0xFFFFFFFF);

#if defined(TARGET_BPI)
    add_line("Memory:   512 MB DDR3 RAM", 0xFFFFFFFF);
    add_line("Display:  HDMI 720p (800x600 Canvas)", 0xFFFFFFFF);
#else
    add_line("Memory:   256 MB RAM", 0xFFFFFFFF);
    add_line("Display:  PL110 (800x600 32bpp)", 0xFFFFFFFF);
#endif
    add_line("--------------------------------------", 0xFF555555);
}

static void term_init(void) {
    input_buf[0] = '\0';
    input_len = 0;
    history_count = 0;
    
    print_neofetch();
    add_line("Type 'help' for available commands.", 0xFFAAAAAA);
    add_line("", 0xFFFFFFFF);
}

static void term_update(void) {
    char c;
    while (nykon_get_keyboard(&c)) {
        if (c == '\n' || c == '\r') {
            char prompt_buf[150];
            str_concat(prompt_buf, "> ", input_buf);
            add_line(prompt_buf, 0xFF00FF7F);
            
            if (str_cmp(input_buf, "help") == 0) {
                add_line("Available commands:", 0xFF00E5FF);
                add_line("  neofetch / fetch - Print system info", 0xFFFFFFFF);
                add_line("  clear / cls      - Clear screen", 0xFFFFFFFF);
                add_line("  ls               - List filesystem contents", 0xFFFFFFFF);
                add_line("  cat <path>       - Read text file", 0xFFFFFFFF);
                add_line("  time             - Display system clock", 0xFFFFFFFF);
                add_line("  lock             - Lock screen", 0xFFFFFFFF);
                add_line("  exit             - Exit terminal", 0xFFFFFFFF);
            } else if (str_cmp(input_buf, "neofetch") == 0 || str_cmp(input_buf, "fetch") == 0) {
                print_neofetch();
            } else if (str_cmp(input_buf, "clear") == 0 || str_cmp(input_buf, "cls") == 0) {
                history_count = 0;
            } else if (str_cmp(input_buf, "lock") == 0) {
                nykon_lock_screen();
            } else if (str_cmp(input_buf, "exit") == 0) {
                nykon_exit_app();
            } else if (str_cmp(input_buf, "ls") == 0 || str_cmp(input_buf, "dir") == 0) {
                add_line("[DIR]  apps/", 0xFF00E5FF);
                add_line("[DIR]  packages/", 0xFF00E5FF);
                add_line("[DIR]  sys/", 0xFF00E5FF);
                add_line("[DIR]  wallpapers/", 0xFF00E5FF);
            } else if (str_cmp(input_buf, "time") == 0) {
                int h, m, s;
                nykon_get_time_of_day(&h, &m, &s);
                char time_buf[32];
                time_buf[0] = 'T'; time_buf[1] = 'i'; time_buf[2] = 'm'; time_buf[3] = 'e'; time_buf[4] = ':'; time_buf[5] = ' ';
                time_buf[6] = (h / 10) + '0'; time_buf[7] = (h % 10) + '0'; time_buf[8] = ':';
                time_buf[9] = (m / 10) + '0'; time_buf[10] = (m % 10) + '0'; time_buf[11] = ':';
                time_buf[12] = (s / 10) + '0'; time_buf[13] = (s % 10) + '0'; time_buf[14] = '\0';
                add_line(time_buf, 0xFFFFFFFF);
            } else if (input_len > 0) {
                if ((input_buf[0] == 'c' && input_buf[1] == 'a' && input_buf[2] == 't' && input_buf[3] == ' ') ||
                    (input_buf[0] == 'r' && input_buf[1] == 'd' && input_buf[2] == ' ')) {
                    const char *rel_path = (input_buf[0] == 'c') ? (input_buf + 4) : (input_buf + 3);
                    unsigned int size = 0;
                    char *content = nykon_file_read(rel_path, &size);
                    if (content) {
                        char *ptr = content;
                        while (ptr && *ptr) {
                            char *nl = str_chr(ptr, '\n');
                            if (nl) *nl = '\0';
                            add_line(ptr, 0xFFE0E0E0);
                            if (!nl) break;
                            ptr = nl + 1;
                        }
                    } else {
                        char err_msg[128];
                        str_concat(err_msg, "File not found: ", rel_path);
                        add_line(err_msg, 0xFFFF5555);
                    }
                } else {
                    char not_found[128];
                    str_concat(not_found, "Command not found: ", input_buf);
                    add_line(not_found, 0xFFFF5555);
                }
            }
            
            input_len = 0;
            input_buf[0] = '\0';
            nykon_request_redraw();
        } else if (c == '\b' && input_len > 0) {
            input_len--;
            input_buf[input_len] = '\0';
            nykon_request_redraw();
        } else if (c >= 32 && c <= 126 && input_len < MAX_LINE_LEN - 1) {
            input_buf[input_len++] = c;
            input_buf[input_len] = '\0';
            nykon_request_redraw();
        }
    }
}

static void term_draw(void) {
    int px, py, pw, ph;
    nykon_get_screen_bounds(&px, &py, &pw, &ph);
    
    // Pure pitch black terminal background
    nykon_draw_rect(px, py, pw, ph, 0xFF000000);
    
    // Calculate visible lines
    int line_height = 20;
    int max_visible = (ph - 40) / line_height;
    int start_idx = 0;
    if (history_count > max_visible) {
        start_idx = history_count - max_visible;
    }
    
    int y = py + 20;
    for (int i = start_idx; i < history_count; i++) {
        nykon_draw_string(px + 10, y, history[i], history_colors[i]);
        y += line_height;
    }
    
    // Draw prompt and input line
    char prompt_buf[160];
    str_concat(prompt_buf, "> ", input_buf);
    
    // Blinking cursor
    if (nykon_get_time() % 2 == 0) {
        int len = 0;
        while (prompt_buf[len] != '\0') len++;
        prompt_buf[len++] = '_';
        prompt_buf[len] = '\0';
    }
    
    nykon_draw_string(px + 10, y, prompt_buf, 0xFF00FF00);
}

NykonApp terminal_app = {
    "Terminal",
    "apps/terminal/icon.png",
    0xFF111111,
    term_init,
    term_update,
    term_draw
};

