#include "../../sys/nykon_api.h"
#include "builtin_rom.h"

#define ENABLE_LCD 1
#define PEANUT_GB_HIGH_LCD_ACCURACY 0
#define ENABLE_SOUND 0
#include "peanut_gb.h"

// 32KB Cart RAM buffer (supports MBC1/MBC3/MBC5 saves)
static uint8_t cart_ram[32768];

static const uint8_t *rom_ptr = builtin_rom_data;
static unsigned int rom_len = 0;
static char current_rom_name[64] = "FlappyBoy";

static struct gb_s gb_ctx;
static int gb_running = 0;

// App States
enum {
    STATE_START_MENU = 0,
    STATE_BROWSER,
    STATE_PLAYING
};
static int app_state = STATE_START_MENU;

// ROM list structure
typedef struct {
    char name[64];
    char path[128];
    unsigned int size;
    int is_builtin;
} RomEntry;

#define MAX_ROMS 16
static RomEntry rom_list[MAX_ROMS];
static int rom_count = 0;
static int selected_rom_idx = 0;

// Pure Monochrome Gray Palette (R == G == B, 100% neutral)
static const unsigned int dmg_palette[4] = {
    0xFFE8E8E8, // Pure Light Gray / White (232, 232, 232)
    0xFFA0A0A0, // Pure Medium Gray (160, 160, 160)
    0xFF505050, // Pure Dark Gray (80, 80, 80)
    0xFF141414  // Pure Charcoal / Black (20, 20, 20)
};

// 160x144 internal framebuffer
static unsigned int gb_frame[LCD_HEIGHT][LCD_WIDTH];

// String helpers
static int str_len(const char *s) {
    int len = 0;
    while (s && s[len]) len++;
    return len;
}

static int str_cmp_case(const char *s1, const char *s2) {
    int i = 0;
    while (s1[i] && s2[i]) {
        char c1 = s1[i];
        char c2 = s2[i];
        if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
        if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
        if (c1 != c2) return c1 - c2;
        i++;
    }
    return (unsigned char)s1[i] - (unsigned char)s2[i];
}

static int has_gb_extension(const char *name) {
    int len = str_len(name);
    if (len < 4) return 0;
    if (str_cmp_case(name + len - 3, ".gb") == 0) return 1;
    if (len >= 5 && str_cmp_case(name + len - 4, ".gbc") == 0) return 1;
    return 0;
}

