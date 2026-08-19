#ifndef NYKON_API_H
#define NYKON_API_H

// Core drawing functions
void nykon_draw_rect(int x, int y, int w, int h, unsigned int color);
void nykon_draw_string(int x, int y, const char *text, unsigned int color);
void nykon_draw_image(const char *filepath, int x, int y);
void nykon_draw_line(int x1, int y1, int x2, int y2, unsigned int color);
void nykon_draw_sprite(const char *filepath, int x, int y, unsigned int color_key);
void nykon_draw_sprite_rotated(const char *filepath, int cx, int cy, int angle, unsigned int color_key);
int nykon_check_pixel_collision(const char *sprite1, int cx1, int cy1, int angle1, const char *sprite2, int x2, int y2, unsigned int color_key);

void nykon_draw_triangle(int x1, int y1, int x2, int y2, int x3, int y3,
                         unsigned int color);
void nykon_draw_filled_circle(int cx, int cy, int r, unsigned int color);
void nykon_draw_string_scaled(int x, int y, const char *text,
                              unsigned int color, int scale);

// Input functions
int nykon_get_mouse(int *x, int *y, int *left_click);
int nykon_get_keyboard(char *ascii_out);
int nykon_get_back_pressed(void);

// System functions
unsigned int nykon_get_time();
void nykon_get_time_of_day(int *hour, int *minute, int *second);

typedef struct {
  int is_dark_mode;
  int fps_enabled;
  char current_wallpaper[100];
} NykonSettings;

void nykon_get_settings(NykonSettings *out_settings);
int nykon_get_back_pressed(void);
void nykon_get_screen_bounds(int *x, int *y, int *w, int *h); 

// Audio functions
void nykon_audio_play(const char *file_path);

// File System functions
int nykon_file_write(const char *path, const char *data, int size);
char *nykon_file_read(const char *path, unsigned int *out_size);

// UI Components
int nykon_button(int x, int y, int w, int h, const char *text);
void nykon_exit_app(void);
void nykon_request_redraw(void);

// The standard Application structure that every app must export
typedef struct {
  const char *name;
  unsigned int icon_color;
  void (*init)(void);
  void (*update)(void);
  void (*draw)(void);
} NykonApp;

#define RGB(r, g, b) (0xFF000000 | ((b) << 16) | ((g) << 8) | (r))

#endif
