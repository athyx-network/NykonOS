#include "../../sys/nykon_api.h"

extern NykonApp flappy_app;
extern NykonApp terminal_app;
extern NykonApp gameboy_app;
extern NykonApp app_photos;

NykonApp* registered_apps[32] = {
    &flappy_app,
    &terminal_app,
    &gameboy_app,
    &app_photos
};

int num_registered_apps = 4;
