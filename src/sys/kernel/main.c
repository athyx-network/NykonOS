#include "fb.h"
#include "mouse.h"
#include "keyboard.h"
#include "fs.h"
#include "mm.h"
#include "../nykon_api.h"

extern NykonApp* registered_apps[];
extern int num_registered_apps;

int current_app_idx = -1; // -1 means no third-party app is running

// API variables exported for api.c
int app_left_click_state = 0;
int app_mouse_down = 0;
int system_back_pressed = 0;
char app_key_pressed = 0;

#define RGB(r, g, b) (0xFF000000 | ((b) << 16) | ((g) << 8) | (r))

typedef struct {
    const char *name;
    unsigned char top_r, top_g, top_b;
    unsigned char bot_r, bot_g, bot_b;
    unsigned char border_r, border_g, border_b;
} IconTheme;

IconTheme icon_themes[] = {
    {"Dark Slate",     70, 70, 70,    10, 10, 10,   140, 140, 140},
    {"Midnight Blue",  40, 70, 130,   10, 15, 40,   100, 150, 230},
    {"Emerald Cyber",  30, 110, 60,   10, 35, 20,    70, 180, 110},
    {"Crimson Wine",  130, 30, 55,    35, 10, 18,   200,  70, 100},
    {"Sunset Amber",  140, 80, 20,    40, 20, 10,   220, 140,  60},
    {"Amethyst Purple", 90, 40, 130,  25, 10, 40,   160,  90, 210},
    {"Neon Cyan",      20, 120, 140,  10, 35, 45,    60, 200, 220},
    {"Pure Black",     25, 25, 25,     0,  0,  0,   120, 120, 120},
    {"Frost Silver",  220, 220, 225, 160, 160, 170, 255, 255, 255},
};
#define NUM_ICON_THEMES (sizeof(icon_themes)/sizeof(icon_themes[0]))

int current_icon_theme = 0; // Default: Dark Slate

void draw_icon_border(int x, int y, unsigned int border_color) {
    extern void draw_pixel(int px, int py, unsigned int color);
    int r = 12;
    for (int j = 0; j < 48; j++) {
        for (int i = 0; i < 48; i++) {
            int is_border = 0;
            if (i >= r && i < 48 - r) {
                if (j == 0 || j == 47) is_border = 1;
            } else if (j >= r && j < 48 - r) {
                if (i == 0 || i == 47) is_border = 1;
            } else {
                int cx = (i < r) ? r : 48 - 1 - r;
                int cy = (j < r) ? r : 48 - 1 - r;
                int dx = i - cx;
                int dy = j - cy;
                int d2 = dx * dx + dy * dy;
                if (d2 <= r * r && d2 >= (r - 1) * (r - 1)) {
                    is_border = 1;
                }
            }
            if (is_border) {
                draw_pixel(x + i, y + j, border_color);
            }
        }
    }
}

void draw_icon_generic(int x, int y, unsigned int color) {
    extern void draw_pixel(int px, int py, unsigned int color);
    int r = 12;
    IconTheme theme = icon_themes[current_icon_theme];
    unsigned int border_color = RGB(theme.border_r, theme.border_g, theme.border_b);

    for (int j = 0; j < 48; j++) {
        // Interpolate gradient from top to bottom
        int cur_r = theme.top_r + ((int)(theme.bot_r - theme.top_r) * j / 48);
        int cur_g = theme.top_g + ((int)(theme.bot_g - theme.top_g) * j / 48);
        int cur_b = theme.top_b + ((int)(theme.bot_b - theme.top_b) * j / 48);
        unsigned int row_color = RGB(cur_r, cur_g, cur_b);
        
        for (int i = 0; i < 48; i++) {
            int in_rect = 0;
            int is_border = 0;

            if (i >= r && i < 48 - r) {
                in_rect = 1;
                if (j == 0 || j == 47) is_border = 1;
            } else if (j >= r && j < 48 - r) {
                in_rect = 1;
                if (i == 0 || i == 47) is_border = 1;
            } else {
                int cx = (i < r) ? r : 48 - 1 - r;
                int cy = (j < r) ? r : 48 - 1 - r;
                int dx = i - cx;
                int dy = j - cy;
                int d2 = dx * dx + dy * dy;
                if (d2 <= r * r) {
                    in_rect = 1;
                    if (d2 >= (r - 1) * (r - 1)) {
                        is_border = 1;
                    }
                }
            }
            if (in_rect) {
                draw_pixel(x + i, y + j, is_border ? border_color : row_color);
            }
        }
    }
}

#ifdef LINUX_BUILD
#include <time.h>
unsigned int get_hw_time() {
    return (unsigned int)time(NULL);
}
#elif defined(TARGET_BPI)
volatile unsigned int * const SUNXI_TIMER_CNT = (unsigned int *)0x01C20CA0;
unsigned int get_hw_time() {
    return *SUNXI_TIMER_CNT / 24000000;
}
#else
volatile unsigned int * const RTC0_DR = (unsigned int *)0x101E8000;
unsigned int get_hw_time() {
    return *RTC0_DR;
}
#endif

unsigned int boot_epoch = 0;
unsigned int get_system_uptime() {
    if (boot_epoch == 0) return 0;
    unsigned int now = get_hw_time();
    if (now >= boot_epoch) return now - boot_epoch;
    return 0;
}

void get_current_time(char *buffer) {
    unsigned int epoch = get_hw_time();
    unsigned int time_of_day = epoch % 86400;
    unsigned int hour = time_of_day / 3600;
    unsigned int minute = (time_of_day % 3600) / 60;
    
    unsigned int display_hour = hour % 12;
    if (display_hour == 0) display_hour = 12;
    
    int idx = 0;
    if (display_hour >= 10) {
        buffer[idx++] = (display_hour / 10) + '0';
    }
    buffer[idx++] = (display_hour % 10) + '0';
    buffer[idx++] = ':';
    buffer[idx++] = (minute / 10) + '0';
    buffer[idx++] = (minute % 10) + '0';
    buffer[idx++] = '\0';
}

#define HOME_SCREEN 0
#define SETTINGS_SCREEN 1
#define ABOUT_SCREEN 2
#define FILES_SCREEN 3
#define FILE_VIEWER_SCREEN 4
#define PERSONALIZATION_SCREEN 5
#define LOCK_SCREEN 6
#define DEV_OPTIONS_SCREEN 8
#define KEYBOARD_TEST_SCREEN 9
#define MEDIA_PLAYER_SCREEN 10
#define APPS_LIST_SCREEN 11
#define ICON_BG_SCREEN 13
#define INSTALLER_SCREEN 14
#define FONTS_SCREEN 15
#define BOOT_SCREEN 7
int current_screen = BOOT_SCREEN;

int fps_enabled = 0;
int sys_monitor_enabled = 0;
int frames_drawn = 0;
int current_fps = 0;
unsigned int last_fps_second = 0;

int phone_w = 320, phone_h = 560;
int phone_x = (800 - 320) / 2, phone_y = (600 - 560) / 2;

char current_dir[100] = "";
FileInfo current_files[14];
int current_file_count = 0;

int is_dark_mode = 1;
char current_file[100] = "";
int is_picking_wallpaper = 0;
int is_picking_sys_font = 0;
int is_picking_lock_font = 0;
int is_picking_app_title_font = 0;
int is_picking_package = 0;
int is_picking_app_file = 0;
char app_picker_filter[32] = "";
char app_picked_file_path[128] = "";
int app_file_picked_ready = 0;
int app_picker_caller_idx = -1;
char current_wallpaper[100] = "wallpapers/tall_city.png";
char current_lock_font[100] = "sys/fonts/Canterbury_large.nfn";
char app_title_font[100] = "sys/fonts/Roboto-Regular.nfn";
int is_dragging = 0;
int drag_start_x = 0;
int cursor_x = 400;
int cursor_y = 300;

char key_buf[32] = {0};
int key_idx = 0;

int force_screen_redraw = 0;

static int new_file_counter = 1;
static int new_folder_counter = 1;

void create_item(int is_dir) {
    char new_path[100];
    int len = 0;
    while (current_dir[len] != '\0') {
        new_path[len] = current_dir[len];
        len++;
    }
    if (len > 0 && new_path[len - 1] != '/') {
        new_path[len++] = '/';
    }
    
    char *prefix = is_dir ? "New_Folder_" : "New_File_";
    int p = 0;
    while (prefix[p] != '\0') new_path[len++] = prefix[p++];
    
    int num = is_dir ? new_folder_counter++ : new_file_counter++;
    char num_str[10];
    int n_idx = 0;
    while (num > 0) {
        num_str[n_idx++] = (num % 10) + '0';
        num /= 10;
    }
    if (n_idx == 0) num_str[n_idx++] = '0';
    while (n_idx > 0) new_path[len++] = num_str[--n_idx];
    
    if (is_dir) {
        new_path[len++] = '/';
    } else {
        new_path[len++] = '.';
        new_path[len++] = 't';
        new_path[len++] = 'x';
        new_path[len++] = 't';
    }
    new_path[len] = '\0';
    
    fs_create_file(new_path, is_dir);
}

void draw_phone_frame() {
    // Intentionally empty to remove phone frame
}

void draw_status_bar() {
    unsigned int bg_col = is_dark_mode ? RGB(20, 20, 20) : RGB(245, 245, 245);
    unsigned int text_col = is_dark_mode ? RGB(255, 255, 255) : RGB(20, 20, 20);
    draw_rect(phone_x, phone_y, phone_w, 20, bg_col);
    char time_str[6];
    get_current_time(time_str);
    extern char sys_font_path[100];
    int time_w = fb_get_string_width(time_str, sys_font_path);
    draw_string(phone_x + (phone_w - time_w) / 2, phone_y, time_str, text_col);
}

void draw_wallpaper_or_bg() {
    int drawn_wallpaper = 0;
    if (current_wallpaper[0] != '\0') {
        unsigned int size = 0;
        char *data = fs_get_file_data(current_wallpaper, &size);
        if (data && size >= 8 && data[0] == 'N' && data[1] == 'Y' && data[2] == 'K' && data[3] == 'N') {
            unsigned short width = *((unsigned short*)(data + 4));
            unsigned short height = *((unsigned short*)(data + 6));
            unsigned int *pixels = (unsigned int*)(data + 8);
            
            int img_x = phone_x + (phone_w - width) / 2;
            int img_y = phone_y + 20 + (phone_h - 20 - height) / 2;
            
            extern unsigned int *back_buffer;
            extern int screen_width;
            
            for (int y = 0; y < height; y++) {
                int screen_y = img_y + y;
                if (screen_y >= phone_y + 20 && screen_y < phone_y + phone_h) {
                    unsigned int *dst = &back_buffer[screen_y * screen_width + img_x];
                    unsigned int *src = &pixels[y * width];
                    for (int x = 0; x < width; x++) {
                        if (img_x + x >= phone_x && img_x + x < phone_x + phone_w) {
                            dst[x] = src[x];
                        }
                    }
                }
            }
            drawn_wallpaper = 1;
        }
    }
    
    if (!drawn_wallpaper) {
        unsigned int def_bg = is_dark_mode ? RGB(15, 20, 40) : RGB(235, 240, 248);
        draw_rect(phone_x, phone_y + 20, phone_w, phone_h - 20, def_bg);
    }
}

void draw_home_screen_offset(int offset_y) {
    draw_rect(0, 0, 800, 600, RGB(10, 10, 10)); // Clear entire desktop to remove app artifacts
    draw_wallpaper_or_bg();
    
    int start_x = phone_x + 20, start_y = phone_y + 50 + offset_y, spacing_x = 76, spacing_y = 76;
    
    // Setting
    extern void nykon_draw_sprite_rounded(const char *filepath, int x, int y, int r, unsigned int color_key);
    draw_icon_generic(start_x, start_y, RGB(180, 180, 180));
    nykon_draw_sprite_rounded("sys/img/settings_icon.png", start_x, start_y, 12, 0xFFFF00FF);
    draw_icon_border(start_x, start_y, RGB(icon_themes[current_icon_theme].border_r, icon_themes[current_icon_theme].border_g, icon_themes[current_icon_theme].border_b));
    const char *label_font = app_title_font;
    int setting_w = fb_get_string_width("Setting", label_font);
    draw_string_ttf(start_x + 24 - (setting_w / 2), start_y + 52, "Setting", label_font, RGB(255, 255, 255));
    
    // Files
    draw_icon_generic(start_x + spacing_x, start_y, RGB(100, 100, 255));
    nykon_draw_sprite_rounded("sys/img/files_icon.png", start_x + spacing_x, start_y, 12, 0xFFFF00FF);
    draw_icon_border(start_x + spacing_x, start_y, RGB(icon_themes[current_icon_theme].border_r, icon_themes[current_icon_theme].border_g, icon_themes[current_icon_theme].border_b));
    int files_w = fb_get_string_width("Files", label_font);
    draw_string_ttf(start_x + spacing_x + 24 - (files_w / 2), start_y + 52, "Files", label_font, RGB(255, 255, 255));

    // Installer (System App)
    draw_icon_generic(start_x + spacing_x * 2, start_y, RGB(50, 180, 100));
    nykon_draw_sprite_rounded("sys/img/installer_icon.png", start_x + spacing_x * 2, start_y, 12, 0xFFFF00FF);
    draw_icon_border(start_x + spacing_x * 2, start_y, RGB(icon_themes[current_icon_theme].border_r, icon_themes[current_icon_theme].border_g, icon_themes[current_icon_theme].border_b));
    int installer_w = fb_get_string_width("Installer", label_font);
    draw_string_ttf(start_x + spacing_x * 2 + 24 - (installer_w / 2), start_y + 52, "Installer", label_font, RGB(255, 255, 255));

    // Third-party Apps
    int current_x = start_x + spacing_x * 3;
    int current_y = start_y;
    for (int i = 0; i < num_registered_apps; i++) {
        if (current_x > phone_x + phone_w - 50) {
            current_x = start_x;
            current_y += spacing_y;
        }
        draw_icon_generic(current_x, current_y, registered_apps[i]->icon_color);
        int has_icon = 0;
        if (registered_apps[i]->icon_path) {
            extern char *fs_get_file_data(const char *filepath, unsigned int *size_out);
            unsigned int size = 0;
            char *data = fs_get_file_data(registered_apps[i]->icon_path, &size);
            if (data && size >= 8 && data[0] == 'N' && data[1] == 'Y' && data[2] == 'K' && data[3] == 'N') {
                const unsigned char *ud = (const unsigned char *)data;
                unsigned short w = (unsigned short)(((unsigned short)ud[4]) | (((unsigned short)ud[5]) << 8));
                unsigned short h = (unsigned short)(((unsigned short)ud[6]) | (((unsigned short)ud[7]) << 8));
                nykon_draw_sprite_rounded(registered_apps[i]->icon_path, current_x + (48 - w)/2, current_y + (48 - h)/2, 12, 0xFFFF00FF);
                has_icon = 1;
            } else if (data && size > 0) {
                nykon_draw_sprite_rounded(registered_apps[i]->icon_path, current_x, current_y, 12, 0xFFFF00FF);
                has_icon = 1;
            }
        }
        
        // If app doesn't have an icon image, draw the first letter of its name
        if (!has_icon && registered_apps[i]->name && registered_apps[i]->name[0] != '\0') {
            char letter_str[2] = { registered_apps[i]->name[0], '\0' };
            int lw = fb_get_string_width(letter_str, "sys/fonts/Roboto-Regular.nfn");
            draw_string_ttf(current_x + 24 - (lw / 2), current_y + 16, letter_str, "sys/fonts/Roboto-Regular.nfn", RGB(230, 230, 230));
        }

        // Always draw the squircle outline so the icon shape is clearly visible
        draw_icon_border(current_x, current_y, RGB(icon_themes[current_icon_theme].border_r, icon_themes[current_icon_theme].border_g, icon_themes[current_icon_theme].border_b));
        
        int app_name_w = fb_get_string_width(registered_apps[i]->name, label_font);
        draw_string_ttf(current_x + 24 - (app_name_w / 2), current_y + 52, registered_apps[i]->name, label_font, RGB(255, 255, 255));
        
        current_x += spacing_x;
    }
}

void draw_home_screen() {
    draw_home_screen_offset(0);
}

void draw_lock_slider();

void draw_lock_screen() {
    draw_wallpaper_or_bg();
    
    char time_str[10];
    get_current_time(time_str);
    
    int approx_w = fb_get_string_width(time_str, current_lock_font);
    
    draw_string_ttf(phone_x + (phone_w - approx_w) / 2, phone_y + 100, time_str, current_lock_font, RGB(255, 255, 255));
    
    draw_lock_slider();
}

void draw_lock_slider() {
    extern void draw_rounded_rect(int x, int y, int w, int h, int r, unsigned int color);
    
    // Slide to unlock track (capsule shape)
    int track_y = phone_y + phone_h - 80;
    draw_rounded_rect(phone_x + 20, track_y, phone_w - 40, 48, 24, RGB(40, 40, 40));
    
    // Slider Thumb (circular)
    int slider_x = phone_x + 20;
    if (is_dragging) {
        slider_x = cursor_x - 24; // Center the slider on the cursor
        if (slider_x < phone_x + 20) slider_x = phone_x + 20;
        if (slider_x > phone_x + phone_w - 20 - 48) slider_x = phone_x + phone_w - 20 - 48;
    }
    
    // "slide to unlock" text
    const char *prompt = "slide to unlock";
    int prompt_len = 15;
    draw_string_ttf(phone_x + 80, track_y + 14, prompt, "sys/fonts/Roboto-Regular.nfn", RGB(180, 180, 180));
    
    draw_rounded_rect(slider_x, track_y, 48, 48, 24, RGB(200, 200, 200));
    extern void nykon_draw_sprite(const char *filepath, int x, int y, unsigned int color_key);
    nykon_draw_sprite("sys/img/unlock_sprite.png", slider_x, track_y, 0xFFFF00FF);
}

