#include "../nykon_api.h"
#include "fb.h"
char *fs_get_file_data(const char *path, unsigned int *out_size);
void *memcpy(void *dest, const void *src, unsigned int n);

// Forward declarations of internal OS functions
void draw_rect(int x, int y, int w, int h, unsigned int color);
void draw_string(int x, int y, const char *str, unsigned int color);
void draw_string_scaled(int x, int y, const char *str, unsigned int color,
                        int scale);
int mouse_poll(int *dx, int *dy, int *left_click);
int keyboard_poll(char *ascii_out);
unsigned int get_hw_time();

// Expose these via the API wrappers
void nykon_draw_rect(int x, int y, int w, int h, unsigned int color) {
  draw_rect(x, y, w, h, color);
}

void nykon_draw_string(int x, int y, const char *text, unsigned int color) {
  draw_string(x, y, text, color);
}

void nykon_draw_string_scaled(int x, int y, const char *text,
                              unsigned int color, int scale) {
  draw_string_scaled(x, y, text, color, scale);
}

// Note: Apps shouldn't really poll hardware directly.
// A better OS would pass events to the app, but since our apps
// run in the main loop and have global access, we can expose poll.
// However, main.c already polled mouse and updated cursor_x/cursor_y.
// For apps to get the mouse, they just need the CURRENT cursor_x/y.
// Let's declare the globals from main.c
extern int cursor_x;
extern int cursor_y;
extern int last_left_click;
extern int current_left_click; // We should define this in main.c

// Wait! If an app calls mouse_poll(), it consumes the hardware event,
// breaking the OS mouse cursor. We should NOT let them poll.
// Instead, nykon_get_mouse should just return the global state.
int nykon_get_mouse(int *x, int *y, int *left_click) {
  if (x)
    *x = cursor_x;
  if (y)
    *y = cursor_y;
  // We need to know if it's clicked this frame.
  // main.c needs to export `int app_left_click_state;`
  extern int app_left_click_state;
  if (left_click)
    *left_click = app_left_click_state;
  return 1;
}

void nykon_exit_app(void) {
  extern int current_app_idx;
  extern int current_screen;
  void render_screen(void);
  void fb_swap_buffers(void);

  current_app_idx = -1;
  current_screen = 0; // HOME_SCREEN
  render_screen();
  fb_swap_buffers();
}

void nykon_request_redraw(void) {
  extern int force_screen_redraw;
  force_screen_redraw = 1;
}

void nykon_draw_line(int x1, int y1, int x2, int y2, unsigned int color) {
  draw_line(x1, y1, x2, y2, color);
}

void nykon_draw_triangle(int x1, int y1, int x2, int y2, int x3, int y3,
                         unsigned int color) {
  draw_triangle(x1, y1, x2, y2, x3, y3, color);
}

void nykon_draw_filled_circle(int cx, int cy, int r, unsigned int color) {
  draw_filled_circle(cx, cy, r, color);
}

void nykon_draw_image(const char *filepath, int x, int y) {
  static const char *last_filepath = 0;
  static char *last_data = 0;
  static unsigned int last_size = 0;

  char *data = 0;
  unsigned int size = 0;

  int match = 0;
  if (last_filepath != 0) {
    match = 1;
    for (int i = 0; filepath[i] != '\0' || last_filepath[i] != '\0'; i++) {
      if (filepath[i] != last_filepath[i]) {
        match = 0;
        break;
      }
    }
  }

  if (match) {
    data = last_data;
    size = last_size;
  } else {
    data = fs_get_file_data(filepath, &size);
    if (data) {
      last_filepath = filepath;
      last_data = data;
      last_size = size;
    }
  }

  if (data && size >= 8 && data[0] == 'N' && data[1] == 'Y' && data[2] == 'K' &&
      data[3] == 'N') {
    unsigned short width = *((unsigned short *)(data + 4));
    unsigned short height = *((unsigned short *)(data + 6));
    unsigned int *pixels = (unsigned int *)(data + 8);

    extern unsigned int *back_buffer;

    int start_y = (y < 0) ? -y : 0;
    int start_x = (x < 0) ? -x : 0;
    int end_y = (y + height > 600) ? 600 - y : height;
    int end_x = (x + width > 800) ? 800 - x : width;

    for (int row = start_y; row < end_y; row++) {
      unsigned int *dst = &back_buffer[(y + row) * 800 + x + start_x];
      unsigned int *src = &pixels[row * width + start_x];
      int num_pixels = end_x - start_x;
      memcpy(dst, src, num_pixels * sizeof(unsigned int));
    }
  }
}

