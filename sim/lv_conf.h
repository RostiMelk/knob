#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_USE_DEV_VERSION

/* Color depth: 16 = RGB565 to match hardware */
#define LV_COLOR_DEPTH 16

/* Memory */
#define LV_MEM_CUSTOM 0
#define LV_MEM_SIZE (2 * 1024 * 1024)

/* Display */
#define LV_DPI_DEF 200

/* Drawing */
#define LV_DRAW_BUF_STRIDE_ALIGN 1
#define LV_DRAW_BUF_ALIGN 4
#define LV_USE_DRAW_SW 1

/* OS / tick */
#define LV_USE_OS LV_OS_NONE
#define LV_TICK_CUSTOM 1
#define LV_TICK_CUSTOM_INCLUDE <SDL2/SDL.h>
#define LV_TICK_CUSTOM_SYS_TIME_EXPR ((uint32_t)SDL_GetTicks())

/* SDL driver */
#define LV_USE_SDL 1
#define LV_SDL_WINDOW_TITLE "Sonos Radio Simulator"
#define LV_SDL_INCLUDE_PATH <SDL2/SDL.h>
#define LV_SDL_MOUSEWHEEL_MODE LV_SDL_MOUSEWHEEL_MODE_ENCODER
#define LV_SDL_BUF_COUNT 2

/* Logging */
#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1

/* Fonts — match what we use in UI code */
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_48 1
#define LV_FONT_DEFAULT &lv_font_montserrat_16

/* Symbols */
#define LV_USE_FONT_PLACEHOLDER 1

/* Widgets */
#define LV_USE_ARC 1
#define LV_USE_LABEL 1
#define LV_USE_BTN 1
#define LV_USE_OBJ 1
#define LV_USE_SLIDER 0
#define LV_USE_SWITCH 0
#define LV_USE_DROPDOWN 0
#define LV_USE_ROLLER 0
#define LV_USE_TEXTAREA 1
#define LV_USE_TABLE 0
#define LV_USE_CHECKBOX 0
#define LV_USE_CHART 0
#define LV_USE_BAR 0
#define LV_USE_CALENDAR 0
#define LV_USE_CANVAS 0
#define LV_USE_LINE 0
#define LV_USE_MENU 0
#define LV_USE_MSGBOX 0
#define LV_USE_SPINNER 0
#define LV_USE_TABVIEW 0
#define LV_USE_TILEVIEW 0
#define LV_USE_WIN 0
#define LV_USE_SPAN 0
#define LV_USE_SPINBOX 0
#define LV_USE_IMAGEBUTTON 0
#define LV_USE_LED 0
#define LV_USE_LIST 0
#define LV_USE_KEYBOARD 0
#define LV_USE_ANIMIMG 0
#define LV_USE_BTNMATRIX 0
#define LV_USE_IMAGE 1
#define LV_USE_SCALE 0

/* Layouts */
#define LV_USE_FLEX 1
#define LV_USE_GRID 0

/* Themes */
#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 1

/* Animations */
#define LV_USE_ANIM 1

/* Scroll */
#define LV_USE_SCROLL 1

/* Asserts — on for dev */
#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1
#define LV_USE_ASSERT_OBJ 1

/* Unused features — off */
#define LV_USE_SNAPSHOT 0
#define LV_USE_SYSMON 0
#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR 0
#define LV_USE_PROFILER 0
#define LV_USE_MONKEY 0
#define LV_USE_GRIDNAV 0
#define LV_USE_FRAGMENT 0
#define LV_USE_OBSERVER 1
#define LV_USE_IME_PINYIN 0
#define LV_USE_FILE_EXPLORER 0

/* Filesystem — POSIX for loading PNGs from disk */
#define LV_USE_FS_STDIO 0
#define LV_USE_FS_POSIX 1
#define LV_FS_POSIX_LETTER 'A'
#define LV_FS_POSIX_PATH ""
#define LV_FS_POSIX_CACHE_SIZE 0

/* Image decoders — PNG for station logos */
#define LV_USE_LODEPNG 1
#define LV_USE_BMP 0
#define LV_USE_GIF 0
#define LV_USE_TJPGD 0

#endif /* LV_CONF_H */
