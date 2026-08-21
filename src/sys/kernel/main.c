#include "fb.h"
#include "mouse.h"
#include "keyboard.h"
#include "fs.h"
#include "../nykon_api.h"

extern NykonApp* registered_apps[];
extern int num_registered_apps;

int current_app_idx = -1; // -1 means no third-party app is running

// API variables exported for api.c
int app_left_click_state = 0;
int system_back_pressed = 0;
char app_key_pressed = 0;

#define RGB(r, g, b) (0xFF000000 | ((b) << 16) | ((g) << 8) | (r))

void draw_icon_generic(int x, int y, unsigned int color) {
    draw_rounded_rect(x, y, 48, 48, 12, color);
}

#ifdef LINUX_BUILD
#include <time.h>
unsigned int get_hw_time() {
    return (unsigned int)time(NULL);
}
#else
volatile unsigned int * const RTC0_DR = (unsigned int *)0x101E8000;
unsigned int get_hw_time() {
    return *RTC0_DR;
}
#endif

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
char current_wallpaper[100] = "sys/img/win_7_fish.png";
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
    draw_rect(phone_x, phone_y, phone_w, 20, RGB(20, 20, 20));
    char time_str[6];
    get_current_time(time_str);
    extern char sys_font_path[100];
    int time_w = fb_get_string_width(time_str, sys_font_path);
    draw_string(phone_x + (phone_w - time_w) / 2, phone_y, time_str, RGB(255, 255, 255));
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
        draw_rect(phone_x, phone_y + 20, phone_w, phone_h - 20, RGB(15, 20, 40));
    }
}

void draw_home_screen() {
    draw_rect(0, 0, 800, 600, RGB(10, 10, 10)); // Clear entire desktop to remove app artifacts
    draw_wallpaper_or_bg();
    
    int start_x = phone_x + 20, start_y = phone_y + 50, spacing_x = 76, spacing_y = 76;
    
    // Setting
    extern void nykon_draw_sprite_rounded(const char *filepath, int x, int y, int r, unsigned int color_key);
    draw_icon_generic(start_x, start_y, RGB(180, 180, 180));
    nykon_draw_sprite_rounded("sys/img/settings_icon.png", start_x, start_y, 12, 0xFFFF00FF);
    const char *label_font = "sys/fonts/Lato-Black.nfn";
    int setting_w = fb_get_string_width("Setting", label_font);
    draw_string_ttf(start_x + 24 - (setting_w / 2), start_y + 52, "Setting", label_font, RGB(255, 255, 255));
    
    // Files
    draw_icon_generic(start_x + spacing_x, start_y, RGB(100, 100, 255));
    nykon_draw_sprite_rounded("sys/img/files_icon.png", start_x + spacing_x, start_y, 12, 0xFFFF00FF);
    int files_w = fb_get_string_width("Files", label_font);
    draw_string_ttf(start_x + spacing_x + 24 - (files_w / 2), start_y + 52, "Files", label_font, RGB(255, 255, 255));

    // Third-party Apps
    int current_x = start_x + spacing_x * 2;
    int current_y = start_y;
    for (int i = 0; i < num_registered_apps; i++) {
        draw_icon_generic(current_x, current_y, registered_apps[i]->icon_color);
        if (registered_apps[i]->icon_path) {
            extern char *fs_get_file_data(const char *filepath, unsigned int *size_out);
            unsigned int size = 0;
            char *data = fs_get_file_data(registered_apps[i]->icon_path, &size);
            if (data && size >= 8 && data[0] == 'N' && data[1] == 'Y' && data[2] == 'K' && data[3] == 'N') {
                unsigned short w = *((unsigned short *)(data + 4));
                unsigned short h = *((unsigned short *)(data + 6));
                nykon_draw_sprite_rounded(registered_apps[i]->icon_path, current_x + (48 - w)/2, current_y + (48 - h)/2, 12, 0xFFFF00FF);
            } else {
                nykon_draw_sprite_rounded(registered_apps[i]->icon_path, current_x, current_y, 12, 0xFFFF00FF);
            }
        }
        
        int app_name_w = fb_get_string_width(registered_apps[i]->name, label_font);
        draw_string_ttf(current_x + 24 - (app_name_w / 2), current_y + 52, registered_apps[i]->name, label_font, RGB(255, 255, 255));
        
        current_x += spacing_x;
        if (current_x > phone_x + phone_w - 50) {
            current_x = start_x;
            current_y += spacing_y;
        }
    }
}

void draw_lock_slider();

