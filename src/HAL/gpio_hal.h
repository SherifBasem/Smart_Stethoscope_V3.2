/**
 * @file    gpio_hal.h
 * @brief   HAL — GPIO raw register abstraction for ESP32-S3
 * @layer   HAL
 */

#ifndef GPIO_HAL_H
#define GPIO_HAL_H

#include <Arduino.h>

/* ------ Config ------ */
#define BTN_SELECT_PIN  10   /**< GPIO 10 — SELECT / confirm           */
#define BTN_BACK_PIN    11   /**< GPIO 11 — BACK / cancel              */
#define BTN_UP_PIN      12   /**< GPIO 12 — scroll UP                  */
#define BTN_DOWN_PIN    13   /**< GPIO 13 — scroll DOWN                */

/* ------ Types ------ */
typedef enum {
    HAL_GPIO_LOW  = 0,
    HAL_GPIO_HIGH = 1
} HAL_GPIO_PinState_t;

typedef enum {
    HAL_GPIO_MODE_INPUT        = INPUT,
    HAL_GPIO_MODE_INPUT_PULLUP = INPUT_PULLUP,
    HAL_GPIO_MODE_OUTPUT       = OUTPUT
} HAL_GPIO_Mode_t;

/* ------ API ------ */
static inline void HAL_GPIO_Init(uint8_t pin, HAL_GPIO_Mode_t mode) {
    pinMode(pin, (uint8_t)mode);
}
static inline HAL_GPIO_PinState_t HAL_GPIO_Read(uint8_t pin) {
    return (digitalRead(pin) == HIGH) ? HAL_GPIO_HIGH : HAL_GPIO_LOW;
}
static inline void HAL_GPIO_Write(uint8_t pin, HAL_GPIO_PinState_t state) {
    digitalWrite(pin, (state == HAL_GPIO_HIGH) ? HIGH : LOW);
}

#endif /* GPIO_HAL_H */
