/**
 * @file    button_mcal.h
 * @brief   MCAL — 4-button abstraction with debounce
 * @layer   MCAL
 *
 * Button layout:
 *   BTN_UP     (GPIO 12) → scroll up   in menus
 *   BTN_DOWN   (GPIO 13) → scroll down in menus
 *   BTN_SELECT (GPIO 10) → confirm / enter sub-screen
 *   BTN_BACK   (GPIO 11) → cancel / return to previous screen
 *
 * All buttons are active-LOW with internal pull-up enabled.
 * Events are posted to a FreeRTOS queue — no polling needed in tasks.
 */

#ifndef BUTTON_MCAL_H
#define BUTTON_MCAL_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include "../HAL/gpio_hal.h"

/* ------ Config ------ */
#define BTN_DEBOUNCE_MS     50     /**< Min press duration to register   */
#define BTN_HELD_MS         1000   /**< Press duration to fire held event */

/* ------ Types ------ */
typedef enum {
    BTN_EVENT_NONE = 0,

    /* Scroll buttons */
    BTN_EVENT_UP_PRESSED,        /**< GPIO 12 short press */
    BTN_EVENT_UP_HELD,           /**< GPIO 12 held >1 s   */
    BTN_EVENT_DOWN_PRESSED,      /**< GPIO 13 short press */
    BTN_EVENT_DOWN_HELD,         /**< GPIO 13 held >1 s   */

    /* Action buttons — fully separate from scroll */
    BTN_EVENT_SELECT_PRESSED,    /**< GPIO 10 short press — CONFIRM */
    BTN_EVENT_SELECT_HELD,       /**< GPIO 10 held >1 s             */
    BTN_EVENT_BACK_PRESSED,      /**< GPIO 11 short press — BACK    */
    BTN_EVENT_BACK_HELD          /**< GPIO 11 held >1 s             */
} ButtonEvent_t;

/* ------ API ------ */
/**
 * @brief  Initialise all 4 GPIO pins + create FreeRTOS event queue.
 *         Call once from setup() before starting tasks.
 * @return Handle to the button event queue
 */
QueueHandle_t MCAL_Button_Init(void);
/**
 * @brief  Poll all 4 buttons and push events to the queue.
 *         Call from the UI task loop every 10–50 ms.
 */
void MCAL_Button_Poll(void);
/**
 * @brief  Retrieve one event from the queue (non-blocking).
 * @param  event  Output — filled when returning true
 * @return true if an event was available
 */
bool MCAL_Button_GetEvent(ButtonEvent_t *event);

/**
 * @brief  Reset internal debounce/held state and clear pending events.
 *         Useful after wake-from-sleep to avoid stuck button states.
 */
void MCAL_Button_Reset(void);

/**
 * @brief  Re-initialize button GPIO pins (pull-ups) after sleep.
 */
void MCAL_Button_ReinitPins(void);

/**
 * @brief  Global semaphore used to wake the UI task on SELECT presses.
 *         Defined in Smart_Stethoscope_V3.ino
 */
extern SemaphoreHandle_t g_btnSemaphore;

#endif /* BUTTON_MCAL_H */
