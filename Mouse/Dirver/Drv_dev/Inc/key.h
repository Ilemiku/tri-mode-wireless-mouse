#ifndef __KEY_H
#define __KEY_H

#include "reg32f1.h"
#include "gpio.h"
#include "systick.h"
#include "mouse.h"

void Key_Init(void);

void Key_Scan(void);

#endif
