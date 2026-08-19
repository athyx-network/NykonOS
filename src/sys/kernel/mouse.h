#ifndef MOUSE_H
#define MOUSE_H

void mouse_init();
int mouse_poll(int *dx, int *dy, int *left_click);

#endif