void update_lock_slider() {
    int track_y = phone_y + phone_h - 80;
    draw_lock_slider();
    fb_swap_rect(phone_x + 20, track_y, phone_w - 40, 48);
}

void draw_status_bar();
void draw_bottom_nav_bar();
void render_screen();

void play_boot_animation() {
    int bar_max_w = 200;
    int bar_h = 8;
    int bar_x = phone_x + (phone_w - bar_max_w) / 2;
    int bar_y = phone_y + phone_h - 100;

    for (int frame = 0; frame <= bar_max_w; frame += 2) {
        draw_rect(phone_x, phone_y, phone_w, phone_h, RGB(0,0,0));
        nykon_draw_sprite("sys/img/logo_sprite.png", phone_x + (phone_w - 136) / 2, phone_y + 150, 0xFFFF00FF);
        
        // Draw loading bar track
        draw_rounded_rect(bar_x, bar_y, bar_max_w, bar_h, bar_h / 2, RGB(50, 50, 50));
        // Draw loading bar fill
        if (frame > 0) {
            int r = (frame > bar_h) ? (bar_h / 2) : (frame / 2);
            draw_rounded_rect(bar_x, bar_y, frame, bar_h, r, RGB(255, 255, 255));
        }
        
        fb_swap_buffers();
        
        for (volatile int d = 0; d < 40000; d++); // delay to simulate loading time
    }
    
    current_screen = LOCK_SCREEN;
}

void play_unlock_animation() {
    int steps = 14;
    for (int s = 1; s <= steps; s++) {
        int t_norm = (s * 1024) / steps;
        int inv = 1024 - t_norm;
        int ease = 1024 - ((inv * inv / 1024) * inv / 1024); // 0 to 1024
        
        int icon_offset = (32 * (1024 - ease)) / 1024;
        draw_home_screen_offset(icon_offset);
        
        // Big lock clock glides upwards AND smoothly fades out
        int clock_offset = (120 * ease) / 1024;
        char lock_time_str[10];
        get_current_time(lock_time_str);
        int approx_w = fb_get_string_width(lock_time_str, current_lock_font);
        int lock_clock_y = (phone_y + 100) - clock_offset;
        
        // True transparent alpha fade-out for the big lock screen clock
        int clock_alpha = 255 - (255 * ease) / 1024;
        if (clock_alpha > 0) {
            draw_string_ttf_alpha(phone_x + (phone_w - approx_w) / 2, lock_clock_y, lock_time_str, current_lock_font, RGB(255, 255, 255), clock_alpha);
        }
        
        // Lock slider glides downwards
        int slider_offset = (100 * ease) / 1024;
        int track_y = phone_y + phone_h - 80 + slider_offset;
        if (track_y < phone_y + phone_h) {
            extern void draw_rounded_rect(int x, int y, int w, int h, int r, unsigned int color);
            draw_rounded_rect(phone_x + 20, track_y, phone_w - 40, 48, 24, RGB(40, 40, 40));
            int slider_x = phone_x + phone_w - 20 - 48;
            draw_rounded_rect(slider_x, track_y, 48, 48, 24, RGB(200, 200, 200));
            extern void nykon_draw_sprite(const char *filepath, int x, int y, unsigned int color_key);
            nykon_draw_sprite("sys/img/unlock_sprite.png", slider_x, track_y, 0xFFFF00FF);
        }
        
        // Status bar smoothly slides down from top
        int bar_y_offset = (20 * (1024 - ease)) / 1024;
        draw_rect(phone_x, phone_y - bar_y_offset, phone_w, 20, RGB(20, 20, 20));
        char status_time_str[6];
        get_current_time(status_time_str);
        extern char sys_font_path[100];
        int status_time_w = fb_get_string_width(status_time_str, sys_font_path);
        draw_string(phone_x + (phone_w - status_time_w) / 2, phone_y - bar_y_offset, status_time_str, RGB(255, 255, 255));
        
        draw_bottom_nav_bar();
        fb_swap_buffers();
        for (volatile int d = 0; d < 4500; d++);
    }
}

void draw_app_icon_centered(int center_x, int center_y, const char *icon_path, const char *app_name, unsigned int app_color) {
    int icon_x = center_x - 24;
    int icon_y = center_y - 24;
    extern void nykon_draw_sprite_rounded(const char *filepath, int x, int y, int r, unsigned int color_key);
    
    draw_icon_generic(icon_x, icon_y, app_color);
    int has_icon = 0;
    if (icon_path) {
        extern char *fs_get_file_data(const char *filepath, unsigned int *size_out);
        unsigned int size = 0;
        char *data = fs_get_file_data(icon_path, &size);
        if (data && size >= 8 && data[0] == 'N' && data[1] == 'Y' && data[2] == 'K' && data[3] == 'N') {
            const unsigned char *ud = (const unsigned char *)data;
            unsigned short w = (unsigned short)(((unsigned short)ud[4]) | (((unsigned short)ud[5]) << 8));
            unsigned short h = (unsigned short)(((unsigned short)ud[6]) | (((unsigned short)ud[7]) << 8));
            nykon_draw_sprite_rounded(icon_path, icon_x + (48 - w)/2, icon_y + (48 - h)/2, 12, 0xFFFF00FF);
            has_icon = 1;
        } else if (data && size > 0) {
            nykon_draw_sprite_rounded(icon_path, icon_x, icon_y, 12, 0xFFFF00FF);
            has_icon = 1;
        }
    }
    
    if (!has_icon && app_name && app_name[0] != '\0') {
        char letter_str[2] = { app_name[0], '\0' };
        int lw = fb_get_string_width(letter_str, "sys/fonts/Roboto-Regular.nfn");
        draw_string_ttf(icon_x + 24 - (lw / 2), icon_y + 16, letter_str, "sys/fonts/Roboto-Regular.nfn", RGB(230, 230, 230));
    }
    
    draw_icon_border(icon_x, icon_y, RGB(icon_themes[current_icon_theme].border_r, icon_themes[current_icon_theme].border_g, icon_themes[current_icon_theme].border_b));
}

void play_app_open_animation(int from_x, int from_y, unsigned int app_color, const char *icon_path, const char *app_name) {
    int target_x = phone_x;
    int target_y = phone_y + 20;
    int target_w = phone_w;
    int target_h = phone_h - 20;
    int steps = 12;
    
    unsigned int bg_col = is_dark_mode ? RGB(18, 18, 18) : RGB(245, 245, 245);
    extern void draw_rounded_rect(int x, int y, int w, int h, int r, unsigned int color);
    
    // 1. Expand the app frame with the app icon in the middle all the way to full screen
    for (int s = 1; s <= steps; s++) {
        int t_norm = (s * 1024) / steps;
        int inv = 1024 - t_norm;
        int ease = 1024 - ((inv * inv / 1024) * inv / 1024); // Cubic Ease-Out
        
        int cur_x = from_x + ((target_x - from_x) * ease) / 1024;
        int cur_y = from_y + ((target_y - from_y) * ease) / 1024;
        int cur_w = 48 + ((target_w - 48) * ease) / 1024;
        int cur_h = 48 + ((target_h - 48) * ease) / 1024;
        int cur_r = 12 - (12 * ease) / 1024;
        if (cur_r < 0) cur_r = 0;
        
        draw_home_screen();
        
        // Elevation drop shadow under floating app card
        if (cur_r > 0 && cur_w < phone_w - 4) {
            draw_rounded_rect(cur_x - 2, cur_y - 2, cur_w + 4, cur_h + 4, cur_r + 2, RGB(12, 12, 12));
        }
        
        // App Card Surface
        draw_rounded_rect(cur_x, cur_y, cur_w, cur_h, cur_r, bg_col);
        
        // Center icon and title in the middle of the expanding card
        int center_x = cur_x + cur_w / 2;
        int center_y = cur_y + cur_h / 2;
        draw_app_icon_centered(center_x, center_y, icon_path, app_name, app_color);
        if (cur_h > 120 && app_name) {
            int name_w = fb_get_string_width(app_name, app_title_font);
            unsigned int text_col = is_dark_mode ? RGB(240, 240, 240) : RGB(20, 20, 20);
            draw_string_ttf(center_x - (name_w / 2), center_y + 36, app_name, app_title_font, text_col);
        }
        
        draw_status_bar();
        draw_bottom_nav_bar();
        fb_swap_buffers();
        for (volatile int d = 0; d < 4500; d++);
    }
    
    // 2. The frame has reached full screen -> Fade in the actual app UI
    render_screen();
    
    extern unsigned int *back_buffer;
    unsigned int *app_snapshot = (unsigned int *)nykon_malloc(phone_w * phone_h * sizeof(unsigned int));
    if (app_snapshot) {
        for (int y = 0; y < phone_h; y++) {
            for (int x = 0; x < phone_w; x++) {
                app_snapshot[y * phone_w + x] = back_buffer[(phone_y + y) * 800 + (phone_x + x)];
            }
        }
        
        int fade_steps = 7;
        for (int f = 1; f <= fade_steps; f++) {
            int alpha = (f * 255) / fade_steps;
            
            // Full screen splash frame with icon in the middle
            draw_rect(phone_x, phone_y + 20, phone_w, phone_h - 20, bg_col);
            int center_x = phone_x + phone_w / 2;
            int center_y = phone_y + phone_h / 2;
            draw_app_icon_centered(center_x, center_y, icon_path, app_name, app_color);
            if (app_name) {
                int name_w = fb_get_string_width(app_name, app_title_font);
                unsigned int text_col = is_dark_mode ? RGB(240, 240, 240) : RGB(20, 20, 20);
                draw_string_ttf(center_x - (name_w / 2), center_y + 36, app_name, app_title_font, text_col);
            }
            
            // Crossfade the live app UI over the splash frame
            for (int y = 20; y < phone_h; y++) {
                int screen_row = (phone_y + y) * 800 + phone_x;
                int snap_row = y * phone_w;
                for (int x = 0; x < phone_w; x++) {
                    unsigned int splash_pix = back_buffer[screen_row + x];
                    unsigned int app_pix = app_snapshot[snap_row + x];
                    
                    unsigned int sr = splash_pix & 0xFF;
                    unsigned int sg = (splash_pix >> 8) & 0xFF;
                    unsigned int sb = (splash_pix >> 16) & 0xFF;
                    
                    unsigned int ar = app_pix & 0xFF;
                    unsigned int ag = (app_pix >> 8) & 0xFF;
                    unsigned int ab = (app_pix >> 16) & 0xFF;
                    
                    unsigned int inv_a = 255 - alpha;
                    unsigned int out_r = ((sr * inv_a) + (ar * alpha)) >> 8;
                    unsigned int out_g = ((sg * inv_a) + (ag * alpha)) >> 8;
                    unsigned int out_b = ((sb * inv_a) + (ab * alpha)) >> 8;
                    
                    back_buffer[screen_row + x] = 0xFF000000 | (out_b << 16) | (out_g << 8) | out_r;
                }
            }
            
            draw_status_bar();
            draw_bottom_nav_bar();
            fb_swap_buffers();
            for (volatile int d = 0; d < 5000; d++);
        }
        
        nykon_free(app_snapshot);
    }
    
    render_screen();
    fb_swap_buffers();
}

void draw_settings_screen() {
    extern char sys_font_path[100];
    unsigned int bg_col = is_dark_mode ? 0xFF000000 : 0xFFF0F2F5;
    unsigned int header_bg = is_dark_mode ? 0xFF181818 : 0xFFFFFFFF;
    unsigned int header_pill_bg = is_dark_mode ? 0xFF2D2D2D : 0xFFE4E6EB;
    unsigned int header_pill_text = is_dark_mode ? 0xFFFFFFFF : 0xFF111111;
    unsigned int header_title = is_dark_mode ? 0xFFFFFFFF : 0xFF111111;
    unsigned int card_bg = is_dark_mode ? 0xFF141414 : 0xFFFFFFFF;
    unsigned int card_border = is_dark_mode ? 0xFF282828 : 0xFFDCDFE4;
    unsigned int card_title = is_dark_mode ? 0xFFA0A0A0 : 0xFF606770;
    unsigned int row_bg = is_dark_mode ? 0xFF1E1E1E : 0xFFF4F6F9;
    unsigned int badge_bg = is_dark_mode ? 0xFF2D2D2D : 0xFFE2E6EC;
    unsigned int badge_text = is_dark_mode ? 0xFFAAAAAA : 0xFF4E5564;
    unsigned int text_primary = is_dark_mode ? 0xFFFFFFFF : 0xFF111111;
    unsigned int text_muted = is_dark_mode ? 0xFF888888 : 0xFF8D949E;
    unsigned int pill_btn_bg = is_dark_mode ? 0xFF282828 : 0xFFE4E6EB;

    // 1. Background
    draw_rect(phone_x, phone_y + 20, phone_w, phone_h - 20, bg_col);
    
    // 2. Modern Header Card
    draw_rounded_rect(phone_x + 12, phone_y + 26, phone_w - 24, 46, 12, header_bg);
    
    int title_tw = fb_get_string_width("Settings", sys_font_path);
    draw_string(phone_x + (phone_w - title_tw) / 2, phone_y + 39, "Settings", header_title);
    
    // 3. Section 1: Appearance Card
    int card1_x = phone_x + 12;
    int card1_y = phone_y + 78;
    int card1_w = phone_w - 24;
    int card1_h = 92;
    draw_rounded_rect(card1_x - 1, card1_y - 1, card1_w + 2, card1_h + 2, 14, card_border);
    draw_rounded_rect(card1_x, card1_y, card1_w, card1_h, 14, card_bg);
    draw_string(card1_x + 16, card1_y + 12, "APPEARANCE", card_title);
    
    // Personalization row
    int r1_y = card1_y + 36;
    draw_rounded_rect(card1_x + 8, r1_y, card1_w - 16, 44, 10, row_bg);
    draw_rounded_rect(card1_x + 14, r1_y + 10, 28, 24, 6, 0xFF303030);
    draw_filled_circle(card1_x + 28, r1_y + 22, 5, 0xFF80A0FF);
    draw_string(card1_x + 48, r1_y + 11, "Personalization", text_primary);
    draw_rounded_rect(card1_x + card1_w - 40, r1_y + 10, 26, 24, 8, pill_btn_bg);
    draw_string(card1_x + card1_w - 32, r1_y + 11, ">", text_muted);
    
    // 4. Section 2: System & Device Card
    int card2_x = phone_x + 12;
    int card2_y = phone_y + 178;
    int card2_w = phone_w - 24;
    int card2_h = 192;
    draw_rounded_rect(card2_x - 1, card2_y - 1, card2_w + 2, card2_h + 2, 14, card_border);
    draw_rounded_rect(card2_x, card2_y, card2_w, card2_h, 14, card_bg);
    draw_string(card2_x + 16, card2_y + 12, "SYSTEM & DEVICE", card_title);
    
    // About row
    int r2_y = card2_y + 36;
    draw_rounded_rect(card2_x + 8, r2_y, card2_w - 16, 44, 10, row_bg);
    draw_rounded_rect(card2_x + 14, r2_y + 10, 28, 24, 6, badge_bg);
    draw_string(card2_x + 19, r2_y + 11, "OS", badge_text);
    draw_string(card2_x + 48, r2_y + 11, "About Nykon OS", text_primary);
    draw_rounded_rect(card2_x + card2_w - 40, r2_y + 10, 26, 24, 8, pill_btn_bg);
    draw_string(card2_x + card2_w - 32, r2_y + 11, ">", text_muted);
    
    // Developer Options row
    int r3_y = r2_y + 50;
    draw_rounded_rect(card2_x + 8, r3_y, card2_w - 16, 44, 10, row_bg);
    draw_rounded_rect(card2_x + 14, r3_y + 10, 28, 24, 6, badge_bg);
    draw_string(card2_x + 16, r3_y + 11, "DEV", badge_text);
    draw_string(card2_x + 48, r3_y + 11, "Developer Options", text_primary);
    draw_rounded_rect(card2_x + card2_w - 40, r3_y + 10, 26, 24, 8, pill_btn_bg);
    draw_string(card2_x + card2_w - 32, r3_y + 11, ">", text_muted);
    
    // Installed Apps row
    int r4_y = r3_y + 50;
    draw_rounded_rect(card2_x + 8, r4_y, card2_w - 16, 44, 10, row_bg);
    draw_rounded_rect(card2_x + 14, r4_y + 10, 28, 24, 6, badge_bg);
    draw_string(card2_x + 16, r4_y + 11, "APP", badge_text);
    draw_string(card2_x + 48, r4_y + 11, "Installed Apps", text_primary);
    draw_rounded_rect(card2_x + card2_w - 40, r4_y + 10, 26, 24, 8, pill_btn_bg);
    draw_string(card2_x + card2_w - 32, r4_y + 11, ">", text_muted);
}

