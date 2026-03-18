#pragma once

#include "app_config.h"
#include "lvgl.h"

void voice_ui_build(lv_obj_t *parent);
void voice_ui_enter();
void voice_ui_exit();
void voice_ui_set_state(VoiceState state);
void voice_ui_set_transcript(const char *text, bool is_user);
void voice_ui_tick();
