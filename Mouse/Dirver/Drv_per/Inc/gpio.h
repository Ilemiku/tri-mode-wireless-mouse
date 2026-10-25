#ifndef __GPIO_H
#define __GPIO_H
#include "reg32f1.h"
#include <stdint.h>
#include <stdio.h>

void GPIO_Set(GPIO_TypeDef *GPIOx, u32 GPIO_PIN);

void GPIO_Reset(GPIO_TypeDef *GPIOx, u32 GPIO_PIN);

void GPIO_Read(GPIO_TypeDef *GPIOx, u32 GPIO_PIN);

void GPIO_Init(void);

#endif