void draw_apps_list_screen() {
    extern char sys_font_path[100];
    unsigned int bg_col = is_dark_mode ? 0xFF000000 : 0xFFF0F2F5;
    unsigned int header_bg = is_dark_mode ? 0xFF181818 : 0xFFFFFFFF;
    unsigned int header_pill_bg = is_dark_mode ? 0xFF2D2D2D : 0xFFE4E6EB;
    unsigned int header_pill_text = is_dark_mode ? 0xFFFFFFFF : 0xFF111111;
    unsigned int header_title = is_dark_mode ? 0xFFFFFFFF : 0xFF111111;
    unsigned int row_bg = is_dark_mode ? 0xFF1E1E1E : 0xFFF4F6F9;
    unsigned int badge_bg = is_dark_mode ? 0xFF353535 : 0xFFE2E6EC;
    unsigned int text_primary = is_dark_mode ? 0xFFFFFFFF : 0xFF111111;
    unsigned int pill_btn_bg = is_dark_mode ? 0xFF2D2D2D : 0xFFE4E6EB;

    draw_rect(phone_x, phone_y + 20, phone_w, phone_h - 20, bg_col);
    
    // Modern Header
    draw_rounded_rect(phone_x + 12, phone_y + 26, phone_w - 24, 46, 12, header_bg);
    draw_rounded_rect(phone_x + 18, phone_y + 33, 56, 32, 8, header_pill_bg);
    int back_tw = fb_get_string_width("< Back", sys_font_path);
    draw_string(phone_x + 18 + ((56 - back_tw) / 2), phone_y + 39, "< Back", header_pill_text);
    
    int title_tw = fb_get_string_width("Installed Apps", sys_font_path);
    draw_string(phone_x + (phone_w - title_tw) / 2, phone_y + 39, "Installed Apps", header_title);
    
    int list_y = phone_y + 78;
    for (int i = 0; i < num_registered_apps && i < 8; i++) {
        draw_rounded_rect(phone_x + 12, list_y, phone_w - 24, 44, 10, row_bg);
        
        // App badge
        draw_rounded_rect(phone_x + 20, list_y + 10, 24, 24, 6, badge_bg);
        draw_filled_circle(phone_x + 32, list_y + 22, 4, registered_apps[i]->icon_color);
        
        draw_string(phone_x + 52, list_y + 11, registered_apps[i]->name, text_primary);
        
        // [ Open ] pill
        int op_w = 52;
        int op_h = 24;
        int op_x = phone_x + phone_w - 20 - op_w;
        int op_y = list_y + 10;
        draw_rounded_rect(op_x, op_y, op_w, op_h, 12, pill_btn_bg);
        int op_tw = fb_get_string_width("Open", sys_font_path);
        draw_string(op_x + ((op_w - op_tw) / 2), op_y + 2, "Open", text_primary);
        
        list_y += 50;
    }
}

void draw_dev_options_screen() {
    extern char sys_font_path[100];
    extern int sys_monitor_enabled;
    unsigned int bg_col = is_dark_mode ? 0xFF000000 : 0xFFF0F2F5;
    unsigned int header_bg = is_dark_mode ? 0xFF181818 : 0xFFFFFFFF;
    unsigned int header_pill_bg = is_dark_mode ? 0xFF2D2D2D : 0xFFE4E6EB;
    unsigned int header_pill_text = is_dark_mode ? 0xFFFFFFFF : 0xFF111111;
    unsigned int header_title = is_dark_mode ? 0xFFFFFFFF : 0xFF111111;
    unsigned int card_bg = is_dark_mode ? 0xFF141414 : 0xFFFFFFFF;
    unsigned int card_border = is_dark_mode ? 0xFF282828 : 0xFFDCDFE4;
    unsigned int card_title = is_dark_mode ? 0xFFA0A0A0 : 0xFF606770;
    unsigned int row_bg = is_dark_mode ? 0xFF1E1E1E : 0xFFF4F6F9;
    unsigned int badge_bg = is_dark_mode ? 0xFF2D2D2D : 0xFFE2E6EC;
    unsigned int badge_text = is_dark_mode ? 0xFFAAAAAA : 0xFF4E5564;
    unsigned int text_primary = is_dark_mode ? 0xFFFFFFFF : 0xFF111111;
    unsigned int text_muted = is_dark_mode ? 0xFF888888 : 0xFF8D949E;
    unsigned int pill_btn_bg = is_dark_mode ? 0xFF282828 : 0xFFE4E6EB;

    draw_rect(phone_x, phone_y + 20, phone_w, phone_h - 20, bg_col);
    
    // Header
    draw_rounded_rect(phone_x + 12, phone_y + 26, phone_w - 24, 46, 12, header_bg);
    draw_rounded_rect(phone_x + 18, phone_y + 33, 56, 32, 8, header_pill_bg);
    int back_tw = fb_get_string_width("< Back", sys_font_path);
    draw_string(phone_x + 18 + ((56 - back_tw) / 2), phone_y + 39, "< Back", header_pill_text);
    
    int title_tw = fb_get_string_width("Developer Options", sys_font_path);
    draw_string(phone_x + (phone_w - title_tw) / 2, phone_y + 39, "Developer Options", header_title);
    
    int card_x = phone_x + 12;
    int card_y = phone_y + 78;
    int card_w = phone_w - 24;
    int card_h = 192;
    draw_rounded_rect(card_x - 1, card_y - 1, card_w + 2, card_h + 2, 14, card_border);
    draw_rounded_rect(card_x, card_y, card_w, card_h, 14, card_bg);
    draw_string(card_x + 16, card_y + 12, "DIAGNOSTICS & TOOLS", card_title);
    
    // FPS Toggle Row
    int r1_y = card_y + 36;
    draw_rounded_rect(card_x + 8, r1_y, card_w - 16, 44, 10, row_bg);
    draw_string(card_x + 20, r1_y + 11, "Show FPS Counter", text_primary);
    unsigned int fps_bg = fps_enabled ? 0xFF2EB85C : pill_btn_bg;
    unsigned int fps_txt = fps_enabled ? 0xFFFFFFFF : text_muted;
    draw_rounded_rect(card_x + card_w - 56, r1_y + 10, 44, 24, 12, fps_bg);
    int fps_tw = fb_get_string_width(fps_enabled ? "ON" : "OFF", sys_font_path);
    draw_string(card_x + card_w - 56 + ((44 - fps_tw) / 2), r1_y + 11, fps_enabled ? "ON" : "OFF", fps_txt);
    
    // KB Test Row
    int r2_y = r1_y + 50;
    draw_rounded_rect(card_x + 8, r2_y, card_w - 16, 44, 10, row_bg);
    draw_rounded_rect(card_x + 14, r2_y + 10, 28, 24, 6, badge_bg);
    draw_string(card_x + 18, r2_y + 11, "KB", badge_text);
    draw_string(card_x + 48, r2_y + 11, "Keyboard Test", text_primary);
    draw_rounded_rect(card_x + card_w - 40, r2_y + 10, 26, 24, 8, pill_btn_bg);
    draw_string(card_x + card_w - 32, r2_y + 11, ">", text_muted);
    
    // System Monitor Row
    int r3_y = r2_y + 50;
    draw_rounded_rect(card_x + 8, r3_y, card_w - 16, 44, 10, row_bg);
    draw_string(card_x + 20, r3_y + 11, "Show System Monitor", text_primary);
    unsigned int sys_bg = sys_monitor_enabled ? 0xFF2EB85C : pill_btn_bg;
    unsigned int sys_txt = sys_monitor_enabled ? 0xFFFFFFFF : text_muted;
    draw_rounded_rect(card_x + card_w - 56, r3_y + 10, 44, 24, 12, sys_bg);
    int sys_tw = fb_get_string_width(sys_monitor_enabled ? "ON" : "OFF", sys_font_path);
    draw_string(card_x + card_w - 56 + ((44 - sys_tw) / 2), r3_y + 11, sys_monitor_enabled ? "ON" : "OFF", sys_txt);
}

void draw_keyboard_test_screen() {
    extern char sys_font_path[100];
    unsigned int bg_col = is_dark_mode ? 0xFF000000 : 0xFFF0F2F5;
    unsigned int header_bg = is_dark_mode ? 0xFF181818 : 0xFFFFFFFF;
    unsigned int header_pill_bg = is_dark_mode ? 0xFF2D2D2D : 0xFFE4E6EB;
    unsigned int header_pill_text = is_dark_mode ? 0xFFFFFFFF : 0xFF111111;
    unsigned int header_title = is_dark_mode ? 0xFFFFFFFF : 0xFF111111;
    unsigned int card_bg = is_dark_mode ? 0xFF141414 : 0xFFFFFFFF;
    unsigned int card_border = is_dark_mode ? 0xFF282828 : 0xFFDCDFE4;
    unsigned int card_title = is_dark_mode ? 0xFFA0A0A0 : 0xFF606770;
    unsigned int row_bg = is_dark_mode ? 0xFF1E1E1E : 0xFFF4F6F9;
    unsigned int text_muted = is_dark_mode ? 0xFF666666 : 0xFF8D949E;

    draw_rect(phone_x, phone_y + 20, phone_w, phone_h - 20, bg_col);
    
    // Header
    draw_rounded_rect(phone_x + 12, phone_y + 26, phone_w - 24, 46, 12, header_bg);
    draw_rounded_rect(phone_x + 18, phone_y + 33, 56, 32, 8, header_pill_bg);
    int back_tw = fb_get_string_width("< Back", sys_font_path);
    draw_string(phone_x + 18 + ((56 - back_tw) / 2), phone_y + 39, "< Back", header_pill_text);
    
    int title_tw = fb_get_string_width("Keyboard Test", sys_font_path);
    draw_string(phone_x + (phone_w - title_tw) / 2, phone_y + 39, "Keyboard Test", header_title);
    
    // Card container
    int card_x = phone_x + 12;
    int card_y = phone_y + 78;
    int card_w = phone_w - 24;
    int card_h = 200;
    draw_rounded_rect(card_x - 1, card_y - 1, card_w + 2, card_h + 2, 14, card_border);
    draw_rounded_rect(card_x, card_y, card_w, card_h, 14, card_bg);
    
    draw_string(card_x + 16, card_y + 16, "Type on your physical keyboard:", card_title);
    
    draw_rounded_rect(card_x + 14, card_y + 44, card_w - 28, 136, 10, row_bg);
    if (key_buf[0] != '\0') {
        draw_string_scaled(card_x + 24, card_y + 64, key_buf, 0xFF2EB85C, 2);
    } else {
        draw_string(card_x + 24, card_y + 64, "Press any key to test...", text_muted);
    }
}

void draw_personalization_screen() {
    extern char sys_font_path[100];
    unsigned int bg_col = is_dark_mode ? 0xFF000000 : 0xFFF0F2F5;
    unsigned int header_bg = is_dark_mode ? 0xFF181818 : 0xFFFFFFFF;
    unsigned int header_pill_bg = is_dark_mode ? 0xFF2D2D2D : 0xFFE4E6EB;
    unsigned int header_pill_text = is_dark_mode ? 0xFFFFFFFF : 0xFF111111;
    unsigned int header_title = is_dark_mode ? 0xFFFFFFFF : 0xFF111111;
    unsigned int card_bg = is_dark_mode ? 0xFF141414 : 0xFFFFFFFF;
    unsigned int card_border = is_dark_mode ? 0xFF282828 : 0xFFDCDFE4;
    unsigned int card_title = is_dark_mode ? 0xFFA0A0A0 : 0xFF606770;
    unsigned int row_bg = is_dark_mode ? 0xFF1E1E1E : 0xFFF4F6F9;
    unsigned int text_primary = is_dark_mode ? 0xFFFFFFFF : 0xFF111111;
    unsigned int text_secondary = is_dark_mode ? 0xFFA0A0A0 : 0xFF606770;
    unsigned int text_muted = is_dark_mode ? 0xFF888888 : 0xFF8D949E;
    unsigned int pill_btn_bg = is_dark_mode ? 0xFF282828 : 0xFFE4E6EB;

    draw_rect(phone_x, phone_y + 20, phone_w, phone_h - 20, bg_col);
    
    // Header
    draw_rounded_rect(phone_x + 12, phone_y + 26, phone_w - 24, 46, 12, header_bg);
    draw_rounded_rect(phone_x + 18, phone_y + 33, 56, 32, 8, header_pill_bg);
    int back_tw = fb_get_string_width("< Back", sys_font_path);
    draw_string(phone_x + 18 + ((56 - back_tw) / 2), phone_y + 39, "< Back", header_pill_text);
    
    int title_tw = fb_get_string_width("Personalization", sys_font_path);
    draw_string(phone_x + (phone_w - title_tw) / 2, phone_y + 39, "Personalization", header_title);
    
    int card_x = phone_x + 12;
    int card_y = phone_y + 78;
    int card_w = phone_w - 24;
    int card_h = 242;
    draw_rounded_rect(card_x - 1, card_y - 1, card_w + 2, card_h + 2, 14, card_border);
    draw_rounded_rect(card_x, card_y, card_w, card_h, 14, card_bg);
    draw_string(card_x + 16, card_y + 12, "THEME & CUSTOMIZATION", card_title);
    
    // Row 1: Theme
    int r1_y = card_y + 36;
    draw_rounded_rect(card_x + 8, r1_y, card_w - 16, 44, 10, row_bg);
    draw_string(card_x + 20, r1_y + 11, "Theme Mode", text_primary);
    draw_rounded_rect(card_x + card_w - 68, r1_y + 10, 56, 24, 12, pill_btn_bg);
    const char *thm_str = is_dark_mode ? "Dark" : "Light";
    int thm_tw = fb_get_string_width(thm_str, sys_font_path);
    draw_string(card_x + card_w - 68 + ((56 - thm_tw) / 2), r1_y + 11, thm_str, text_primary);
    
    // Row 2: Wallpaper
    int r2_y = r1_y + 50;
    draw_rounded_rect(card_x + 8, r2_y, card_w - 16, 44, 10, row_bg);
    draw_string(card_x + 20, r2_y + 11, "Wallpaper", text_primary);
    draw_rounded_rect(card_x + card_w - 40, r2_y + 10, 26, 24, 8, pill_btn_bg);
    draw_string(card_x + card_w - 32, r2_y + 11, ">", text_muted);
    
    // Row 3: Icon Background
    int r3_y = r2_y + 50;
    draw_rounded_rect(card_x + 8, r3_y, card_w - 16, 44, 10, row_bg);
    draw_string(card_x + 20, r3_y + 11, "Icon Theme", text_primary);
    int ic_tw = fb_get_string_width(icon_themes[current_icon_theme].name, sys_font_path);
    draw_string(card_x + card_w - 46 - ic_tw, r3_y + 11, icon_themes[current_icon_theme].name, text_secondary);
    draw_rounded_rect(card_x + card_w - 40, r3_y + 10, 26, 24, 8, pill_btn_bg);
    draw_string(card_x + card_w - 32, r3_y + 11, ">", text_muted);
    
    // Row 4: Fonts
    int r4_y = r3_y + 50;
    draw_rounded_rect(card_x + 8, r4_y, card_w - 16, 44, 10, row_bg);
    draw_string(card_x + 20, r4_y + 11, "Typography & Fonts", text_primary);
    draw_rounded_rect(card_x + card_w - 40, r4_y + 10, 26, 24, 8, pill_btn_bg);
    draw_string(card_x + card_w - 32, r4_y + 11, ">", text_muted);
}

void draw_fonts_screen() {
    extern char sys_font_path[100];
    unsigned int bg_col = is_dark_mode ? 0xFF000000 : 0xFFF0F2F5;
    unsigned int header_bg = is_dark_mode ? 0xFF181818 : 0xFFFFFFFF;
    unsigned int header_pill_bg = is_dark_mode ? 0xFF2D2D2D : 0xFFE4E6EB;
    unsigned int header_pill_text = is_dark_mode ? 0xFFFFFFFF : 0xFF111111;
    unsigned int header_title = is_dark_mode ? 0xFFFFFFFF : 0xFF111111;
    unsigned int card_bg = is_dark_mode ? 0xFF141414 : 0xFFFFFFFF;
    unsigned int card_border = is_dark_mode ? 0xFF282828 : 0xFFDCDFE4;
    unsigned int card_title = is_dark_mode ? 0xFFA0A0A0 : 0xFF606770;
    unsigned int row_bg = is_dark_mode ? 0xFF1E1E1E : 0xFFF4F6F9;
    unsigned int badge_bg = is_dark_mode ? 0xFF2D2D2D : 0xFFE2E6EC;
    unsigned int badge_text = is_dark_mode ? 0xFFAAAAAA : 0xFF4E5564;
    unsigned int text_primary = is_dark_mode ? 0xFFFFFFFF : 0xFF111111;
    unsigned int text_muted = is_dark_mode ? 0xFF888888 : 0xFF8D949E;
    unsigned int pill_btn_bg = is_dark_mode ? 0xFF282828 : 0xFFE4E6EB;

    draw_rect(phone_x, phone_y + 20, phone_w, phone_h - 20, bg_col);
    
    // Header
    draw_rounded_rect(phone_x + 12, phone_y + 26, phone_w - 24, 46, 12, header_bg);
    draw_rounded_rect(phone_x + 18, phone_y + 33, 56, 32, 8, header_pill_bg);
    int back_tw = fb_get_string_width("< Back", sys_font_path);
    draw_string(phone_x + 18 + ((56 - back_tw) / 2), phone_y + 39, "< Back", header_pill_text);
    
    int title_tw = fb_get_string_width("Typography & Fonts", sys_font_path);
    draw_string(phone_x + (phone_w - title_tw) / 2, phone_y + 39, "Typography & Fonts", header_title);
    
    int card_x = phone_x + 12;
    int card_y = phone_y + 78;
    int card_w = phone_w - 24;
    int card_h = 192;
    draw_rounded_rect(card_x - 1, card_y - 1, card_w + 2, card_h + 2, 14, card_border);
    draw_rounded_rect(card_x, card_y, card_w, card_h, 14, card_bg);
    draw_string(card_x + 16, card_y + 12, "SYSTEM TYPOGRAPHY", card_title);
    
    // Row 1: App Title Font
    int r1_y = card_y + 36;
    draw_rounded_rect(card_x + 8, r1_y, card_w - 16, 44, 10, row_bg);
    draw_rounded_rect(card_x + 14, r1_y + 10, 28, 24, 6, badge_bg);
    draw_string(card_x + 16, r1_y + 11, "TTF", badge_text);
    draw_string(card_x + 48, r1_y + 11, "App Title Font", text_primary);
    draw_rounded_rect(card_x + card_w - 40, r1_y + 10, 26, 24, 8, pill_btn_bg);
    draw_string(card_x + card_w - 32, r1_y + 11, ">", text_muted);
    
    // Row 2: Lockscreen Clock Font
    int r2_y = r1_y + 50;
    draw_rounded_rect(card_x + 8, r2_y, card_w - 16, 44, 10, row_bg);
    draw_rounded_rect(card_x + 14, r2_y + 10, 28, 24, 6, badge_bg);
    draw_string(card_x + 16, r2_y + 11, "CLK", badge_text);
    draw_string(card_x + 48, r2_y + 11, "Lockscreen Clock Font", text_primary);
    draw_rounded_rect(card_x + card_w - 40, r2_y + 10, 26, 24, 8, pill_btn_bg);
    draw_string(card_x + card_w - 32, r2_y + 11, ">", text_muted);
    
    // Row 3: System Apps Font
    int r3_y = r2_y + 50;
    draw_rounded_rect(card_x + 8, r3_y, card_w - 16, 44, 10, row_bg);
    draw_rounded_rect(card_x + 14, r3_y + 10, 28, 24, 6, badge_bg);
    draw_string(card_x + 16, r3_y + 11, "SYS", badge_text);
    draw_string(card_x + 48, r3_y + 11, "System Apps Font", text_primary);
    draw_rounded_rect(card_x + card_w - 40, r3_y + 10, 26, 24, 8, pill_btn_bg);
    draw_string(card_x + card_w - 32, r3_y + 11, ">", text_muted);
}