const int sin_table[360] = {0, 4, 9, 13, 18, 22, 27, 31, 36, 40, 44, 49, 53, 58, 62, 66, 71, 75, 79, 83, 88, 92, 96, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 139, 143, 147, 150, 154, 158, 161, 165, 168, 171, 175, 178, 181, 184, 187, 190, 193, 196, 199, 202, 204, 207, 210, 212, 215, 217, 219, 222, 224, 226, 228, 230, 232, 234, 236, 237, 239, 241, 242, 243, 245, 246, 247, 248, 249, 250, 251, 252, 253, 254, 254, 255, 255, 255, 256, 256, 256, 256, 256, 256, 256, 255, 255, 255, 254, 254, 253, 252, 251, 250, 249, 248, 247, 246, 245, 243, 242, 241, 239, 237, 236, 234, 232, 230, 228, 226, 224, 222, 219, 217, 215, 212, 210, 207, 204, 202, 199, 196, 193, 190, 187, 184, 181, 178, 175, 171, 168, 165, 161, 158, 154, 150, 147, 143, 139, 136, 132, 128, 124, 120, 116, 112, 108, 104, 100, 96, 92, 88, 83, 79, 75, 71, 66, 62, 58, 53, 49, 44, 40, 36, 31, 27, 22, 18, 13, 9, 4, 0, -4, -9, -13, -18, -22, -27, -31, -36, -40, -44, -49, -53, -58, -62, -66, -71, -75, -79, -83, -88, -92, -96, -100, -104, -108, -112, -116, -120, -124, -128, -132, -136, -139, -143, -147, -150, -154, -158, -161, -165, -168, -171, -175, -178, -181, -184, -187, -190, -193, -196, -199, -202, -204, -207, -210, -212, -215, -217, -219, -222, -224, -226, -228, -230, -232, -234, -236, -237, -239, -241, -242, -243, -245, -246, -247, -248, -249, -250, -251, -252, -253, -254, -254, -255, -255, -255, -256, -256, -256, -256, -256, -256, -256, -255, -255, -255, -254, -254, -253, -252, -251, -250, -249, -248, -247, -246, -245, -243, -242, -241, -239, -237, -236, -234, -232, -230, -228, -226, -224, -222, -219, -217, -215, -212, -210, -207, -204, -202, -199, -196, -193, -190, -187, -184, -181, -178, -175, -171, -168, -165, -161, -158, -154, -150, -147, -143, -139, -136, -132, -128, -124, -120, -116, -112, -108, -104, -100, -96, -92, -88, -83, -79, -75, -71, -66, -62, -58, -53, -49, -44, -40, -36, -31, -27, -22, -18, -13, -9, -4};

static int get_sin(int angle) {
  while (angle < 0) angle += 360;
  while (angle >= 360) angle -= 360;
  return sin_table[angle];
}

static int get_cos(int angle) {
  return get_sin(angle + 90);
}