char current_lock_font[100] = "sys/fonts/Roboto-Regular_large.nfn";

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
    draw_string_ttf(phone_x + 80, track_y + 14, prompt, "sys/fonts/Lato-Black.nfn", RGB(180, 180, 180));
    
    draw_rounded_rect(slider_x, track_y, 48, 48, 24, RGB(200, 200, 200));
    extern void nykon_draw_sprite(const char *filepath, int x, int y, unsigned int color_key);
    nykon_draw_sprite("sys/img/unlock_sprite.png", slider_x, track_y, 0xFFFF00FF);
}

void update_lock_slider() {
    int track_y = phone_y + phone_h - 80;
    draw_lock_slider();
    fb_swap_rect(phone_x + 20, track_y, phone_w - 40, 48);
}

void play_boot_animation() {
    int bar_max_w = 200;
    int bar_h = 8;
    int bar_x = phone_x + (phone_w - bar_max_w) / 2;
    int bar_y = phone_y + phone_h - 100;

    for (int frame = 0; frame <= bar_max_w; frame += 2) {
        draw_rect(phone_x, phone_y, phone_w, phone_h, RGB(0,0,0));
        nykon_draw_sprite("sys/img/logo_sprite.png", phone_x + phone_w/2 - 75, phone_y + phone_h/2 - 75, 0xFFFF00FF);
        
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

void draw_settings_screen() {
    unsigned int bg_color = is_dark_mode ? RGB(10, 10, 10) : RGB(255, 255, 255);
    unsigned int title_bg = is_dark_mode ? RGB(30, 30, 30) : RGB(240, 240, 240);
    unsigned int text_primary = is_dark_mode ? RGB(255, 255, 255) : RGB(0, 0, 0);
    unsigned int text_secondary = is_dark_mode ? RGB(150, 150, 150) : RGB(100, 100, 100);
    unsigned int border_color = is_dark_mode ? RGB(50, 50, 50) : RGB(220, 220, 220);

    draw_rect(phone_x, phone_y + 20, phone_w, phone_h - 20, bg_color);
    
    // Title Bar
    draw_rect(phone_x, phone_y + 20, phone_w, 40, title_bg);
    draw_string(phone_x + phone_w/2 - 32, phone_y + 36, "Settings", text_primary);
    
    int list_y = phone_y + 80;
    
    // General Section
    draw_string(phone_x + 20, list_y, "General", text_secondary);
    list_y += 24;
    
    // Personalization row
    draw_string(phone_x + 20, list_y + 16, "Personalization", text_primary);
    draw_string(phone_x + phone_w - 20, list_y + 16, ">", text_secondary);
    draw_rect(phone_x + 20, list_y + 40, phone_w - 20, 1, border_color);
    list_y += 40;
    
    // System Section
    list_y += 10; // Extra spacing
    draw_string(phone_x + 20, list_y, "System", text_secondary);
    list_y += 24;
    
    // About row
    draw_string(phone_x + 20, list_y + 16, "About Nykon OS", text_primary);
    draw_string(phone_x + phone_w - 20, list_y + 16, ">", text_secondary);
    draw_rect(phone_x + 20, list_y + 40, phone_w - 20, 1, border_color);
    list_y += 40;
    
    // Developer Options row
    draw_string(phone_x + 20, list_y + 16, "Developer Options", text_primary);
    draw_string(phone_x + phone_w - 20, list_y + 16, ">", text_secondary);
    draw_rect(phone_x + 20, list_y + 40, phone_w - 20, 1, border_color);
    list_y += 40;
    
    // Installed Apps row
    draw_string(phone_x + 20, list_y + 16, "Installed Apps", text_primary);
    draw_string(phone_x + phone_w - 20, list_y + 16, ">", text_secondary);
    draw_rect(phone_x + 20, list_y + 40, phone_w - 20, 1, border_color);
    list_y += 40;
}

void draw_apps_list_screen() {
    unsigned int bg_color = is_dark_mode ? RGB(10, 10, 10) : RGB(255, 255, 255);
    unsigned int title_bg = is_dark_mode ? RGB(30, 30, 30) : RGB(240, 240, 240);
    unsigned int text_primary = is_dark_mode ? RGB(255, 255, 255) : RGB(0, 0, 0);
    unsigned int text_secondary = is_dark_mode ? RGB(150, 150, 150) : RGB(100, 100, 100);
    unsigned int border_color = is_dark_mode ? RGB(50, 50, 50) : RGB(220, 220, 220);

    draw_rect(phone_x, phone_y + 20, phone_w, phone_h - 20, bg_color);
    
    // Title Bar
    draw_rect(phone_x, phone_y + 20, phone_w, 40, title_bg);
    draw_string(phone_x + phone_w/2 - 20, phone_y + 36, "Apps", text_primary);
    
    int list_y = phone_y + 80;
    
    draw_string(phone_x + 20, list_y, "Installed Apps", text_secondary);
    list_y += 24;
    
    for (int i = 0; i < num_registered_apps; i++) {
        draw_string(phone_x + 20, list_y + 16, registered_apps[i]->name, text_primary);
        draw_rect(phone_x + 20, list_y + 40, phone_w - 20, 1, border_color);
        list_y += 40;
    }
}

void draw_dev_options_screen() {
    unsigned int bg_color = is_dark_mode ? RGB(10, 10, 10) : RGB(255, 255, 255);
    unsigned int title_bg = is_dark_mode ? RGB(30, 30, 30) : RGB(240, 240, 240);
    unsigned int text_primary = is_dark_mode ? RGB(255, 255, 255) : RGB(0, 0, 0);
    unsigned int text_secondary = is_dark_mode ? RGB(150, 150, 150) : RGB(100, 100, 100);
    unsigned int border_color = is_dark_mode ? RGB(50, 50, 50) : RGB(220, 220, 220);

    draw_rect(phone_x, phone_y + 20, phone_w, phone_h - 20, bg_color);
    
    // Title Bar
    draw_rect(phone_x, phone_y + 20, phone_w, 40, title_bg);
    draw_string(phone_x + phone_w/2 - 40, phone_y + 36, "Developer", text_primary);
    
    int list_y = phone_y + 80;
    
    // FPS Toggle Row
    draw_string(phone_x + 20, list_y + 16, "Show FPS Counter", text_primary);
    draw_string(phone_x + phone_w - 50, list_y + 16, fps_enabled ? "ON" : "OFF", fps_enabled ? RGB(0, 200, 0) : text_secondary);
    draw_rect(phone_x + 20, list_y + 40, phone_w - 20, 1, border_color);
    list_y += 40;
    
    // Keyboard Test Row
    draw_string(phone_x + 20, list_y + 16, "Keyboard Test", text_primary);
    draw_string(phone_x + phone_w - 20, list_y + 16, ">", text_secondary);
    draw_rect(phone_x + 20, list_y + 40, phone_w - 20, 1, border_color);
    list_y += 40;
    
    // System Monitor Toggle Row
    extern int sys_monitor_enabled;
    draw_string(phone_x + 20, list_y + 16, "Show System Monitor", text_primary);
    draw_string(phone_x + phone_w - 50, list_y + 16, sys_monitor_enabled ? "ON" : "OFF", sys_monitor_enabled ? RGB(0, 200, 0) : text_secondary);
    draw_rect(phone_x + 20, list_y + 40, phone_w - 20, 1, border_color);
}

void draw_keyboard_test_screen() {
    unsigned int bg_color = is_dark_mode ? RGB(10, 10, 10) : RGB(255, 255, 255);
    unsigned int title_bg = is_dark_mode ? RGB(30, 30, 30) : RGB(240, 240, 240);
    unsigned int text_primary = is_dark_mode ? RGB(255, 255, 255) : RGB(0, 0, 0);

    draw_rect(phone_x, phone_y + 20, phone_w, phone_h - 20, bg_color);
    
    // Title Bar
    draw_rect(phone_x, phone_y + 20, phone_w, 40, title_bg);
    draw_string(phone_x + phone_w/2 - 32, phone_y + 36, "KB Test", text_primary);
    
    // Typewriter Area
    draw_string(phone_x + 20, phone_y + 80, "Type on your keyboard:", RGB(150, 150, 150));
    
    // Draw typed text scaled up
    if (key_buf[0] != '\0') {
        draw_string_scaled(phone_x + 20, phone_y + 120, key_buf, RGB(0, 200, 0), 2);
    }
}

void draw_personalization_screen() {
    unsigned int bg_color = is_dark_mode ? RGB(10, 10, 10) : RGB(255, 255, 255);
    unsigned int title_bg = is_dark_mode ? RGB(30, 30, 30) : RGB(240, 240, 240);
    unsigned int text_primary = is_dark_mode ? RGB(255, 255, 255) : RGB(0, 0, 0);
    unsigned int text_secondary = is_dark_mode ? RGB(150, 150, 150) : RGB(100, 100, 100);
    unsigned int border_color = is_dark_mode ? RGB(50, 50, 50) : RGB(220, 220, 220);

    draw_rect(phone_x, phone_y + 20, phone_w, phone_h - 20, bg_color);
    
    // Title Bar
    draw_rect(phone_x, phone_y + 20, phone_w, 40, title_bg);
    draw_string(phone_x + phone_w/2 - 60, phone_y + 36, "Personalization", text_primary);
    
    int list_y = phone_y + 80;
    
    draw_string(phone_x + 20, list_y, "Appearance", text_secondary);
    list_y += 24;
    
    draw_string(phone_x + 20, list_y + 16, is_dark_mode ? "Theme: Dark" : "Theme: Light", text_primary);
    draw_string(phone_x + phone_w - 20, list_y + 16, ">", text_secondary);
    draw_rect(phone_x + 20, list_y + 40, phone_w - 20, 1, border_color);
    list_y += 40;
    
    draw_string(phone_x + 20, list_y + 16, "Wallpaper", text_primary);
    draw_string(phone_x + phone_w - 20, list_y + 16, ">", text_secondary);
    draw_rect(phone_x + 20, list_y + 40, phone_w - 20, 1, border_color);
    list_y += 40;
    
    // Font row
    extern char sys_font_path[100];
    draw_string(phone_x + 20, list_y + 16, "System Font", text_primary);
    draw_string(phone_x + phone_w - 20, list_y + 16, ">", text_secondary);
    draw_rect(phone_x + 20, list_y + 40, phone_w - 20, 1, border_color);
    list_y += 40;

    // Lock Font row
    draw_string(phone_x + 20, list_y + 16, "Lock Font", text_primary);
    draw_string(phone_x + phone_w - 20, list_y + 16, ">", text_secondary);
    draw_rect(phone_x + 20, list_y + 40, phone_w - 20, 1, border_color);
}

void draw_about_screen() {
    unsigned int bg_color = is_dark_mode ? RGB(10, 10, 10) : RGB(255, 255, 255);
    unsigned int title_bg = is_dark_mode ? RGB(30, 30, 30) : RGB(240, 240, 240);
    unsigned int text_primary = is_dark_mode ? RGB(255, 255, 255) : RGB(0, 0, 0);
    unsigned int text_secondary = is_dark_mode ? RGB(150, 150, 150) : RGB(100, 100, 100);

    draw_rect(phone_x, phone_y + 20, phone_w, phone_h - 20, bg_color);
    
    // Title Bar
    draw_rect(phone_x, phone_y + 20, phone_w, 40, title_bg);
    draw_string(phone_x + phone_w/2 - 20, phone_y + 36, "About", text_primary);
    
    // Statistics
    int list_y = phone_y + 80;
    
    draw_string(phone_x + 20, list_y, "OS Name", text_secondary);
    draw_string(phone_x + 20, list_y + 16, "Nykon OS v1.0", text_primary);
    list_y += 40;
    
    draw_string(phone_x + 20, list_y, "Device", text_secondary);
    draw_string(phone_x + 20, list_y + 16, "ARM Versatile PB", text_primary);
    list_y += 40;
    
    draw_string(phone_x + 20, list_y, "Processor", text_secondary);
    draw_string(phone_x + 20, list_y + 16, "ARM926EJ-S", text_primary);
    list_y += 40;
    
    draw_string(phone_x + 20, list_y, "Memory", text_secondary);
    draw_string(phone_x + 20, list_y + 16, "512 MB RAM", text_primary);
    list_y += 40;
    
    draw_string(phone_x + 20, list_y, "Graphics", text_secondary);
    draw_string(phone_x + 20, list_y + 16, "PL110 800x600", text_primary);
}

void draw_files_screen() {
    unsigned int bg_color = is_dark_mode ? RGB(10, 10, 10) : RGB(255, 255, 255);
    unsigned int title_bg = is_dark_mode ? RGB(30, 30, 30) : RGB(240, 240, 240);
    unsigned int text_primary = is_dark_mode ? RGB(255, 255, 255) : RGB(0, 0, 0);
    unsigned int text_secondary = is_dark_mode ? RGB(150, 150, 150) : RGB(150, 150, 150);
    unsigned int border_color = is_dark_mode ? RGB(50, 50, 50) : RGB(220, 220, 220);
    unsigned int folder_color = is_dark_mode ? RGB(64, 156, 255) : RGB(100, 100, 255);

    draw_rect(phone_x, phone_y + 20, phone_w, phone_h - 20, bg_color);
    
    // Title Bar
    draw_rect(phone_x, phone_y + 20, phone_w, 40, title_bg);
    
    extern char sys_font_path[100];
    if (current_dir[0] == '\0') {
        int title_w = fb_get_string_width("Files", sys_font_path);
        draw_string(phone_x + (phone_w - title_w) / 2, phone_y + 30, "Files", text_primary);
    } else {
        draw_string(phone_x + 10, phone_y + 30, "< Up", RGB(0, 122, 255));
        draw_string(phone_x + 80, phone_y + 30, current_dir, text_primary);
    }
    
    int in_sys = 0;
    if (current_dir[0] == 's' && current_dir[1] == 'y' && current_dir[2] == 's' && current_dir[3] == '/') {
        in_sys = 1;
    }
    
    if (!in_sys) {
        int right_align_w = fb_get_string_width("+Fil", sys_font_path);
        draw_string(phone_x + phone_w - right_align_w - 20, phone_y + 30, "+Fil", RGB(0, 122, 255));
        
        int right_align_fldr = fb_get_string_width("+Fldr", sys_font_path);
        draw_string(phone_x + phone_w - right_align_w - 20 - right_align_fldr - 15, phone_y + 30, "+Fldr", RGB(0, 122, 255));
    }
    
    current_file_count = fs_list_dir(current_dir, current_files, 14);
    
    int list_y = phone_y + 80;
    for (int i = 0; i < current_file_count; i++) {
        char display_name[30];
        int j = 0;
        for (; j < 29 && current_files[i].name[j] != '\0'; j++) {
            display_name[j] = current_files[i].name[j];
        }
        if (current_files[i].name[j] != '\0') {
            display_name[j-3] = '.';
            display_name[j-2] = '.';
            display_name[j-1] = '.';
        }
        display_name[j] = '\0';
        
        unsigned int color = current_files[i].is_dir ? folder_color : text_primary;
        draw_string(phone_x + 20, list_y + 10, display_name, color);
        
        if (!current_files[i].is_dir) {
            char size_str[16];
            int size = current_files[i].size;
            int idx = 0;
            if (size == 0) {
                size_str[idx++] = '0';
            } else {
                char temp[16];
                int t = 0;
                while(size > 0) {
                    temp[t++] = (size % 10) + '0';
                    size /= 10;
                }
                while(t > 0) size_str[idx++] = temp[--t];
            }
            size_str[idx++] = ' ';
            size_str[idx++] = 'B';
            size_str[idx++] = '\0';
            
            extern char sys_font_path[100];
            int size_w = fb_get_string_width(size_str, sys_font_path);
            draw_string(phone_x + phone_w - size_w - 20, list_y + 10, size_str, text_secondary);
        } else {
            draw_string(phone_x + phone_w - 20, list_y + 10, ">", text_secondary);
        }
        
        draw_rect(phone_x + 20, list_y + 35, phone_w - 20, 1, border_color);
        list_y += 40;
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
    unsigned int status_bg = is_dark_mode ? RGB(0, 0, 0) : RGB(230, 230, 230);
    unsigned int text_col = is_dark_mode ? RGB(255, 255, 255) : RGB(0, 0, 0);
    draw_rect(phone_x + phone_w - 110, phone_y, 110, 20, status_bg);
    
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
    draw_string(phone_x + phone_w - 100, phone_y + 6, fps_str, text_col);
}

void draw_sys_monitor() {
    extern int sys_monitor_enabled;
    if (!sys_monitor_enabled) return;
    unsigned int status_bg = is_dark_mode ? RGB(0, 0, 0) : RGB(230, 230, 230);
    unsigned int text_col = is_dark_mode ? RGB(255, 255, 255) : RGB(0, 0, 0);
    
    int monitor_w = 140;
    int monitor_x = phone_x + 60; // Moved left but not overlapping time
    draw_rect(monitor_x, phone_y, monitor_w, 20, status_bg);
    
    int cpu_usage = 12 + (get_hw_time() % 14);
    int ram_usage_dec = get_hw_time() % 10;
    char monitor_str[32];
    monitor_str[0] = 'C'; monitor_str[1] = 'P'; monitor_str[2] = 'U'; monitor_str[3] = ':';
    monitor_str[4] = (cpu_usage / 10) + '0';
    monitor_str[5] = (cpu_usage % 10) + '0';
    monitor_str[6] = '%'; monitor_str[7] = ' '; monitor_str[8] = ' ';
    monitor_str[9] = 'R'; monitor_str[10] = 'A'; monitor_str[11] = 'M'; monitor_str[12] = ':';
    monitor_str[13] = '8'; monitor_str[14] = '.'; monitor_str[15] = '0' + ram_usage_dec; 
    monitor_str[16] = 'M'; monitor_str[17] = 'B'; monitor_str[18] = '\0';
    
    draw_string(monitor_x + 2, phone_y + 6, monitor_str, text_col);
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
    draw_rect(phone_x, phone_y + phone_h - 30, phone_w, 30, RGB(20, 20, 20));
    extern void draw_circle(int cx, int cy, int r, int thickness, unsigned int color);
    draw_circle(phone_x + phone_w / 2, phone_y + phone_h - 15, 8, 2, RGB(200, 200, 200));
    draw_triangle(phone_x + 88, phone_y + phone_h - 15, phone_x + 100, phone_y + phone_h - 23, phone_x + 100, phone_y + phone_h - 7, RGB(200, 200, 200));
    draw_triangle(phone_x + 89, phone_y + phone_h - 15, phone_x + 100, phone_y + phone_h - 22, phone_x + 100, phone_y + phone_h - 8, RGB(200, 200, 200));
    draw_triangle(phone_x + 90, phone_y + phone_h - 15, phone_x + 100, phone_y + phone_h - 21, phone_x + 100, phone_y + phone_h - 9, RGB(200, 200, 200));
}

void render_screen() {
    
    if (current_app_idx != -1) {
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
    fb_init();
    keyboard_init();
    mouse_init();
    fs_init();
    
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
                // To avoid flickering the mouse cursor when updating the counters,
                // we restore the old cursor background, draw the counters, and redraw the cursor.
                restore_pixels(cursor_x, cursor_y, 16, 16, cursor_bg);
                if (fps_enabled) draw_fps_counter();
                if (sys_monitor_enabled) draw_sys_monitor();
                save_pixels(cursor_x, cursor_y, 16, 16, cursor_bg);
                draw_cursor(cursor_x, cursor_y);
                screen_changed = 1;
            }
        }
        
        char ascii;
        if (keyboard_poll(&ascii)) {
            if (current_app_idx != -1) {
                app_key_pressed = ascii;
                registered_apps[current_app_idx]->update();
                
                restore_pixels(cursor_x, cursor_y, 16, 16, cursor_bg);
                render_screen();
                save_pixels(cursor_x, cursor_y, 16, 16, cursor_bg);
                draw_cursor(cursor_x, cursor_y);
                screen_changed = 1;
            } else if (ascii == '\b' && key_idx > 0) {
                key_idx--;
                key_buf[key_idx] = '\0';
                
                restore_pixels(cursor_x, cursor_y, 16, 16, cursor_bg);
                render_screen();
                save_pixels(cursor_x, cursor_y, 16, 16, cursor_bg);
                draw_cursor(cursor_x, cursor_y);
                screen_changed = 1;
            } else if (ascii != 0 && ascii != '\b' && key_idx < 31) {
                key_buf[key_idx++] = ascii;
                key_buf[key_idx] = '\0';
                
                restore_pixels(cursor_x, cursor_y, 16, 16, cursor_bg);
                render_screen();
                save_pixels(cursor_x, cursor_y, 16, 16, cursor_bg);
                draw_cursor(cursor_x, cursor_y);
                screen_changed = 1;
            }
        }
        
        int dx, dy, left_click;
        int mouse_active = mouse_poll(&dx, &dy, &left_click);
        int old_x = cursor_x;
        int old_y = cursor_y;
        
        app_left_click_state = 0;
        if (mouse_active) {
            app_left_click_state = left_click && !last_left_click;
        }

        if (current_app_idx != -1) {
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
                        if (current_app_idx != -1) {
                            system_back_pressed = 1;
                        } else {
                            if (current_screen == SETTINGS_SCREEN || (current_screen == FILES_SCREEN && current_dir[0] == '\0' && !is_picking_wallpaper && !is_picking_sys_font && !is_picking_lock_font) || current_screen == MEDIA_PLAYER_SCREEN) {
                                current_screen = HOME_SCREEN;
                            } else if (current_screen == ABOUT_SCREEN || current_screen == DEV_OPTIONS_SCREEN || current_screen == APPS_LIST_SCREEN || current_screen == PERSONALIZATION_SCREEN) {
                                current_screen = SETTINGS_SCREEN;
                            } else if (current_screen == 12) {
                                current_screen = ABOUT_SCREEN;
                            } else if (current_screen == KEYBOARD_TEST_SCREEN) {
                                current_screen = DEV_OPTIONS_SCREEN;
                            } else if (current_screen == FILE_VIEWER_SCREEN) {
                                current_screen = FILES_SCREEN;
                            } else if (current_screen == FILES_SCREEN && current_dir[0] == '\0' && (is_picking_wallpaper || is_picking_sys_font || is_picking_lock_font)) {
                                is_picking_wallpaper = 0;
                                is_picking_sys_font = 0;
                                is_picking_lock_font = 0;
                                current_screen = PERSONALIZATION_SCREEN;
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
                } else if (current_app_idx != -1) {
                    screen_changed = 1; // Redraw app on click
                } else if (current_screen == HOME_SCREEN && current_app_idx == -1) {
                    // Settings Icon hit test
                    int set_x = phone_x + 20, set_y = phone_y + 50, spacing_x = 76, spacing_y = 76;
                    if (cursor_x >= set_x && cursor_x <= set_x + 48 &&
                        cursor_y >= set_y && cursor_y <= set_y + 48) {
                        current_screen = SETTINGS_SCREEN;
                        screen_changed = 1;
                    }
                    // Files Icon hit test
                    else if (cursor_x >= set_x + spacing_x && cursor_x <= set_x + spacing_x + 48 &&
                        cursor_y >= set_y && cursor_y <= set_y + 48) {
                        current_screen = FILES_SCREEN;
                        screen_changed = 1;
                    }
                    else {
                        // Third-party App hit test
                        int current_x = set_x + spacing_x * 2;
                        int current_y = set_y;
                        for (int i = 0; i < num_registered_apps; i++) {
                            if (cursor_x >= current_x && cursor_x <= current_x + 48 &&
                                cursor_y >= current_y && cursor_y <= current_y + 48) {
                                current_app_idx = i;
                                registered_apps[i]->init();
                                screen_changed = 1;
                                break;
                            }
                            current_x += spacing_x;
                            if (current_x > phone_x + phone_w - 50) {
                                current_x = set_x;
                                current_y += spacing_y;
                            }
                        }
                    }
                } else if (current_screen == SETTINGS_SCREEN && current_app_idx == -1) {
                    // Personalization row hit test (y = phone_y + 104, height 40)
                    if (cursor_x >= phone_x && cursor_x <= phone_x + phone_w &&
                             cursor_y >= phone_y + 104 && cursor_y < phone_y + 144) {
                        current_screen = PERSONALIZATION_SCREEN;
                        screen_changed = 1;
                    }
                    // About row hit test (y = phone_y + 178, height 40)
                    else if (cursor_x >= phone_x && cursor_x <= phone_x + phone_w &&
                             cursor_y >= phone_y + 178 && cursor_y < phone_y + 218) {
                        current_screen = ABOUT_SCREEN;
                        screen_changed = 1;
                    }
                    // Developer Options row hit test (y = phone_y + 218, height 40)
                    else if (cursor_x >= phone_x && cursor_x <= phone_x + phone_w &&
                             cursor_y >= phone_y + 218 && cursor_y < phone_y + 258) {
                        current_screen = DEV_OPTIONS_SCREEN;
                        screen_changed = 1;
                    }
                    // Installed Apps row hit test (y = phone_y + 258, height 40)
                    else if (cursor_x >= phone_x && cursor_x <= phone_x + phone_w &&
                             cursor_y >= phone_y + 258 && cursor_y < phone_y + 298) {
                        current_screen = APPS_LIST_SCREEN;
                        screen_changed = 1;
                    }
                } else if (current_screen == APPS_LIST_SCREEN) {
                    int hit_y = phone_y + 104;
                        for (int i = 0; i < num_registered_apps; i++) {
                            if (cursor_x >= phone_x && cursor_x <= phone_x + phone_w &&
                                cursor_y >= hit_y && cursor_y < hit_y + 40) {
                                current_app_idx = i;
                                registered_apps[i]->init();
                                screen_changed = 1;
                                break;
                            }
                            hit_y += 40;
                        }
                } else if (current_screen == DEV_OPTIONS_SCREEN) {
                    // FPS row hit test (y = phone_y + 80, height 40)
                    if (cursor_x >= phone_x && cursor_x <= phone_x + phone_w &&
                             cursor_y >= phone_y + 80 && cursor_y < phone_y + 120) {
                        fps_enabled = !fps_enabled;
                        screen_changed = 1;
                    }
                    // KB Test row hit test (y = phone_y + 120, height 40)
                    else if (cursor_x >= phone_x && cursor_x <= phone_x + phone_w &&
                             cursor_y >= phone_y + 120 && cursor_y < phone_y + 160) {
                        current_screen = KEYBOARD_TEST_SCREEN;
                        screen_changed = 1;
                    }
                    // System Monitor hit test (y = phone_y + 160, height 40)
                    else if (cursor_x >= phone_x && cursor_x <= phone_x + phone_w &&
                             cursor_y >= phone_y + 160 && cursor_y < phone_y + 200) {
                        extern int sys_monitor_enabled;
                        sys_monitor_enabled = !sys_monitor_enabled;
                        screen_changed = 1;
                    }
                } else if (current_screen == KEYBOARD_TEST_SCREEN) {
                    // Nothing to hit test here natively
                } else if (current_screen == PERSONALIZATION_SCREEN) {
                    // Theme row hit test (y = phone_y + 104, height 40)
                    if (cursor_x >= phone_x && cursor_x <= phone_x + phone_w &&
                             cursor_y >= phone_y + 104 && cursor_y < phone_y + 144) {
                        is_dark_mode = !is_dark_mode;
                        screen_changed = 1;
                    }
                    // Wallpaper row hit test (y = phone_y + 144, height 40)
                    else if (cursor_x >= phone_x && cursor_x <= phone_x + phone_w &&
                             cursor_y >= phone_y + 144 && cursor_y < phone_y + 184) {
                        is_picking_wallpaper = 1;
                        current_screen = FILES_SCREEN;
                        screen_changed = 1;
                    }
                    // Font row hit test (y = phone_y + 184, height 40)
                    else if (cursor_x >= phone_x && cursor_x <= phone_x + phone_w &&
                             cursor_y >= phone_y + 184 && cursor_y < phone_y + 224) {
                        is_picking_sys_font = 1;
                        current_screen = FILES_SCREEN;
                        screen_changed = 1;
                    }
                    // Lock Font row hit test (y = phone_y + 224, height 40)
                    else if (cursor_x >= phone_x && cursor_x <= phone_x + phone_w &&
                             cursor_y >= phone_y + 224 && cursor_y < phone_y + 264) {
                        is_picking_lock_font = 1;
                        current_screen = FILES_SCREEN;
                        screen_changed = 1;
                    }
                } else if (current_screen == ABOUT_SCREEN) {
                    if (cursor_x >= phone_x && cursor_x <= phone_x + phone_w &&
                        cursor_y >= phone_y + 80 && cursor_y < phone_y + 120) {
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
                        cursor_y >= phone_y + 20 && cursor_y <= phone_y + 60) {
                        if (current_dir[0] == '\0') {
                            if (is_picking_wallpaper || is_picking_sys_font || is_picking_lock_font) {
                                is_picking_wallpaper = 0;
                                is_picking_sys_font = 0;
                                is_picking_lock_font = 0;
                                current_screen = PERSONALIZATION_SCREEN;
                            } else {
                                current_screen = HOME_SCREEN;
                            }
                        } else {
                            int len = 0;
                            while (current_dir[len] != '\0') len++;
                            if (len > 0 && current_dir[len-1] == '/') len--;
                            while (len > 0 && current_dir[len-1] != '/') len--;
                            current_dir[len] = '\0';
                        }
                        screen_changed = 1;
                    }
                    
                    int in_sys = 0;
                    if (current_dir[0] == 's' && current_dir[1] == 'y' && current_dir[2] == 's' && current_dir[3] == '/') {
                        in_sys = 1;
                    }
                    int clicked_button = 0;
                    if (!in_sys) {
                        // +Fldr button
                        if (cursor_x >= phone_x + phone_w - 90 && cursor_x <= phone_x + phone_w - 50 &&
                            cursor_y >= phone_y + 20 && cursor_y <= phone_y + 60) {
                            create_item(1);
                            screen_changed = 1;
                            clicked_button = 1;
                        }
                        // +Fil button
                        else if (cursor_x >= phone_x + phone_w - 45 && cursor_x <= phone_x + phone_w &&
                                 cursor_y >= phone_y + 20 && cursor_y <= phone_y + 60) {
                            create_item(0);
                            screen_changed = 1;
                            clicked_button = 1;
                        }
                    }
                    
                    // List item hit test
                    if (!clicked_button && cursor_x >= phone_x && cursor_x <= phone_x + phone_w &&
                             cursor_y >= phone_y + 80 && cursor_y <= phone_y + 80 + current_file_count * 40) {
                        int idx = (cursor_y - (phone_y + 80)) / 40;
                        if (idx >= 0 && idx < current_file_count) {
                            if (current_files[idx].is_dir) {
                                int d_len = 0;
                                while (current_dir[d_len] != '\0') d_len++;
                                int n_len = 0;
                                while (current_files[idx].name[n_len] != '\0') {
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
                                
                                if (is_img || is_audio || is_font) {
                                    int d_len = 0;
                                    while (current_dir[d_len] != '\0') {
                                        current_file[d_len] = current_dir[d_len];
                                        d_len++;
                                    }
                                    int n2 = 0;
                                    while (name[n2] != '\0') {
                                        current_file[d_len + n2] = name[n2];
                                        n2++;
                                    }
                                    current_file[d_len + n2] = '\0';
                                    
                                    if (is_audio) {
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
                                        current_screen = PERSONALIZATION_SCREEN;
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
                                        current_screen = PERSONALIZATION_SCREEN;
                                        screen_changed = 1;
                                    } else {
                                        current_screen = FILE_VIEWER_SCREEN;
                                        screen_changed = 1;
                                    }
                                }
                            }
                        }
                    }
                } else if (current_screen == FILE_VIEWER_SCREEN) {
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
                        }
                        render_screen();
                        screen_changed = 1;
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
            
            save_pixels(cursor_x, cursor_y, 16, 16, cursor_bg);
            draw_cursor(cursor_x, cursor_y);
            
            last_left_click = left_click;
        }
        
        if (screen_changed) {
            render_screen();
            save_pixels(cursor_x, cursor_y, 16, 16, cursor_bg);
            draw_cursor(cursor_x, cursor_y);
            fb_swap_buffers();
        } else if (mouse_active && (dx != 0 || dy != 0)) {
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
