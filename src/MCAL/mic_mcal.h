/**
 * @file    mic_mcal.h
 * @brief   MCAL — MAX4466 Microphone abstraction
 * @layer   MCAL
 *
 * Responsibilities
 * ────────────────
 * 1. Continuous sampling (4 kHz) when the Lung Sound screen is active.
 * 2. Real-time dBSPL estimation for the VU / bar-graph display.
 * 3. One-shot recording: fills a pre-allocated PCM buffer for up to
 *    MIC_MAX_RECORD_SEC seconds then signals completion.
 * 4. Base64 encoding of the captured PCM for HTTP multipart upload.
 *
 * Recording lifecycle
 * ───────────────────
 *   MCAL_Mic_StartRecording()   → state = MIC_STATE_RECORDING
 *   Mic_Task calls MCAL_Mic_Tick() every sample interval
 *   Buffer fills / time limit hit → state = MIC_STATE_DONE
 *   UI calls MCAL_Mic_GetRecording() to retrieve the buffer
 *   MCAL_Mic_ResetRecording()   → state = MIC_STATE_IDLE
 *
 * PCM format sent to the ML API
 * ──────────────────────────────
 *   Encoding  : signed 16-bit PCM, little-endian
 *   Sample rate: MIC_SAMPLE_RATE_HZ (4000 Hz)
 *   Channels  : mono
 *   Duration  : up to MIC_MAX_RECORD_SEC (60 s)
 *   Transport : JSON body with base64 field  OR  multipart/form-data
 *               (configured in mic_mcal.cpp)
 *
 * Thread safety
 * ─────────────
 *   MCAL_Mic_Tick()           — called from Mic_Task (Core 0)
 *   MCAL_Mic_GetLiveReading() — called from UI_Task  (Core 1)
 *   A single volatile flag + FreeRTOS queue decouple the cores.
 *   The large recording buffer is only written by Mic_Task and only
 *   read by UI_Task after state == MIC_STATE_DONE, so no mutex needed.
 */

#ifndef MIC_MCAL_H
#define MIC_MCAL_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

/* ------ Config ------ */
#define MIC_SAMPLE_RATE_HZ       4000        /**< ADC sample rate (Hz)          */
#define MIC_MIN_RECORD_SEC       30          /**< Minimum recording length (s)  */
#define MIC_MAX_RECORD_SEC       60          /**< Maximum recording length (s)  */
#define MIC_MAX_RECORD_SAMPLES   (MIC_SAMPLE_RATE_HZ * MIC_MAX_RECORD_SEC)
                                             /**< = 240 000 int16 samples        */
#define MIC_LIVE_WINDOW_MS       100         /**< Window for dBSPL / peak calc  */
#define MIC_LIVE_WINDOW_SAMPLES  (MIC_SAMPLE_RATE_HZ * MIC_LIVE_WINDOW_MS / 1000)
                                             /**< = 400 samples                  */
#define MIC_DB_FLOOR             30.0f       /**< Silence floor (dBSPL ref)     */
#define MIC_DB_CEIL              90.0f       /**< Clipping ceiling (dBSPL)      */
#define MIC_IR_FINGER_THRESHOLD  50000UL     /**< Reused from heart_mcal        */

/* ------ Types ------ */

typedef enum {
    MIC_STATE_IDLE = 0,   /**< Sampling for display only, not recording      */
    MIC_STATE_RECORDING,  /**< Actively filling the PCM buffer               */
    MIC_STATE_DONE,       /**< Buffer full or time limit reached             */
    MIC_STATE_UPLOADING,  /**< HTTP POST in progress                         */
    MIC_STATE_UPLOAD_OK,  /**< Upload succeeded — result available           */
    MIC_STATE_UPLOAD_ERR, /**< Upload failed                                 */
} MicState_t;

/**
 * @brief  Live reading — updated every MIC_LIVE_WINDOW_MS.
 *         Safe to read from UI_Task (the Mic_Task overwrites atomically).
 */
typedef struct {
    float    dbSPL;        /**< Estimated SPL in dBFS (0 dB = full scale)   */
    uint8_t  barPercent;   /**< 0–100 % mapped from MIC_DB_FLOOR..CEIL      */
    int16_t  peakAmp;      /**< Peak signed amplitude in last window         */
    bool     clipping;     /**< true if any sample saturated ADC             */
    bool     isConnected;  /**< true if ADC reads valid audio range          */
} MicLiveReading_t;

/**
 * @brief  Completed recording descriptor (valid when state == MIC_STATE_DONE).
 */
