#include "key.h"
#include "mouse.h"

typedef struct
{
    GPIO_TypeDef *port;
    u16 pin;
    u8 btn;     // 1~4=报告按键, 0=功能键(不进报告)
    u8 dpi_key; // 1=DPI切换键
} key_map_t;

static u8 key_stable[6]; // 消抖后的稳定态
static u8 key_prev[6];   // 上一次稳定态（找边沿）
static u8 key_cnt[6];    // 连续不一致计数

static const key_map_t key_map[] = {
    {GPIOE, GPIO_PIN_0, MOUSE_BTN_LEFT, 0},   // 左键
    {GPIOE, GPIO_PIN_6, MOUSE_BTN_RIGHT, 0},  // 右键
    {GPIOF, GPIO_PIN_3, MOUSE_BTN_MIDDLE, 0}, // 侧键1 → 中键
    {GPIOE, GPIO_PIN_3, 0, 1},                // DPI键(同时上报键4)
    {GPIOF, GPIO_PIN_4, MOUSE_BTN_BACK, 0},   // 侧键2 暂不接
    {GPIOE, GPIO_PIN_4, 0, 0},                // 模式键 暂不接
};

#define KEY_COUNT (sizeof(key_map) / sizeof(key_map[0]))
#define KEY_DEBOUNCE_THRESHOLD 3 // 3×10ms=30ms

void Key_Init(void)
{
    for (u8 i = 0; i < KEY_COUNT; i++)
    {
        key_stable[i] = 1; // 上拉：松开 = 1
        key_prev[i] = 1;
        key_cnt[i] = 0;
    }
}

void Key_Scan(void)
{
    for (u8 i = 0; i < KEY_COUNT; i++)
    {
        u8 current = (key_map[i].port->IDR & key_map[i].pin) ? 1 : 0; // 读引脚
        if (current == key_stable[i])                                 // 消抖
        {
            key_cnt[i] = 0;
        }
        else
        {
            key_cnt[i]++;
            if (key_cnt[i] >= KEY_DEBOUNCE_THRESHOLD)
            {
                if (key_prev[i] == 1 && current == 0)
                { // 按下沿
                    if (key_map[i].dpi_key)
                    {
                        Mouse_DPICycle(); // DPI 键：切档（反馈走串口）
                    }
                    else if (key_map[i].btn >= 1)
                    {
                        Mouse_SetButton(key_map[i].btn, 1); // 普通鼠标键
                    }
                    else
                    {
                        Mouse_ModeCycle(); // 模式键：三模切换
                    }
                }
                else if (key_prev[i] == 0 && current == 1)
                { // 松开沿
                    Mouse_SetButton(key_map[i].btn, 0);
                }
                key_prev[i] = current;
                key_stable[i] = current;
                key_cnt[i] = 0;
            }
        }
    }
}