void draw_icon_bg_screen() {
    extern char sys_font_path[100];
    unsigned int bg_col = is_dark_mode ? 0xFF000000 : 0xFFF0F2F5;
    unsigned int header_bg = is_dark_mode ? 0xFF181818 : 0xFFFFFFFF;
    unsigned int header_pill_bg = is_dark_mode ? 0xFF2D2D2D : 0xFFE4E6EB;
    unsigned int header_pill_text = is_dark_mode ? 0xFFFFFFFF : 0xFF111111;
    unsigned int header_title = is_dark_mode ? 0xFFFFFFFF : 0xFF111111;
    unsigned int row_bg = is_dark_mode ? 0xFF1E1E1E : 0xFFF4F6F9;
    unsigned int text_primary = is_dark_mode ? 0xFFFFFFFF : 0xFF111111;

    draw_rect(phone_x, phone_y + 20, phone_w, phone_h - 20, bg_col);
    
    // Header
    draw_rounded_rect(phone_x + 12, phone_y + 26, phone_w - 24, 46, 12, header_bg);
    draw_rounded_rect(phone_x + 18, phone_y + 33, 56, 32, 8, header_pill_bg);
    int back_tw = fb_get_string_width("< Back", sys_font_path);
    draw_string(phone_x + 18 + ((56 - back_tw) / 2), phone_y + 39, "< Back", header_pill_text);
    
    int title_tw = fb_get_string_width("Icon Theme", sys_font_path);
    draw_string(phone_x + (phone_w - title_tw) / 2, phone_y + 39, "Icon Theme", header_title);
    
    int list_y = phone_y + 78;
    for (int t = 0; t < (int)NUM_ICON_THEMES; t++) {
        draw_rounded_rect(phone_x + 12, list_y, phone_w - 24, 44, 10, row_bg);
        
        // Swatch
        int swatch_x = phone_x + 20;
        int swatch_y = list_y + 10;
        int sw_r = 6;
        unsigned int sw_border = RGB(icon_themes[t].border_r, icon_themes[t].border_g, icon_themes[t].border_b);
        
        for (int j = 0; j < 24; j++) {
            int cur_r = icon_themes[t].top_r + ((int)(icon_themes[t].bot_r - icon_themes[t].top_r) * j / 24);
            int cur_g = icon_themes[t].top_g + ((int)(icon_themes[t].bot_g - icon_themes[t].top_g) * j / 24);
            int cur_b = icon_themes[t].top_b + ((int)(icon_themes[t].bot_b - icon_themes[t].top_b) * j / 24);
            unsigned int sw_row = RGB(cur_r, cur_g, cur_b);
            
            for (int i = 0; i < 24; i++) {
                int in_rect = 0;
                int is_b = 0;
                if ((i >= sw_r && i < 24 - sw_r) || (j >= sw_r && j < 24 - sw_r)) {
                    in_rect = 1;
                    if (i == 0 || i == 23 || j == 0 || j == 23) is_b = 1;
                } else {
                    int cx = (i < sw_r) ? sw_r : 24 - 1 - sw_r;
                    int cy = (j < sw_r) ? sw_r : 24 - 1 - sw_r;
                    int dx = i - cx;
                    int dy = j - cy;
                    int d2 = dx * dx + dy * dy;
                    if (d2 <= sw_r * sw_r) {
                        in_rect = 1;
                        if (d2 >= (sw_r - 1) * (sw_r - 1)) is_b = 1;
                    }
                }
                if (in_rect) {
                    draw_pixel(swatch_x + i, swatch_y + j, is_b ? sw_border : sw_row);
                }
            }
        }
        
        draw_string(phone_x + 55, list_y + 11, (char *)icon_themes[t].name, text_primary);
        
        if (t == current_icon_theme) {
            int act_w = 60;
            int act_h = 24;
            int act_x = phone_x + phone_w - 20 - act_w;
            int act_y = list_y + 10;
            draw_rounded_rect(act_x, act_y, act_w, act_h, 12, 0xFF2EB85C);
            int act_tw = fb_get_string_width("Active", sys_font_path);
            draw_string(act_x + ((act_w - act_tw) / 2), act_y + 2, "Active", 0xFFFFFFFF);
        }
        
        list_y += 48;
    }
}

typedef struct {
    char filename[64];
    char full_path[128];
    char app_name[64];
    char app_folder[64];
    char version[16];
    char author[32];
    unsigned int size;
    int file_count;
    int is_installed;
} PackageInfo;

#define MAX_PACKAGES 16
static PackageInfo available_packages[MAX_PACKAGES];
static int num_packages = 0;
static int selected_package_idx = -1;
static char installer_status_msg[64] = "";
static unsigned int installer_status_color = 0;

static inline unsigned int read_u32_le(const unsigned char *p) {
    return ((unsigned int)p[0]) |
           (((unsigned int)p[1]) << 8) |
           (((unsigned int)p[2]) << 16) |
           (((unsigned int)p[3]) << 24);
}

static inline unsigned short read_u16_le(const unsigned char *p) {
    return (unsigned short)(((unsigned short)p[0]) | (((unsigned short)p[1]) << 8));
}

static void parse_json_str(const char *json, const char *key, char *out, int max_len) {
    out[0] = '\0';
    char pattern[32];
    int p = 0;
    pattern[p++] = '"';
    int k = 0;
    while (key[k] != '\0' && p < 30) pattern[p++] = key[k++];
    pattern[p++] = '"';
    pattern[p] = '\0';
    
    const char *found = 0;
    for (int i = 0; json[i] != '\0'; i++) {
        int match = 1;
        for (int j = 0; pattern[j] != '\0'; j++) {
            if (json[i + j] != pattern[j]) { match = 0; break; }
        }
        if (match) { found = &json[i + p]; break; }
    }
    if (!found) return;
    
    while (*found == ':' || *found == ' ' || *found == '\t') found++;
    if (*found == '"') {
        found++;
        int o = 0;
        while (*found != '"' && *found != '\0' && o < max_len - 1) {
            out[o++] = *found++;
        }
        out[o] = '\0';
    } else {
        int o = 0;
        while (*found != ',' && *found != '}' && *found != ' ' && *found != '\0' && o < max_len - 1) {
            out[o++] = *found++;
        }
        out[o] = '\0';
    }
}

void scan_packages() {
    num_packages = 0;
    const char *search_dirs[] = {"packages", "test", ""};
    
    for (int d_idx = 0; d_idx < 3; d_idx++) {
        const char *dir_name = search_dirs[d_idx];
        NYKON_DIR *dir = fs_opendir(dir_name);
        if (!dir) continue;
        
        nykon_dirent *ent;
        while ((ent = fs_readdir(dir)) != 0) {
            if (num_packages >= MAX_PACKAGES) break;
            int len = 0;
            while (ent->d_name[len] != '\0') len++;
            if (len >= 6 && ent->d_name[len-6] == '.' && ent->d_name[len-5] == 'n' && ent->d_name[len-4] == 'k' && ent->d_name[len-3] == 'p' && ent->d_name[len-2] == 'k' && ent->d_name[len-1] == 'g') {
                // Check if already added
                int already_found = 0;
                for (int p = 0; p < num_packages; p++) {
                    if (my_strncmp(available_packages[p].filename, ent->d_name, 64) == 0) {
                        already_found = 1;
                        break;
                    }
                }
                if (already_found) continue;

                PackageInfo *pkg = &available_packages[num_packages];
                my_strncpy(pkg->filename, ent->d_name, 63);
                pkg->filename[63] = '\0';
                
                pkg->full_path[0] = '\0';
                int fp = 0;
                if (dir_name[0] != '\0') {
                    int dp = 0;
                    while (dir_name[dp] != '\0') { pkg->full_path[fp++] = dir_name[dp++]; }
                    pkg->full_path[fp++] = '/';
                }
                int fn = 0;
                while (ent->d_name[fn] != '\0' && fp < 127) { pkg->full_path[fp++] = ent->d_name[fn++]; }
                pkg->full_path[fp] = '\0';
                
                unsigned int fsize = 0;
                char *fdata = fs_get_file_data(pkg->full_path, &fsize);
                if (!fdata) {
                    fdata = fs_get_file_data(ent->d_name, &fsize);
                    if (fdata) my_strncpy(pkg->full_path, ent->d_name, 127);
                }
                
                if (fdata && fsize >= 10 && fdata[0] == 'N' && fdata[1] == 'K' && fdata[2] == 'P' && fdata[3] == 'K') {
                    pkg->size = fsize;
                    const unsigned char *uf = (const unsigned char *)fdata;
                    unsigned int manifest_len = read_u32_le(uf + 6);
                    if (manifest_len > 0 && 10 + manifest_len <= fsize) {
                        char *manifest_str = fdata + 10;
                        parse_json_str(manifest_str, "name", pkg->app_name, 63);
                        parse_json_str(manifest_str, "folder", pkg->app_folder, 63);
                        parse_json_str(manifest_str, "version", pkg->version, 15);
                        parse_json_str(manifest_str, "author", pkg->author, 31);
                        if (pkg->app_name[0] == '\0') my_strncpy(pkg->app_name, ent->d_name, 63);
                        if (pkg->app_folder[0] == '\0') my_strncpy(pkg->app_folder, pkg->app_name, 63);
                        if (pkg->version[0] == '\0') my_strncpy(pkg->version, "1.0", 15);
                        if (pkg->author[0] == '\0') my_strncpy(pkg->author, "Developer", 31);
                        
                        unsigned short file_count = read_u16_le(uf + 10 + manifest_len);
                        pkg->file_count = (int)file_count;
                    }
                    num_packages++;
                }
            }
        }
        fs_closedir(dir);
    }
}

static PackageInfo current_inspected_pkg;
static int has_inspected_pkg = 0;

int inspect_package_file(const char *filepath) {
    unsigned int fsize = 0;
    char *fdata = fs_get_file_data(filepath, &fsize);
    if (!fdata || fsize < 10 || fdata[0] != 'N' || fdata[1] != 'K' || fdata[2] != 'P' || fdata[3] != 'K') {
        my_strncpy(installer_status_msg, "Not a valid .nkpkg file!", 63);
        installer_status_color = RGB(255, 60, 60);
        has_inspected_pkg = 0;
        return 0;
    }
    
    my_strncpy(current_inspected_pkg.full_path, filepath, 127);
    
    int last_slash = -1;
    for (int i = 0; filepath[i] != '\0'; i++) {
        if (filepath[i] == '/') last_slash = i;
    }
    const char *fn = (last_slash >= 0) ? &filepath[last_slash + 1] : filepath;
    my_strncpy(current_inspected_pkg.filename, fn, 63);
    
    current_inspected_pkg.size = fsize;
    current_inspected_pkg.is_installed = 0;
    
    const unsigned char *uf = (const unsigned char *)fdata;
    unsigned int manifest_len = read_u32_le(uf + 6);
    if (manifest_len > 0 && 10 + manifest_len <= fsize) {
        char *manifest_str = fdata + 10;
        parse_json_str(manifest_str, "name", current_inspected_pkg.app_name, 63);
        parse_json_str(manifest_str, "folder", current_inspected_pkg.app_folder, 63);
        parse_json_str(manifest_str, "version", current_inspected_pkg.version, 15);
        parse_json_str(manifest_str, "author", current_inspected_pkg.author, 31);
        
        if (current_inspected_pkg.app_name[0] == '\0') my_strncpy(current_inspected_pkg.app_name, current_inspected_pkg.filename, 63);
        if (current_inspected_pkg.app_folder[0] == '\0') my_strncpy(current_inspected_pkg.app_folder, current_inspected_pkg.app_name, 63);
        if (current_inspected_pkg.version[0] == '\0') my_strncpy(current_inspected_pkg.version, "1.0", 15);
        if (current_inspected_pkg.author[0] == '\0') my_strncpy(current_inspected_pkg.author, "Developer", 31);
        
        unsigned short file_count = read_u16_le(uf + 10 + manifest_len);
        current_inspected_pkg.file_count = (int)file_count;
    }
    
    has_inspected_pkg = 1;
    installer_status_msg[0] = '\0';
    return 1;
}

static void dynamic_pkg_init() {}
static void dynamic_pkg_update() {}
static void dynamic_pkg_draw() {
    draw_rect(phone_x, phone_y + 20, phone_w, phone_h - 20, RGB(18, 22, 32));
    draw_string(phone_x + 40, phone_y + 100, "App Running", RGB(255, 255, 255));
}

int install_inspected_package() {
    if (!has_inspected_pkg) return 0;
    PackageInfo *pkg = &current_inspected_pkg;
    
    unsigned int fsize = 0;
    char *fdata = fs_get_file_data(pkg->full_path, &fsize);
    if (!fdata || fsize < 12 || fdata[0] != 'N' || fdata[1] != 'K' || fdata[2] != 'P' || fdata[3] != 'K') {
        my_strncpy(installer_status_msg, "Corrupt package!", 63);
        installer_status_color = RGB(255, 60, 60);
        return 0;
    }
    
    const unsigned char *uf = (const unsigned char *)fdata;
    unsigned int manifest_len = read_u32_le(uf + 6);
    unsigned int offset = 10 + manifest_len;
    if (offset + 2 > fsize) return 0;
    
    unsigned short file_count = read_u16_le(uf + offset);
    offset += 2;
    
    char app_dir_path[128] = "apps/";
    int ad_p = 5;
    int af_p = 0;
    while (pkg->app_folder[af_p] != '\0' && ad_p < 120) {
        app_dir_path[ad_p++] = pkg->app_folder[af_p++];
    }
    app_dir_path[ad_p++] = '/';
    app_dir_path[ad_p] = '\0';
    
    fs_create_file("apps", 1);
    fs_create_file(app_dir_path, 1);
    
    for (int i = 0; i < (int)file_count; i++) {
        if (offset + 2 > fsize) break;
        unsigned short path_len = read_u16_le(uf + offset);
        offset += 2;
        if (offset + path_len + 4 > fsize) break;
        
        char rel_path[128];
        int r = 0;
        while (r < path_len && r < 127) {
            rel_path[r] = fdata[offset + r];
            r++;
        }
        rel_path[r] = '\0';
        offset += path_len;
        
        unsigned int item_size = read_u32_le(uf + offset);
        offset += 4;
        if (offset + item_size > fsize) break;
        
        char *item_data = fdata + offset;
        offset += item_size;
        
        char dest_path[256];
        int dp = 0;
        int s = 0;
        while (app_dir_path[s] != '\0') dest_path[dp++] = app_dir_path[s++];
        int rp = 0;
        while (rel_path[rp] != '\0' && dp < 255) dest_path[dp++] = rel_path[rp++];
        dest_path[dp] = '\0';
        
        fs_create_file(dest_path, 0);
        fs_write_file(dest_path, item_data, item_size);
    }
    
    // Register the app dynamically so it shows up on the Home screen
    int already_reg = 0;
    for (int a = 0; a < num_registered_apps; a++) {
        if (my_strncmp(registered_apps[a]->name, pkg->app_name, 64) == 0) {
            already_reg = 1;
            break;
        }
    }
    
    if (!already_reg && num_registered_apps < 32) {
        static NykonApp dynamic_apps[8];
        static int dyn_app_count = 0;
        if (dyn_app_count < 8) {
            NykonApp *da = &dynamic_apps[dyn_app_count++];
            da->name = pkg->app_name;
            da->icon_path = 0;
            da->icon_color = RGB(100, 200, 255);
            da->init = dynamic_pkg_init;
            da->update = dynamic_pkg_update;
            da->draw = dynamic_pkg_draw;
            registered_apps[num_registered_apps++] = da;
        }
    }
    
    pkg->is_installed = 1;
    my_strncpy(installer_status_msg, "Installed successfully!", 63);
    installer_status_color = RGB(50, 220, 80);
    return 1;
}