typedef struct {
    const int16_t *samples;    /**< Pointer into static PCM buffer           */
    uint32_t       count;      /**< Number of valid samples                  */
    uint32_t       sampleRate; /**< Always MIC_SAMPLE_RATE_HZ                */
    uint32_t       durationMs; /**< Actual recorded duration in ms           */
} MicRecording_t;

/**
 * @brief  ML model inference result (populated after upload).
 */
typedef struct {
    char    diagnosis[64];  /**< Model output label e.g. "Normal", "Murmur" */
    float   confidence;     /**< Confidence 0.0–1.0                          */
    bool    valid;          /**< true if result has been populated           */
} MicMLResult_t;

/* ------ API ------ */

/**
 * @brief  Initialise ADC, allocate live-window buffer.
 *         Call from setup() before Mic_Task starts.
 * @param  liveQueue  Length-1 FreeRTOS queue; receives MicLiveReading_t
 * @return true on success
 */
bool MCAL_Mic_Init(QueueHandle_t liveQueue);
/**
 * @brief Return true when ADC setup and recording buffers are available.
 */
bool MCAL_Mic_IsReady(void);

/**
 * @brief  Single sample-and-update tick.
 *         Call from the Mic_Task in a tight loop at MIC_SAMPLE_RATE_HZ.
 *         Handles live dBSPL update, recording fill, and time-limit enforcement.
 */
void MCAL_Mic_Tick(void);

/**
 * @brief  Enable or disable the microphone sampling loop.
 *         When disabled the Mic_Task sleeps and ADC is not read.
 *         Call HeartTask_SetActive-style from UI_Task on screen transitions.
 */
void MCAL_Mic_SetActive(bool active);
bool MCAL_Mic_IsActive(void);

/**
 * @brief  Start a timed recording.
 *         Resets the PCM buffer and switches state to MIC_STATE_RECORDING.
 *         No-op if already recording.
 * @param  maxSec  Recording time limit (clamped to MIC_MAX_RECORD_SEC).
 */
void MCAL_Mic_StartRecording(uint8_t maxSec);

/**
 * @brief  Abort an in-progress recording and reset to IDLE.
 */
void MCAL_Mic_StopRecording(void);

/**
 * @brief  Retrieve recording descriptor (valid only when state == DONE).
 * @param  out  Filled on success
 * @return true if a completed recording is available
 */
bool MCAL_Mic_GetRecording(MicRecording_t *out);

/**
 * @brief  Reset state back to IDLE after consuming the recording.
 */
void MCAL_Mic_ResetRecording(void);

/**
 * @brief  Get the current Mic state machine state.
 */
MicState_t MCAL_Mic_GetState(void);

/**
 * @brief  Return remaining recording time in seconds (0 when idle/done).
 */
uint8_t MCAL_Mic_GetSecondsRemaining(void);

/**
 * @brief  Return elapsed recording time in seconds.
 */
uint8_t MCAL_Mic_GetSecondsElapsed(void);

/**
 * @brief  Calculate available recording seconds based on free heap memory.
 *         Returns the maximum seconds recordable with current available memory,
 *         clamped to MIC_MIN_RECORD_SEC..MIC_MAX_RECORD_SEC range.
 * @return Available recording seconds
 */
uint8_t MCAL_Mic_GetAvailableRecordSeconds(void);

/**
 * @brief  Peek at the latest live reading from the queue (non-blocking).
 * @param  out  Filled on success
 * @return true if a reading was available
 */
bool MCAL_Mic_GetLiveReading(MicLiveReading_t *out);

/**
 * @brief  Upload the completed PCM buffer to the ML model API.
 *         Encodes PCM as base64, POSTs JSON to MIC_ML_API_URL.
 *         Call from UI_Task after state == MIC_STATE_DONE.
 *         This function BLOCKS for the duration of the HTTP request.
 *         State transitions: UPLOADING → UPLOAD_OK or UPLOAD_ERR.
 */
void MCAL_Mic_UploadRecording(void);

/**
 * @brief  Retrieve the ML model result (valid after state == UPLOAD_OK).
 * @param  out  Filled on success
 * @return true if a valid result exists
 */
bool MCAL_Mic_GetMLResult(MicMLResult_t *out);

/**
 * @brief  Return the waveform snapshot buffer for OLED visualisation.
 *         Fills `out` with up to `count` signed 16-bit samples from the
 *         most recent live window (oldest first).
 * @param  out    Caller-supplied buffer
 * @param  count  Number of samples requested (≤ MIC_LIVE_WINDOW_SAMPLES)
 * @return Actual number of samples copied
 */
uint16_t MCAL_Mic_GetWaveformSnapshot(int16_t *out, uint16_t count);

#endif /* MIC_MCAL_H */
