#include "../../sys/nykon_api.h"

extern NykonApp flappy_app;
extern NykonApp stress_app;

NykonApp* registered_apps[] = {
    &flappy_app,
    &stress_app
};

int num_registered_apps = 2;
