#include "lvgl.h"
#include "ui/display.h"

void display_init(lv_display_t **disp, lv_indev_t **touch) {
  *disp = lv_sdl_window_create(360, 360);
  lv_display_set_default(*disp);
  *touch = lv_sdl_mouse_create();
}

bool display_lock(int) { return true; }

void display_unlock() {}
