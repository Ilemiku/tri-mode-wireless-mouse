#include "app_ui.h"
#include "FreeRTOS.h"
#include "task.h"
#include "lvgl.h"
#include "lcd_bus.h"
#include "lcd_st7796.h"
#include "touch_gt9xx.h"
#include "touch_xpt2046.h"
#include "esp32_link.h"
#include "systick.h"
#include "ui_fonts.h"
#include "ui_texts.h"
#include <stdio.h>

/* 阅读顺序：App_UI_Start -> ui_main -> 页面事件/触摸 -> flush/finish_flush。
 * 界面只提交DPI请求，Input执行并读回后才更新实际值。
 * 全部常规LVGL API只在本任务调用：不让网络、Input或ISR并发操作控件。
 * 单个8行像素缓冲7680字节，屏幕自身GRAM保存整帧，ZE无需全屏帧缓冲。
 */
static lv_color_t pixels[LCD_WIDTH * 8];
static lv_disp_draw_buf_t draw_buffer;
static lv_disp_drv_t display_driver;
static TaskHandle_t ui_task;
static lv_obj_t *dpi_label, *mode_label, *status_label, *buttons[4];
static const u16 dpi_presets[] = {800, 1600, 3200, 6400};
static u32 request_serial, pending_id, pending_start;
static u16 shown_dpi;
static u8 shown_mode = 255;
static lv_disp_drv_t *pending_flush;
static volatile u8 dma_success;
static lv_obj_t *calibration_layer, *cross_h, *cross_v, *calibration_hint, *calibration_count;
static lv_obj_t *settings_screen;
volatile u32 g_ui_calibration_visual;
static const u16 targets[4][2] = {{40, 70}, {440, 70}, {40, 240}, {240, 160}};
static u16 raw_points[3][2];
static s32 dx1, dy1, dx2, dy2, determinant;
static u32 sample_x, sample_y;
static u8 sample_count;
static lv_indev_drv_t pointer_driver;
volatile AppUiStats g_ui_stats;

static void touch_read(lv_indev_drv_t *driver, lv_indev_data_t *data);

static void register_pointer(void)
{
    lv_indev_drv_init(&pointer_driver);
    pointer_driver.type = LV_INDEV_TYPE_POINTER;
    pointer_driver.read_cb = touch_read;
    if (!lv_indev_drv_register(&pointer_driver))
        App_UI_Fault();
}

/* 三点仿射映射自动处理坐标轴交换、反向和比例；第四点只验证，不强行通过。
 * 64位中间量避免12位ADC差值乘屏幕跨度时溢出。 */
static void raw_to_pixel(u16 raw_x, u16 raw_y, s32 *x, s32 *y)
{
    s32 rx = (s32)raw_x - raw_points[0][0], ry = (s32)raw_y - raw_points[0][1];
    *x = 40 + (s32)(((int64_t)rx * dy2 - (int64_t)ry * dx2) * 400 / determinant);
    *y = 70 + (s32)(((int64_t)ry * dx1 - (int64_t)rx * dy1) * 170 / determinant);
}

/* 总线层在DMA IRQ调用此桥接函数；只记录结果并通知UI，不绘图。
 * 此任务的通知专门留给LCD DMA，不和其他用途混用。 */
static void dma_done(u8 success)
{
    BaseType_t wake = pdFALSE;
    dma_success = success;
    vTaskNotifyGiveFromISR(ui_task, &wake);
    portYIELD_FROM_ISR(wake);
}

static void finish_flush(lv_disp_drv_t *driver)
{
    if (!pending_flush)
        return;
    /* CPU在等DMA时阻塞，Input/Transfer仍可执行。超时必须中止搬运后才复用缓冲。 */
    if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50)) == 0)
    {
        LCD_Bus_DmaAbort();
        g_ui_stats.dma_timeouts++;
    }
    else if (!dma_success)
        g_ui_stats.dma_failures++;
    pending_flush = 0;
    lv_disp_flush_ready(driver);
}

static void flush(lv_disp_drv_t *driver, const lv_area_t *area, lv_color_t *color)
{
    u32 count = (u32)(area->x2 - area->x1 + 1) * (u32)(area->y2 - area->y1 + 1);
    (void)ulTaskNotifyTake(pdTRUE, 0); /* 丢弃已完成/中止的旧通知，防止误认新一轮已搬完 */
    LCD_ST7796_Window((u16)area->x1, (u16)area->y1, (u16)area->x2, (u16)area->y2);
    dma_success = 0;
    pending_flush = driver;
    if (count > 65535U || !LCD_Bus_DmaStart((const u16 *)color, (u16)count, dma_done))
    {
        pending_flush = 0;
        g_ui_stats.dma_failures++;
        lv_disp_flush_ready(driver);
    }
    else
        g_ui_stats.flushes++;
}

