#include "fb.h"
#include "font.h"
#include "fs.h"

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

void draw_string_basic(int x, int y, const char *str, unsigned int color) {
    while (*str) {
        draw_char(x, y, *str, color);
        x += 8;
        str++;
    }
}
char sys_font_path[100] = "sys/fonts/Roboto-Regular.nfn";

void fb_set_font(const char *path) {
    int i = 0;
    while(path[i] != '\0' && i < 99) {
        sys_font_path[i] = path[i];
        i++;
    }
    sys_font_path[i] = '\0';
}

void draw_string(int x, int y, const char *str, unsigned int color) {
    draw_string_ttf(x, y, str, sys_font_path, color);
}

int fb_get_string_width(const char *str, const char *font_path) {
    if (font_path == 0 || str == 0) return 0;
    
    unsigned int size = 0;
    char *data = fs_get_file_data(font_path, &size);
    if (!data || size < 6 || data[0] != 'N') {
        int len = 0;
        while (str[len]) len++;
        return len * 8; // fallback basic font width
    }
    
    int font_height = (unsigned char)data[4];
    int char_adv_widths[95];
    int current_offset = 5;
    for (int i = 0; i < 95; i++) {
        if (current_offset >= size - 1) break;
        int adv_w = (unsigned char)data[current_offset++];
        int bmp_w = (unsigned char)data[current_offset++];
        char_adv_widths[i] = adv_w;
        current_offset += (bmp_w * font_height);
    }
    
    int width = 0;
    while (*str) {
        char c = *str;
        if (c >= 32 && c <= 126) {
            width += char_adv_widths[c - 32];
        } else if (c == ' ') {
            width += 4;
        }
        str++;
    }
    return width;
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
    // Basic scaling
    while (*str) {
        draw_char_scaled(x, y, *str, color, scale);
        x += 8 * scale;
        str++;
    }
}

static char *cached_font_data = 0;
static char cached_font_path[100] = "";
static unsigned int cached_font_size = 0;

void draw_string_ttf(int x, int y, const char *str, const char *font_path, unsigned int color) {
    if (font_path == 0 || str == 0) return;
    
    int same_font = 1;
    for (int i = 0; i < 100; i++) {
        if (cached_font_path[i] != font_path[i]) {
            same_font = 0;
            break;
        }
        if (font_path[i] == '\0') break;
    }
    
    if (!same_font || cached_font_data == 0) {
        cached_font_data = fs_get_file_data(font_path, &cached_font_size);
        if (cached_font_data) {
            int i = 0;
            while(font_path[i] != '\0' && i < 99) {
                cached_font_path[i] = font_path[i];
                i++;
            }
            cached_font_path[i] = '\0';
        } else {
            draw_string_basic(x, y, str, color);
            return;
        }
    }
    
    if (cached_font_size < 6 || cached_font_data[0] != 'N' || cached_font_data[1] != 'Y' || cached_font_data[2] != 'F' || cached_font_data[3] != 'N') {
        draw_string_basic(x, y, str, color);
        return;
    }
    
    int font_height = (unsigned char)cached_font_data[4];
    
    int char_offsets[95];
    int char_adv_widths[95];
    int char_bmp_widths[95];
    int current_offset = 5;
    for (int i = 0; i < 95; i++) {
        if (current_offset >= cached_font_size - 1) break;
        int adv_w = (unsigned char)cached_font_data[current_offset++];
        int bmp_w = (unsigned char)cached_font_data[current_offset++];
        char_adv_widths[i] = adv_w;
        char_bmp_widths[i] = bmp_w;
        char_offsets[i] = current_offset;
        current_offset += (bmp_w * font_height);
    }
    
    int cursor_x = x;
    while (*str) {
        char c = *str;
        if (c >= 32 && c <= 126) {
            int idx = c - 32;
            int adv_w = char_adv_widths[idx];
            int bmp_w = char_bmp_widths[idx];
            int offset = char_offsets[idx];
            
            for (int r = 0; r < font_height; r++) {
                for (int c_col = 0; c_col < bmp_w; c_col++) {
                    unsigned char alpha = cached_font_data[offset + r * bmp_w + c_col];
                    if (alpha > 0) {
                        int px_x = cursor_x + c_col;
                        int px_y = y + r;
                        if (px_x >= 0 && px_x < screen_width && px_y >= 0 && px_y < screen_height) {
                            if (alpha == 255) {
                                draw_pixel(px_x, px_y, color);
                            } else {
                                unsigned int bg = back_buffer[px_y * screen_width + px_x];
                                unsigned int bg_r = bg & 0xFF;
                                unsigned int bg_g = (bg >> 8) & 0xFF;
                                unsigned int bg_b = (bg >> 16) & 0xFF;
                                
                                unsigned int fg_r = color & 0xFF;
                                unsigned int fg_g = (color >> 8) & 0xFF;
                                unsigned int fg_b = (color >> 16) & 0xFF;
                                
                                unsigned int out_r = (fg_r * alpha + bg_r * (255 - alpha)) / 255;
                                unsigned int out_g = (fg_g * alpha + bg_g * (255 - alpha)) / 255;
                                unsigned int out_b = (fg_b * alpha + bg_b * (255 - alpha)) / 255;
                                
                                draw_pixel(px_x, px_y, 0xFF000000 | (out_b << 16) | (out_g << 8) | out_r);
                            }
                        }
                    }
                }
            }
            cursor_x += adv_w;
        } else if (c == ' ') {
            cursor_x += 4;
        }
        str++;
    }
}
void draw_pixel_blend(int x, int y, unsigned int color, int alpha);