void nykon_draw_sprite(const char *filepath, int x, int y, unsigned int color_key) {
  extern char *fs_get_file_data(const char *filepath, unsigned int *size_out);
  static const char *last_filepath_sprite = 0;
  static char *last_data_sprite = 0;
  static unsigned int last_size_sprite = 0;

  char *data = 0;
  unsigned int size = 0;

  int match = 0;
  if (last_filepath_sprite && filepath) {
    match = 1;
    for (int i = 0; filepath[i] != '\0' || last_filepath_sprite[i] != '\0'; i++) {
      if (filepath[i] != last_filepath_sprite[i]) {
        match = 0;
        break;
      }
    }
  }

  if (match) {
    data = last_data_sprite;
    size = last_size_sprite;
  } else {
    data = fs_get_file_data(filepath, &size);
    if (data) {
      last_filepath_sprite = filepath;
      last_data_sprite = data;
      last_size_sprite = size;
    }
  }

  if (data && size >= 8 && data[0] == 'N' && data[1] == 'Y' && data[2] == 'K' && data[3] == 'N') {
    unsigned short width = *((unsigned short *)(data + 4));
    unsigned short height = *((unsigned short *)(data + 6));
    unsigned int *pixels = (unsigned int *)(data + 8);

    extern unsigned int *back_buffer;
    extern int phone_x, phone_y, phone_w, phone_h;

    int clip_x1 = phone_x;
    int clip_y1 = phone_y;
    int clip_x2 = phone_x + phone_w;
    int clip_y2 = phone_y + phone_h;

    int start_y = (y < clip_y1) ? clip_y1 - y : 0;
    int start_x = (x < clip_x1) ? clip_x1 - x : 0;
    int end_y = (y + height > clip_y2) ? clip_y2 - y : height;
    int end_x = (x + width > clip_x2) ? clip_x2 - x : width;

    for (int row = start_y; row < end_y; row++) {
      unsigned int *dst = &back_buffer[(y + row) * 800 + x + start_x];
      unsigned int *src = &pixels[row * width + start_x];
      int num_pixels = end_x - start_x;
      for (int col = 0; col < num_pixels; col++) {
        if (src[col] != color_key) {
          dst[col] = src[col];
        }
      }
    }
  }
}

