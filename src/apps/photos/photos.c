#include "../../sys/nykon_api.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define MAX_PHOTOS 64
#define FILENAME_MAX_LEN 64
#define PATH_MAX_LEN 128

typedef struct {
    char name[FILENAME_MAX_LEN];
    char path[PATH_MAX_LEN];
    unsigned short width;
    unsigned short height;
    unsigned int size;
    int is_wallpaper;
} PhotoEntry;

static PhotoEntry photo_list[MAX_PHOTOS];
static int photo_count = 0;
static int selected_photo_idx = 0;
static int gallery_scroll_row = 0; // scroll by row (3 items per row)

enum {
    VIEW_GALLERY = 0,
    VIEW_PHOTO
};
static int current_view = VIEW_GALLERY;

static unsigned int toast_start_time = 0;
static char toast_message[64] = "";

// Helper string functions
static int str_len(const char *s) {
    int len = 0;
    while (s && s[len]) len++;
    return len;
}

static void str_copy(char *dest, const char *src, int max_len) {
    int i = 0;
    while (src && src[i] && i < max_len - 1) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

static int str_equal(const char *s1, const char *s2) {
    int i = 0;
    while (s1[i] && s2[i]) {
        if (s1[i] != s2[i]) return 0;
        i++;
    }
    return (s1[i] == s2[i]);
}

static int str_contains_case_insensitive(const char *haystack, const char *needle) {
    if (!haystack || !needle || !needle[0]) return 0;
    int hlen = str_len(haystack);
    int nlen = str_len(needle);
    for (int i = 0; i <= hlen - nlen; i++) {
        int match = 1;
        for (int j = 0; j < nlen; j++) {
            char c1 = haystack[i + j];
            char c2 = needle[j];
            if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
            if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
            if (c1 != c2) {
                match = 0;
                break;
            }
        }
        if (match) return 1;
    }
    return 0;
}

static int is_image_filename(const char *name) {
    int len = str_len(name);
    if (len < 4) return 0;
    const char *ext = name + len - 4;
    if (ext[0] == '.' && (ext[1] == 'p' || ext[1] == 'P') && (ext[2] == 'n' || ext[2] == 'N') && (ext[3] == 'g' || ext[3] == 'G')) return 1;
    if (ext[0] == '.' && (ext[1] == 't' || ext[1] == 'T') && (ext[2] == 'g' || ext[2] == 'G') && (ext[3] == 'a' || ext[3] == 'A')) return 1;
    if (len >= 5) {
        const char *ext5 = name + len - 5;
        if (ext5[0] == '.' && (ext5[1] == 'j' || ext5[1] == 'J') && (ext5[2] == 'p' || ext5[2] == 'P') && (ext5[3] == 'e' || ext5[3] == 'E') && (ext5[4] == 'g' || ext5[4] == 'G')) return 1;
    }
    return 0;
}

static int is_system_asset(const char *dir_prefix, const char *filename) {
    // Exclude system directories
    if (dir_prefix) {
        if (str_contains_case_insensitive(dir_prefix, "sys")) return 1;
        if (str_contains_case_insensitive(dir_prefix, "apps")) return 1;
        if (str_contains_case_insensitive(dir_prefix, "packages")) return 1;
    }
    // Exclude system icons and sprites
    if (str_contains_case_insensitive(filename, "icon")) return 1;
    if (str_contains_case_insensitive(filename, "sprite")) return 1;
    if (str_contains_case_insensitive(filename, "cursor")) return 1;
    if (str_contains_case_insensitive(filename, "logo")) return 1;
    if (str_contains_case_insensitive(filename, "unlock")) return 1;

    return 0;
}

static void add_photo_if_valid(const char *dir_prefix, const char *filename, unsigned int size) {
    if (photo_count >= MAX_PHOTOS) return;
    if (!is_image_filename(filename)) return;
    if (is_system_asset(dir_prefix, filename)) return;

    char full_path[PATH_MAX_LEN];
    int p = 0;
    if (dir_prefix && dir_prefix[0] != '\0') {
        for (int i = 0; dir_prefix[i] && p < PATH_MAX_LEN - 2; i++) {
            full_path[p++] = dir_prefix[i];
        }
        if (p > 0 && full_path[p - 1] != '/' && p < PATH_MAX_LEN - 2) {
            full_path[p++] = '/';
        }
    }
    for (int i = 0; filename[i] && p < PATH_MAX_LEN - 1; i++) {
        full_path[p++] = filename[i];
    }
    full_path[p] = '\0';

    // Check for duplicates
    for (int i = 0; i < photo_count; i++) {
        if (str_equal(photo_list[i].path, full_path)) return;
    }

    // Inspect image header to obtain real dimensions
    unsigned int fsize = 0;
    char *data = nykon_file_read(full_path, &fsize);
    unsigned short w = 0, h = 0;
    if (data && fsize >= 8 && data[0] == 'N' && data[1] == 'Y' && data[2] == 'K' && data[3] == 'N') {
        w = *((unsigned short *)(data + 4));
        h = *((unsigned short *)(data + 6));
    }

    // Ignore tiny system asset icons (< 64x64)
    if (w > 0 && h > 0 && w <= 48 && h <= 48) {
        return;
    }

    str_copy(photo_list[photo_count].name, filename, FILENAME_MAX_LEN);
    str_copy(photo_list[photo_count].path, full_path, PATH_MAX_LEN);
    photo_list[photo_count].width = w;
    photo_list[photo_count].height = h;
    photo_list[photo_count].size = (size > 0) ? size : fsize;

    int is_wp = 0;
    if (dir_prefix && (dir_prefix[0] == 'w' || (dir_prefix[0] == '/' && dir_prefix[1] == 'w'))) {
        is_wp = 1;
    } else if (w >= 300 && h >= 400) {
        is_wp = 1;
    }
    photo_list[photo_count].is_wallpaper = is_wp;

    photo_count++;
}

static void scan_directory_for_images(const char *dir_path) {
    NykonFileInfo files[32];
    int count = nykon_list_dir(dir_path, files, 32);
    for (int i = 0; i < count; i++) {
        if (files[i].is_dir) {
            if (files[i].name[0] != '.' && (!dir_path || dir_path[0] == '\0')) {
                // Ignore system directories
                if (!str_contains_case_insensitive(files[i].name, "sys") &&
                    !str_contains_case_insensitive(files[i].name, "apps") &&
                    !str_contains_case_insensitive(files[i].name, "packages")) {
                    scan_directory_for_images(files[i].name);
                }
            }
        } else {
            add_photo_if_valid(dir_path, files[i].name, files[i].size);
        }
    }
}

static void scan_all_device_photos() {
    photo_count = 0;
    // 1. Wallpapers folder
    scan_directory_for_images("wallpapers");
    // 2. Photos / Pictures folders if created
    scan_directory_for_images("photos");
    scan_directory_for_images("pictures");
    scan_directory_for_images("downloads");
    // 3. Test directory
    scan_directory_for_images("test");
    // 4. Root non-system images
    scan_directory_for_images("");
}

static void show_toast(const char *msg) {
    str_copy(toast_message, msg, sizeof(toast_message));
    toast_start_time = nykon_get_time();
}

// Draws a center-cropped square thumbnail (thumb_size x thumb_size) with rounded corners
static void draw_square_thumbnail(const char *filepath, int dst_x, int dst_y, int thumb_size, int r) {
    unsigned int size = 0;
    char *data = nykon_file_read(filepath, &size);
    if (!data || size < 8 || data[0] != 'N' || data[1] != 'Y' || data[2] != 'K' || data[3] != 'N') {
        nykon_draw_rounded_rect(dst_x, dst_y, thumb_size, thumb_size, r, 0xFF333333);
        int tw = nykon_get_string_width("IMG", NULL);
        nykon_draw_string(dst_x + (thumb_size - tw) / 2, dst_y + (thumb_size - 12) / 2, "IMG", 0xFFAAAAAA);
        return;
    }

    unsigned short width = *((unsigned short *)(data + 4));
    unsigned short height = *((unsigned short *)(data + 6));
    unsigned int *pixels = (unsigned int *)(data + 8);

    if (width == 0 || height == 0) return;

    // Calculate center-crop box inside the source image
    int crop_w = width;
    int crop_h = height;
    int start_src_x = 0;
    int start_src_y = 0;

    if (width > height) {
        crop_w = height;
        start_src_x = (width - crop_w) / 2;
    } else if (height > width) {
        crop_h = width;
        start_src_y = (height - crop_h) / 2;
    }

    static const unsigned char corner_cut[10] = { 6, 4, 3, 2, 1, 1, 0, 0, 0, 0 };
    static unsigned int row_scanline[88];

    for (int y = 0; y < thumb_size; y++) {
        int src_y = start_src_y + (y * crop_h) / thumb_size;
        if (src_y >= height) src_y = height - 1;
        unsigned int *src_row = &pixels[src_y * width];

        int cut = 0;
        if (y < 10) cut = corner_cut[y];
        else if (y >= thumb_size - 10) cut = corner_cut[thumb_size - 1 - y];

        int span_len = thumb_size - 2 * cut;
        if (span_len <= 0) continue;

        for (int i = 0; i < span_len; i++) {
            int x = cut + i;
            int src_x = start_src_x + (x * crop_w) / thumb_size;
            if (src_x >= width) src_x = width - 1;
            row_scanline[i] = src_row[src_x];
        }
        nykon_draw_framebuffer(row_scanline, dst_x + cut, dst_y + y, span_len, 1);
    }
}

void photos_init(void) {
    scan_all_device_photos();
    current_view = VIEW_GALLERY;
    selected_photo_idx = 0;
    gallery_scroll_row = 0;
    toast_message[0] = '\0';
}

void photos_update(void) {
    int px, py, pw, ph;
    nykon_get_screen_bounds(&px, &py, &pw, &ph);

    // Handle system back button
    if (nykon_get_back_pressed()) {
        if (current_view == VIEW_PHOTO) {
            current_view = VIEW_GALLERY;
            nykon_request_redraw();
            return;
        } else {
            nykon_exit_app();
            return;
        }
    }

    // Handle picked file from system picker
    char picked_path[PATH_MAX_LEN];
    if (nykon_get_picked_file(picked_path, PATH_MAX_LEN)) {
        add_photo_if_valid("", picked_path, 0);
        // Find index of newly added photo and open it
        for (int i = 0; i < photo_count; i++) {
            if (str_equal(photo_list[i].path, picked_path)) {
                selected_photo_idx = i;
                current_view = VIEW_PHOTO;
                break;
            }
        }
        nykon_request_redraw();
        return;
    }

    int mx, my, left_click;
    nykon_get_mouse(&mx, &my, &left_click);
    static int prev_left_click = 0;
    int clicked = (left_click && !prev_left_click);
    prev_left_click = left_click;

    if (!clicked) return;

    // ==========================================
    // VIEW 1: GALLERY VIEW CLICK HANDLING (3 Squares Per Line)
    // ==========================================
    if (current_view == VIEW_GALLERY) {
        // Scroll controls if total rows > 4
        int total_rows = (photo_count + 2) / 3;
        int sub_y = py + 78;

        if (total_rows > 4) {
            // Scroll Up Button [ ^ ] (px + pw - 64, sub_y, 24x24)
            if (mx >= px + pw - 64 && mx <= px + pw - 40 && my >= sub_y && my <= sub_y + 24) {
                if (gallery_scroll_row > 0) {
                    gallery_scroll_row--;
                    nykon_request_redraw();
                }
                return;
            }
            // Scroll Down Button [ v ] (px + pw - 36, sub_y, 24x24)
            if (mx >= px + pw - 36 && mx <= px + pw - 12 && my >= sub_y && my <= sub_y + 24) {
                if (gallery_scroll_row + 4 < total_rows) {
                    gallery_scroll_row++;
                    nykon_request_redraw();
                }
                return;
            }
        }

        // 3 Squares Per Line Grid Click Handling
        // Tile size = 88x88, Gap = 12px, Left margin = 16px, Top margin = py + 108
        int tile_size = 88;
        int gap = 12;
        int grid_left = px + 16;
        int grid_top = py + 108;

        for (int r = 0; r < 4; r++) {
            int row_idx = gallery_scroll_row + r;
            for (int c = 0; c < 3; c++) {
                int photo_idx = (row_idx * 3) + c;
                if (photo_idx >= photo_count) break;

                int tile_x = grid_left + c * (tile_size + gap);
                int tile_y = grid_top + r * (tile_size + gap);

                if (mx >= tile_x && mx <= tile_x + tile_size &&
                    my >= tile_y && my <= tile_y + tile_size) {
                    selected_photo_idx = photo_idx;
                    current_view = VIEW_PHOTO;
                    nykon_request_redraw();
                    return;
                }
            }
        }
        return;
    }

    // ==========================================
    // VIEW 2: SINGLE PHOTO VIEWER CLICK HANDLING
    // ==========================================
    if (current_view == VIEW_PHOTO) {
        // "< Back" Pill Button (px + 18, py + 33, 56x32)
        if (mx >= px + 18 && mx <= px + 74 && my >= py + 33 && my <= py + 65) {
            current_view = VIEW_GALLERY;
            nykon_request_redraw();
            return;
        }

        // "[ Set Wallpaper ]" Button in Header (px + pw - 110, py + 33, 94x32)
        if (mx >= px + pw - 110 && mx <= px + pw - 16 && my >= py + 33 && my <= py + 65) {
            if (selected_photo_idx >= 0 && selected_photo_idx < photo_count) {
                nykon_set_wallpaper(photo_list[selected_photo_idx].path);
                show_toast("Wallpaper Applied!");
                nykon_request_redraw();
                return;
            }
        }

        // Bottom Bar Action Buttons (py + ph - 68)
        int bar_y = py + ph - 68;

        // [ < Prev ] Button (px + 16, bar_y, 76x36)
        if (mx >= px + 16 && mx <= px + 92 && my >= bar_y && my <= bar_y + 36) {
            if (selected_photo_idx > 0) {
                selected_photo_idx--;
                nykon_request_redraw();
            }
            return;
        }

        // [ Next > ] Button (px + pw - 92, bar_y, 76x36)
        if (mx >= px + pw - 92 && mx <= px + pw - 16 && my >= bar_y && my <= bar_y + 36) {
            if (selected_photo_idx + 1 < photo_count) {
                selected_photo_idx++;
                nykon_request_redraw();
            }
            return;
        }

        // Center [ Rescan / Details ]
        if (mx >= px + 100 && mx <= px + pw - 100 && my >= bar_y && my <= bar_y + 36) {
            scan_all_device_photos();
            show_toast("Gallery Refreshed");
            nykon_request_redraw();
            return;
        }
    }
}

void photos_draw(void) {
    int px, py, pw, ph;
    nykon_get_screen_bounds(&px, &py, &pw, &ph);

    NykonSettings settings;
    nykon_get_settings(&settings);
    int is_dark = settings.is_dark_mode;

    // Theme Palette Tokens (matching Peanut GB aesthetic)
    unsigned int bg_col = is_dark ? 0xFF000000 : 0xFFF0F2F5;
    unsigned int header_bg = is_dark ? 0xFF181818 : 0xFFFFFFFF;
    unsigned int header_title = is_dark ? 0xFFFFFFFF : 0xFF111111;
    unsigned int text_primary = is_dark ? 0xFFFFFFFF : 0xFF111111;
    unsigned int text_secondary = is_dark ? 0xFFA0A0A0 : 0xFF606770;
    unsigned int pill_btn_bg = is_dark ? 0xFF282828 : 0xFFE4E6EB;
    unsigned int card_tile_bg = is_dark ? 0xFF181818 : 0xFFE8EAED;
    unsigned int accent_green = 0xFF2EB85C;

    // ==========================================
    // VIEW 1: GALLERY VIEW (3 Squares Per Line Grid)
    // ==========================================
    if (current_view == VIEW_GALLERY) {
        // 1. Background
        nykon_draw_rect(px, py + 20, pw, ph - 20, bg_col);

        // 2. Modern Header Card
        nykon_draw_rounded_rect(px + 12, py + 26, pw - 24, 46, 12, header_bg);

        // Title
        int title_tw = nykon_get_string_width("Photos", NULL);
        nykon_draw_string(px + (pw - title_tw) / 2, py + 39, "Photos", header_title);

        // 3. Subheader Bar (Photo count & pagination controls)
        int sub_y = py + 78;
        char count_str[32];
        int csl = 0;
        int tc = photo_count, tci = 0;
        char num_buf[8];
        if (tc == 0) num_buf[tci++] = '0';
        while (tc > 0) { num_buf[tci++] = (tc % 10) + '0'; tc /= 10; }
        while (tci > 0) count_str[csl++] = num_buf[--tci];
        const char *suffix = (photo_count == 1) ? " Photo" : " Photos";
        for (int i = 0; suffix[i]; i++) count_str[csl++] = suffix[i];
        count_str[csl] = '\0';

        nykon_draw_string(px + 18, sub_y + 4, count_str, text_secondary);

        int total_rows = (photo_count + 2) / 3;
        if (total_rows > 4) {
            // Up scroll pill [ ^ ]
            nykon_draw_rounded_rect(px + pw - 64, sub_y, 24, 24, 8, pill_btn_bg);
            nykon_draw_string(px + pw - 56, sub_y + 2, "^", text_primary);

            // Down scroll pill [ v ]
            nykon_draw_rounded_rect(px + pw - 36, sub_y, 24, 24, 8, pill_btn_bg);
            nykon_draw_string(px + pw - 28, sub_y + 2, "v", text_primary);
        }

        // 4. Grid of Square Photos (3 per line)
        int tile_size = 88;
        int gap = 12;
        int grid_left = px + 16;
        int grid_top = py + 108;

        for (int r = 0; r < 4; r++) {
            int row_idx = gallery_scroll_row + r;
            for (int c = 0; c < 3; c++) {
                int photo_idx = (row_idx * 3) + c;
                if (photo_idx >= photo_count) break;

                int tile_x = grid_left + c * (tile_size + gap);
                int tile_y = grid_top + r * (tile_size + gap);

                // Draw tile container card
                nykon_draw_rounded_rect(tile_x, tile_y, tile_size, tile_size, 10, card_tile_bg);

                // Draw center-cropped square thumbnail with high-speed scanline blits
                draw_square_thumbnail(photo_list[photo_idx].path, tile_x, tile_y, tile_size, 10);
            }
        }

        if (photo_count == 0) {
            nykon_draw_string(px + 24, py + 160, "No photos found on device.", text_secondary);
        }
    }

    // ==========================================
    // VIEW 2: SINGLE PHOTO VIEWER
    // ==========================================
    if (current_view == VIEW_PHOTO) {
        if (selected_photo_idx < 0 || selected_photo_idx >= photo_count) {
            current_view = VIEW_GALLERY;
            return;
        }

        PhotoEntry *p = &photo_list[selected_photo_idx];

        // 1. Pure OLED Black Canvas for Viewer
        nykon_draw_rect(px, py + 20, pw, ph - 20, 0xFF000000);

        // 2. Render Image Centered on Screen (High-Performance Scanline Blitting)
        unsigned int size = 0;
        char *data = nykon_file_read(p->path, &size);
        if (data && size >= 8 && data[0] == 'N' && data[1] == 'Y' && data[2] == 'K' && data[3] == 'N') {
            unsigned short width = *((unsigned short *)(data + 4));
            unsigned short height = *((unsigned short *)(data + 6));
            unsigned int *pixels = (unsigned int *)(data + 8);

            int max_w = pw;
            int max_h = ph - 148;
            int img_top = py + 72;

            if (width == 320 && height == 540) {
                // Full wallpaper size -> direct high-speed framebuffer blit
                int render_h = (max_h < height) ? max_h : height;
                nykon_draw_framebuffer(pixels, px, img_top, width, render_h);
            } else {
                // Aspect ratio fit with fast line buffer blits
                int render_w = width;
                int render_h = height;
                if (render_w > max_w || render_h > max_h) {
                    if (render_w * max_h > render_h * max_w) {
                        render_h = (height * max_w) / width;
                        render_w = max_w;
                    } else {
                        render_w = (width * max_h) / height;
                        render_h = max_h;
                    }
                }
                if (render_w <= 0) render_w = 1;
                if (render_h <= 0) render_h = 1;

                int offset_x = px + (pw - render_w) / 2;
                int offset_y = img_top + (max_h - render_h) / 2;

                static unsigned int line_buf[320];

                for (int y = 0; y < render_h; y++) {
                    int src_y = (y * height) / render_h;
                    if (src_y >= height) src_y = height - 1;
                    unsigned int *src_row = &pixels[src_y * width];

                    for (int x = 0; x < render_w; x++) {
                        int src_x = (x * width) / render_w;
                        if (src_x >= width) src_x = width - 1;
                        line_buf[x] = src_row[src_x];
                    }
                    nykon_draw_framebuffer(line_buf, offset_x, offset_y + y, render_w, 1);
                }
            }
        }

        // 3. Floating Glassmorphism Header Bar (Overlay)
        nykon_draw_rounded_rect(px + 12, py + 26, pw - 24, 46, 12, 0xEE1E1E1E);

        // [< Gallery] Back Button
        nykon_draw_rounded_rect(px + 18, py + 33, 68, 32, 8, 0xFF333333);
        int gtw = nykon_get_string_width("< Gallery", NULL);
        nykon_draw_string(px + 18 + ((68 - gtw) / 2), py + 39, "< Gallery", 0xFFFFFFFF);

        // Photo Index Indicator (e.g. "1 / 4")
        char idx_str[32];
        int il = 0;
        int ci = selected_photo_idx + 1;
        char num_b[8];
        int ni = 0;
        while (ci > 0) { num_b[ni++] = (ci % 10) + '0'; ci /= 10; }
        while (ni > 0) idx_str[il++] = num_b[--ni];
        idx_str[il++] = ' ';
        idx_str[il++] = '/';
        idx_str[il++] = ' ';
        int tot = photo_count;
        if (tot == 0) idx_str[il++] = '0';
        while (tot > 0) { num_b[ni++] = (tot % 10) + '0'; tot /= 10; }
        while (ni > 0) idx_str[il++] = num_b[--ni];
        idx_str[il] = '\0';

        int itw = nykon_get_string_width(idx_str, NULL);
        nykon_draw_string(px + (pw - itw) / 2, py + 39, idx_str, 0xFFCCCCCC);

        // [ Set Wallpaper ] Pill Button
        int sw_w = 98;
        int sw_x = px + pw - 18 - sw_w;
        nykon_draw_rounded_rect(sw_x, py + 33, sw_w, 32, 8, accent_green);
        int sw_tw = nykon_get_string_width("Set Wallpaper", NULL);
        nykon_draw_string(sw_x + ((sw_w - sw_tw) / 2), py + 39, "Set Wallpaper", 0xFFFFFFFF);

        // 4. Bottom Navigation & Info Bar
        int bot_y = py + ph - 68;
        nykon_draw_rounded_rect(px + 12, bot_y, pw - 24, 46, 12, 0xEE1E1E1E);

        // [< Prev] Button
        if (selected_photo_idx > 0) {
            nykon_draw_rounded_rect(px + 18, bot_y + 7, 72, 32, 8, 0xFF333333);
            int ptw = nykon_get_string_width("< Prev", NULL);
            nykon_draw_string(px + 18 + ((72 - ptw) / 2), bot_y + 13, "< Prev", 0xFFFFFFFF);
        }

        // Center: Photo Name & Info
        int nw = nykon_get_string_width(p->name, NULL);
        if (nw > 110) {
            char short_name[16];
            str_copy(short_name, p->name, 12);
            short_name[11] = '.';
            short_name[12] = '.';
            short_name[13] = '\0';
            int stw = nykon_get_string_width(short_name, NULL);
            nykon_draw_string(px + (pw - stw) / 2, bot_y + 13, short_name, 0xFFAAAAAA);
        } else {
            nykon_draw_string(px + (pw - nw) / 2, bot_y + 13, p->name, 0xFFAAAAAA);
        }

        // [Next >] Button
        if (selected_photo_idx + 1 < photo_count) {
            int nxt_w = 72;
            int nxt_x = px + pw - 18 - nxt_w;
            nykon_draw_rounded_rect(nxt_x, bot_y + 7, nxt_w, 32, 8, 0xFF333333);
            int ntw = nykon_get_string_width("Next >", NULL);
            nykon_draw_string(nxt_x + ((nxt_w - ntw) / 2), bot_y + 13, "Next >", 0xFFFFFFFF);
        }
    }

    // 5. Floating Toast Feedback Notification
    if (toast_message[0] != '\0') {
        unsigned int elapsed = nykon_get_time() - toast_start_time;
        if (elapsed < 2000) {
            int tw = nykon_get_string_width(toast_message, NULL);
            int tw_box = tw + 28;
            int tx = px + (pw - tw_box) / 2;
            int ty = py + ph - 110;
            nykon_draw_rounded_rect(tx, ty, tw_box, 30, 8, 0xDD222222);
            nykon_draw_string(tx + 14, ty + 5, toast_message, 0xFFFFFFFF);
        } else {
            toast_message[0] = '\0';
        }
    }
}

NykonApp app_photos = {
    .name = "Photos",
    .init = photos_init,
    .update = photos_update,
    .draw = photos_draw,
    .icon_path = "sys/img/photos.png"
};