void draw_circle(int cx, int cy, int r, int thickness, unsigned int color) {
    int r_out_scaled = r * 256;
    int r_in_scaled = (r - thickness) * 256;
    for (int y = -r - 1; y <= r + 1; y++) {
        for (int x = -r - 1; x <= r + 1; x++) {
            int d2 = x * x + y * y;
            if (d2 <= (r - thickness - 1) * (r - thickness - 1)) {
                continue;
            } else if (d2 > (r + 1) * (r + 1)) {
                continue;
            }
            int val = d2 << 16;
            unsigned int temp, g = 0;
            unsigned int b = 0x8000;
            while (b > 0) {
                temp = g + b;
                if (temp * temp <= (unsigned int)val) {
                    g = temp;
                }
                b >>= 1;
            }
            int dist_scaled = g;
            int alpha_out = (r_out_scaled + 128) - dist_scaled;
            int alpha_in = dist_scaled - (r_in_scaled - 128);
            int alpha = (alpha_out < alpha_in) ? alpha_out : alpha_in;
            if (alpha < 0) alpha = 0;
            if (alpha > 255) alpha = 255;
            if (alpha > 0) {
                draw_pixel_blend(cx + x, cy + y, color, alpha);
            }
        }
    }
}

static unsigned int blend_colors(unsigned int fg, unsigned int bg, int alpha) {
    if (alpha <= 0) return bg;
    if (alpha >= 255) return fg;
    
    unsigned char fg_r = (fg >> 16) & 0xFF;
    unsigned char fg_g = (fg >> 8) & 0xFF;
    unsigned char fg_b = fg & 0xFF;
    
    unsigned char bg_r = (bg >> 16) & 0xFF;
    unsigned char bg_g = (bg >> 8) & 0xFF;
    unsigned char bg_b = bg & 0xFF;
    
    unsigned char r = (unsigned char)((fg_r * alpha + bg_r * (255 - alpha)) / 255);
    unsigned char g = (unsigned char)((fg_g * alpha + bg_g * (255 - alpha)) / 255);
    unsigned char b = (unsigned char)((fg_b * alpha + bg_b * (255 - alpha)) / 255);
    
    return (r << 16) | (g << 8) | b;
}

void draw_pixel_blend(int x, int y, unsigned int color, int alpha) {
    if (alpha <= 0) return;
    if (alpha >= 255) {
        draw_pixel(x, y, color);
        return;
    }
    extern unsigned int *back_buffer;
    extern int screen_width, screen_height;
    if (x >= 0 && x < screen_width && y >= 0 && y < screen_height) {
        unsigned int bg = back_buffer[y * screen_width + x];
        back_buffer[y * screen_width + x] = blend_colors(color, bg, alpha);
    }
}

void draw_filled_circle(int cx, int cy, int r, unsigned int color) {
    int r_scaled = r * 256;
    for (int y = -r - 1; y <= r + 1; y++) {
        for (int x = -r - 1; x <= r + 1; x++) {
            int d2 = x * x + y * y;
            if (d2 <= (r - 1) * (r - 1)) {
                draw_pixel(cx + x, cy + y, color);
            } else if (d2 <= (r + 1) * (r + 1)) {
                int val = d2 << 16;
                unsigned int temp, g = 0;
                unsigned int b = 0x8000;
                while (b > 0) {
                    temp = g + b;
                    if (temp * temp <= (unsigned int)val) {
                        g = temp;
                    }
                    b >>= 1;
                }
                int dist_scaled = g;
                int alpha = (r_scaled + 128) - dist_scaled;
                if (alpha < 0) alpha = 0;
                if (alpha > 255) alpha = 255;
                draw_pixel_blend(cx + x, cy + y, color, alpha);
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
    #define ABS(x) ((x) < 0 ? -(x) : (x))
    int steep = ABS(y1 - y0) > ABS(x1 - x0);
    if (steep) {
        int temp = x0; x0 = y0; y0 = temp;
        temp = x1; x1 = y1; y1 = temp;
    }
    if (x0 > x1) {
        int temp = x0; x0 = x1; x1 = temp;
        temp = y0; y0 = y1; y1 = temp;
    }
    
    int dx = x1 - x0;
    int dy = y1 - y0;
    
    int gradient = 0;
    if (dx != 0) {
        gradient = (dy << 8) / dx;
    }
    
    int intery = y0 << 8;
    
    if (steep) {
        for (int x = x0; x <= x1; x++) {
            int ipart = intery >> 8;
            int fpart = intery & 0xFF;
            draw_pixel_blend(ipart, x, color, 255 - fpart);
            draw_pixel_blend(ipart + 1, x, color, fpart);
            intery += gradient;
        }
    } else {
        for (int x = x0; x <= x1; x++) {
            int ipart = intery >> 8;
            int fpart = intery & 0xFF;
            draw_pixel_blend(x, ipart, color, 255 - fpart);
            draw_pixel_blend(x, ipart + 1, color, fpart);
            intery += gradient;
        }
    }
}

void draw_triangle(int x1, int y1, int x2, int y2, int x3, int y3, unsigned int color) {
    draw_line(x1, y1, x2, y2, color);
    draw_line(x2, y2, x3, y3, color);
    draw_line(x3, y3, x1, y1, color);
}