void nykon_draw_sprite_rotated(const char *filepath, int cx, int cy, int angle, unsigned int color_key) {
  extern char *fs_get_file_data(const char *filepath, unsigned int *size_out);
  static const char *last_filepath_rot = 0;
  static char *last_data_rot = 0;
  static unsigned int last_size_rot = 0;

  char *data = 0;
  unsigned int size = 0;

  int match = 0;
  if (last_filepath_rot && filepath) {
    match = 1;
    for (int i = 0; filepath[i] != '\0' || last_filepath_rot[i] != '\0'; i++) {
      if (filepath[i] != last_filepath_rot[i]) {
        match = 0;
        break;
      }
    }
  }

  if (match) {
    data = last_data_rot;
    size = last_size_rot;
  } else {
    data = fs_get_file_data(filepath, &size);
    if (data) {
      last_filepath_rot = filepath;
      last_data_rot = data;
      last_size_rot = size;
    }
  }

  if (data && size >= 8 && data[0] == 'N' && data[1] == 'Y' && data[2] == 'K' && data[3] == 'N') {
    unsigned short width = *((unsigned short *)(data + 4));
    unsigned short height = *((unsigned short *)(data + 6));
    unsigned int *pixels = (unsigned int *)(data + 8);

    extern unsigned int *back_buffer;
    extern int phone_x, phone_y, phone_w, phone_h;

    int clip_x1 = phone_x;
    int clip_y1 = phone_y;
    int clip_x2 = phone_x + phone_w;
    int clip_y2 = phone_y + phone_h;

    int s = get_sin(angle);
    int c = get_cos(angle);
    
    // Iterate over a bounding box that's roughly 1.5x the max dimension
    int max_dim = (width > height ? width : height) * 3 / 2;
    int r = max_dim / 2 + 1;
    
    int hw = width / 2;
    int hh = height / 2;

    for (int dy = -r; dy <= r; dy++) {
      for (int dx = -r; dx <= r; dx++) {
        // Destination pixel on screen
        int px = cx + dx;
        int py = cy + dy;
        
        if (px >= clip_x1 && px < clip_x2 && py >= clip_y1 && py < clip_y2) {
          // Source pixel from image
          int sx = ((dx * c + dy * s) >> 8);
          int sy = ((-dx * s + dy * c) >> 8);
          
          if (sx >= -hw && sx < width - hw && sy >= -hh && sy < height - hh) {
            unsigned int src_color = pixels[(sy + hh) * width + (sx + hw)];
            if (src_color != color_key) {
              back_buffer[py * 800 + px] = src_color;
            }
          }
        }
      }
    }
  }
}
int nykon_check_pixel_collision(const char *sprite1, int cx1, int cy1, int angle1, const char *sprite2, int x2, int y2, unsigned int color_key) {
  extern char *fs_get_file_data(const char *filepath, unsigned int *size_out);
  char *data1 = 0, *data2 = 0;
  unsigned int size1 = 0, size2 = 0;

  data1 = fs_get_file_data(sprite1, &size1);
  data2 = fs_get_file_data(sprite2, &size2);

  if (!data1 || size1 < 8 || data1[0] != 'N' || data1[1] != 'Y' || data1[2] != 'K' || data1[3] != 'N') return 0;
  if (!data2 || size2 < 8 || data2[0] != 'N' || data2[1] != 'Y' || data2[2] != 'K' || data2[3] != 'N') return 0;

  unsigned short w1 = *((unsigned short *)(data1 + 4));
  unsigned short h1 = *((unsigned short *)(data1 + 6));
  unsigned int *p1 = (unsigned int *)(data1 + 8);

  unsigned short w2 = *((unsigned short *)(data2 + 4));
  unsigned short h2 = *((unsigned short *)(data2 + 6));
  unsigned int *p2 = (unsigned int *)(data2 + 8);

  // Calculate bounding box for rotated sprite1
  int max_dim = (w1 > h1 ? w1 : h1) * 3 / 2;
  int r = max_dim / 2 + 1;
  int bb1_x1 = cx1 - r;
  int bb1_y1 = cy1 - r;
  int bb1_x2 = cx1 + r;
  int bb1_y2 = cy1 + r;

  // Bounding box for sprite2
  int bb2_x1 = x2;
  int bb2_y1 = y2;
  int bb2_x2 = x2 + w2;
  int bb2_y2 = y2 + h2;

  // Find intersection
  int ix1 = bb1_x1 > bb2_x1 ? bb1_x1 : bb2_x1;
  int iy1 = bb1_y1 > bb2_y1 ? bb1_y1 : bb2_y1;
  int ix2 = bb1_x2 < bb2_x2 ? bb1_x2 : bb2_x2;
  int iy2 = bb1_y2 < bb2_y2 ? bb1_y2 : bb2_y2;

  if (ix1 >= ix2 || iy1 >= iy2) return 0; // No overlap

  int s = get_sin(angle1);
  int c = get_cos(angle1);
  int hw1 = w1 / 2;
  int hh1 = h1 / 2;

  // Check pixels in intersection
  for (int py = iy1; py < iy2; py++) {
    for (int px = ix1; px < ix2; px++) {
      int sx2 = px - x2;
      int sy2 = py - y2;
      if (sx2 >= 0 && sx2 < w2 && sy2 >= 0 && sy2 < h2) {
        if (p2[sy2 * w2 + sx2] != color_key) {
          int dx = px - cx1;
          int dy = py - cy1;
          int sx1 = ((dx * c + dy * s) >> 8);
          int sy1 = ((-dx * s + dy * c) >> 8);
          if (sx1 >= -hw1 && sx1 < w1 - hw1 && sy1 >= -hh1 && sy1 < h1 - hh1) {
            if (p1[(sy1 + hh1) * w1 + (sx1 + hw1)] != color_key) {
              return 1; // Collision
            }
          }
        }
      }
    }
  }
  return 0;
}
int nykon_get_keyboard(char *ascii_out) {
  // Similarly, apps shouldn't steal keyboard events.
  // We will just expose the raw poll for now, but the OS might miss keys.
  // Actually, main.c polls the keyboard.
  // We'll add a buffer in main.c that apps can read from.
  extern char app_key_pressed;
  if (app_key_pressed != 0) {
    if (ascii_out)
      *ascii_out = app_key_pressed;
    app_key_pressed = 0; // consume
    return 1;
  }
  return 0;
}

int nykon_get_back_pressed(void) {
  extern int system_back_pressed;
  if (system_back_pressed) {
    system_back_pressed = 0;
    return 1;
  }
  return 0;
}

void nykon_get_screen_bounds(int *x, int *y, int *w, int *h) {
  extern int phone_x, phone_y, phone_w, phone_h;
  if (x) *x = phone_x;
  if (y) *y = phone_y;
  if (w) *w = phone_w;
  if (h) *h = phone_h;
}

unsigned int nykon_get_time() { return get_hw_time(); }

void nykon_get_time_of_day(int *hour, int *minute, int *second) {
  unsigned int epoch = get_hw_time();
  unsigned int time_of_day = epoch % 86400;
  if (hour)
    *hour = time_of_day / 3600;
  if (minute)
    *minute = (time_of_day % 3600) / 60;
  if (second)
    *second = time_of_day % 60;
}