void draw_installer_screen() {
    extern char sys_font_path[100];
    unsigned int bg_col = is_dark_mode ? 0xFF000000 : 0xFFF0F2F5;
    unsigned int header_bg = is_dark_mode ? 0xFF181818 : 0xFFFFFFFF;
    unsigned int header_pill_bg = is_dark_mode ? 0xFF2D2D2D : 0xFFE4E6EB;
    unsigned int header_pill_text = is_dark_mode ? 0xFFFFFFFF : 0xFF111111;
    unsigned int header_title = is_dark_mode ? 0xFFFFFFFF : 0xFF111111;
    unsigned int card_bg = is_dark_mode ? 0xFF141414 : 0xFFFFFFFF;
    unsigned int card_border = is_dark_mode ? 0xFF282828 : 0xFFDCDFE4;
    unsigned int row_bg = is_dark_mode ? 0xFF1E1E1E : 0xFFF4F6F9;
    unsigned int badge_bg = is_dark_mode ? 0xFF2D2D2D : 0xFFE2E6EC;
    unsigned int text_primary = is_dark_mode ? 0xFFFFFFFF : 0xFF111111;
    unsigned int text_secondary = is_dark_mode ? 0xFFAAAAAA : 0xFF606770;
    unsigned int divider = is_dark_mode ? 0xFF222222 : 0xFFE4E6EB;
    unsigned int pill_btn_bg = is_dark_mode ? 0xFF282828 : 0xFFE4E6EB;

    draw_rect(phone_x, phone_y + 20, phone_w, phone_h - 20, bg_col);
    
    // 1. Header Card
    draw_rounded_rect(phone_x + 12, phone_y + 26, phone_w - 24, 46, 12, header_bg);
    
    int title_tw = fb_get_string_width("App Installer", sys_font_path);
    draw_string(phone_x + (phone_w - title_tw) / 2, phone_y + 39, "App Installer", header_title);

    // 2. Top "Browse File Manager" Action Card
    int b_x = phone_x + 12, b_y = phone_y + 78, b_w = phone_w - 24, b_h = 44;
    draw_rounded_rect(b_x - 1, b_y - 1, b_w + 2, b_h + 2, 14, card_border);
    draw_rounded_rect(b_x, b_y, b_w, b_h, 14, row_bg);
    const char *browse_str = "+ Browse File Manager";
    int br_tw = fb_get_string_width(browse_str, sys_font_path);
    draw_string(b_x + ((b_w - br_tw) / 2), b_y + 11, browse_str, text_primary);

    if (has_inspected_pkg) {
        PackageInfo *pkg = &current_inspected_pkg;
        
        int card_x = phone_x + 12;
        int card_y = phone_y + 130;
        int card_w = phone_w - 24;
        int card_h = 246;
        draw_rounded_rect(card_x - 1, card_y - 1, card_w + 2, card_h + 2, 14, card_border);
        draw_rounded_rect(card_x, card_y, card_w, card_h, 14, card_bg);
        
        // App icon / badge
        draw_rounded_rect(card_x + 16, card_y + 14, 38, 38, 8, badge_bg);
        draw_string(card_x + 22, card_y + 22, "PKG", text_primary);
        
        // App Name
        draw_string(card_x + 62, card_y + 16, pkg->app_name, text_primary);
        
        // Version
        char v_line[64] = "v";
        int vl = 1;
        int vp = 0;
        while (pkg->version[vp] != '\0' && vl < 60) v_line[vl++] = pkg->version[vp++];
        v_line[vl] = '\0';
        draw_rounded_rect(card_x + 62, card_y + 36, 44, 18, 8, pill_btn_bg);
        draw_string(card_x + 68, card_y + 36, v_line, text_secondary);
        
        // Divider
        draw_rect(card_x + 12, card_y + 60, card_w - 24, 1, divider);
        
        // File path
        char path_line[64] = "File: ";
        int pl = 6;
        int pp = 0;
        while (pkg->full_path[pp] != '\0' && pl < 60) path_line[pl++] = pkg->full_path[pp++];
        path_line[pl] = '\0';
        draw_string(card_x + 16, card_y + 70, path_line, text_secondary);
        
        // Size
        char sz_line[64] = "Size: ";
        int sl = 6;
        int kb = pkg->size / 1024;
        if (kb == 0) kb = 1;
        char kb_s[16];
        int ki = 0;
        int tk = kb;
        while (tk > 0) { kb_s[ki++] = (tk % 10) + '0'; tk /= 10; }
        if (ki == 0) kb_s[ki++] = '0';
        while (ki > 0) sz_line[sl++] = kb_s[--ki];
        sz_line[sl++] = ' '; sz_line[sl++] = 'K'; sz_line[sl++] = 'B';
        sz_line[sl] = '\0';
        draw_string(card_x + 16, card_y + 92, sz_line, text_secondary);

        // INSTALL APP Button
        int btn_y = card_y + 120;
        draw_rounded_rect(card_x + 16, btn_y, card_w - 32, 42, 12, 0xFF2EB85C);
        const char *inst_str = pkg->is_installed ? "Reinstall App" : "Install App";
        int inst_tw = fb_get_string_width(inst_str, sys_font_path);
        draw_string(card_x + 16 + ((card_w - 32 - inst_tw) / 2), btn_y + 11, inst_str, 0xFFFFFFFF);
        
        // Status message
        if (installer_status_msg[0] != '\0') {
            draw_string(card_x + 16, card_y + 172, installer_status_msg, installer_status_color);
        }
        
        // Clear Selection button
        int clr_y = card_y + 196;
        draw_rounded_rect(card_x + 16, clr_y, card_w - 32, 34, 10, pill_btn_bg);
        const char *clr_str = "Clear Selection";
        int clr_tw = fb_get_string_width(clr_str, sys_font_path);
        draw_string(card_x + 16 + ((card_w - 32 - clr_tw) / 2), clr_y + 7, clr_str, text_secondary);
    } else {
        // Explanatory card
        int card_x = phone_x + 12;
        int card_y = phone_y + 130;
        int card_w = phone_w - 24;
        int card_h = 176;
        draw_rounded_rect(card_x - 1, card_y - 1, card_w + 2, card_h + 2, 14, card_border);
        draw_rounded_rect(card_x, card_y, card_w, card_h, 14, card_bg);
        
        draw_rounded_rect(card_x + 16, card_y + 14, 38, 38, 8, badge_bg);
        draw_string(card_x + 22, card_y + 22, "PKG", text_primary);
        draw_string(card_x + 64, card_y + 22, "No Package Selected", text_primary);
        
        draw_rect(card_x + 12, card_y + 60, card_w - 24, 1, divider);
        
        draw_string(card_x + 16, card_y + 74, "Tap '+ Browse File Manager'", text_secondary);
        draw_string(card_x + 16, card_y + 96, "to pick any .nkpkg package from", text_secondary);
        draw_string(card_x + 16, card_y + 118, "your folders (like test/ or apps/)", text_secondary);
        draw_string(card_x + 16, card_y + 140, "to install and launch it.", text_secondary);
    }
}

static unsigned int read_hw_cpu_midr(void) {
    unsigned int val = 0;
#ifndef LINUX_BUILD
    __asm__ volatile("mrc p15, 0, %0, c0, c0, 0" : "=r"(val));
#endif
    return val;
}

static void get_hardware_cpu_info(char *name_buf, char *midr_hex_buf) {
    unsigned int midr = read_hw_cpu_midr();
    unsigned int part = (midr >> 4) & 0xFFF;
    unsigned int var = (midr >> 20) & 0xF;
    unsigned int rev = midr & 0xF;

    const char *hex_chars = "0123456789ABCDEF";
    midr_hex_buf[0] = '0'; midr_hex_buf[1] = 'x';
    for (int i = 7; i >= 0; i--) {
        midr_hex_buf[2 + (7 - i)] = hex_chars[(midr >> (i * 4)) & 0xF];
    }
    midr_hex_buf[10] = '\0';

    if (part == 0xC07) {
        // Cortex-A7 (Banana Pi M2 Zero)
        name_buf[0] = 'C'; name_buf[1] = 'o'; name_buf[2] = 'r'; name_buf[3] = 't';
        name_buf[4] = 'e'; name_buf[5] = 'x'; name_buf[6] = '-'; name_buf[7] = 'A';
        name_buf[8] = '7'; name_buf[9] = ' '; name_buf[10] = '('; name_buf[11] = 'r';
        name_buf[12] = '0' + var; name_buf[13] = 'p'; name_buf[14] = '0' + rev;
        name_buf[15] = ')'; name_buf[16] = '\0';
    } else if (part == 0x926) {
        // ARM926EJ-S (QEMU VersatilePB)
        name_buf[0] = 'A'; name_buf[1] = 'R'; name_buf[2] = 'M'; name_buf[3] = '9';
        name_buf[4] = '2'; name_buf[5] = '6'; name_buf[6] = 'E'; name_buf[7] = 'J';
        name_buf[8] = '-'; name_buf[9] = 'S'; name_buf[10] = ' '; name_buf[11] = '(';
        name_buf[12] = 'r'; name_buf[13] = '0' + var; name_buf[14] = 'p'; name_buf[15] = '0' + rev;
        name_buf[16] = ')'; name_buf[17] = '\0';
    } else {
        // Generic ARM
        name_buf[0] = 'A'; name_buf[1] = 'R'; name_buf[2] = 'M'; name_buf[3] = ' ';
        name_buf[4] = '0'; name_buf[5] = 'x';
        name_buf[6] = hex_chars[(part >> 8) & 0xF];
        name_buf[7] = hex_chars[(part >> 4) & 0xF];
        name_buf[8] = hex_chars[part & 0xF];
        name_buf[9] = '\0';
    }
}

void draw_about_screen() {
    extern char sys_font_path[100];
    unsigned int bg_col = is_dark_mode ? 0xFF000000 : 0xFFF0F2F5;
    unsigned int header_bg = is_dark_mode ? 0xFF181818 : 0xFFFFFFFF;
    unsigned int header_pill_bg = is_dark_mode ? 0xFF2D2D2D : 0xFFE4E6EB;
    unsigned int header_pill_text = is_dark_mode ? 0xFFFFFFFF : 0xFF111111;
    unsigned int header_title = is_dark_mode ? 0xFFFFFFFF : 0xFF111111;
    unsigned int card_bg = is_dark_mode ? 0xFF141414 : 0xFFFFFFFF;
    unsigned int card_border = is_dark_mode ? 0xFF282828 : 0xFFDCDFE4;
    unsigned int card_title = is_dark_mode ? 0xFFA0A0A0 : 0xFF606770;
    unsigned int row_bg = is_dark_mode ? 0xFF1E1E1E : 0xFFF4F6F9;
    unsigned int badge_bg = is_dark_mode ? 0xFF2D2D2D : 0xFFE2E6EC;
    unsigned int badge_text = is_dark_mode ? 0xFFAAAAAA : 0xFF4E5564;
    unsigned int text_primary = is_dark_mode ? 0xFFFFFFFF : 0xFF111111;

    draw_rect(phone_x, phone_y + 20, phone_w, phone_h - 20, bg_col);
    
    // Header
    draw_rounded_rect(phone_x + 12, phone_y + 26, phone_w - 24, 46, 12, header_bg);
    draw_rounded_rect(phone_x + 18, phone_y + 33, 56, 32, 8, header_pill_bg);
    int back_tw = fb_get_string_width("< Back", sys_font_path);
    draw_string(phone_x + 18 + ((56 - back_tw) / 2), phone_y + 39, "< Back", header_pill_text);
    
    int title_tw = fb_get_string_width("About Nykon OS", sys_font_path);
    draw_string(phone_x + (phone_w - title_tw) / 2, phone_y + 39, "About Nykon OS", header_title);
    
    int card_x = phone_x + 12;
    int card_y = phone_y + 78;
    int card_w = phone_w - 24;
    int card_h = 242;
    draw_rounded_rect(card_x - 1, card_y - 1, card_w + 2, card_h + 2, 14, card_border);
    draw_rounded_rect(card_x, card_y, card_w, card_h, 14, card_bg);
    draw_string(card_x + 16, card_y + 12, "SYSTEM INFORMATION", card_title);
    
    // OS Row
    int r1_y = card_y + 36;
    draw_rounded_rect(card_x + 8, r1_y, card_w - 16, 44, 10, row_bg);
    draw_rounded_rect(card_x + 14, r1_y + 10, 28, 24, 6, badge_bg);
    draw_string(card_x + 18, r1_y + 11, "OS", badge_text);
    draw_string(card_x + 48, r1_y + 11, "Nykon OS v1.0", text_primary);
    
    // Processor Row
    char cpu_name[32];
    char midr_hex[16];
    get_hardware_cpu_info(cpu_name, midr_hex);
    char proc_line[64];
    int pidx = 0;
    for (int i = 0; cpu_name[i]; i++) proc_line[pidx++] = cpu_name[i];
    proc_line[pidx++] = ' '; proc_line[pidx++] = '[';
    for (int i = 0; midr_hex[i]; i++) proc_line[pidx++] = midr_hex[i];
    proc_line[pidx++] = ']'; proc_line[pidx] = '\0';
    
    int r2_y = r1_y + 50;
    draw_rounded_rect(card_x + 8, r2_y, card_w - 16, 44, 10, row_bg);
    draw_rounded_rect(card_x + 14, r2_y + 10, 28, 24, 6, badge_bg);
    draw_string(card_x + 16, r2_y + 11, "CPU", badge_text);
    draw_string(card_x + 48, r2_y + 11, proc_line, text_primary);
    
    // Memory Row
    unsigned int used_kb = mm_get_used_bytes() / 1024;
    char mem_line[64];
    int midx = 0;
#if defined(TARGET_BPI)
    const char *phys_prefix = "512MB (Heap: ";
#else
    const char *phys_prefix = "256MB (Heap: ";
#endif
    for (int i = 0; phys_prefix[i]; i++) mem_line[midx++] = phys_prefix[i];
    
    if (used_kb >= 1000) {
        mem_line[midx++] = (used_kb / 1000) + '0';
        mem_line[midx++] = '.';
        mem_line[midx++] = ((used_kb % 1000) / 100) + '0';
        mem_line[midx++] = 'M'; mem_line[midx++] = 'B';
    } else {
        if (used_kb >= 100) mem_line[midx++] = (used_kb / 100) + '0';
        if (used_kb >= 10) mem_line[midx++] = ((used_kb / 10) % 10) + '0';
        mem_line[midx++] = (used_kb % 10) + '0';
        mem_line[midx++] = 'K'; mem_line[midx++] = 'B';
    }
    mem_line[midx++] = ' '; mem_line[midx++] = 'u'; mem_line[midx++] = 's'; mem_line[midx++] = 'e'; mem_line[midx++] = 'd'; mem_line[midx++] = ')';
    mem_line[midx] = '\0';
    
    int r3_y = r2_y + 50;
    draw_rounded_rect(card_x + 8, r3_y, card_w - 16, 44, 10, row_bg);
    draw_rounded_rect(card_x + 14, r3_y + 10, 28, 24, 6, badge_bg);
    draw_string(card_x + 16, r3_y + 11, "RAM", badge_text);
    draw_string(card_x + 48, r3_y + 11, mem_line, text_primary);
    
    // Display Row
    int r4_y = r3_y + 50;
    draw_rounded_rect(card_x + 8, r4_y, card_w - 16, 44, 10, row_bg);
    draw_rounded_rect(card_x + 14, r4_y + 10, 28, 24, 6, badge_bg);
    draw_string(card_x + 14, r4_y + 11, "DISP", badge_text);
#if defined(TARGET_BPI)
    draw_string(card_x + 48, r4_y + 11, "HDMI 720p Double-Buffered", text_primary);
#else
    draw_string(card_x + 48, r4_y + 11, "PL110 800x600 32bpp", text_primary);
#endif
}