static void touch_read(lv_indev_drv_t *driver, lv_indev_data_t *data)
{
    TouchPoint point = {0};
    (void)driver;
    if (g_ui_stats.touch_backend == 1)
        Touch_GT_Read(&point);
    else if (g_ui_stats.calibrated)
    {
        u16 raw_x, raw_y;
        if (Touch_XPT_ReadRaw(&raw_x, &raw_y))
        {
            s32 x, y;
            raw_to_pixel(raw_x, raw_y, &x, &y);
            point.x = (u16)(x < 0 ? 0 : (x > 479 ? 479 : x));
            point.y = (u16)(y < 0 ? 0 : (y > 319 ? 319 : y));
            point.pressed = 1;
        }
    }
    data->point.x = point.x;
    data->point.y = point.y;
    data->state = point.pressed ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
}

static void clicked(lv_event_t *event)
{
    u16 dpi = (u16)(u32)lv_event_get_user_data(event);
    if (pending_id)
        return;
    request_serial++;
    if (!request_serial)
        request_serial++;
    if (Mouse_SettingsLocalRequest(request_serial, 2, dpi))
    {
        pending_id = request_serial;
        pending_start = g_ms_tick;
        g_ui_stats.local_requests++;
        lv_label_set_text(status_label, UI_TXT_APPLYING);
    }
    else
        lv_label_set_text(status_label, UI_TXT_BUSY);
}

static lv_obj_t *label(lv_obj_t *parent, const char *text, int x, int y, const lv_font_t *font,
                       u32 color)
{
    lv_obj_t *object = lv_label_create(parent);
    lv_label_set_text(object, text);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_style_text_font(object, font, 0);
    lv_obj_set_style_text_color(object, lv_color_hex(color), 0);
    return object;
}

static void move_cross(void)
{
    u8 step = g_ui_stats.calibration_step;
    char text[12];
    lv_obj_set_pos(cross_h, targets[step][0] - 15, targets[step][1] - 2);
    lv_obj_set_pos(cross_v, targets[step][0] - 2, targets[step][1] - 15);
    sprintf(text, "%u / 4", (unsigned)(step + 1));
    lv_label_set_text(calibration_count, text);
}

