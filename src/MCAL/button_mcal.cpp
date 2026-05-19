/**
 * @file    button_mcal.cpp
 * @brief   MCAL — 4-button implementation
 * @layer   MCAL
 *
 * Polls UP (12), DOWN (13), SELECT (10), BACK (11).
 * Generates short-press and held events for each.
 * All logic is in a single loop — no copy-paste per button.
 */

#include "button_mcal.h"

/* ───────────────────────── Private ──────────────────────────────── */

/* Index map: 0=UP  1=DOWN  2=SELECT  3=BACK */
static const uint8_t BTN_PINS[4] = {
    BTN_UP_PIN,     /* 12 */
    BTN_DOWN_PIN,   /* 13 */
    BTN_SELECT_PIN, /* 10 */
    BTN_BACK_PIN    /* 11 */
};

/* Short-press event per button index */
static const ButtonEvent_t EVT_PRESS[4] = {
    BTN_EVENT_UP_PRESSED,
    BTN_EVENT_DOWN_PRESSED,
    BTN_EVENT_SELECT_PRESSED,
    BTN_EVENT_BACK_PRESSED
};

/* Held event per button index */
static const ButtonEvent_t EVT_HELD[4] = {
    BTN_EVENT_UP_HELD,
    BTN_EVENT_DOWN_HELD,
    BTN_EVENT_SELECT_HELD,
    BTN_EVENT_BACK_HELD
};

/* Per-button state tracking */
static struct {
    bool     wasPressed;  /* true = was LOW (pressed) last poll */
    uint32_t pressedAt;   /* millis() when falling edge detected */
    bool     heldFired;   /* true = held event already sent this press */
} s_btn[4];

static QueueHandle_t s_queue = NULL;

/* ───────────────────────── Init ─────────────────────────────────── */
QueueHandle_t MCAL_Button_Init(void) {
    for (int i = 0; i < 4; i++) {
        HAL_GPIO_Init(BTN_PINS[i], HAL_GPIO_MODE_INPUT_PULLUP);
        s_btn[i].wasPressed = false;
        s_btn[i].pressedAt  = 0;
        s_btn[i].heldFired  = false;
    }
    /* Queue holds 16 events — large enough to never drop one */
    s_queue = xQueueCreate(16, sizeof(ButtonEvent_t));
    return s_queue;
}

/* ───────────────────────── Poll ─────────────────────────────────── */
void MCAL_Button_Poll(void) {
    if (!s_queue) return;

    uint32_t now = (uint32_t)millis();

    for (int i = 0; i < 4; i++) {
        /* Active-LOW: HAL_GPIO_LOW means button is physically pressed */
        bool isPressed = (HAL_GPIO_Read(BTN_PINS[i]) == HAL_GPIO_LOW);

        if (isPressed && !s_btn[i].wasPressed) {
            /* ── Falling edge: button just went down ── */
            s_btn[i].pressedAt = now;
            s_btn[i].heldFired = false;

        } else if (isPressed && s_btn[i].wasPressed) {
            /* ── Still held ── fire held event once threshold passes */
            if (!s_btn[i].heldFired &&
                (now - s_btn[i].pressedAt) >= BTN_HELD_MS) {
                ButtonEvent_t evt = EVT_HELD[i];
                xQueueSend(s_queue, &evt, 0);
                s_btn[i].heldFired = true;
            }

        } else if (!isPressed && s_btn[i].wasPressed) {
            /* ── Rising edge: button released ──
             * Only fire short-press if debounce passed AND held wasn't fired */
            uint32_t dur = now - s_btn[i].pressedAt;
            if (dur >= BTN_DEBOUNCE_MS && !s_btn[i].heldFired) {
                ButtonEvent_t evt = EVT_PRESS[i];
                xQueueSend(s_queue, &evt, 0);
            }
        }

        s_btn[i].wasPressed = isPressed;
    }
}

/* ───────────────────────── GetEvent ─────────────────────────────── */
bool MCAL_Button_GetEvent(ButtonEvent_t *event) {
    if (!s_queue) return false;
    return (xQueueReceive(s_queue, event, 0) == pdTRUE);
}

void MCAL_Button_Reset(void) {
    for (int i = 0; i < 4; i++) {
        s_btn[i].wasPressed = false;
        s_btn[i].pressedAt  = 0;
        s_btn[i].heldFired  = false;
    }
    if (s_queue) {
        xQueueReset(s_queue);
    }
}
