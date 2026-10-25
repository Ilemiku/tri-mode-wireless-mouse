#ifndef MOUSE_LV_CONF_H
#define MOUSE_LV_CONF_H
#define LV_CONF_H
/* 固定LVGL 8.4配置。64KiB SRAM中不用全屏帧缓冲；图形堆与RTOS堆分开预算。 */
#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0
#define LV_MEM_CUSTOM 0
#define LV_MEM_SIZE (20U * 1024U)
#define LV_MEM_BUF_MAX_NUM 8
#define LV_DISP_DEF_REFR_PERIOD 30
#define LV_INDEV_DEF_READ_PERIOD 20
#define LV_DPI_DEF 130
#define LV_TICK_CUSTOM 1
#define LV_TICK_CUSTOM_INCLUDE "systick.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (g_ms_tick)
#define LV_DRAW_COMPLEX 0
#define LV_USE_LOG 0
#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1
#define LV_ASSERT_HANDLER_INCLUDE "app_ui.h"
#define LV_ASSERT_HANDLER App_UI_Fault();
#define LV_FONT_MONTSERRAT_14 0
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_DEFAULT &lv_font_montserrat_16
#define LV_USE_THEME_DEFAULT 0
#define LV_USE_THEME_BASIC 1
#define LV_USE_THEME_MONO 0
#define LV_USE_FLEX 0
#define LV_USE_GRID 0
/* 首版只编入按钮和标签，禁用未使用控件。需要新控件时再按RAM预算启用。 */
#define LV_USE_ARC 0
#define LV_USE_ANIMIMG 0
#define LV_USE_BAR 0
#define LV_USE_BTN 1
#define LV_USE_BTNMATRIX 0
#define LV_USE_CANVAS 0
#define LV_USE_CHECKBOX 0
#define LV_USE_DROPDOWN 0
#define LV_USE_IMG 0
#define LV_USE_LABEL 1
#define LV_USE_LINE 0
#define LV_USE_ROLLER 0
#define LV_USE_SLIDER 0
#define LV_USE_SWITCH 0
#define LV_USE_TABLE 0
#define LV_USE_TEXTAREA 0
#define LV_USE_CALENDAR 0
#define LV_USE_CHART 0
#define LV_USE_COLORWHEEL 0
#define LV_USE_IMGBTN 0
#define LV_USE_KEYBOARD 0
#define LV_USE_LED 0
#define LV_USE_LIST 0
#define LV_USE_MENU 0
#define LV_USE_METER 0
#define LV_USE_MSGBOX 0
#define LV_USE_SPAN 0
#define LV_USE_SPINBOX 0
#define LV_USE_SPINNER 0
#define LV_USE_TABVIEW 0
#define LV_USE_TILEVIEW 0
#define LV_USE_WIN 0
#endif