static void begin_calibration(void)
{
    /* 独立校准screen，不再把两页文字叠在透明top layer上。
     * LVGL仅启用basic主题的编译开关并不会自动装载该主题；本界面显式设置样式。 */
    settings_screen = lv_scr_act();
    calibration_layer = lv_obj_create(NULL);
    lv_obj_set_pos(calibration_layer, 0, 0);
    lv_obj_set_size(calibration_layer, 480, 320);
    lv_obj_set_style_pad_all(calibration_layer, 0, 0);
    lv_obj_set_style_border_width(calibration_layer, 0, 0);
    lv_obj_set_style_bg_color(calibration_layer, lv_color_hex(0x101A2B), 0);
    lv_obj_set_style_bg_opa(calibration_layer, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(calibration_layer, 0, 0);
    lv_obj_clear_flag(calibration_layer, LV_OBJ_FLAG_SCROLLABLE);
    label(calibration_layer, UI_TXT_CAL_TITLE, 18, 14, &ui_font_20, 0x66DCC0);
    calibration_count =
        label(calibration_layer, "1 / 4", 360, 18, &lv_font_montserrat_20, 0x91A4BA);
    calibration_hint = label(calibration_layer, UI_TXT_CAL_STEP, 18, 270, &ui_font_16, 0xE8F2FF);
    lv_obj_set_width(calibration_hint, 444);
    lv_label_set_long_mode(calibration_hint, LV_LABEL_LONG_WRAP);
    cross_h = lv_obj_create(calibration_layer);
    cross_v = lv_obj_create(calibration_layer);
    lv_obj_set_size(cross_h, 31, 5);
    lv_obj_set_size(cross_v, 5, 31);
    lv_obj_set_style_bg_color(cross_h, lv_color_hex(0xFFD54F), 0);
    lv_obj_set_style_bg_color(cross_v, lv_color_hex(0xFFD54F), 0);
    lv_obj_set_style_bg_opa(cross_h, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(cross_v, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(cross_h, 0, 0);
    lv_obj_set_style_radius(cross_v, 0, 0);
    lv_obj_set_style_border_width(cross_h, 0, 0);
    lv_obj_set_style_border_width(cross_v, 0, 0);
    g_ui_stats.calibration_step = 0;
    move_cross();
    lv_scr_load(calibration_layer);
    lv_obj_update_layout(calibration_layer);
    /* 可读诊断：低8位校准背景透明度，次8位十字透明度，bit16/17为两条线是否可见。 */
    g_ui_calibration_visual = (u32)lv_obj_get_style_bg_opa(calibration_layer, 0) |
                              ((u32)lv_obj_get_style_bg_opa(cross_h, 0) << 8) |
                              ((u32)lv_obj_is_visible(cross_h) << 16) |
                              ((u32)lv_obj_is_visible(cross_v) << 17);
}

static void calibration_poll(void)
{
    u16 x, y;
    u8 step = g_ui_stats.calibration_step;
    if (Touch_XPT_ReadRaw(&x, &y))
    {
        if (sample_count < 50)
        {
            sample_x += x;
            sample_y += y;
            sample_count++;
        }
        return;
    }
    if (g_xpt_pressed || !sample_count)
        return;
    if (sample_count >= 3)
    {
        x = (u16)(sample_x / sample_count);
        y = (u16)(sample_y / sample_count);
        if (step < 3)
        {
            raw_points[step][0] = x;
            raw_points[step][1] = y;
            g_ui_stats.calibration_step++;
            lv_label_set_text(calibration_hint, g_ui_stats.calibration_step == 3 ? UI_TXT_CAL_CHECK
                                                                                 : UI_TXT_CAL_STEP);
            move_cross();
        }
        else
        {
            s32 px = 0, py = 0;
            dx1 = (s32)raw_points[1][0] - raw_points[0][0];
            dy1 = (s32)raw_points[1][1] - raw_points[0][1];
            dx2 = (s32)raw_points[2][0] - raw_points[0][0];
            dy2 = (s32)raw_points[2][1] - raw_points[0][1];
            determinant = dx1 * dy2 - dy1 * dx2;
            if (determinant > 100000 || determinant < -100000)
                raw_to_pixel(x, y, &px, &py);
            if (px >= 215 && px <= 265 && py >= 135 && py <= 185)
            {
                g_ui_stats.calibrated = 1;
                g_ui_stats.touch_ready = 1;
                lv_scr_load(settings_screen);
                lv_obj_del(calibration_layer);
                calibration_layer = 0;
                lv_label_set_text(status_label, UI_TXT_CAL_READY);
                register_pointer();
            }
            else
            {
                g_ui_stats.calibration_step = 0;
                lv_label_set_text(calibration_hint, UI_TXT_CAL_RETRY);
                move_cross();
            }
        }
    }
    sample_count = 0;
    sample_x = sample_y = 0;
}

static void create_page(void)
{
    lv_obj_t *screen = lv_scr_act(), *card;
    u8 i;
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x101A2B), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(screen, 0, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    label(screen, UI_TXT_TITLE, 18, 14, &ui_font_20, 0x66DCC0);
    card = lv_obj_create(screen);
    lv_obj_set_pos(card, 16, 50);
    lv_obj_set_size(card, 448, 105);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x18283C), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_radius(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    label(card, UI_TXT_ACTUAL_DPI, 0, 0, &ui_font_16, 0x91A4BA);
    dpi_label = label(card, "-- DPI", 0, 28, &lv_font_montserrat_28, 0xE8F2FF);
    mode_label = label(card, UI_TXT_MODE_USB, 300, 38, &ui_font_20, 0x66DCC0);
    for (i = 0; i < 4; i++)
    {
        char text[8];
        lv_obj_t *caption;
        buttons[i] = lv_btn_create(screen);
        lv_obj_set_pos(buttons[i], 16 + i * 114, 177);
        lv_obj_set_size(buttons[i], 106, 64);
        lv_obj_set_style_radius(buttons[i], 0, 0);
        lv_obj_set_style_bg_color(buttons[i], lv_color_hex(0x24374E), 0);
        lv_obj_set_style_bg_opa(buttons[i], LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(buttons[i], lv_color_hex(0xE8F2FF), 0);
        lv_obj_set_style_border_width(buttons[i], 2, 0);
        lv_obj_set_style_border_opa(buttons[i], LV_OPA_COVER, 0);
        lv_obj_set_style_text_font(buttons[i], &lv_font_montserrat_20, 0);
        lv_obj_add_event_cb(buttons[i], clicked, LV_EVENT_CLICKED, (void *)(u32)dpi_presets[i]);
        sprintf(text, "%u", (unsigned)dpi_presets[i]);
        caption = lv_label_create(buttons[i]);
        lv_label_set_text(caption, text);
        lv_obj_center(caption);
    }
    status_label = label(screen, g_ui_stats.touch_ready ? UI_TXT_HINT : UI_TXT_TOUCH_WAIT, 18, 251,
                         &ui_font_16, 0x91A4BA);
    lv_obj_set_width(status_label, 444);
    lv_label_set_long_mode(status_label, LV_LABEL_LONG_WRAP);
    label(screen, UI_TXT_FOOTER, 18, 294, &ui_font_16, 0x66DCC0);
}

static void update_page(void)
{
    MouseSettingsState state;
    char text[32];
    u8 i;
    Mouse_SettingsGetState(&state);
    if (state.updated_ms && shown_dpi != state.dpi)
    {
        shown_dpi = state.dpi;
        sprintf(text, "%u DPI", (unsigned)state.dpi);
        lv_label_set_text(dpi_label, text);
        for (i = 0; i < 4; i++)
            lv_obj_set_style_border_color(
                buttons[i], lv_color_hex(dpi_presets[i] == state.dpi ? 0x66DCC0 : 0x34445C), 0);
    }
    if (shown_mode != state.mode)
    {
        static const char *names[] = {UI_TXT_MODE_USB, UI_TXT_MODE_RF, UI_TXT_MODE_BLE};
        shown_mode = state.mode;
        lv_label_set_text(mode_label, state.mode < 3 ? names[state.mode] : "?");
    }
    if (pending_id && state.local_id == pending_id)
    {
        pending_id = 0;
        lv_label_set_text(status_label, state.local_status == 0 ? UI_TXT_APPLIED : UI_TXT_FAILED);
    }
    else if (pending_id && (u32)(g_ms_tick - pending_start) > 2000)
    {
        pending_id = 0;
        lv_label_set_text(status_label, UI_TXT_TIMEOUT);
    }
}

static void ui_main(void *argument)
{
    u32 last_update = 0, last_stack = 0;
    (void)argument;
    g_ui_stats.stage = 1;
    g_ui_stats.lcd_id = LCD_ST7796_Init();
    if (g_ui_stats.lcd_id != 0x7796)
    {
        g_ui_stats.stage = 0xE1;
        vTaskSuspend(NULL);
    }
    g_ui_stats.stage = 2;
    g_ui_stats.touch_ready = Touch_GT_Init();
    if (g_ui_stats.touch_ready)
        g_ui_stats.touch_backend = 1;
    else if (Touch_XPT_Init())
        g_ui_stats.touch_backend = 2;
    lv_init();
    lv_disp_draw_buf_init(&draw_buffer, pixels, NULL, LCD_WIDTH * 8);
    lv_disp_drv_init(&display_driver);
    display_driver.hor_res = LCD_WIDTH;
    display_driver.ver_res = LCD_HEIGHT;
    display_driver.draw_buf = &draw_buffer;
    display_driver.flush_cb = flush;
    display_driver.wait_cb = finish_flush;
    if (!lv_disp_drv_register(&display_driver))
        App_UI_Fault();
    if (g_ui_stats.touch_ready)
    {
        register_pointer();
    }
    create_page();
    g_ui_stats.stage = 3;
    if (g_ui_stats.touch_backend == 2)
        begin_calibration();
    for (;;)
    {
        /* 电阻式坐标先以原始量验证。未校准前绝不把ADC值当像素触发设置按钮。 */
        if (calibration_layer)
            calibration_poll();
        if ((u32)(g_ms_tick - last_update) >= 100)
        {
            last_update = g_ms_tick;
            update_page();
        }
        (void)lv_timer_handler();
        /* 最后一块DMA也可能在lv_timer_handler返回后仍在搬；及时回收，不能等下次重绘才处理。 */
        if (pending_flush)
            finish_flush(pending_flush);
        g_ui_stats.cycles++;
        if ((u32)(g_ms_tick - last_stack) >= 1000)
        {
            lv_mem_monitor_t memory;
            last_stack = g_ms_tick;
            g_ui_stats.stack_free = uxTaskGetStackHighWaterMark(NULL);
            lv_mem_monitor(&memory);
            g_ui_stats.lv_free = (u32)memory.free_size;
            g_ui_stats.lv_biggest = (u32)memory.free_biggest_size;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void App_UI_Start(void)
{
    configASSERT(xTaskCreate(ui_main, "UI", 1024, NULL, 1, &ui_task) == pdPASS);
}

void App_UI_Fault(void)
{
    g_ui_stats.faults++;
    g_ui_stats.stage = 0xEE;
    LCD_Bus_DmaAbort();
    LCD_Bus_Backlight(0);
    vTaskSuspend(NULL);
    for (;;)
    {
    } /* UI故障停本任务，不让整个鼠标因图形库断言卡死 */
}
