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

#elif defined(TARGET_BPI)
// Banana Pi M2 Zero (Allwinner H2+/H3 - 512MB DDR3)
#define FB_BASE           0x5FA00000
#define FB_BASE_ALT1      0x5F000000
#define FB_BASE_ALT2      0x4FA00000

unsigned int *framebuffer = (unsigned int *)FB_BASE;
int screen_width = 800;
int screen_height = 600;

unsigned int *back_buffer = (unsigned int *)0x45000000;

void fb_init() {
    // Framebuffer initialized by U-Boot on HDMI output
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
    extern int phone_x, phone_y, phone_w, phone_h;
    
    // Efficiently copy the active phone canvas and status bar margin (saves ~60% bus bandwidth)
    int start_y = phone_y - 10;
    if (start_y < 0) start_y = 0;
    int end_y = phone_y + phone_h + 20;
    if (end_y > screen_height) end_y = screen_height;
    
    int start_x = phone_x - 10;
    if (start_x < 0) start_x = 0;
    int copy_w = (phone_w + 20) * sizeof(unsigned int);
    if (start_x + phone_w + 20 > screen_width) copy_w = (screen_width - start_x) * sizeof(unsigned int);
    
    for (int i = start_y; i < end_y; i++) {
        memcpy(&framebuffer[i * screen_width + start_x], &back_buffer[i * screen_width + start_x], copy_w);
        #if defined(TARGET_BPI)
        memcpy((void *)(FB_BASE_ALT1 + (i * screen_width + start_x) * sizeof(unsigned int)), &back_buffer[i * screen_width + start_x], copy_w);
        memcpy((void *)(FB_BASE_ALT2 + (i * screen_width + start_x) * sizeof(unsigned int)), &back_buffer[i * screen_width + start_x], copy_w);
        #endif
    }
}

void fb_swap_rect(int x, int y, int w, int h) {
    if (!framebuffer) return;
    int start_x = x < 0 ? 0 : x;
    int start_y = y < 0 ? 0 : y;
    int end_x = x + w > screen_width ? screen_width : x + w;
    int end_y = y + h > screen_height ? screen_height : y + h;
    extern void *memcpy(void *dest, const void *src, unsigned int n);
    for (int i = start_y; i < end_y; i++) {
        unsigned int offset = (i * screen_width + start_x) * sizeof(unsigned int);
        unsigned int len = (end_x - start_x) * sizeof(unsigned int);
        memcpy(&framebuffer[i * screen_width + start_x], &back_buffer[i * screen_width + start_x], len);
        #if defined(TARGET_BPI)
        memcpy((void *)(FB_BASE_ALT1 + offset), &back_buffer[i * screen_width + start_x], len);
        memcpy((void *)(FB_BASE_ALT2 + offset), &back_buffer[i * screen_width + start_x], len);
        #endif
    }
}

