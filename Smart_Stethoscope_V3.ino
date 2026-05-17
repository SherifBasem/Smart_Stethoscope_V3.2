/* ── Application layer ── */
#include "src/APP/ui_task.h"
#include "src/APP/wifi_task.h"
#include "src/APP/uart_task.h"
#include "src/APP/heart_task.h"
#include "src/APP/mic_task.h"

/* ── MCAL (middle layer services) ── */
#include "src/MCAL/button_mcal.h"
#include "src/MCAL/wifi_mcal.h"
#include "src/MCAL/uart_mcal.h"
#include "src/MCAL/oled_mcal.h"
#include "src/MCAL/heart_mcal.h"
#include "src/MCAL/battery_mcal.h"
#include "src/MCAL/mic_mcal.h"

/* ── HAL (low-level hardware/library primitives) ── */
#include "src/HAL/gpio_hal.h"
#include "src/HAL/uart_hal.h"
#include "src/HAL/wifi_hal.h"
#include "src/HAL/i2c_hal.h"
#include "src/HAL/battery_hal.h"
#include "src/HAL/mic_hal.h"
#include <driver/gpio.h>
#include <freertos/semphr.h>

/* ── Shared inter-task queues ── */
static QueueHandle_t g_wifiStatusQueue = NULL;
static QueueHandle_t g_btnQueue        = NULL;
static QueueHandle_t g_heartQueue      = NULL;
static QueueHandle_t g_micLiveQueue    = NULL;

/* ── Global RTOS primitives ── */
SemaphoreHandle_t g_uartMutex = NULL;
SemaphoreHandle_t g_i2cMutex  = NULL;
SemaphoreHandle_t g_btnSemaphore = NULL;

/* ── Task handles (for diagnostics) ── */
static TaskHandle_t g_uiTaskHandle   = NULL;
static TaskHandle_t g_wifiTaskHandle = NULL;
static TaskHandle_t g_uartTaskHandle = NULL;
static TaskHandle_t g_heartTaskHandle = NULL;
static TaskHandle_t g_micTaskHandle   = NULL;

static void IRAM_ATTR btn_select_isr(void *arg) {
    (void)arg;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (g_btnSemaphore) {
        xSemaphoreGiveFromISR(g_btnSemaphore, &xHigherPriorityTaskWoken);
        if (xHigherPriorityTaskWoken) portYIELD_FROM_ISR();
    }
}

void setup() {

    /* ── 1. UART debug ── */
    HAL_UART_Init();
    delay(200);
    HAL_UART_SendLine("\r\n[BOOT] Smart Stethoscope v0.4 starting...");

    /* ── 2. I2C bus ── */
    HAL_I2C_Init();
    HAL_UART_SendLine("[BOOT] I2C bus up (SDA=GPIO8, SCL=GPIO9, 400kHz).");

    /* ── 3. OLED ── */
    if (!MCAL_OLED_Init()) {
        HAL_UART_SendLine("[BOOT] WARNING: OLED init failed. Continuing in UART-only mode.");
    }

    /* ── 4. Battery monitor ── */
    MCAL_Battery_Init();

    /* ── 5. Queues ── */
    g_wifiStatusQueue = xQueueCreate(1, sizeof(WiFiHAL_Status_t));
    g_btnQueue        = MCAL_Button_Init();

    /* ── 5b. Mutexes + button semaphore ── */
    g_uartMutex = xSemaphoreCreateMutex();
    g_i2cMutex  = xSemaphoreCreateMutex();
    g_btnSemaphore = xSemaphoreCreateBinary();
    if (g_btnSemaphore) {
        gpio_set_intr_type((gpio_num_t)BTN_SELECT_PIN, GPIO_INTR_NEGEDGE);
        gpio_install_isr_service(0);
        gpio_isr_handler_add((gpio_num_t)BTN_SELECT_PIN, btn_select_isr, NULL);
    }

    /* ── 6. Task params ── */
    static HeartTask_Params_t heartParams;
    heartParams.heartQueue = NULL;

    static MicTask_Params_t micParams;
    micParams.micLiveQueue = NULL;

    static UITask_Params_t uiParams;
    uiParams.btnQueue        = g_btnQueue;
    uiParams.wifiStatusQueue = g_wifiStatusQueue;
    uiParams.heartQueue      = NULL;
    uiParams.micLiveQueue    = NULL;

    static WiFiTask_Params_t wifiParams;
    wifiParams.wifiStatusQueue = g_wifiStatusQueue;

    static UARTTask_Params_t uartParams;
    uartParams.wifiStatusQueue = g_wifiStatusQueue;
    uartParams.heartQueue      = NULL;

    /* ── 7. Heart task first (so queue handle is available) ── */
    g_heartTaskHandle = HeartTask_Start(&heartParams);
    g_heartQueue = heartParams.heartQueue;

    uiParams.heartQueue   = g_heartQueue;
    uartParams.heartQueue = g_heartQueue;

    /* ── 8. Mic task ── */
    g_micTaskHandle = MicTask_Start(&micParams);
    g_micLiveQueue = micParams.micLiveQueue;
    uiParams.micLiveQueue = g_micLiveQueue;

    /* ── 9. Remaining tasks ── */
    g_wifiTaskHandle = WiFiTask_Start(&wifiParams);
    g_uiTaskHandle = UITask_Start(&uiParams);

    uartParams.uiTaskHandle    = g_uiTaskHandle;
    uartParams.wifiTaskHandle  = g_wifiTaskHandle;
    uartParams.heartTaskHandle = g_heartTaskHandle;
    uartParams.micTaskHandle   = g_micTaskHandle;

    g_uartTaskHandle = UARTTask_Start(&uartParams);

    HAL_UART_SendLine("[BOOT] All tasks created. FreeRTOS scheduler running.");
    HAL_UART_SendLine("[BOOT] Type HELP in serial monitor for commands.");
}

void loop() {
    vTaskDelay(portMAX_DELAY);
}
