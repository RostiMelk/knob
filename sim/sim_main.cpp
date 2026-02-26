#include "app_config.h"
#include "ui/ui.h"

#include "lvgl.h"
#include <SDL2/SDL.h>
#include <cstdio>
#include <cstdlib>

ESP_EVENT_DEFINE_BASE(APP_EVENT);

static bool s_running = true;

static int sdl_event_watch(void *, SDL_Event *event) {
  if (event->type == SDL_QUIT) {
    s_running = false;
    return 0;
  }

  if (event->type == SDL_KEYDOWN && event->key.repeat == 0) {
    switch (event->key.keysym.sym) {
    case SDLK_UP:
      ui_on_encoder_rotate(1);
      break;
    case SDLK_DOWN:
      ui_on_encoder_rotate(-1);
      break;
    case SDLK_RETURN:
    case SDLK_SPACE:
      ui_on_touch_tap();
      break;
    case SDLK_l:
      ui_on_touch_long_press();
      break;
    case SDLK_q:
    case SDLK_ESCAPE:
      s_running = false;
      break;
    default:
      break;
    }
  }

  if (event->type == SDL_MOUSEWHEEL && event->wheel.y != 0) {
    ui_on_encoder_rotate(event->wheel.y > 0 ? 1 : -1);
  }

  return 1;
}

int main(int, char **) {
  SDL_SetHint(SDL_HINT_VIDEODRIVER, "x11,wayland,cocoa,windows");

  lv_init();
  ui_init();

  ui_set_wifi_status(true);
  ui_set_speaker_name("Living Room");
  ui_set_play_state(PlayState::Stopped);

  SDL_AddEventWatch(sdl_event_watch, nullptr);

  printf("──────────────────────────────────────\n");
  printf("  Sonos Radio Simulator (360x360)\n");
  printf("──────────────────────────────────────\n");
  printf("  Up/Down     Encoder rotate\n");
  printf("  Enter/Space Tap (play / browse / select)\n");
  printf("  L           Long press (stop)\n");
  printf("  Mouse click Tap\n");
  printf("  Scroll      Encoder rotate\n");
  printf("  Q/Esc       Quit\n");
  printf("──────────────────────────────────────\n");

  while (s_running) {
    uint32_t delay = lv_timer_handler();
    if (delay < 1)
      delay = 1;
    if (delay > 16)
      delay = 16;
    SDL_Delay(delay);
  }

  return 0;
}
