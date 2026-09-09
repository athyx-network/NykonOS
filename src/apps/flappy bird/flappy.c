#include "../../sys/nykon_api.h"

static int state = 0; // 0 = Menu, 1 = Playing, 2 = Game Over
static int bird_y = 270 << 8;
static int velocity = 0;
static int gravity = 30;      // slower fall
static int jump_power = -800; // higher jump

static int pipe_x = 320;
static int pipe_gap_y = 270;
static int pipe_w = 120; // wider pipe
static int pipe_gap_h = 130;
static int pipe_speed = 3; // even slower pipe movement

static int score = 0;
static unsigned int last_time = 0;

static int rand_seed = 12345;
static int get_rand() {
  rand_seed = rand_seed * 1103515245 + 12345;
  return (unsigned int)(rand_seed / 65536) % 32768;
}

static void reset_game() {
  bird_y = 270 << 8;
  velocity = 0;
  pipe_x = 320;
  pipe_gap_y = 150 + (get_rand() % 240);
  score = 0;
}

static void flappy_init(void) {
  state = 0;
  reset_game();
}

static int pending_click = 0;

static void flappy_update(void) {
  int phone_x, phone_y, phone_w, phone_h;
  nykon_get_screen_bounds(&phone_x, &phone_y, &phone_w, &phone_h);

  int mx, my, mclick;
  nykon_get_mouse(&mx, &my, &mclick);

  // Jump if clicking anywhere on the screen
  static int last_click = 0;
  if (mclick && !last_click) {
    pending_click = 1;
  }
  last_click = mclick;

  // Simple frame delay (reduced drastically for better FPS)
  for (volatile int i = 0; i < 5000; i++) {
  }

  int clicked = pending_click;
  pending_click = 0;

  if (state == 0) { // Menu
    if (clicked) {
      state = 1;
      reset_game();
      velocity = jump_power;
    }
  } else if (state == 1) { // Playing
    if (clicked) {
      velocity = jump_power;
    }

    velocity += gravity;
    bird_y += velocity;

    pipe_x -= pipe_speed;

    if (pipe_x < -pipe_w) {
      pipe_x = phone_w;
      pipe_gap_y = 100 + (get_rand() % 300);
      score++;
    }

    // Collisions
    int by = bird_y >> 8;
    if (by > phone_h - 15 || by < 0) {
      state = 2; // Game over
    }

    int angle = (velocity * 45) / 600;
    if (angle < -35)
      angle = -35;
    if (angle > 45)
      angle = 45;

    int bird_x = 100;
    int top_pipe_bottom = pipe_gap_y - (pipe_gap_h / 2);
    int bottom_pipe_top = pipe_gap_y + (pipe_gap_h / 2);

    int col_top = nykon_check_pixel_collision(
        "apps/flappy bird/character.png", phone_x + bird_x, phone_y + by, angle,
        "apps/flappy bird/pipe_down_sprite.png", phone_x + pipe_x,
        phone_y + top_pipe_bottom - 500, 0xFFFF00FF);
    int col_bot = nykon_check_pixel_collision(
        "apps/flappy bird/character.png", phone_x + bird_x, phone_y + by, angle,
        "apps/flappy bird/pipe_up_sprite.png", phone_x + pipe_x,
        phone_y + bottom_pipe_top, 0xFFFF00FF);

    if (col_top || col_bot) {
      state = 2; // Game over
    }
  } else if (state == 2) { // Game Over
    if (clicked) {
      state = 0;
    }
  }

  nykon_request_redraw();
}

static void flappy_draw(void) {
  int phone_x, phone_y, phone_w, phone_h;
  nykon_get_screen_bounds(&phone_x, &phone_y, &phone_w, &phone_h);

  // Background inside phone frame
  nykon_draw_rect(phone_x, phone_y, phone_w, phone_h, RGB(135, 206, 235));

  // Draw pipes
  int top_pipe_bottom = pipe_gap_y - (pipe_gap_h / 2);
  int bottom_pipe_top = pipe_gap_y + (pipe_gap_h / 2);

  // Top pipe (pipe_down_sprite.png)
  nykon_draw_sprite("apps/flappy bird/pipe_down_sprite.png", phone_x + pipe_x,
                    phone_y + top_pipe_bottom - 500, 0xFFFF00FF);
  // Bottom pipe (pipe_up_sprite.png)
  nykon_draw_sprite("apps/flappy bird/pipe_up_sprite.png", phone_x + pipe_x,
                    phone_y + bottom_pipe_top, 0xFFFF00FF);

  // Cap velocity (terminal velocity)
  if (velocity > 600)
    velocity = 600; // slower terminal velocity

  // Calculate smooth tilt angle (limit max downward tilt to 45 degrees)
  int angle = (velocity * 45) / 600;
  if (angle < -35)
    angle = -35;
  if (angle > 45)
    angle = 45;

  // Draw bird sprite with smooth rotation
  int cx = phone_x + 100;
  int cy = phone_y + (bird_y >> 8);
  nykon_draw_sprite_rotated("apps/flappy bird/character.png", cx, cy, angle,
                            0xFFFF00FF);

  // UI
  char score_str[16];
  score_str[0] = '0' + (score / 10);
  score_str[1] = '0' + (score % 10);
  score_str[2] = '\0';
  if (score < 10) {
    score_str[0] = '0' + score;
    score_str[1] = '\0';
  }

  if (state == 0) {
    nykon_draw_string_scaled(phone_x + 20, phone_y + 150, "FLAPPY BIRD",
                             RGB(255, 255, 255), 3);
    nykon_draw_string(phone_x + 90, phone_y + 250, "Click to start",
                      RGB(255, 255, 255));
  } else if (state == 1) {
    nykon_draw_string_scaled(phone_x + 130, phone_y + 50, score_str,
                             RGB(255, 255, 255), 4);
  } else if (state == 2) {
    nykon_draw_string_scaled(phone_x + 30, phone_y + 150, "GAME OVER",
                             RGB(255, 0, 0), 4);
    nykon_draw_string_scaled(phone_x + 130, phone_y + 220, score_str,
                             RGB(255, 255, 255), 4);
    nykon_draw_string(phone_x + 70, phone_y + 320, "Click to continue",
                      RGB(255, 255, 255));
  }

  // Exit button logic has been removed since the global Home button handles
  // exiting now.
}

NykonApp flappy_app = {"Flappy Bird",  "apps/flappy bird/icon.png",
                       RGB(0, 200, 0), flappy_init,
                       flappy_update,  flappy_draw};
