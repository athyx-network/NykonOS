#include "fb.h"
#include "font.h"

#ifndef NULL
#define NULL ((void *)0)
#endif

#ifdef LINUX_BUILD
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>

int fbfd = -1;
unsigned int *framebuffer = NULL;
unsigned int *back_buffer = NULL;
int screen_width = 800;
int screen_height = 600;

void fb_init() {
    fbfd = open("/dev/fb0", O_RDWR);
    if (fbfd == -1) {
        printf("Error: cannot open framebuffer device.\n");
        return;
    }
    struct fb_var_screeninfo vinfo;
    if (ioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo) == -1) {
        printf("Error reading variable information.\n");
        return;
    }
    screen_width = vinfo.xres;
    screen_height = vinfo.yres;
    long screensize = vinfo.yres_virtual * vinfo.xres_virtual * vinfo.bits_per_pixel / 8;
    framebuffer = (unsigned int *)mmap(0, screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);
    
    // Allocate back buffer (simple malloc since we are in user space on Linux)
    #include <stdlib.h>
    back_buffer = (unsigned int *)malloc(screen_width * screen_height * sizeof(unsigned int));
}

#else
#define SYS_OSCCLK4       (*(volatile unsigned int *)0x1000001c)
#define PL110_TIM0        (*(volatile unsigned int *)0x10120000)
#define PL110_TIM1        (*(volatile unsigned int *)0x10120004)
#define PL110_TIM2        (*(volatile unsigned int *)0x10120008)
#define PL110_UPBASE      (*(volatile unsigned int *)0x10120010)
#define PL110_CONTROL     (*(volatile unsigned int *)0x10120018)

#define FB_BASE           0x00200000

unsigned int *framebuffer = (unsigned int *)FB_BASE;
int screen_width = 800;
int screen_height = 600;

unsigned int *back_buffer = (unsigned int *)0x00400000;

void fb_init() {
    SYS_OSCCLK4 = 0x2CAC;
    PL110_TIM0 = 0x1313A4C4;
    PL110_TIM1 = 0x0505F657;
    PL110_TIM2 = 0x071F1800;
    PL110_UPBASE = FB_BASE;
    PL110_CONTROL = 0x82b; 
}
#endif

void draw_pixel(int x, int y, unsigned int color) {
    if (x >= 0 && x < screen_width && y >= 0 && y < screen_height) {
        back_buffer[y * screen_width + x] = color;
    }
}

void fb_swap_buffers() {
    if (!framebuffer) return;
    extern void *memcpy(void *dest, const void *src, unsigned int n);
    memcpy(framebuffer, back_buffer, screen_width * screen_height * sizeof(unsigned int));
}

void fb_swap_rect(int x, int y, int w, int h) {
    if (!framebuffer) return;
    int start_x = x < 0 ? 0 : x;
    int start_y = y < 0 ? 0 : y;
    int end_x = x + w > screen_width ? screen_width : x + w;
    int end_y = y + h > screen_height ? screen_height : y + h;
    extern void *memcpy(void *dest, const void *src, unsigned int n);
    for (int i = start_y; i < end_y; i++) {
        memcpy(&framebuffer[i * screen_width + start_x], &back_buffer[i * screen_width + start_x], (end_x - start_x) * sizeof(unsigned int));
    }
}

void draw_rect(int x, int y, int width, int height, unsigned int color) {
    int start_x = x < 0 ? 0 : x;
    int start_y = y < 0 ? 0 : y;
    int end_x = x + width > screen_width ? screen_width : x + width;
    int end_y = y + height > screen_height ? screen_height : y + height;
    
    for (int i = start_y; i < end_y; i++) {
        unsigned int *row_ptr = &back_buffer[i * screen_width + start_x];
        for (int j = start_x; j < end_x; j++) {
            *row_ptr++ = color;
        }
    }
}

void draw_char(int x, int y, char c, unsigned int color) {
    if (c < 0 || c > 127) return;
    char *bitmap = font8x8_basic[(int)c];
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            if ((bitmap[i] >> j) & 1) {
                draw_pixel(x + j, y + i, color);
            }
        }
    }
}

void draw_string(int x, int y, const char *str, unsigned int color) {
    while (*str) {
        draw_char(x, y, *str, color);
        x += 8;
        str++;
    }
}

void draw_char_scaled(int x, int y, char c, unsigned int color, int scale) {
    if (c < 0 || c > 127) return;
    char *bitmap = font8x8_basic[(int)c];
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            if ((bitmap[i] >> j) & 1) {
                draw_rect(x + j * scale, y + i * scale, scale, scale, color);
            }
        }
    }
}

