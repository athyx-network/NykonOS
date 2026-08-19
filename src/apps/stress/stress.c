#include "../../sys/nykon_api.h"

static int rect_count = 100;
static int offset = 0;
static int x_bounds, y_bounds, w_bounds, h_bounds;

static void stress_init() {
    rect_count = 100;
    offset = 0;
    nykon_get_screen_bounds(&x_bounds, &y_bounds, &w_bounds, &h_bounds);
}

static void stress_update() {
    int cx, cy, clicked;
    nykon_get_mouse(&cx, &cy, &clicked);
    
    if (clicked) {
        rect_count += 500;
    }
    
    offset++;
    
    // Always request redraw to keep stressing
    nykon_request_redraw();
}

static int custom_mod(int a, int b) {
    if (b <= 0) return 0;
    while (a >= b) {
        a -= b;
    }
    return a;
}

static void stress_draw() {
    // Clear background
    nykon_draw_rect(x_bounds, y_bounds, w_bounds, h_bounds, RGB(20, 20, 20));
    
    // Draw rectangles to stress the system
    for (int i = 0; i < rect_count; i++) {
        int r_x = x_bounds + custom_mod(offset + (i * 7), w_bounds);
        int r_y = y_bounds + custom_mod(i * 13, h_bounds);
        unsigned int color = RGB((i * 3) & 255, (i * 5) & 255, (i * 7) & 255);
        nykon_draw_rect(r_x, r_y, 8, 8, color);
    }
    
    // UI elements
    nykon_draw_rect(x_bounds, y_bounds, w_bounds, 60, RGB(0, 0, 0));
    nykon_draw_string(x_bounds + 10, y_bounds + 10, "STRESS TEST", RGB(255, 50, 50));
    nykon_draw_string(x_bounds + 10, y_bounds + 30, "Click screen to add 500 rects", RGB(200, 200, 200));
}

NykonApp stress_app = {"Stress Test", RGB(200, 50, 50), stress_init, stress_update, stress_draw};