static void str_copy_safe(char *dest, const char *src, int max_len) {
    int i = 0;
    while (src && src[i] && i < max_len - 1) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

static uint8_t gb_rom_read(struct gb_s *gb, const uint_fast32_t addr) {
    (void)gb;
    if (addr < rom_len && rom_ptr != NULL) {
        return rom_ptr[addr];
    }
    return 0xFF;
}

static uint8_t gb_cart_ram_read(struct gb_s *gb, const uint_fast32_t addr) {
    (void)gb;
    if (addr < sizeof(cart_ram)) {
        return cart_ram[addr];
    }
    return 0xFF;
}

static void gb_cart_ram_write(struct gb_s *gb, const uint_fast32_t addr, const uint8_t val) {
    (void)gb;
    if (addr < sizeof(cart_ram)) {
        cart_ram[addr] = val;
    }
}

static void gb_error(struct gb_s *gb, const enum gb_error_e err, const uint16_t val) {
    (void)gb;
    (void)err;
    (void)val;
}

static void lcd_draw_line(struct gb_s *gb, const uint8_t pixels[LCD_WIDTH], const uint_fast8_t line) {
    (void)gb;
    if (line >= LCD_HEIGHT) return;
    for (int x = 0; x < LCD_WIDTH; x++) {
        uint8_t color_idx = pixels[x] & 0x03;
        gb_frame[line][x] = dmg_palette[color_idx];
    }
}

// File Manager state for importing ROMs
static char fm_current_path[128] = "roms";
static NykonFileInfo fm_files[32];
static int fm_count = 0;
static int fm_scroll = 0;

static void format_size_kb(char *buf, unsigned int bytes) {
    unsigned int kb = (bytes + 1023) / 1024;
    if (kb == 0) kb = 1;
    char temp[16];
    int tlen = 0;
    unsigned int v = kb;
    while (v > 0) {
        temp[tlen++] = '0' + (v % 10);
        v /= 10;
    }
    int blen = 0;
    for (int i = tlen - 1; i >= 0; i--) {
        buf[blen++] = temp[i];
    }
    buf[blen++] = ' ';
    buf[blen++] = 'K';
    buf[blen++] = 'B';
    buf[blen] = '\0';
}

static void fm_refresh_dir(void) {
    fm_count = nykon_list_dir(fm_current_path, fm_files, 32);
    // If opening "roms" directory was empty, fall back to root ""
    if (fm_count == 0 && fm_current_path[0] != '\0') {
        fm_current_path[0] = '\0';
        fm_count = nykon_list_dir(fm_current_path, fm_files, 32);
    }
    if (fm_scroll >= fm_count && fm_count > 0) {
        fm_scroll = 0;
    }
}

static void fm_navigate_up(void) {
    int len = str_len(fm_current_path);
    if (len > 0 && fm_current_path[len - 1] == '/') len--;
    while (len > 0 && fm_current_path[len - 1] != '/') len--;
    if (len > 0 && fm_current_path[len - 1] == '/') len--;
    fm_current_path[len] = '\0';
    fm_scroll = 0;
    fm_refresh_dir();
}

static void fm_navigate_into(const char *dir_name) {
    int d_len = str_len(fm_current_path);
    if (d_len > 0 && fm_current_path[d_len - 1] != '/') {
        fm_current_path[d_len++] = '/';
    }
    int n_len = 0;
    while (dir_name[n_len] && d_len + n_len < 126) {
        fm_current_path[d_len + n_len] = dir_name[n_len];
        n_len++;
    }
    fm_current_path[d_len + n_len] = '\0';
    fm_scroll = 0;
    fm_refresh_dir();
}

static int load_rom_by_index(int idx);

static void import_and_load_picked_rom(const char *name, const char *path) {
    int target_idx = -1;
    for (int i = 0; i < rom_count; i++) {
        if (str_cmp_case(rom_list[i].path, path) == 0 || str_cmp_case(rom_list[i].name, name) == 0) {
            target_idx = i;
            str_copy_safe(rom_list[i].path, path, 128);
            str_copy_safe(rom_list[i].name, name, 64);
            break;
        }
    }
    if (target_idx == -1 && rom_count < MAX_ROMS) {
        target_idx = rom_count;
        str_copy_safe(rom_list[target_idx].name, name, 64);
        str_copy_safe(rom_list[target_idx].path, path, 128);
        rom_list[target_idx].size = 0;
        rom_list[target_idx].is_builtin = 0;
        rom_count++;
    }

    if (target_idx != -1) {
        load_rom_by_index(target_idx);
    }
}

static void fm_import_and_play(const char *file_name) {
    char full_path[128];
    int d_len = str_len(fm_current_path);
    if (d_len > 0) {
        str_copy_safe(full_path, fm_current_path, 128);
        int fp_len = str_len(full_path);
        if (full_path[fp_len - 1] != '/') {
            full_path[fp_len++] = '/';
            full_path[fp_len] = '\0';
        }
        int n_len = 0;
        while (file_name[n_len] && fp_len + n_len < 126) {
            full_path[fp_len + n_len] = file_name[n_len];
            n_len++;
        }
        full_path[fp_len + n_len] = '\0';
    } else {
        str_copy_safe(full_path, file_name, 128);
    }

    import_and_load_picked_rom(file_name, full_path);
}

static void scan_available_roms(void) {
    rom_count = 0;

    // 1. Built-in ROM
    str_copy_safe(rom_list[rom_count].name, "FlappyBoy", 64);
    rom_list[rom_count].path[0] = '\0';
    rom_list[rom_count].size = builtin_rom_size;
    rom_list[rom_count].is_builtin = 1;
    rom_count++;

    // 2. Scan "roms/" folder
    NykonFileInfo files[32];
    int count = nykon_list_dir("roms", files, 32);
    for (int i = 0; i < count && rom_count < MAX_ROMS; i++) {
        if (!files[i].is_dir && has_gb_extension(files[i].name)) {
            str_copy_safe(rom_list[rom_count].name, files[i].name, 64);
            char full_path[128] = "roms/";
            int fp_len = 5;
            for (int k = 0; files[i].name[k] && fp_len < 126; k++) {
                full_path[fp_len++] = files[i].name[k];
            }
            full_path[fp_len] = '\0';
            str_copy_safe(rom_list[rom_count].path, full_path, 128);
            rom_list[rom_count].size = files[i].size;
            rom_list[rom_count].is_builtin = 0;
            rom_count++;
        }
    }

    // 3. Scan root directory "/"
    count = nykon_list_dir("", files, 32);
    for (int i = 0; i < count && rom_count < MAX_ROMS; i++) {
        if (!files[i].is_dir && has_gb_extension(files[i].name)) {
            int already = 0;
            for (int k = 0; k < rom_count; k++) {
                if (str_cmp_case(rom_list[k].name, files[i].name) == 0) {
                    already = 1;
                    break;
                }
            }
            if (!already) {
                str_copy_safe(rom_list[rom_count].name, files[i].name, 64);
                str_copy_safe(rom_list[rom_count].path, files[i].name, 128);
                rom_list[rom_count].size = files[i].size;
                rom_list[rom_count].is_builtin = 0;
                rom_count++;
            }
        }
    }
}

static int load_rom_by_index(int idx) {
    if (idx < 0 || idx >= rom_count) return 0;

    if (rom_list[idx].is_builtin) {
        rom_ptr = builtin_rom_data;
        rom_len = builtin_rom_size;
        str_copy_safe(current_rom_name, rom_list[idx].name, 64);
    } else {
        unsigned int file_size = 0;
        char *data = nykon_file_read(rom_list[idx].path, &file_size);
        if (!data || file_size == 0) {
            // Fallback 1: try base name without directory prefix
            const char *base = rom_list[idx].path;
            for (int k = 0; rom_list[idx].path[k]; k++) {
                if (rom_list[idx].path[k] == '/') base = &rom_list[idx].path[k+1];
            }
            data = nykon_file_read(base, &file_size);
            if (!data || file_size == 0) {
                // Fallback 2: try roms/ prefix
                char alt_path[128] = "roms/";
                int aplen = 5;
                for (int k = 0; base[k] && aplen < 126; k++) alt_path[aplen++] = base[k];
                alt_path[aplen] = '\0';
                data = nykon_file_read(alt_path, &file_size);
            }
        }
        if (!data || file_size == 0) {
            return 0;
        }
        rom_ptr = (const uint8_t *)data;
        rom_len = file_size;
        rom_list[idx].size = file_size;
        str_copy_safe(current_rom_name, rom_list[idx].name, 64);
    }

    // Clear cart RAM
    for (unsigned int i = 0; i < sizeof(cart_ram); i++) {
        cart_ram[i] = 0;
    }

    // Initialize Peanut-GB
    enum gb_init_error_e ret = gb_init(&gb_ctx, gb_rom_read, gb_cart_ram_read, gb_cart_ram_write, gb_error, NULL);
    if (ret == GB_INIT_NO_ERROR || ret == GB_INIT_INVALID_CHECKSUM) {
        gb_init_lcd(&gb_ctx, lcd_draw_line);
        gb_running = 1;
        selected_rom_idx = idx;
        app_state = STATE_PLAYING;
        return 1;
    }

    gb_running = 0;
    return 0;
}

static void gb_app_init(void) {
    app_state = STATE_START_MENU;
    gb_running = 0;
    str_copy_safe(fm_current_path, "roms", 128);
    fm_scroll = 0;
    scan_available_roms();
}

static void gb_app_update(void) {
    // Check if a file was selected via the Nykon API System File Picker
    char picked_path[128];
    if (nykon_get_picked_file(picked_path, 128)) {
        const char *fn = picked_path;
        for (int k = 0; picked_path[k]; k++) {
            if (picked_path[k] == '/') {
                fn = &picked_path[k + 1];
            }
        }
        import_and_load_picked_rom(fn, picked_path);
        return;
    }

    int mx, my, left_click;
    int has_mouse = nykon_get_mouse(&mx, &my, &left_click);
    int px, py, pw, ph;
    nykon_get_screen_bounds(&px, &py, &pw, &ph);

    // ===================================================
    // STATE 1: START MENU (Imported Games with Top-Right + Import)
    // ===================================================
    if (app_state == STATE_START_MENU) {
        if (has_mouse && left_click) {
            int dock_x = px + 16;
            int dock_y = py + 65;
            int dock_w = pw - 32;

            // "[ + Import ]" Button opens system File Picker via Nykon API
            int imp_x = dock_x + dock_w - 88;
            int imp_y = dock_y + 10;
            int imp_w = 76;
            int imp_h = 26;
            if (mx >= imp_x && mx <= imp_x + imp_w && my >= imp_y && my <= imp_y + imp_h) {
                nykon_open_file_picker(".gb");
                return;
            }

            // Game list clicks
            int item_y = dock_y + 54;
            int display_count = rom_count > 6 ? 6 : rom_count;
            for (int i = 0; i < display_count; i++) {
                if (mx >= dock_x + 10 && mx <= dock_x + dock_w - 10 && my >= item_y && my <= item_y + 46) {
                    load_rom_by_index(i);
                    return;
                }
                item_y += 52;
            }
        }

        // Keyboard shortcut: 'I' or 'O' or Enter opens system File Picker
        if (nykon_get_key_state('i') || nykon_get_key_state('I') || nykon_get_key_state('o') || nykon_get_key_state('O')) {
            nykon_open_file_picker(".gb");
            return;
        }

        nykon_request_redraw();
        return;
    }

    // ===================================================
    // STATE 2: INTERACTIVE FILE MANAGER (.gb IMPORT BROWSER)
    // ===================================================
    if (app_state == STATE_BROWSER) {
        if (has_mouse && left_click) {
            // [< Back] Button (px + 18, py + 33, 56x32)
            if (mx >= px + 18 && mx <= px + 74 && my >= py + 33 && my <= py + 65) {
                app_state = STATE_START_MENU;
                return;
            }

            // [^ Up] Button (px + 78, py + 33, 44x32)
            if (fm_current_path[0] != '\0' && mx >= px + 78 && mx <= px + 122 && my >= py + 33 && my <= py + 65) {
                fm_navigate_up();
                return;
            }

            // Scroll Up [▲] Button (px + pw - 70, py + 74, 26x20)
            if (fm_count > 6 && mx >= px + pw - 70 && mx <= px + pw - 44 && my >= py + 74 && my <= py + 94) {
                if (fm_scroll > 0) fm_scroll--;
                return;
            }

            // Scroll Down [▼] Button (px + pw - 40, py + 74, 26x20)
            if (fm_count > 6 && mx >= px + pw - 40 && mx <= px + pw - 14 && my >= py + 74 && my <= py + 94) {
                if (fm_scroll + 6 < fm_count) fm_scroll++;
                return;
            }

            // File & Folder list item clicks
            int display_count = (fm_count - fm_scroll > 6) ? 6 : (fm_count - fm_scroll);
            int item_y = py + 98;
            for (int i = 0; i < display_count; i++) {
                int idx = fm_scroll + i;
                if (mx >= px + 12 && mx <= px + pw - 12 && my >= item_y && my <= item_y + 44) {
                    if (fm_files[idx].is_dir) {
                        fm_navigate_into(fm_files[idx].name);
                    } else if (has_gb_extension(fm_files[idx].name)) {
                        fm_import_and_play(fm_files[idx].name);
                    }
                    return;
                }
                item_y += 50;
            }
        }

        // Keyboard shortcuts
        if (nykon_get_key_state(27)) {
            app_state = STATE_START_MENU;
            return;
        }
        if (nykon_get_key_state('w') || nykon_get_key_state('W')) {
            if (fm_scroll > 0) fm_scroll--;
        }
        if (nykon_get_key_state('s') || nykon_get_key_state('S')) {
            if (fm_scroll + 6 < fm_count) fm_scroll++;
        }

        nykon_request_redraw();
        return;
    }

    // ===================================================
    // STATE 3: PLAYING GAME BOY EMULATOR
    // ===================================================
    if (app_state == STATE_PLAYING) {
        if (!gb_running) return;

        // 1. Reset joypad bits FIRST (active low: 1 = released, 0 = pressed)
        gb_ctx.direct.joypad_bits.up = 1;
        gb_ctx.direct.joypad_bits.down = 1;
        gb_ctx.direct.joypad_bits.left = 1;
        gb_ctx.direct.joypad_bits.right = 1;
        gb_ctx.direct.joypad_bits.a = 1;
        gb_ctx.direct.joypad_bits.b = 1;
        gb_ctx.direct.joypad_bits.start = 1;
        gb_ctx.direct.joypad_bits.select = 1;

        // 2. Mouse / Touch Controls
        if (has_mouse && left_click) {
            // Top Bar [ Home ] Button (px + 236, py + 312, 68x24)
            int home_w = 68;
            int home_x = px + pw - 16 - home_w;
            if (mx >= home_x - 4 && mx <= home_x + home_w + 4 && my >= py + 308 && my <= py + 340) {
                app_state = STATE_START_MENU;
                nykon_request_redraw();
                return;
            }

            // A Button (ax = px + 265, ay = py + 385, radius 32)
            int ax = px + 265, ay = py + 385;
            if ((mx - ax) * (mx - ax) + (my - ay) * (my - ay) <= 32 * 32) {
                gb_ctx.direct.joypad_bits.a = 0;
            }

            // B Button (bx = px + 205, by = py + 425, radius 32)
            int bx = px + 205, by = py + 425;
            if ((mx - bx) * (mx - bx) + (my - by) * (my - by) <= 32 * 32) {
                gb_ctx.direct.joypad_bits.b = 0;
            }

            // D-Pad bounds (center at px + 75, py + 415)
            int dpad_cx = px + 75;
            int dpad_cy = py + 415;
            if (mx >= dpad_cx - 45 && mx <= dpad_cx + 45 && my >= dpad_cy - 45 && my <= dpad_cy + 45) {
                int dx = mx - dpad_cx;
                int dy = my - dpad_cy;
                if (dy < -10) gb_ctx.direct.joypad_bits.up = 0;
                else if (dy > 10) gb_ctx.direct.joypad_bits.down = 0;
                if (dx < -10) gb_ctx.direct.joypad_bits.left = 0;
                else if (dx > 10) gb_ctx.direct.joypad_bits.right = 0;
            }

            // Select (px + 80 to px + 145, py + 475 to py + 520)
            if (mx >= px + 80 && mx <= px + 145 && my >= py + 475 && my <= py + 520) {
                gb_ctx.direct.joypad_bits.select = 0;
            }

            // Start (px + 150 to px + 220, py + 475 to py + 520)
            if (mx >= px + 150 && mx <= px + 220 && my >= py + 475 && my <= py + 520) {
                gb_ctx.direct.joypad_bits.start = 0;
            }

            // Screen tap to flap/jump
            if (my >= py + 20 && my <= py + 300) {
                gb_ctx.direct.joypad_bits.a = 0;
            }
        }

        // 3. Keyboard controls
        if (nykon_get_key_state('w') || nykon_get_key_state('W')) gb_ctx.direct.joypad_bits.up = 0;
        if (nykon_get_key_state('s') || nykon_get_key_state('S')) gb_ctx.direct.joypad_bits.down = 0;
        if (nykon_get_key_state('a') || nykon_get_key_state('A')) gb_ctx.direct.joypad_bits.left = 0;
        if (nykon_get_key_state('d') || nykon_get_key_state('D')) gb_ctx.direct.joypad_bits.right = 0;
        if (nykon_get_key_state('j') || nykon_get_key_state('J') || nykon_get_key_state('z') || nykon_get_key_state('Z') || nykon_get_key_state('k') || nykon_get_key_state('K')) gb_ctx.direct.joypad_bits.a = 0;
        if (nykon_get_key_state('x') || nykon_get_key_state('X')) gb_ctx.direct.joypad_bits.b = 0;
        if (nykon_get_key_state('\n') || nykon_get_key_state('\r')) gb_ctx.direct.joypad_bits.start = 0;
        if (nykon_get_key_state(' ') || nykon_get_key_state('\t')) {
            gb_ctx.direct.joypad_bits.a = 0;
            gb_ctx.direct.joypad_bits.select = 0;
        }

        // Keyboard hotkeys to browse
        if (nykon_get_key_state('o') || nykon_get_key_state('O') || nykon_get_key_state('l') || nykon_get_key_state('L')) {
            scan_available_roms();
            app_state = STATE_BROWSER;
            return;
        }

        // Step Game Boy emulation frame
        gb_run_frame(&gb_ctx);
        nykon_request_redraw();
    }
}

static void gb_app_draw(void) {
    int px, py, pw, ph;
    nykon_get_screen_bounds(&px, &py, &pw, &ph);

    // ===================================================
    // STATE 1: MODERN PURE NEUTRAL GRAY START MENU
    // ===================================================
    if (app_state == STATE_START_MENU) {
        // 1. Pure OLED Black Background
        nykon_draw_rect(px, py + 20, pw, ph - 20, 0xFF000000);

        // 2. Top Header Title: "Peanut GB"
        nykon_draw_string(px + 20, py + 36, "Peanut GB", 0xFFFFFFFF);

        // 3. Main Floating Card: "Imported Games"
        int dock_x = px + 16;
        int dock_y = py + 68;
        int dock_w = pw - 32;
        int dock_h = (ph - 20) - 78;
        nykon_draw_rounded_rect(dock_x - 1, dock_y - 1, dock_w + 2, dock_h + 2, 16, 0xFF303030);
        nykon_draw_rounded_rect(dock_x, dock_y, dock_w, dock_h, 16, 0xFF141414);

        // Header Title
        nykon_draw_string(dock_x + 16, dock_y + 16, "IMPORTED GAMES", 0xFFA0A0A0);

        // "[ + Import ]" Rounded Button on Top-Right of Card
        int imp_w = 80;
        int imp_h = 26;
        int imp_x = dock_x + dock_w - 14 - imp_w;
        int imp_y = dock_y + 11;
        nykon_draw_rounded_rect(imp_x - 1, imp_y - 1, imp_w + 2, imp_h + 2, 13, 0xFF4A4A4A);
        nykon_draw_rounded_rect(imp_x, imp_y, imp_w, imp_h, 13, 0xFF282828);
        int imp_txt_w = nykon_get_string_width("+ Import", NULL);
        nykon_draw_string(imp_x + ((imp_w - imp_txt_w) / 2), imp_y + 2, "+ Import", 0xFFFFFFFF);

        // Divider
        nykon_draw_rect(dock_x + 12, dock_y + 46, dock_w - 24, 1, 0xFF222222);

        // Game items list in main card
        int item_y = dock_y + 54;
        int display_count = rom_count > 6 ? 6 : rom_count;
        for (int i = 0; i < display_count; i++) {
            // Neutral Gray Item Card (R=34, G=34, B=34)
            nykon_draw_rounded_rect(dock_x + 10, item_y, dock_w - 20, 44, 10, 0xFF222222);

            // Icon badge (R=55, G=55, B=55)
            nykon_draw_rounded_rect(dock_x + 18, item_y + 10, 24, 24, 6, 0xFF363636);
            nykon_draw_filled_circle(dock_x + 30, item_y + 22, 4, 0xFFCCCCCC);

            // Clean White Title
            nykon_draw_string(dock_x + 50, item_y + 11, rom_list[i].name, 0xFFFFFFFF);

            // Neutral Gray Play Pill (R=52, G=52, B=52)
            int play_w = 52;
            int play_h = 24;
            int play_x = dock_x + dock_w - 18 - play_w;
            int play_y = item_y + 10;
            nykon_draw_rounded_rect(play_x, play_y, play_w, play_h, 12, 0xFF343434);
            int play_txt_w = nykon_get_string_width("PLAY", NULL);
            nykon_draw_string(play_x + ((play_w - play_txt_w) / 2), play_y + 2, "PLAY", 0xFFFFFFFF);

            item_y += 50;
        }

        if (rom_count == 0) {
            nykon_draw_string(dock_x + 20, dock_y + 60, "No imported games found", 0xFF808080);
        }
        return;
    }

    // ===================================================
    // STATE 2: INTERACTIVE MODERN FILE MANAGER
    // ===================================================
    if (app_state == STATE_BROWSER) {
        // OLED Pure Black Background
        nykon_draw_rect(px, py + 20, pw, ph - 20, 0xFF000000);

        // Modern Neutral Gray Header (R=24, G=24, B=24)
        nykon_draw_rounded_rect(px + 12, py + 26, pw - 24, 46, 12, 0xFF181818);
        
        // [< Back] Pill Button (R=45, G=45, B=45)
        nykon_draw_rounded_rect(px + 18, py + 33, 56, 32, 8, 0xFF2D2D2D);
        int back_tw = nykon_get_string_width("< Back", NULL);
        nykon_draw_string(px + 18 + ((56 - back_tw) / 2), py + 39, "< Back", 0xFFFFFFFF);

        // [^ Up] Button (if inside a subfolder)
        if (fm_current_path[0] != '\0') {
            nykon_draw_rounded_rect(px + 78, py + 33, 44, 32, 8, 0xFF2D2D2D);
            int up_tw = nykon_get_string_width("^ Up", NULL);
            nykon_draw_string(px + 78 + ((44 - up_tw) / 2), py + 38, "^ Up", 0xFFFFFFFF);
            
            // Path display
            char path_title[64] = "/";
            int pt_len = 1;
            for (int k = 0; fm_current_path[k] && pt_len < 60; k++) {
                path_title[pt_len++] = fm_current_path[k];
            }
            path_title[pt_len] = '\0';
            nykon_draw_string(px + 130, py + 39, path_title, 0xFFFFFFFF);
        } else {
            nykon_draw_string(px + 86, py + 39, "/ (Root Files)", 0xFFFFFFFF);
        }

        // Header right tag
        nykon_draw_string(px + pw - 90, py + 39, "Peanut GB", 0xFFA0A0A0);

        // Subheader / scroll controls
        nykon_draw_string(px + 16, py + 78, "Select a .gb file to import:", 0xFF888888);
        if (fm_count > 6) {
            nykon_draw_rounded_rect(px + pw - 70, py + 74, 26, 20, 6, 0xFF282828);
            nykon_draw_string(px + pw - 63, py + 74, "^", 0xFFFFFFFF);
            nykon_draw_rounded_rect(px + pw - 40, py + 74, 26, 20, 6, 0xFF282828);
            nykon_draw_string(px + pw - 33, py + 74, "v", 0xFFFFFFFF);
        }

        // File & Folder List entries
        int display_count = (fm_count - fm_scroll > 6) ? 6 : (fm_count - fm_scroll);
        int item_y = py + 98;
        char sz_buf[32];
        for (int i = 0; i < display_count; i++) {
            int idx = fm_scroll + i;
            nykon_draw_rounded_rect(px + 12, item_y, pw - 24, 44, 10, 0xFF1E1E1E);

            if (fm_files[idx].is_dir) {
                // Folder badge
                nykon_draw_rounded_rect(px + 20, item_y + 10, 32, 24, 6, 0xFF2D2D2D);
                int tw = nykon_get_string_width("DIR", NULL);
                nykon_draw_string(px + 20 + ((32 - tw) / 2), item_y + 11, "DIR", 0xFFAAAAAA);

                // Folder Name
                nykon_draw_string(px + 60, item_y + 11, fm_files[idx].name, 0xFFFFFFFF);

                // Arrow
                nykon_draw_string(px + pw - 30, item_y + 11, ">", 0xFF888888);
            } else if (has_gb_extension(fm_files[idx].name)) {
                // GB ROM badge
                nykon_draw_rounded_rect(px + 20, item_y + 10, 32, 24, 6, 0xFF3D3D3D);
                int tw = nykon_get_string_width("GB", NULL);
                nykon_draw_string(px + 20 + ((32 - tw) / 2), item_y + 11, "GB", 0xFFFFFFFF);

                // Game Name
                nykon_draw_string(px + 60, item_y + 11, fm_files[idx].name, 0xFFFFFFFF);

                // File size
                format_size_kb(sz_buf, fm_files[idx].size);
                nykon_draw_string(px + pw - 148, item_y + 11, sz_buf, 0xFF808080);

                // [ Import ] pill button
                int imp_btn_w = 64;
                int imp_btn_h = 24;
                int imp_btn_x = px + pw - 18 - imp_btn_w;
                int imp_btn_y = item_y + 10;
                nykon_draw_rounded_rect(imp_btn_x, imp_btn_y, imp_btn_w, imp_btn_h, 12, 0xFF353535);
                int imp_tw = nykon_get_string_width("Import", NULL);
                nykon_draw_string(imp_btn_x + ((imp_btn_w - imp_tw) / 2), imp_btn_y + 2, "Import", 0xFFFFFFFF);
            } else {
                // Non-GB File badge
                nykon_draw_rounded_rect(px + 20, item_y + 10, 32, 24, 6, 0xFF222222);
                int tw = nykon_get_string_width("FIL", NULL);
                nykon_draw_string(px + 20 + ((32 - tw) / 2), item_y + 11, "FIL", 0xFF777777);

                // Name
                nykon_draw_string(px + 60, item_y + 11, fm_files[idx].name, 0xFF888888);

                // Size
                format_size_kb(sz_buf, fm_files[idx].size);
                nykon_draw_string(px + pw - 70, item_y + 11, sz_buf, 0xFF666666);
            }

            item_y += 50;
        }

        if (fm_count == 0) {
            nykon_draw_string(px + 24, py + 120, "No files found in this folder.", 0xFF808080);
        }
        return;
    }

    // ===================================================
    // STATE 3: MODERN PURE NEUTRAL GRAY GAME BOY EMULATOR
    // ===================================================
    if (app_state == STATE_PLAYING) {
        // 1. Pure Neutral Gray Console Body (R=22, G=22, B=22)
        nykon_draw_rect(px, py + 20, pw, ph - 20, 0xFF161616);

        // 2. LCD Bezel (Deep Black R=10, G=10, B=10)
        nykon_draw_rounded_rect(px, py + 20, pw, 290, 8, 0xFF0A0A0A);

        // 3. Render 160x144 Game Boy Screen at 2x Scale (320x288) in Monochrome Gray
        int screen_top = py + 21;
        for (int y = 0; y < LCD_HEIGHT; y++) {
            for (int x = 0; x < LCD_WIDTH; x++) {
                unsigned int col = gb_frame[y][x];
                int draw_x = px + (x * 2);
                int draw_y = screen_top + (y * 2);
                nykon_draw_rect(draw_x, draw_y, 2, 2, col);
            }
        }

        // 4. Top-Left Title & Menu Button
        nykon_draw_filled_circle(px + 16, py + 324, 3, 0xFFFFFFFF); // Power LED (White)
        nykon_draw_string(px + 26, py + 314, "Peanut GB", 0xFFAAAAAA);

        // [ Home ] Rounded Gray Pill Button (R=45, G=45, B=45)
        int home_w = 68;
        int home_h = 24;
        int home_x = px + pw - 16 - home_w;
        int home_y = py + 312;
        nykon_draw_rounded_rect(home_x, home_y, home_w, home_h, 12, 0xFF2D2D2D);
        int home_tw = nykon_get_string_width("Home", NULL);
        nykon_draw_string(home_x + ((home_w - home_tw) / 2), home_y + 2, "Home", 0xFFFFFFFF);

        // 5. Modern Pure Neutral Gray D-Pad
        int dpad_cx = px + 75;
        int dpad_cy = py + 415;
        nykon_draw_rounded_rect(dpad_cx - 36, dpad_cy - 12, 72, 24, 8, 0xFF252525);
        nykon_draw_rounded_rect(dpad_cx - 12, dpad_cy - 36, 24, 72, 8, 0xFF252525);
        nykon_draw_filled_circle(dpad_cx, dpad_cy, 10, 0xFF353535);
        nykon_draw_filled_circle(dpad_cx, dpad_cy, 4, 0xFF181818);

        // 6. Action Buttons A & B (Pixel-perfect centered labels inside buttons)
        int bx = px + 205, by = py + 425;
        int b_pressed = (gb_ctx.direct.joypad_bits.b == 0);
        unsigned int b_col = b_pressed ? 0xFFFFFFFF : 0xFF353535;
        unsigned int b_txt = b_pressed ? 0xFF000000 : 0xFFFFFFFF;
        nykon_draw_filled_circle(bx, by, 22, b_col);
        int bw = nykon_get_string_width("B", NULL);
        nykon_draw_string(bx - (bw / 2), by - 10, "B", b_txt);

        int ax = px + 265, ay = py + 385;
        int a_pressed = (gb_ctx.direct.joypad_bits.a == 0);
        unsigned int a_col = a_pressed ? 0xFFFFFFFF : 0xFF353535;
        unsigned int a_txt = a_pressed ? 0xFF000000 : 0xFFFFFFFF;
        nykon_draw_filled_circle(ax, ay, 22, a_col);
        int aw = nykon_get_string_width("A", NULL);
        nykon_draw_string(ax - (aw / 2), ay - 10, "A", a_txt);

        // 7. Modern Pill Select & Start Buttons (Pixel-perfect centered text inside buttons)
        int sel_pressed = (gb_ctx.direct.joypad_bits.select == 0);
        unsigned int sel_col = sel_pressed ? 0xFFFFFFFF : 0xFF2A2A2A;
        unsigned int sel_txt = sel_pressed ? 0xFF000000 : 0xFFD0D0D0;
        int sel_x = px + 82, sel_y = py + 492, sel_w = 68, sel_h = 24;
        nykon_draw_rounded_rect(sel_x, sel_y, sel_w, sel_h, 12, sel_col);
        int sel_txt_w = nykon_get_string_width("SELECT", NULL);
        nykon_draw_string(sel_x + ((sel_w - sel_txt_w) / 2), sel_y + 2, "SELECT", sel_txt);

        int str_pressed = (gb_ctx.direct.joypad_bits.start == 0);
        unsigned int str_col = str_pressed ? 0xFFFFFFFF : 0xFF2A2A2A;
        unsigned int str_txt = str_pressed ? 0xFF000000 : 0xFFD0D0D0;
        int str_x = px + 170, str_y = py + 492, str_w = 68, str_h = 24;
        nykon_draw_rounded_rect(str_x, str_y, str_w, str_h, 12, str_col);
        int str_txt_w = nykon_get_string_width("START", NULL);
        nykon_draw_string(str_x + ((str_w - str_txt_w) / 2), str_y + 2, "START", str_txt);
    }
}

NykonApp gameboy_app = {
    "Peanut GB",
    "apps/gameboy/icon.png",
    0xFF303030,
    gb_app_init,
    gb_app_update,
    gb_app_draw
};