extern int is_dark_mode;
extern int fps_enabled;
extern char current_wallpaper[100];

void nykon_get_settings(NykonSettings *out_settings) {
  if (!out_settings)
    return;
  out_settings->is_dark_mode = is_dark_mode;
  out_settings->fps_enabled = fps_enabled;
  int i = 0;
  while (current_wallpaper[i] != '\0' && i < 99) {
    out_settings->current_wallpaper[i] = current_wallpaper[i];
    i++;
  }
  out_settings->current_wallpaper[i] = '\0';
}

int fs_write_file(const char *path, const char *data, unsigned int size);
char *fs_get_file_data(const char *path, unsigned int *out_size);

int nykon_file_write(const char *path, const char *data, int size) {
  return fs_write_file(path, data, (unsigned int)size);
}

char *nykon_file_read(const char *path, unsigned int *out_size) {
  return fs_get_file_data(path, out_size);
}

#ifndef LINUX_BUILD
void *memset(void *s, int c, unsigned int n) {
  unsigned char *p8 = (unsigned char *)s;
  if (((unsigned int)p8 & 3) == 0) {
    unsigned int c32 = (unsigned char)c;
    c32 |= c32 << 8;
    c32 |= c32 << 16;
    unsigned int *p32 = (unsigned int *)p8;
    while (n >= 4) {
      *p32++ = c32;
      n -= 4;
    }
    p8 = (unsigned char *)p32;
  }
  while (n--) {
    *p8++ = (unsigned char)c;
  }
  return s;
}

void *memcpy(void *dest, const void *src, unsigned int n) {
  unsigned char *d8 = (unsigned char *)dest;
  const unsigned char *s8 = (const unsigned char *)src;
  if (((unsigned int)d8 & 3) == 0 && ((unsigned int)s8 & 3) == 0) {
    unsigned int *d32 = (unsigned int *)d8;
    const unsigned int *s32 = (const unsigned int *)s8;
    while (n >= 4) {
      *d32++ = *s32++;
      n -= 4;
    }
    d8 = (unsigned char *)d32;
    s8 = (const unsigned char *)s32;
  }
  while (n--) {
    *d8++ = *s8++;
  }
  return dest;
}
#endif

unsigned int __aeabi_uidiv(unsigned int num, unsigned int den) {
  unsigned int quot = 0, qbit = 1;
  if (den == 0)
    return 0;
  while ((int)den >= 0) {
    den <<= 1;
    qbit <<= 1;
  }
  while (qbit) {
    if (den <= num) {
      num -= den;
      quot += qbit;
    }
    den >>= 1;
    qbit >>= 1;
  }
  return quot;
}

int __aeabi_idiv(int num, int den) {
  int minus = 0;
  if (num < 0) {
    num = -num;
    minus = 1;
  }
  if (den < 0) {
    den = -den;
    minus ^= 1;
  }
  int res = __aeabi_uidiv((unsigned int)num, (unsigned int)den);
  if (minus)
    res = -res;
  return res;
}

unsigned int strlen(const char *str) {
  unsigned int len = 0;
  while (*str++)
    len++;
  return len;
}

int nykon_button(int x, int y, int w, int h, const char *text) {
  extern int cursor_x;
  extern int cursor_y;
  extern int app_left_click_state;

  int hover = (cursor_x >= x && cursor_x <= x + w && cursor_y >= y &&
               cursor_y <= y + h);
  int clicked = hover && app_left_click_state;

  unsigned int bg =
      hover ? 0xFFE0E0E0 : 0xFFC8C8C8; // RGB(224,224,224) vs RGB(200,200,200)
  if (clicked)
    bg = 0xFFB4B4B4; // RGB(180,180,180)

  nykon_draw_rect(x, y, w, h, bg);

  // Attempt to center text roughly
  int text_len = 0;
  while (text[text_len] != '\0')
    text_len++;
  int text_w = text_len * 8; // approx 8px per char

  nykon_draw_string(x + (w - text_w) / 2, y + (h - 16) / 2, text, 0xFF000000);

  return clicked;
}
