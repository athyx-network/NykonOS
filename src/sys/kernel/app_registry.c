#include "../../sys/nykon_api.h"

extern NykonApp flappy_app;
extern NykonApp terminal_app;

NykonApp* registered_apps[] = {
    &flappy_app,
    &terminal_app
};

int num_registered_apps = 2;