void draw_string_scaled(int x, int y, const char *str, unsigned int color, int scale) {
    while (*str) {
        draw_char_scaled(x, y, *str, color, scale);
        x += 8 * scale;
        str++;
    }
}

void draw_circle(int cx, int cy, int r, int thickness, unsigned int color) {
    int r2_out = r * r;
    int r2_in = (r - thickness) * (r - thickness);
    for (int y = -r; y <= r; y++) {
        for (int x = -r; x <= r; x++) {
            int d2 = x * x + y * y;
            if (d2 <= r2_out && d2 >= r2_in) {
                draw_pixel(cx + x, cy + y, color);
            }
        }
    }
}

void draw_filled_circle(int cx, int cy, int r, unsigned int color) {
    for (int y = -r; y <= r; y++) {
        for (int x = -r; x <= r; x++) {
            if (x * x + y * y <= r * r) {
                draw_pixel(cx + x, cy + y, color);
            }
        }
    }
}

void draw_rounded_rect(int x, int y, int w, int h, int r, unsigned int color) {
    draw_rect(x + r, y, w - 2 * r, h, color);
    draw_rect(x, y + r, w, h - 2 * r, color);
    draw_filled_circle(x + r, y + r, r, color);
    draw_filled_circle(x + w - 1 - r, y + r, r, color);
    draw_filled_circle(x + r, y + h - 1 - r, r, color);
    draw_filled_circle(x + w - 1 - r, y + h - 1 - r, r, color);
}

void save_pixels(int x, int y, int w, int h, unsigned int *buffer) {
    int idx = 0;
    for (int i = y; i < y + h; i++) {
        for (int j = x; j < x + w; j++) {
            if (j >= 0 && j < screen_width && i >= 0 && i < screen_height) {
                buffer[idx++] = back_buffer[i * screen_width + j];
            } else {
                buffer[idx++] = 0;
            }
        }
    }
}

void restore_pixels(int x, int y, int w, int h, unsigned int *buffer) {
    int idx = 0;
    for (int i = y; i < y + h; i++) {
        for (int j = x; j < x + w; j++) {
            if (j >= 0 && j < screen_width && i >= 0 && i < screen_height) {
                back_buffer[i * screen_width + j] = buffer[idx];
            }
            idx++;
        }
    }
}

static const unsigned char cursor_bitmap[16][16] = {
    {2,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {2,1,2,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {2,1,1,2,0,0,0,0,0,0,0,0,0,0,0,0},
    {2,1,1,1,2,0,0,0,0,0,0,0,0,0,0,0},
    {2,1,1,1,1,2,0,0,0,0,0,0,0,0,0,0},
    {2,1,1,1,1,1,2,0,0,0,0,0,0,0,0,0},
    {2,1,1,1,1,1,1,2,0,0,0,0,0,0,0,0},
    {2,1,1,1,1,1,1,1,2,0,0,0,0,0,0,0},
    {2,1,1,1,1,1,1,1,1,2,0,0,0,0,0,0},
    {2,1,1,1,1,1,2,2,2,2,0,0,0,0,0,0},
    {2,1,1,2,1,1,2,0,0,0,0,0,0,0,0,0},
    {2,1,2,0,2,1,1,2,0,0,0,0,0,0,0,0},
    {2,2,0,0,2,1,1,2,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,2,1,1,2,0,0,0,0,0,0,0},
    {0,0,0,0,0,2,1,1,2,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,2,2,0,0,0,0,0,0,0,0}
};

void draw_cursor(int x, int y) {
    for (int i = 0; i < 16; i++) {
        for (int j = 0; j < 16; j++) {
            if (cursor_bitmap[i][j] == 1) {
                draw_pixel(x + j, y + i, 0x00000000); // Black fill
            } else if (cursor_bitmap[i][j] == 2) {
                draw_pixel(x + j, y + i, 0x00FFFFFF); // White outline
            }
        }
    }
}

void draw_line(int x0, int y0, int x1, int y1, unsigned int color) {
    int dx = x1 - x0;
    if (dx < 0) dx = -dx;
    int dy = y1 - y0;
    if (dy < 0) dy = -dy;
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = (dx > dy ? dx : -dy) / 2, e2;
    
    while (1) {
        draw_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        e2 = err;
        if (e2 > -dx) { err -= dy; x0 += sx; }
        if (e2 < dy) { err += dx; y0 += sy; }
    }
}

void draw_triangle(int x1, int y1, int x2, int y2, int x3, int y3, unsigned int color) {
    draw_line(x1, y1, x2, y2, color);
    draw_line(x2, y2, x3, y3, color);
    draw_line(x3, y3, x1, y1, color);
}