void draw_files_screen() {
    extern char sys_font_path[100];
    unsigned int bg_col = is_dark_mode ? 0xFF000000 : 0xFFF0F2F5;
    unsigned int header_bg = is_dark_mode ? 0xFF181818 : 0xFFFFFFFF;
    unsigned int header_pill_bg = is_dark_mode ? 0xFF2D2D2D : 0xFFE4E6EB;
    unsigned int header_pill_text = is_dark_mode ? 0xFFFFFFFF : 0xFF111111;
    unsigned int header_title = is_dark_mode ? 0xFFFFFFFF : 0xFF111111;
    unsigned int row_bg = is_dark_mode ? 0xFF1E1E1E : 0xFFF4F6F9;
    unsigned int badge_bg = is_dark_mode ? 0xFF2D2D2D : 0xFFE2E6EC;
    unsigned int badge_text = is_dark_mode ? 0xFFAAAAAA : 0xFF4E5564;
    unsigned int text_primary = is_dark_mode ? 0xFFFFFFFF : 0xFF111111;
    unsigned int text_secondary = is_dark_mode ? 0xFFA0A0A0 : 0xFF606770;
    unsigned int text_muted = is_dark_mode ? 0xFF888888 : 0xFF8D949E;
    unsigned int pill_btn_bg = is_dark_mode ? 0xFF282828 : 0xFFE4E6EB;

    draw_rect(phone_x, phone_y + 20, phone_w, phone_h - 20, bg_col);
    
    // Modern Header Card
    draw_rounded_rect(phone_x + 12, phone_y + 26, phone_w - 24, 46, 12, header_bg);
    
    int is_picker = (is_picking_app_file || is_picking_package || is_picking_wallpaper || is_picking_sys_font || is_picking_lock_font || is_picking_app_title_font);
    
    if (current_dir[0] == '\0') {
        if (is_picker) {
            const char *nav_label = "< Cancel";
            int nav_tw = fb_get_string_width(nav_label, sys_font_path);
            int nav_w = nav_tw + 16;
            if (nav_w < 56) nav_w = 56;
            draw_rounded_rect(phone_x + 18, phone_y + 33, nav_w, 32, 8, header_pill_bg);
            draw_string(phone_x + 18 + ((nav_w - nav_tw) / 2), phone_y + 39, nav_label, header_pill_text);
        }
        
        const char *t_title = is_picking_package ? "Select Package" : (is_picking_app_file ? "Select File" : "Files");
        int title_w = fb_get_string_width(t_title, sys_font_path);
        draw_string(phone_x + (phone_w - title_w) / 2, phone_y + 39, t_title, header_title);
    } else {
        draw_rounded_rect(phone_x + 18, phone_y + 33, 56, 32, 8, header_pill_bg);
        int up_tw = fb_get_string_width("< Up", sys_font_path);
        draw_string(phone_x + 18 + ((56 - up_tw) / 2), phone_y + 39, "< Up", header_pill_text);
        
        char path_disp[64] = "/";
        int pd_len = 1;
        for (int k = 0; current_dir[k] && pd_len < 60; k++) path_disp[pd_len++] = current_dir[k];
        path_disp[pd_len] = '\0';
        draw_string(phone_x + 82, phone_y + 39, path_disp, header_title);
    }
    
    int in_sys = 0;
    if (current_dir[0] == 's' && current_dir[1] == 'y' && current_dir[2] == 's' && current_dir[3] == '/') {
        in_sys = 1;
    }
    
    if (!in_sys && !is_picker) {
        // [ + Fil ] pill
        int fil_w = 44;
        int fil_x = phone_x + phone_w - 18 - fil_w;
        draw_rounded_rect(fil_x, phone_y + 33, fil_w, 32, 8, header_pill_bg);
        int ftw = fb_get_string_width("+Fil", sys_font_path);
        draw_string(fil_x + ((fil_w - ftw) / 2), phone_y + 39, "+Fil", header_pill_text);
        
        // [ + Fldr ] pill
        int fldr_w = 48;
        int fldr_x = fil_x - 8 - fldr_w;
        draw_rounded_rect(fldr_x, phone_y + 33, fldr_w, 32, 8, header_pill_bg);
        int fdtw = fb_get_string_width("+Fldr", sys_font_path);
        draw_string(fldr_x + ((fldr_w - fdtw) / 2), phone_y + 39, "+Fldr", header_pill_text);
    }
    
    NYKON_DIR *dir = fs_opendir(current_dir);
    current_file_count = 0;
    if (dir) {
        nykon_dirent *ent;
        while ((ent = fs_readdir(dir)) != 0 && current_file_count < 14) {
            // Hide boot test file and hidden dotfiles
            if (ent->d_name[0] == '.' || 
                my_strncmp(ent->d_name, "TEST.TXT", 8) == 0 ||
                my_strncmp(ent->d_name, "test.txt", 8) == 0) {
                continue;
            }
            my_strncpy(current_files[current_file_count].name, ent->d_name, 100);
            current_files[current_file_count].is_dir = (ent->d_type == DT_DIR);
            current_files[current_file_count].size = ent->d_size;
            current_file_count++;
        }
        fs_closedir(dir);
    }
    
    int list_y = phone_y + 78;
    for (int i = 0; i < current_file_count && i < 9; i++) {
        draw_rounded_rect(phone_x + 12, list_y, phone_w - 24, 44, 10, row_bg);
        
        if (current_files[i].is_dir) {
            // Directory badge
            draw_rounded_rect(phone_x + 20, list_y + 10, 32, 24, 6, badge_bg);
            int tw = fb_get_string_width("DIR", sys_font_path);
            draw_string(phone_x + 20 + ((32 - tw) / 2), list_y + 11, "DIR", badge_text);
            
            draw_string(phone_x + 60, list_y + 11, current_files[i].name, text_primary);
            
            draw_rounded_rect(phone_x + phone_w - 38, list_y + 10, 20, 24, 6, pill_btn_bg);
            draw_string(phone_x + phone_w - 32, list_y + 11, ">", text_muted);
        } else {
            // Determine file type badge
            const char *fn = current_files[i].name;
            int flen = 0;
            while (fn[flen]) flen++;
            
            const char *badge = "FIL";
            unsigned int badge_col = badge_bg;
            unsigned int b_txt_col = is_dark_mode ? 0xFFFFFFFF : 0xFF222222;
            if (flen >= 6 && (my_strncmp(fn + flen - 6, ".nkpkg", 6) == 0)) {
                badge = "PKG";
                badge_col = is_dark_mode ? 0xFF3D3D3D : 0xFFD8DCE4;
            } else if (flen >= 3 && (my_strncmp(fn + flen - 3, ".gb", 3) == 0 || my_strncmp(fn + flen - 4, ".gbc", 4) == 0)) {
                badge = "GB";
                badge_col = is_dark_mode ? 0xFF3D2D3D : 0xFFE8D8E8;
            } else if (flen >= 4 && (my_strncmp(fn + flen - 4, ".png", 4) == 0 || my_strncmp(fn + flen - 4, ".tga", 4) == 0)) {
                badge = "IMG";
                badge_col = is_dark_mode ? 0xFF2D3D2D : 0xFFD8E8D8;
            }
            
            draw_rounded_rect(phone_x + 20, list_y + 10, 32, 24, 6, badge_col);
            int tw = fb_get_string_width(badge, sys_font_path);
            draw_string(phone_x + 20 + ((32 - tw) / 2), list_y + 11, badge, b_txt_col);
            
            draw_string(phone_x + 60, list_y + 11, current_files[i].name, text_primary);
            
            // Format size
            char size_str[16];
            int size = current_files[i].size;
            int kb = (size + 1023) / 1024;
            if (kb == 0 && size > 0) kb = 1;
            char temp[16];
            int t = 0;
            int tk = kb;
            while (tk > 0) { temp[t++] = (tk % 10) + '0'; tk /= 10; }
            if (t == 0) temp[t++] = '0';
            int idx = 0;
            while (t > 0) size_str[idx++] = temp[--t];
            size_str[idx++] = ' '; size_str[idx++] = 'K'; size_str[idx++] = 'B';
            size_str[idx++] = '\0';
            
            int size_w = fb_get_string_width(size_str, sys_font_path);
            draw_string(phone_x + phone_w - size_w - 24, list_y + 11, size_str, text_secondary);
        }
        
        list_y += 48;
    }
    
    if (current_file_count == 0) {
        draw_string(phone_x + 24, phone_y + 120, "No files found in this folder.", text_muted);
    }
}

void draw_file_viewer_screen() {
    unsigned int bg_color = is_dark_mode ? RGB(10, 10, 10) : RGB(255, 255, 255);
    unsigned int title_bg = is_dark_mode ? RGB(30, 30, 30) : RGB(240, 240, 240);
    unsigned int text_primary = is_dark_mode ? RGB(255, 255, 255) : RGB(0, 0, 0);

    draw_rect(phone_x, phone_y + 20, phone_w, phone_h - 20, bg_color);
    
    // Title Bar
    draw_rect(phone_x, phone_y + 20, phone_w, 40, title_bg);
    draw_string(phone_x + phone_w/2 - 40, phone_y + 36, "Photo Viewer", text_primary);
    
    unsigned int size = 0;
    char *data = fs_get_file_data(current_file, &size);
    if (data && size >= 8) {
        if (data[0] == 'N' && data[1] == 'Y' && data[2] == 'K' && data[3] == 'N') {
            unsigned short width = *((unsigned short*)(data + 4));
            unsigned short height = *((unsigned short*)(data + 6));
            unsigned int *pixels = (unsigned int*)(data + 8);
            
            int img_x = phone_x + (phone_w - width) / 2;
            int img_y = phone_y + 60 + (phone_h - 60 - height) / 2;
            
            for (int y = 0; y < height; y++) {
                for (int x = 0; x < width; x++) {
                    if (img_x + x >= phone_x && img_x + x < phone_x + phone_w &&
                        img_y + y >= phone_y + 60 && img_y + y < phone_y + phone_h) {
                        draw_pixel(img_x + x, img_y + y, pixels[y * width + x]);
                    }
                }
            }
        } else {
            draw_string(phone_x + 20, phone_y + 100, "Not an image", text_primary);
        }
    } else {
        draw_string(phone_x + 20, phone_y + 100, "File not found", text_primary);
    }
}

void draw_fps_counter() {
    if (!fps_enabled) return;
    
    char fps_str[16];
    fps_str[0] = 'F'; fps_str[1] = 'P'; fps_str[2] = 'S'; fps_str[3] = ':'; fps_str[4] = ' ';
    int fps_val = current_fps;
    int idx = 5;
    
    int started = 0;
    int digit;
    
    digit = (fps_val / 1000000) % 10;
    if (digit > 0 || started) { fps_str[idx++] = digit + '0'; started = 1; }
    
    digit = (fps_val / 100000) % 10;
    if (digit > 0 || started) { fps_str[idx++] = digit + '0'; started = 1; }
    
    digit = (fps_val / 10000) % 10;
    if (digit > 0 || started) { fps_str[idx++] = digit + '0'; started = 1; }
    
    digit = (fps_val / 1000) % 10;
    if (digit > 0 || started) { fps_str[idx++] = digit + '0'; started = 1; }
    
    digit = (fps_val / 100) % 10;
    if (digit > 0 || started) { fps_str[idx++] = digit + '0'; started = 1; }
    
    digit = (fps_val / 10) % 10;
    if (digit > 0 || started) { fps_str[idx++] = digit + '0'; started = 1; }
    
    fps_str[idx++] = (fps_val % 10) + '0';
    fps_str[idx] = '\0';
    
    extern char sys_font_path[100];
    int fps_w = fb_get_string_width(fps_str, sys_font_path);
    draw_string(phone_x + phone_w - fps_w - 10, phone_y, fps_str, RGB(255, 255, 255));
}

void draw_sys_monitor() {
    extern int sys_monitor_enabled;
    if (!sys_monitor_enabled) return;
    
    int cpu_usage = 8 + (get_hw_time() % 12);
    unsigned int used_kb = mm_get_used_bytes() / 1024;
    
    char monitor_str[48];
    int mi = 0;
    monitor_str[mi++] = 'C'; monitor_str[mi++] = 'P'; monitor_str[mi++] = 'U'; monitor_str[mi++] = ':'; monitor_str[mi++] = ' ';
    if (cpu_usage >= 100) monitor_str[mi++] = (cpu_usage / 100) + '0';
    if (cpu_usage >= 10) monitor_str[mi++] = ((cpu_usage / 10) % 10) + '0';
    monitor_str[mi++] = (cpu_usage % 10) + '0';
    monitor_str[mi++] = '%'; monitor_str[mi++] = ' ';
    monitor_str[mi++] = '|'; monitor_str[mi++] = ' ';
    monitor_str[mi++] = 'R'; monitor_str[mi++] = 'A'; monitor_str[mi++] = 'M'; monitor_str[mi++] = ':'; monitor_str[mi++] = ' ';
    
    if (used_kb >= 1000) {
        monitor_str[mi++] = (used_kb / 1000) + '0';
        monitor_str[mi++] = '.';
        monitor_str[mi++] = ((used_kb % 1000) / 100) + '0';
        monitor_str[mi++] = 'M'; monitor_str[mi++] = 'B';
    } else {
        if (used_kb >= 100) monitor_str[mi++] = (used_kb / 100) + '0';
        if (used_kb >= 10) monitor_str[mi++] = ((used_kb / 10) % 10) + '0';
        monitor_str[mi++] = (used_kb % 10) + '0';
        monitor_str[mi++] = 'K'; monitor_str[mi++] = 'B';
    }
    monitor_str[mi] = '\0';
    
    draw_string(phone_x + 10, phone_y, monitor_str, RGB(255, 255, 255));
}

extern char current_audio_file[256];
extern unsigned int audio_start_time;
extern unsigned int audio_duration;
extern int is_audio_playing;

void render_media_player() {
    draw_rect(phone_x, phone_y + 20, phone_w, phone_h - 20, RGB(30, 30, 30));
    
    // Top bar
    draw_rect(phone_x, phone_y + 20, phone_w, 40, RGB(20, 20, 20));
    draw_string(phone_x + phone_w/2 - 40, phone_y + 36, "Media Player", RGB(255, 255, 255));
    
    // Song title
    int name_x = phone_x + phone_w/2 - 60;
    draw_string(name_x, phone_y + 100, current_audio_file, RGB(255, 255, 255));
    
    // Visualizer (animated bars)
    int elapsed = get_hw_time() - audio_start_time;
    if (!is_audio_playing) elapsed = 0;
    
    int vis_y = phone_y + 250;
    for (int i=0; i<10; i++) {
        int bar_h = 10;
        if (is_audio_playing) {
            bar_h = 10 + ((elapsed * 17 + i * 31) % 80);
        }
        draw_rect(phone_x + 60 + i*20, vis_y - bar_h, 10, bar_h, RGB(0, 200, 255));
    }
    
    // Progress bar
    int bar_w = 200;
    draw_rect(phone_x + 60, phone_y + 300, bar_w, 4, RGB(100, 100, 100));
    int prog_w = 0;
    if (audio_duration > 0) {
        prog_w = (elapsed * bar_w) / audio_duration;
        if (prog_w > bar_w) prog_w = bar_w;
    }
    draw_rect(phone_x + 60, phone_y + 300, prog_w, 4, RGB(0, 200, 255));
    
    // Play/Pause button
    draw_rect(phone_x + phone_w/2 - 25, phone_y + 350, 50, 50, RGB(50, 50, 50));
    if (is_audio_playing) {
        draw_string(phone_x + phone_w/2 - 20, phone_y + 370, "Pause", RGB(255, 255, 255));
    } else {
        draw_string(phone_x + phone_w/2 - 16, phone_y + 370, "Play", RGB(255, 255, 255));
    }
}

void draw_bottom_nav_bar() {
    unsigned int bg_col = is_dark_mode ? RGB(20, 20, 20) : RGB(245, 245, 245);
    unsigned int icon_col = is_dark_mode ? RGB(200, 200, 200) : RGB(60, 60, 60);
    draw_rect(phone_x, phone_y + phone_h - 30, phone_w, 30, bg_col);
    extern void draw_circle(int cx, int cy, int r, int thickness, unsigned int color);
    draw_circle(phone_x + phone_w / 2, phone_y + phone_h - 15, 8, 2, icon_col);
    draw_triangle(phone_x + 88, phone_y + phone_h - 15, phone_x + 100, phone_y + phone_h - 23, phone_x + 100, phone_y + phone_h - 7, icon_col);
    draw_triangle(phone_x + 89, phone_y + phone_h - 15, phone_x + 100, phone_y + phone_h - 22, phone_x + 100, phone_y + phone_h - 8, icon_col);
    draw_triangle(phone_x + 90, phone_y + phone_h - 15, phone_x + 100, phone_y + phone_h - 21, phone_x + 100, phone_y + phone_h - 9, icon_col);
}

void render_screen() {
    
    if (is_picking_app_file) {
        draw_files_screen();
    } else if (current_app_idx != -1) {
        registered_apps[current_app_idx]->draw();
    } else if (current_screen == HOME_SCREEN) {
        draw_home_screen();
    } else if (current_screen == SETTINGS_SCREEN) {
        draw_settings_screen();
    } else if (current_screen == ABOUT_SCREEN) {
        draw_about_screen();
    } else if (current_screen == FILES_SCREEN) {
        draw_files_screen();
    } else if (current_screen == FILE_VIEWER_SCREEN) {
        draw_file_viewer_screen();
    } else if (current_screen == PERSONALIZATION_SCREEN) {
        draw_personalization_screen();
    } else if (current_screen == ICON_BG_SCREEN) {
        draw_icon_bg_screen();
    } else if (current_screen == FONTS_SCREEN) {
        draw_fonts_screen();
    } else if (current_screen == INSTALLER_SCREEN) {
        draw_installer_screen();
    } else if (current_screen == DEV_OPTIONS_SCREEN) {
        draw_dev_options_screen();
    } else if (current_screen == KEYBOARD_TEST_SCREEN) {
        draw_keyboard_test_screen();
    } else if (current_screen == MEDIA_PLAYER_SCREEN) {
        render_media_player();
    } else if (current_screen == APPS_LIST_SCREEN) {
        draw_apps_list_screen();
    } else if (current_screen == LOCK_SCREEN) {
        draw_lock_screen();
    } else if (current_screen == 12) {
        extern void nykon_draw_sprite(const char *filepath, int x, int y, unsigned int color_key);
        draw_rect(phone_x, phone_y + 20, phone_w, phone_h - 20, RGB(0, 0, 0));
        nykon_draw_sprite("sys/img/cat.png", phone_x, phone_y + 20, 0);
    }
    
    // Masking the sides (black outside the phone frame and the phone bezel)
    draw_rect(0, 0, phone_x - 10, 600, 0x000000); // Left
    draw_rect(phone_x + phone_w + 10, 0, 800 - (phone_x + phone_w + 10), 600, 0x000000); // Right
    draw_rect(phone_x - 10, 0, phone_w + 20, phone_y - 20, 0x000000); // Top
    draw_rect(phone_x - 10, phone_y + phone_h + 40, phone_w + 20, 600 - (phone_y + phone_h + 40), 0x000000); // Bottom
    
    // Redraw the physical phone bezel over any overlapping app graphics
    draw_phone_frame();
    
    // Draw status bar ON TOP of everything else
    if (current_screen != LOCK_SCREEN) {
        draw_status_bar();
    }
    
    // Draw Bottom Navigation Bar for all screens except lock screen
    if (current_screen != LOCK_SCREEN) {
        draw_bottom_nav_bar();
    }
    
    draw_fps_counter();
    draw_sys_monitor();
}
// play_boot_animation moved below

