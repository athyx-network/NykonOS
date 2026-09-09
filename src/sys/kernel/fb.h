#ifndef FB_H
#define FB_H

void fb_init();
void draw_pixel(int x, int y, unsigned int color);
void draw_rect(int x, int y, int width, int height, unsigned int color);
void draw_filled_circle(int cx, int cy, int r, unsigned int color);
void draw_rounded_rect(int x, int y, int width, int height, int r, unsigned int color);
void draw_line(int x0, int y0, int x1, int y1, unsigned int color);
void draw_triangle(int x1, int y1, int x2, int y2, int x3, int y3, unsigned int color);
void draw_char(int x, int y, char c, unsigned int color);
void draw_string(int x, int y, const char *str, unsigned int color);
void draw_char_scaled(int x, int y, char c, unsigned int color, int scale);
void draw_string_scaled(int x, int y, const char *str, unsigned int color, int scale);
void draw_string_ttf(int x, int y, const char *str, const char *font_path, unsigned int color);
void draw_string_ttf_alpha(int x, int y, const char *str, const char *font_path, unsigned int color, int global_alpha);
void fb_set_font(const char *path);
int fb_get_string_width(const char *str, const char *font_path);
void save_pixels(int x, int y, int w, int h, unsigned int *buffer);
void restore_pixels(int x, int y, int w, int h, unsigned int *buffer);
void draw_cursor(int x, int y);
void fb_swap_buffers();
void fb_swap_rect(int x, int y, int w, int h);

#endif
