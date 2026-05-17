/**
 * @file    i2c_hal.h
 * @brief   HAL — I2C bus abstraction for ESP32-S3
 * @layer   HAL
 *
 * Wraps Arduino Wire.h behind typed primitives so the HAL layer
 * never calls Wire directly.
 *
 * Bus config (fixed for this board):
 *   SDA → GPIO 8
 *   SCL → GPIO 9
 *   Speed → 400 kHz (Fast-mode) — works for SSD1306 and MAX30102
 */

#ifndef I2C_HAL_H
#define I2C_HAL_H

#include <Arduino.h>
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

extern SemaphoreHandle_t g_i2cMutex;

/* ------ Config ------ */
#define I2C_HAL_SDA_PIN   8
#define I2C_HAL_SCL_PIN   9
#define I2C_HAL_FREQ_HZ   400000UL   /**< 400 kHz Fast-mode */

/* ------ API ------ */
/**
 * @brief  Initialise the I2C bus with the project-standard pins and speed.
 *         Safe to call multiple times — Wire.begin() is idempotent on
 *         the ESP32 Arduino core when called with the same parameters.
 *         Call once from setup() before any I2C device is initialised.
 */
static inline void HAL_I2C_Init(void) {
    Wire.begin(I2C_HAL_SDA_PIN, I2C_HAL_SCL_PIN);
    Wire.setClock(I2C_HAL_FREQ_HZ);
}

static inline void HAL_I2C_Lock(void) {
    if (g_i2cMutex) xSemaphoreTake(g_i2cMutex, portMAX_DELAY);
}

static inline void HAL_I2C_Unlock(void) {
    if (g_i2cMutex) xSemaphoreGive(g_i2cMutex);
}
/**
 * @brief  Probe a 7-bit I2C address to check whether a device responds.
 *         Useful for startup self-test and sensor presence detection.
 * @param  addr  7-bit I2C device address (e.g. 0x57 for MAX30102)
 * @return true if the device ACKed the probe, false if absent/timeout
 */
static inline bool HAL_I2C_Probe(uint8_t addr) {
    Wire.beginTransmission(addr);
    return (Wire.endTransmission() == 0);
}
/**
 * @brief  Write one byte to a register of an I2C device.
 * @param  devAddr  7-bit device address
 * @param  regAddr  Register address
 * @param  value    Byte to write
 * @return true on success (device ACKed)
 */
static inline bool HAL_I2C_WriteReg(uint8_t devAddr,
                                      uint8_t regAddr,
                                      uint8_t value) {
    Wire.beginTransmission(devAddr);
    Wire.write(regAddr);
    Wire.write(value);
    return (Wire.endTransmission() == 0);
}
/**
 * @brief  Read one byte from a register of an I2C device.
 * @param  devAddr  7-bit device address
 * @param  regAddr  Register address
 * @param  out      Output byte (written only on success)
 * @return true on success
 */
static inline bool HAL_I2C_ReadReg(uint8_t devAddr,
                                     uint8_t regAddr,
                                     uint8_t *out) {
    Wire.beginTransmission(devAddr);
    Wire.write(regAddr);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(devAddr, (uint8_t)1) != 1) return false;
    *out = Wire.read();
    return true;
}

#endif /* I2C_HAL_H */
