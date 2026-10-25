#ifndef __LED_H
#define __LED_H
#include <stdint.h>
#include "reg32f1.h"
#include "gpio.h"

void Led_Init(void);

void Led_On(void);

void Led_Off(void);

#endif