void main() {
    boot_epoch = get_hw_time();
    fb_init();
    keyboard_init();
    mouse_init();
    mm_init();
    fs_init();
    
    // Test FAT32 write support
    const char *test_msg = "Hello from Nykon OS Write Support!\n";
    fs_write_file("/TEST.TXT", test_msg, 36);
    
    draw_phone_frame();
    
    play_boot_animation();
    render_screen();
    
    unsigned int cursor_bg[256];
    save_pixels(cursor_x, cursor_y, 16, 16, cursor_bg);
    draw_cursor(cursor_x, cursor_y);
    fb_swap_buffers();

    int last_left_click = 0;

    while(1) {
        int screen_changed = 0;
        frames_drawn++;
        unsigned int current_sec = get_hw_time();
        if (current_sec != last_fps_second) {
            current_fps = frames_drawn;
            frames_drawn = 0;
            last_fps_second = current_sec;
            if (fps_enabled || sys_monitor_enabled) {
                screen_changed = 1;
            }
        }
        
        char ascii;
        while (keyboard_poll(&ascii)) {
            if (ascii != 0) {
                if (current_app_idx != -1 && !is_picking_app_file) {
                    app_key_pressed = ascii;
                    screen_changed = 1;
                } else if (ascii == '\b' && key_idx > 0) {
                    key_idx--;
                    key_buf[key_idx] = '\0';
                    screen_changed = 1;
                } else if (ascii != 0 && ascii != '\b' && key_idx < 31) {
                    key_buf[key_idx++] = ascii;
                    key_buf[key_idx] = '\0';
                    screen_changed = 1;
                }
            }
        }
        
        int dx, dy, left_click;
        int mouse_active = mouse_poll(&dx, &dy, &left_click);
        int old_x = cursor_x;
        int old_y = cursor_y;
        
        app_left_click_state = 0;
        if (mouse_active) {
            app_mouse_down = left_click;
            app_left_click_state = left_click && !last_left_click;
        }

        if (current_app_idx != -1 && !is_picking_app_file) {
            registered_apps[current_app_idx]->update();
            if (mouse_active && (dx != 0 || dy != 0 || app_left_click_state || (!left_click && last_left_click))) {
                screen_changed = 1;
            }
            if (force_screen_redraw) {
                screen_changed = 1;
                force_screen_redraw = 0;
            }
        }
        
        if (mouse_active) {
            restore_pixels(cursor_x, cursor_y, 16, 16, cursor_bg);
            
            if (left_click && !last_left_click) {
                
                if (current_screen == LOCK_SCREEN) {
                    int track_y = phone_y + phone_h - 80;
                    if (cursor_x >= phone_x + 20 && cursor_x <= phone_x + 20 + 48 &&
                        cursor_y >= track_y && cursor_y <= track_y + 48) {
                        is_dragging = 1;
                        drag_start_x = cursor_x;
                    }
                }
                
                // Bottom Nav Bar Hit Tests
                if (cursor_y >= phone_y + phone_h - 30 && cursor_y <= phone_y + phone_h + 14) {
                    // Back button area
                    if (cursor_x >= phone_x + 60 && cursor_x <= phone_x + 130) {
                        if (is_picking_app_file) {
                            if (current_dir[0] == '\0') {
                                is_picking_app_file = 0;
                                current_app_idx = app_picker_caller_idx;
                            } else {
                                int len = 0;
                                while (current_dir[len] != '\0') len++;
                                if (len > 0 && current_dir[len-1] == '/') len--;
                                while (len > 0 && current_dir[len-1] != '/') len--;
                                current_dir[len] = '\0';
                            }
                            screen_changed = 1;
                        } else if (current_app_idx != -1) {
                            system_back_pressed = 1;
                        } else {
                            if (current_screen == SETTINGS_SCREEN || (current_screen == FILES_SCREEN && current_dir[0] == '\0' && !is_picking_wallpaper && !is_picking_sys_font && !is_picking_lock_font && !is_picking_app_title_font && !is_picking_package) || current_screen == MEDIA_PLAYER_SCREEN) {
                                current_screen = HOME_SCREEN;
                            } else if (current_screen == ABOUT_SCREEN || current_screen == DEV_OPTIONS_SCREEN || current_screen == APPS_LIST_SCREEN || current_screen == PERSONALIZATION_SCREEN) {
                                current_screen = SETTINGS_SCREEN;
                            } else if (current_screen == ICON_BG_SCREEN || current_screen == FONTS_SCREEN) {
                                current_screen = PERSONALIZATION_SCREEN;
                            } else if (current_screen == INSTALLER_SCREEN) {
                                current_screen = HOME_SCREEN;
                            } else if (current_screen == 12) {
                                current_screen = ABOUT_SCREEN;
                            } else if (current_screen == KEYBOARD_TEST_SCREEN) {
                                current_screen = DEV_OPTIONS_SCREEN;
                            } else if (current_screen == FILE_VIEWER_SCREEN) {
                                current_screen = FILES_SCREEN;
                            } else if (current_screen == FILES_SCREEN && current_dir[0] == '\0' && (is_picking_wallpaper || is_picking_sys_font || is_picking_lock_font || is_picking_app_title_font || is_picking_package)) {
                                if (is_picking_package) {
                                    is_picking_package = 0;
                                    current_screen = INSTALLER_SCREEN;
                                } else if (is_picking_wallpaper) {
                                    is_picking_wallpaper = 0;
                                    current_screen = PERSONALIZATION_SCREEN;
                                } else {
                                    is_picking_sys_font = 0;
                                    is_picking_lock_font = 0;
                                    is_picking_app_title_font = 0;
                                    current_screen = FONTS_SCREEN;
                                }
                            } else if (current_screen == FILES_SCREEN) {
                                int len = 0;
                                while (current_dir[len] != '\0') len++;
                                if (len > 0 && current_dir[len-1] == '/') len--;
                                while (len > 0 && current_dir[len-1] != '/') len--;
                                current_dir[len] = '\0';
                            }
                            screen_changed = 1;
                        }
                    }
                    // Home button area
                    else if (cursor_x >= phone_x + phone_w/2 - 40 && cursor_x <= phone_x + phone_w/2 + 40) {
                        if (current_screen != HOME_SCREEN && current_screen != LOCK_SCREEN || current_app_idx != -1) {
                            current_screen = HOME_SCREEN;
                            current_app_idx = -1;
                            screen_changed = 1;
                        }
                    }
                } else if (current_app_idx != -1 && !is_picking_app_file) {
                    screen_changed = 1; // Redraw app on click
                } else if (current_screen == HOME_SCREEN && current_app_idx == -1) {
                    // Settings Icon hit test
                    int set_x = phone_x + 20, set_y = phone_y + 50, spacing_x = 76, spacing_y = 76;
                    if (cursor_x >= set_x && cursor_x <= set_x + 48 &&
                        cursor_y >= set_y && cursor_y <= set_y + 48) {
                        current_screen = SETTINGS_SCREEN;
                        play_app_open_animation(set_x, set_y, RGB(180, 180, 180), "sys/img/settings_icon.png", "Settings");
                        screen_changed = 1;
                    }
                    // Files Icon hit test
                    else if (cursor_x >= set_x + spacing_x && cursor_x <= set_x + spacing_x + 48 &&
                        cursor_y >= set_y && cursor_y <= set_y + 48) {
                        current_screen = FILES_SCREEN;
                        play_app_open_animation(set_x + spacing_x, set_y, RGB(100, 100, 255), "sys/img/files_icon.png", "Files");
                        screen_changed = 1;
                    }
                    // Installer Icon hit test
                    else if (cursor_x >= set_x + spacing_x * 2 && cursor_x <= set_x + spacing_x * 2 + 48 &&
                        cursor_y >= set_y && cursor_y <= set_y + 48) {
                        current_screen = INSTALLER_SCREEN;
                        play_app_open_animation(set_x + spacing_x * 2, set_y, RGB(50, 180, 100), "sys/img/installer_icon.png", "Installer");
                        screen_changed = 1;
                    }
                    else {
                        // Third-party App hit test
                        int current_x = set_x + spacing_x * 3;
                        int current_y = set_y;
                        for (int i = 0; i < num_registered_apps; i++) {
                            if (current_x > phone_x + phone_w - 50) {
                                current_x = set_x;
                                current_y += spacing_y;
                            }
                            if (cursor_x >= current_x && cursor_x <= current_x + 48 &&
                                cursor_y >= current_y && cursor_y <= current_y + 48) {
                                current_app_idx = i;
                                registered_apps[i]->init();
                                play_app_open_animation(current_x, current_y, registered_apps[i]->icon_color, registered_apps[i]->icon_path, registered_apps[i]->name);
                                screen_changed = 1;
                                break;
                            }
                            current_x += spacing_x;
                        }
                    }
                } else if (current_screen == SETTINGS_SCREEN && current_app_idx == -1) {
                    // Personalization row (phone_y + 114)
                    if (cursor_x >= phone_x && cursor_x <= phone_x + phone_w &&
                             cursor_y >= phone_y + 106 && cursor_y < phone_y + 165) {
                        current_screen = PERSONALIZATION_SCREEN;
                        screen_changed = 1;
                    }
                    // About row (phone_y + 214)
                    else if (cursor_x >= phone_x && cursor_x <= phone_x + phone_w &&
                             cursor_y >= phone_y + 206 && cursor_y < phone_y + 260) {
                        current_screen = ABOUT_SCREEN;
                        screen_changed = 1;
                    }
                    // Developer Options row (phone_y + 264)
                    else if (cursor_x >= phone_x && cursor_x <= phone_x + phone_w &&
                             cursor_y >= phone_y + 260 && cursor_y < phone_y + 310) {
                        current_screen = DEV_OPTIONS_SCREEN;
                        screen_changed = 1;
                    }
                    // Installed Apps row (phone_y + 314)
                    else if (cursor_x >= phone_x && cursor_x <= phone_x + phone_w &&
                             cursor_y >= phone_y + 310 && cursor_y < phone_y + 365) {
                        current_screen = APPS_LIST_SCREEN;
                        screen_changed = 1;
                    }
                } else if (current_screen == APPS_LIST_SCREEN) {
                    if (cursor_x >= phone_x && cursor_x <= phone_x + 80 &&
                        cursor_y >= phone_y + 20 && cursor_y <= phone_y + 65) {
                        current_screen = SETTINGS_SCREEN;
                        screen_changed = 1;
                    } else {
                        int hit_y = phone_y + 78;
                        for (int i = 0; i < num_registered_apps && i < 8; i++) {
                            if (cursor_x >= phone_x && cursor_x <= phone_x + phone_w &&
                                cursor_y >= hit_y && cursor_y < hit_y + 46) {
                                current_app_idx = i;
                                registered_apps[i]->init();
                                play_app_open_animation(phone_x + 20, hit_y, registered_apps[i]->icon_color, registered_apps[i]->icon_path, registered_apps[i]->name);
                                screen_changed = 1;
                                break;
                            }
                            hit_y += 50;
                        }
                    }
                } else if (current_screen == DEV_OPTIONS_SCREEN) {
                    if (cursor_x >= phone_x && cursor_x <= phone_x + 80 &&
                        cursor_y >= phone_y + 20 && cursor_y <= phone_y + 65) {
                        current_screen = SETTINGS_SCREEN;
                        screen_changed = 1;
                    }
                    // FPS row hit test (phone_y + 114)
                    else if (cursor_x >= phone_x && cursor_x <= phone_x + phone_w &&
                             cursor_y >= phone_y + 106 && cursor_y < phone_y + 160) {
                        fps_enabled = !fps_enabled;
                        screen_changed = 1;
                    }
                    // KB Test row hit test (phone_y + 164)
                    else if (cursor_x >= phone_x && cursor_x <= phone_x + phone_w &&
                             cursor_y >= phone_y + 160 && cursor_y < phone_y + 210) {
                        current_screen = KEYBOARD_TEST_SCREEN;
                        screen_changed = 1;
                    }
                    // System Monitor hit test (phone_y + 214)
                    else if (cursor_x >= phone_x && cursor_x <= phone_x + phone_w &&
                             cursor_y >= phone_y + 210 && cursor_y < phone_y + 265) {
                        extern int sys_monitor_enabled;
                        sys_monitor_enabled = !sys_monitor_enabled;
                        screen_changed = 1;
                    }
                } else if (current_screen == KEYBOARD_TEST_SCREEN) {
                    if (cursor_x >= phone_x && cursor_x <= phone_x + 80 &&
                        cursor_y >= phone_y + 20 && cursor_y <= phone_y + 65) {
                        current_screen = DEV_OPTIONS_SCREEN;
                        screen_changed = 1;
                    }
                } else if (current_screen == PERSONALIZATION_SCREEN) {
                    // Back button in title bar
                    if (cursor_x >= phone_x && cursor_x <= phone_x + 80 &&
                        cursor_y >= phone_y + 20 && cursor_y <= phone_y + 65) {
                        current_screen = SETTINGS_SCREEN;
                        screen_changed = 1;
                    }
                    // Theme row hit test (phone_y + 114)
                    else if (cursor_x >= phone_x && cursor_x <= phone_x + phone_w &&
                             cursor_y >= phone_y + 106 && cursor_y < phone_y + 160) {
                        is_dark_mode = !is_dark_mode;
                        screen_changed = 1;
                    }
                    // Wallpaper row hit test (phone_y + 164)
                    else if (cursor_x >= phone_x && cursor_x <= phone_x + phone_w &&
                             cursor_y >= phone_y + 160 && cursor_y < phone_y + 210) {
                        is_picking_wallpaper = 1;
                        current_screen = FILES_SCREEN;
                        screen_changed = 1;
                    }
                    // Icon Colors row hit test (phone_y + 214)
                    else if (cursor_x >= phone_x && cursor_x <= phone_x + phone_w &&
                             cursor_y >= phone_y + 210 && cursor_y < phone_y + 260) {
                        current_screen = ICON_BG_SCREEN;
                        screen_changed = 1;
                    }
                    // Fonts row hit test (phone_y + 264)
                    else if (cursor_x >= phone_x && cursor_x <= phone_x + phone_w &&
                             cursor_y >= phone_y + 260 && cursor_y < phone_y + 320) {
                        current_screen = FONTS_SCREEN;
                        screen_changed = 1;
                    }
                } else if (current_screen == FONTS_SCREEN) {
                    // Back button in title bar
                    if (cursor_x >= phone_x && cursor_x <= phone_x + 80 &&
                        cursor_y >= phone_y + 20 && cursor_y <= phone_y + 65) {
                        current_screen = PERSONALIZATION_SCREEN;
                        screen_changed = 1;
                    }
                    // App Title Font row (phone_y + 114)
                    else if (cursor_x >= phone_x && cursor_x <= phone_x + phone_w &&
                             cursor_y >= phone_y + 106 && cursor_y < phone_y + 160) {
                        is_picking_app_title_font = 1;
                        my_strncpy(current_dir, "sys/fonts/", 100);
                        current_screen = FILES_SCREEN;
                        screen_changed = 1;
                    }
                    // Lockscreen Clock Font row (phone_y + 164)
                    else if (cursor_x >= phone_x && cursor_x <= phone_x + phone_w &&
                             cursor_y >= phone_y + 160 && cursor_y < phone_y + 210) {
                        is_picking_lock_font = 1;
                        my_strncpy(current_dir, "sys/fonts/", 100);
                        current_screen = FILES_SCREEN;
                        screen_changed = 1;
                    }
                    // System Apps Font row (phone_y + 214)
                    else if (cursor_x >= phone_x && cursor_x <= phone_x + phone_w &&
                             cursor_y >= phone_y + 210 && cursor_y < phone_y + 265) {
                        is_picking_sys_font = 1;
                        my_strncpy(current_dir, "sys/fonts/", 100);
                        current_screen = FILES_SCREEN;
                        screen_changed = 1;
                    }
                } else if (current_screen == ICON_BG_SCREEN) {
                    // Back button in title bar
                    if (cursor_x >= phone_x && cursor_x <= phone_x + 80 &&
                        cursor_y >= phone_y + 20 && cursor_y <= phone_y + 65) {
                        current_screen = PERSONALIZATION_SCREEN;
                        screen_changed = 1;
                    } else {
                        int item_y = phone_y + 78;
                        for (int t = 0; t < (int)NUM_ICON_THEMES; t++) {
                            if (cursor_x >= phone_x && cursor_x <= phone_x + phone_w &&
                                cursor_y >= item_y && cursor_y < item_y + 46) {
                                current_icon_theme = t;
                                screen_changed = 1;
                                break;
                            }
                            item_y += 48;
                        }
                    }
                } else if (current_screen == INSTALLER_SCREEN) {
                    // BROWSE FILE MANAGER button: phone_x + 12, phone_y + 78, phone_w - 24, 44
                    if (cursor_x >= phone_x + 12 && cursor_x <= phone_x + phone_w - 12 &&
                        cursor_y >= phone_y + 76 && cursor_y <= phone_y + 124) {
                        is_picking_package = 1;
                        current_dir[0] = '\0';
                        current_screen = FILES_SCREEN;
                        screen_changed = 1;
                    }
                    else if (has_inspected_pkg) {
                        int card_y = phone_y + 130;
                        // INSTALL APP Button: phone_x + 28, card_y + 120, phone_w - 56, 42
                        if (cursor_x >= phone_x + 24 && cursor_x <= phone_x + phone_w - 24 &&
                            cursor_y >= card_y + 116 && cursor_y <= card_y + 168) {
                            install_inspected_package();
                            screen_changed = 1;
                        }
                        // Clear Selection button: phone_x + 28, card_y + 196, phone_w - 56, 34
                        else if (cursor_x >= phone_x + 24 && cursor_x <= phone_x + phone_w - 24 &&
                            cursor_y >= card_y + 192 && cursor_y <= card_y + 236) {
                            has_inspected_pkg = 0;
                            installer_status_msg[0] = '\0';
                            screen_changed = 1;
                        }
                    }
                } else if (current_screen == ABOUT_SCREEN) {
                    if (cursor_x >= phone_x && cursor_x <= phone_x + 80 &&
                        cursor_y >= phone_y + 20 && cursor_y <= phone_y + 65) {
                        current_screen = SETTINGS_SCREEN;
                        screen_changed = 1;
                    }
                    else if (cursor_x >= phone_x && cursor_x <= phone_x + phone_w &&
                        cursor_y >= phone_y + 78 && cursor_y < phone_y + 140) {
                        static int about_clicks = 0;
                        about_clicks++;
                        if (about_clicks >= 5) {
                            about_clicks = 0;
                            current_screen = 12; // CAT_EASTER_EGG_SCREEN
                            screen_changed = 1;
                        }
                    }
                } else if (current_screen == FILES_SCREEN) {
                    // Back / Up Button hit test
                    if (cursor_x >= phone_x && cursor_x <= phone_x + 80 &&
                        cursor_y >= phone_y + 20 && cursor_y <= phone_y + 65) {
                        if (current_dir[0] == '\0') {
                            if (is_picking_app_file) {
                                is_picking_app_file = 0;
                                current_app_idx = app_picker_caller_idx;
                                screen_changed = 1;
                            } else if (is_picking_package) {
                                is_picking_package = 0;
                                current_screen = INSTALLER_SCREEN;
                                screen_changed = 1;
                            } else if (is_picking_wallpaper) {
                                is_picking_wallpaper = 0;
                                current_screen = PERSONALIZATION_SCREEN;
                                screen_changed = 1;
                            } else if (is_picking_sys_font || is_picking_lock_font || is_picking_app_title_font) {
                                is_picking_sys_font = 0;
                                is_picking_lock_font = 0;
                                is_picking_app_title_font = 0;
                                current_screen = FONTS_SCREEN;
                                screen_changed = 1;
                            }
                        } else {
                            int len = 0;
                            while (current_dir[len] != '\0') len++;
                            if (len > 0 && current_dir[len-1] == '/') len--;
                            while (len > 0 && current_dir[len-1] != '/') len--;
                            current_dir[len] = '\0';
                            screen_changed = 1;
                        }
                    }
                    
                    int in_sys = 0;
                    if (current_dir[0] == 's' && current_dir[1] == 'y' && current_dir[2] == 's' && current_dir[3] == '/') {
                        in_sys = 1;
                    }
                    int clicked_button = 0;
                    if (!in_sys && !is_picking_app_file && !is_picking_package && !is_picking_wallpaper && !is_picking_sys_font && !is_picking_lock_font && !is_picking_app_title_font) {
                        // +Fldr button (fil_x - 8 - 48 to fil_x - 8 = phone_x + phone_w - 118 to phone_x + phone_w - 70)
                        if (cursor_x >= phone_x + phone_w - 125 && cursor_x <= phone_x + phone_w - 65 &&
                            cursor_y >= phone_y + 20 && cursor_y <= phone_y + 65) {
                            create_item(1);
                            screen_changed = 1;
                            clicked_button = 1;
                        }
                        // +Fil button (phone_x + phone_w - 62 to phone_x + phone_w - 18)
                        else if (cursor_x >= phone_x + phone_w - 64 && cursor_x <= phone_x + phone_w &&
                                 cursor_y >= phone_y + 20 && cursor_y <= phone_y + 65) {
                            create_item(0);
                            screen_changed = 1;
                            clicked_button = 1;
                        }
                    }
                    
                    // List item hit test (item_y = phone_y + 78, 48px spacing)
                    if (!clicked_button && cursor_x >= phone_x && cursor_x <= phone_x + phone_w &&
                             cursor_y >= phone_y + 78 && cursor_y <= phone_y + 78 + current_file_count * 48) {
                        int idx = (cursor_y - (phone_y + 78)) / 48;
                        if (idx >= 0 && idx < current_file_count) {
                            if (current_files[idx].is_dir) {
                                int d_len = 0;
                                while (current_dir[d_len] != '\0') d_len++;
                                if (d_len > 0 && current_dir[d_len - 1] != '/') {
                                    current_dir[d_len++] = '/';
                                }
                                int n_len = 0;
                                while (current_files[idx].name[n_len] != '\0' && d_len + n_len < 98) {
                                    current_dir[d_len + n_len] = current_files[idx].name[n_len];
                                    n_len++;
                                }
                                current_dir[d_len + n_len] = '\0';
                                screen_changed = 1;
                            } else {
                                char *name = current_files[idx].name;
                                int n_len = 0;
                                while (name[n_len] != '\0') n_len++;
                                int is_img = 0;
                                int is_audio = 0;
                                int is_font = 0;
                                int is_pkg = 0;
                                int is_gb = 0;
                                if (n_len >= 3 && (name[n_len-3] == '.' && (name[n_len-2] == 'g' || name[n_len-2] == 'G') && (name[n_len-1] == 'b' || name[n_len-1] == 'B'))) {
                                    is_gb = 1;
                                }
                                if (n_len >= 4 && (name[n_len-4] == '.' && (name[n_len-3] == 'g' || name[n_len-3] == 'G') && (name[n_len-2] == 'b' || name[n_len-2] == 'B') && (name[n_len-1] == 'c' || name[n_len-1] == 'C'))) {
                                    is_gb = 1;
                                }
                                if (n_len >= 4) {
                                    if ((name[n_len-4] == '.' && name[n_len-3] == 'j' && name[n_len-2] == 'p' && name[n_len-1] == 'g') ||
                                        (name[n_len-4] == '.' && name[n_len-3] == 'p' && name[n_len-2] == 'n' && name[n_len-1] == 'g')) {
                                        is_img = 1;
                                    }
                                    if ((name[n_len-4] == '.' && name[n_len-3] == 'w' && name[n_len-2] == 'a' && name[n_len-1] == 'v') ||
                                        (name[n_len-4] == '.' && name[n_len-3] == 'm' && name[n_len-2] == 'p' && name[n_len-1] == '3')) {
                                        is_audio = 1;
                                    }
                                    if ((name[n_len-4] == '.' && name[n_len-3] == 't' && name[n_len-2] == 't' && name[n_len-1] == 'f') ||
                                        (name[n_len-4] == '.' && name[n_len-3] == 'n' && name[n_len-2] == 'f' && name[n_len-1] == 'n')) {
                                        is_font = 1;
                                    }
                                }
                                if (n_len >= 5) {
                                    if (name[n_len-5] == '.' && name[n_len-4] == 'j' && name[n_len-3] == 'p' && name[n_len-2] == 'e' && name[n_len-1] == 'g') {
                                        is_img = 1;
                                    }
                                }
                                if (n_len >= 6) {
                                    if (name[n_len-6] == '.' && name[n_len-5] == 'n' && name[n_len-4] == 'k' && name[n_len-3] == 'p' && name[n_len-2] == 'k' && name[n_len-1] == 'g') {
                                        is_pkg = 1;
                                    }
                                }
                                
                                int d_len = 0;
                                while (current_dir[d_len] != '\0') {
                                    current_file[d_len] = current_dir[d_len];
                                    d_len++;
                                }
                                if (d_len > 0 && current_file[d_len - 1] != '/') {
                                    current_file[d_len++] = '/';
                                }
                                int n2 = 0;
                                while (name[n2] != '\0' && d_len + n2 < 98) {
                                    current_file[d_len + n2] = name[n2];
                                    n2++;
                                }
                                current_file[d_len + n2] = '\0';
                                
                                if (is_picking_app_file) {
                                    int match = 1;
                                    if (app_picker_filter[0] != '\0') {
                                        int flen = 0; while (app_picker_filter[flen]) flen++;
                                        if (n_len >= flen) {
                                            match = 1;
                                            for (int k = 0; k < flen; k++) {
                                                char c1 = name[n_len - flen + k];
                                                char c2 = app_picker_filter[k];
                                                if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
                                                if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
                                                if (c1 != c2) { match = 0; break; }
                                            }
                                        } else {
                                            match = 0;
                                        }
                                    }
                                    if (match) {
                                        int pi = 0;
                                        while (current_file[pi] && pi < 127) {
                                            app_picked_file_path[pi] = current_file[pi];
                                            pi++;
                                        }
                                        app_picked_file_path[pi] = '\0';
                                        app_file_picked_ready = 1;
                                        is_picking_app_file = 0;
                                        current_app_idx = app_picker_caller_idx;
                                        screen_changed = 1;
                                    }
                                } else if (is_pkg) {
                                    inspect_package_file(current_file);
                                    is_picking_package = 0;
                                    current_screen = INSTALLER_SCREEN;
                                    screen_changed = 1;
                                } else if (is_gb) {
                                    for (int a = 0; a < num_registered_apps; a++) {
                                        if (registered_apps[a]->name[0] == 'P' && registered_apps[a]->name[1] == 'e' && registered_apps[a]->name[2] == 'a') {
                                            current_app_idx = a;
                                            registered_apps[a]->init();
                                            screen_changed = 1;
                                            break;
                                        }
                                    }
                                } else if (is_audio) {
                                    nykon_audio_play(current_file);
                                    screen_changed = 1;
                                } else if (is_picking_wallpaper) {
                                    int w_len = 0;
                                    while (current_file[w_len] != '\0') {
                                        current_wallpaper[w_len] = current_file[w_len];
                                        w_len++;
                                    }
                                    current_wallpaper[w_len] = '\0';
                                    is_picking_wallpaper = 0;
                                    current_screen = PERSONALIZATION_SCREEN;
                                    screen_changed = 1;
                                } else if (is_picking_app_title_font) {
                                    char cleaned[100];
                                    int len = 0;
                                    while (current_file[len] != '\0') {
                                        cleaned[len] = current_file[len];
                                        len++;
                                    }
                                    if (len >= 4 && cleaned[len-4] == '.') {
                                        len -= 4; // remove .ttf or .nfn
                                    }
                                    if (len >= 6 && cleaned[len-6] == '_' && cleaned[len-5] == 'l' && cleaned[len-4] == 'a' && cleaned[len-3] == 'r' && cleaned[len-2] == 'g' && cleaned[len-1] == 'e') {
                                        len -= 6; // remove _large
                                    }
                                    cleaned[len++] = '.';
                                    cleaned[len++] = 'n';
                                    cleaned[len++] = 'f';
                                    cleaned[len++] = 'n';
                                    cleaned[len] = '\0';
                                    
                                    my_strncpy(app_title_font, cleaned, 100);
                                    is_picking_app_title_font = 0;
                                    current_screen = FONTS_SCREEN;
                                    screen_changed = 1;
                                } else if (is_picking_sys_font) {
                                    char cleaned[100];
                                    int len = 0;
                                    while (current_file[len] != '\0') {
                                        cleaned[len] = current_file[len];
                                        len++;
                                    }
                                    if (len >= 4 && cleaned[len-4] == '.') {
                                        len -= 4; // remove .ttf or .nfn
                                    }
                                    if (len >= 6 && cleaned[len-6] == '_' && cleaned[len-5] == 'l' && cleaned[len-4] == 'a' && cleaned[len-3] == 'r' && cleaned[len-2] == 'g' && cleaned[len-1] == 'e') {
                                        len -= 6; // remove _large
                                    }
                                    cleaned[len++] = '.';
                                    cleaned[len++] = 'n';
                                    cleaned[len++] = 'f';
                                    cleaned[len++] = 'n';
                                    cleaned[len] = '\0';
                                    
                                    fb_set_font(cleaned);
                                    is_picking_sys_font = 0;
                                    current_screen = FONTS_SCREEN;
                                    screen_changed = 1;
                                } else if (is_picking_lock_font) {
                                    int len = 0;
                                    while (current_file[len] != '\0') {
                                        current_lock_font[len] = current_file[len];
                                        len++;
                                    }
                                    if (len >= 4 && current_lock_font[len-4] == '.') {
                                        len -= 4; // remove .ttf or .nfn
                                    }
                                    if (len >= 6 && current_lock_font[len-6] == '_' && current_lock_font[len-5] == 'l' && current_lock_font[len-4] == 'a' && current_lock_font[len-3] == 'r' && current_lock_font[len-2] == 'g' && current_lock_font[len-1] == 'e') {
                                        len -= 6; // remove _large
                                    }
                                    current_lock_font[len++] = '_';
                                    current_lock_font[len++] = 'l';
                                    current_lock_font[len++] = 'a';
                                    current_lock_font[len++] = 'r';
                                    current_lock_font[len++] = 'g';
                                    current_lock_font[len++] = 'e';
                                    current_lock_font[len++] = '.';
                                    current_lock_font[len++] = 'n';
                                    current_lock_font[len++] = 'f';
                                    current_lock_font[len++] = 'n';
                                    current_lock_font[len] = '\0';
                                    
                                    is_picking_lock_font = 0;
                                    current_screen = FONTS_SCREEN;
                                    screen_changed = 1;
                                } else {
                                    current_screen = FILE_VIEWER_SCREEN;
                                    screen_changed = 1;
                                }
                            }
                        }
                    }
                    if (cursor_x >= phone_x && cursor_x <= phone_x + 80 &&
                        cursor_y >= phone_y + 20 && cursor_y <= phone_y + 60) {
                        current_screen = FILES_SCREEN;
                        screen_changed = 1;
                    }
                } else if (current_screen == MEDIA_PLAYER_SCREEN) {
                    if (cursor_x >= phone_x && cursor_x <= phone_x + 80 &&
                        cursor_y >= phone_y + 20 && cursor_y <= phone_y + 60) {
                        current_screen = FILES_SCREEN;
                        is_audio_playing = 0;
                        screen_changed = 1;
                    }
                    else if (cursor_x >= phone_x + phone_w/2 - 25 && cursor_x <= phone_x + phone_w/2 + 25 &&
                             cursor_y >= phone_y + 350 && cursor_y <= phone_y + 400) {
                        is_audio_playing = !is_audio_playing;
                        screen_changed = 1;
                    }
                }
                // screen_changed is handled at the end of the loop
            } else if (!left_click && last_left_click) {
                // Mouse released
                if (is_dragging) {
                    is_dragging = 0;
                    
                    if (current_screen == LOCK_SCREEN) {
                        int track_y = phone_y + phone_h - 80;
                        if (cursor_x > phone_x + phone_w - 20 - 48) {
                            current_screen = HOME_SCREEN;
                            play_unlock_animation();
                            screen_changed = 1;
                        } else {
                            render_screen();
                            screen_changed = 1;
                        }
                    }
                }
            } else if (is_dragging) {
                if (current_screen == LOCK_SCREEN) {
                    if (dx != 0) { // Only update if it actually moved horizontally
                        screen_changed = 1;
                    }
                }
            }
            
            cursor_x += dx;
            cursor_y += dy;
            
            if (cursor_x < 0) cursor_x = 0;
            if (cursor_x >= 800) cursor_x = 799;
            if (cursor_y < 0) cursor_y = 0;
            if (cursor_y >= 600) cursor_y = 599;
            
            last_left_click = left_click;
        }
        
        if (screen_changed) {
            render_screen();
            save_pixels(cursor_x, cursor_y, 16, 16, cursor_bg);
            draw_cursor(cursor_x, cursor_y);
            fb_swap_buffers();
        } else if (mouse_active && (dx != 0 || dy != 0)) {
            save_pixels(cursor_x, cursor_y, 16, 16, cursor_bg);
            draw_cursor(cursor_x, cursor_y);
            fb_swap_rect(old_x, old_y, 16, 16);
            fb_swap_rect(cursor_x, cursor_y, 16, 16);
        }
        
        if (current_screen == MEDIA_PLAYER_SCREEN && is_audio_playing) {
            static int vis_frames = 0;
            vis_frames++;
            if (vis_frames > 500) { // arbitrary loop delay
                vis_frames = 0;
                
                restore_pixels(cursor_x, cursor_y, 16, 16, cursor_bg);
                render_media_player();
                save_pixels(cursor_x, cursor_y, 16, 16, cursor_bg);
                draw_cursor(cursor_x, cursor_y);
                
                fb_swap_rect(phone_x, phone_y + 100, phone_w, phone_h - 100);
            }
        }
    }
}