void draw_rect(int x, int y, int width, int height, unsigned int color) {
    int start_x = x < 0 ? 0 : x;
    int start_y = y < 0 ? 0 : y;
    int end_x = x + width > screen_width ? screen_width : x + width;
    int end_y = y + height > screen_height ? screen_height : y + height;
    int count = end_x - start_x;
    if (count <= 0) return;
    
    for (int i = start_y; i < end_y; i++) {
        unsigned int *row_ptr = &back_buffer[i * screen_width + start_x];
        int n = count;
        while (n >= 8) {
            row_ptr[0] = color; row_ptr[1] = color; row_ptr[2] = color; row_ptr[3] = color;
            row_ptr[4] = color; row_ptr[5] = color; row_ptr[6] = color; row_ptr[7] = color;
            row_ptr += 8;
            n -= 8;
        }
        while (n > 0) {
            *row_ptr++ = color;
            n--;
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

typedef struct {
    char path[64];
    char *data;
    unsigned int size;
    int font_height;
    int char_offsets[95];
    int char_adv_widths[95];
    int char_bmp_widths[95];
    int valid;
} ParsedFont;

#define MAX_CACHED_FONTS 8
static ParsedFont font_cache[MAX_CACHED_FONTS];
static int font_cache_count = 0;

static ParsedFont *get_parsed_font(const char *font_path) {
    if (!font_path) return NULL;
    
    // Check if already in cache
    for (int i = 0; i < font_cache_count; i++) {
        if (font_cache[i].valid) {
            int match = 1;
            for (int k = 0; k < 64; k++) {
                if (font_cache[i].path[k] != font_path[k]) {
                    match = 0;
                    break;
                }
                if (font_path[k] == '\0') break;
            }
            if (match) return &font_cache[i];
        }
    }
    
    // Load from filesystem and parse once
    unsigned int size = 0;
    char *data = fs_get_file_data(font_path, &size);
    if (!data || size < 6 || data[0] != 'N' || data[1] != 'Y' || data[2] != 'F' || data[3] != 'N') {
        return NULL;
    }
    
    int slot = font_cache_count;
    if (slot >= MAX_CACHED_FONTS) slot = MAX_CACHED_FONTS - 1;
    else font_cache_count++;
    
    ParsedFont *pf = &font_cache[slot];
    int p_idx = 0;
    while (font_path[p_idx] != '\0' && p_idx < 63) {
        pf->path[p_idx] = font_path[p_idx];
        p_idx++;
    }
    pf->path[p_idx] = '\0';
    pf->data = data;
    pf->size = size;
    pf->font_height = (unsigned char)data[4];
    
    int current_offset = 5;
    for (int i = 0; i < 95; i++) {
        if (current_offset >= size - 1) {
            pf->char_adv_widths[i] = 8;
            pf->char_bmp_widths[i] = 8;
            pf->char_offsets[i] = 5;
            continue;
        }
        int adv_w = (unsigned char)data[current_offset++];
        int bmp_w = (unsigned char)data[current_offset++];
        pf->char_adv_widths[i] = adv_w;
        pf->char_bmp_widths[i] = bmp_w;
        pf->char_offsets[i] = current_offset;
        current_offset += (bmp_w * pf->font_height);
    }
    pf->valid = 1;
    return pf;
}

int fb_get_string_width(const char *str, const char *font_path) {
    if (font_path == 0 || str == 0) return 0;
    
    ParsedFont *pf = get_parsed_font(font_path);
    if (!pf) {
        int len = 0;
        while (str[len]) len++;
        return len * 8; // fallback basic font width
    }
    
    int width = 0;
    while (*str) {
        char c = *str;
        if (c >= 32 && c <= 126) {
            width += pf->char_adv_widths[c - 32];
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
    while (*str) {
        draw_char_scaled(x, y, *str, color, scale);
        x += 8 * scale;
        str++;
    }
}

void draw_string_ttf(int x, int y, const char *str, const char *font_path, unsigned int color) {
    if (font_path == 0 || str == 0) return;
    
    ParsedFont *pf = get_parsed_font(font_path);
    if (!pf) {
        draw_string_basic(x, y, str, color);
        return;
    }
    
    int font_height = pf->font_height;
    char *data = pf->data;
    int cursor_x = x;
    
    unsigned int fg_r = color & 0xFF;
    unsigned int fg_g = (color >> 8) & 0xFF;
    unsigned int fg_b = (color >> 16) & 0xFF;

    while (*str) {
        char c = *str;
        if (c >= 32 && c <= 126) {
            int idx = c - 32;
            int adv_w = pf->char_adv_widths[idx];
            int bmp_w = pf->char_bmp_widths[idx];
            int offset = pf->char_offsets[idx];
            
            for (int r = 0; r < font_height; r++) {
                int px_y = y + r;
                if (px_y < 0 || px_y >= screen_height) continue;
                
                unsigned int *dst_row = &back_buffer[px_y * screen_width + cursor_x];
                const unsigned char *src_row = (const unsigned char *)&data[offset + r * bmp_w];
                
                for (int c_col = 0; c_col < bmp_w; c_col++) {
                    int px_x = cursor_x + c_col;
                    if (px_x < 0 || px_x >= screen_width) continue;
                    
                    unsigned int alpha = src_row[c_col];
                    if (alpha == 0) continue;
                    
                    if (alpha == 255) {
                        dst_row[c_col] = color;
                    } else {
                        unsigned int bg = dst_row[c_col];
                        unsigned int bg_r = bg & 0xFF;
                        unsigned int bg_g = (bg >> 8) & 0xFF;
                        unsigned int bg_b = (bg >> 16) & 0xFF;
                        
                        unsigned int inv = 255 - alpha;
                        unsigned int out_r = ((fg_r * alpha + bg_r * inv) + 128) >> 8;
                        unsigned int out_g = ((fg_g * alpha + bg_g * inv) + 128) >> 8;
                        unsigned int out_b = ((fg_b * alpha + bg_b * inv) + 128) >> 8;
                        
                        dst_row[c_col] = 0xFF000000 | (out_b << 16) | (out_g << 8) | out_r;
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

void draw_string_ttf_alpha(int x, int y, const char *str, const char *font_path, unsigned int color, int global_alpha) {
    if (font_path == 0 || str == 0 || global_alpha <= 0) return;
    if (global_alpha >= 255) {
        draw_string_ttf(x, y, str, font_path, color);
        return;
    }
    
    ParsedFont *pf = get_parsed_font(font_path);
    if (!pf) {
        draw_string_basic(x, y, str, color);
        return;
    }
    
    int font_height = pf->font_height;
    char *data = pf->data;
    int cursor_x = x;
    
    unsigned int fg_r = color & 0xFF;
    unsigned int fg_g = (color >> 8) & 0xFF;
    unsigned int fg_b = (color >> 16) & 0xFF;

    while (*str) {
        char c = *str;
        if (c >= 32 && c <= 126) {
            int idx = c - 32;
            int adv_w = pf->char_adv_widths[idx];
            int bmp_w = pf->char_bmp_widths[idx];
            int offset = pf->char_offsets[idx];
            
            for (int r = 0; r < font_height; r++) {
                int px_y = y + r;
                if (px_y < 0 || px_y >= screen_height) continue;
                
                unsigned int *dst_row = &back_buffer[px_y * screen_width + cursor_x];
                const unsigned char *src_row = (const unsigned char *)&data[offset + r * bmp_w];
                
                for (int c_col = 0; c_col < bmp_w; c_col++) {
                    int px_x = cursor_x + c_col;
                    if (px_x < 0 || px_x >= screen_width) continue;
                    
                    unsigned int glyph_alpha = src_row[c_col];
                    if (glyph_alpha == 0) continue;
                    
                    unsigned int alpha = (glyph_alpha * global_alpha + 128) >> 8;
                    if (alpha == 0) continue;
                    
                    unsigned int bg = dst_row[c_col];
                    unsigned int bg_r = bg & 0xFF;
                    unsigned int bg_g = (bg >> 8) & 0xFF;
                    unsigned int bg_b = (bg >> 16) & 0xFF;
                    
                    unsigned int inv = 255 - alpha;
                    unsigned int out_r = ((fg_r * alpha + bg_r * inv) + 128) >> 8;
                    unsigned int out_g = ((fg_g * alpha + bg_g * inv) + 128) >> 8;
                    unsigned int out_b = ((fg_b * alpha + bg_b * inv) + 128) >> 8;
                    
                    dst_row[c_col] = 0xFF000000 | (out_b << 16) | (out_g << 8) | out_r;
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
