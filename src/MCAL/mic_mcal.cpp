/**
 * @file    mic_mcal.cpp
 * @brief   MCAL — MAX4466 Microphone implementation
 * @layer   MCAL
 */

#include "mic_mcal.h"
#include "../HAL/mic_hal.h"
#include "../HAL/uart_hal.h"
#include <math.h>
#include <string.h>
#include <HTTPClient.h>
#include <Arduino.h>

/* ═══════════════════════════════════════════════════════════════════
   ML API endpoint — update to your deployed model URL
   ═══════════════════════════════════════════════════════════════════ */
#define MIC_ML_API_URL   "https://your-ml-model-api.example.com/predict"

/* ═══════════════════════════════════════════════════════════════════
   Static PCM recording buffer
   240 000 int16 samples × 2 bytes = 480 KB — fits in ESP32-S3 PSRAM.
   If PSRAM is unavailable we fall back to IRAM_ATTR heap allocation
   with a reduced 30-second limit handled gracefully.
   ═══════════════════════════════════════════════════════════════════ */
static int16_t  *s_pcmBuf       = nullptr;
static uint32_t  s_pcmCapacity  = 0;   /* actual allocated samples */

/* ═══════════════════════════════════════════════════════════════════
   Live window circular buffer (ring buffer, MIC_LIVE_WINDOW_SAMPLES)
   Written by Mic_Task, snapshotted by UI_Task via GetWaveformSnapshot.
   ═══════════════════════════════════════════════════════════════════ */
static int16_t  s_liveRing[MIC_LIVE_WINDOW_SAMPLES];
static uint16_t s_liveHead = 0;         /* next write position */
static uint16_t s_liveFill = 0;         /* samples written so far (≤ window) */

/* ═══════════════════════════════════════════════════════════════════
   State
   ═══════════════════════════════════════════════════════════════════ */
static volatile MicState_t s_state     = MIC_STATE_IDLE;
static volatile bool       s_active    = false;

static uint32_t  s_recIdx        = 0;   /* next write index in s_pcmBuf   */
static uint32_t  s_recStartMs    = 0;   /* millis() when recording began  */
static uint32_t  s_recLimitMs    = 0;   /* recording time limit in ms     */

/* ═══════════════════════════════════════════════════════════════════
   Live reading (published to queue)
   ═══════════════════════════════════════════════════════════════════ */
static QueueHandle_t     s_liveQueue    = NULL;
static MicLiveReading_t  s_lastLive     = {MIC_DB_FLOOR, 0, 0, false};

/* Live-window accumulation counters (reset every MIC_LIVE_WINDOW_SAMPLES) */
static uint32_t s_windowIdx    = 0;
static int64_t  s_windowSumSq  = 0;    /* sum of squares for RMS */
static int16_t  s_windowPeak   = 0;    /* peak absolute amplitude */

/* ═══════════════════════════════════════════════════════════════════
   ML result
   ═══════════════════════════════════════════════════════════════════ */
static MicMLResult_t s_mlResult = {"", 0.0f, false};

/* ═══════════════════════════════════════════════════════════════════
   Base64 encoder (RFC 4648, no line wrapping)
   ═══════════════════════════════════════════════════════════════════ */
static const char B64_CHARS[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/**
 * @brief Encode `inLen` bytes at `in` into caller-supplied `out` buffer.
 *        `outLen` must be at least ((inLen + 2) / 3) * 4 + 1 bytes.
 * @return Number of base64 characters written (excluding null terminator).
 */
static size_t base64Encode(const uint8_t *in, size_t inLen,
                            char *out, size_t outLen) {
    size_t outIdx = 0;
    for (size_t i = 0; i < inLen; i += 3) {
        uint8_t b0 = in[i];
        uint8_t b1 = (i + 1 < inLen) ? in[i + 1] : 0;
        uint8_t b2 = (i + 2 < inLen) ? in[i + 2] : 0;

        if (outIdx + 4 >= outLen) break;   /* guard */
        out[outIdx++] = B64_CHARS[ b0 >> 2];
        out[outIdx++] = B64_CHARS[(b0 & 0x03) << 4 | b1 >> 4];
        out[outIdx++] = (i + 1 < inLen) ? B64_CHARS[(b1 & 0x0F) << 2 | b2 >> 6] : '=';
        out[outIdx++] = (i + 2 < inLen) ? B64_CHARS[ b2 & 0x3F] : '=';
    }
    out[outIdx] = '\0';
    return outIdx;
}

/* ═══════════════════════════════════════════════════════════════════
   dBSPL helper
   Full-scale RMS → dBFS then remapped to a 0-100 % bar value.
   ═══════════════════════════════════════════════════════════════════ */
static float rmsToDB(int64_t sumSq, uint32_t n) {
    if (n == 0) return MIC_DB_FLOOR;
    float rms = sqrtf((float)sumSq / (float)n);
    if (rms < 1.0f) return MIC_DB_FLOOR;
    /* dBFS: 0 dBFS when rms == 2048 (half full-scale, typical max for AC mic) */
    float db = 20.0f * log10f(rms / 2048.0f) + 90.0f; /* shift so 0 dBFS = 90 dBSPL */
    if (db < MIC_DB_FLOOR) db = MIC_DB_FLOOR;
    if (db > MIC_DB_CEIL)  db = MIC_DB_CEIL;
    return db;
}

static uint8_t dbToPercent(float db) {
    float range = MIC_DB_CEIL - MIC_DB_FLOOR;
    float pct   = (db - MIC_DB_FLOOR) / range * 100.0f;
    if (pct < 0.0f)   pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;
    return (uint8_t)pct;
}

/* ═══════════════════════════════════════════════════════════════════
   MCAL_Mic_Init
   ═══════════════════════════════════════════════════════════════════ */
bool MCAL_Mic_Init(QueueHandle_t liveQueue) {
    s_liveQueue = liveQueue;

    HAL_MicAdc_Init();

    /* Try PSRAM first for the large recording buffer */
    if (psramFound()) {
        s_pcmBuf = (int16_t *)ps_malloc((size_t)MIC_MAX_RECORD_SAMPLES * sizeof(int16_t));
        if (s_pcmBuf) {
            s_pcmCapacity = MIC_MAX_RECORD_SAMPLES;
            HAL_UART_SendLine("[Mic] PCM buffer in PSRAM (60 s).");
        }
    }

    /* Fallback: internal heap with reduced capacity (30 s) */
    if (!s_pcmBuf) {
        uint32_t fallbackSamples = MIC_SAMPLE_RATE_HZ * 30;
        s_pcmBuf = (int16_t *)malloc(fallbackSamples * sizeof(int16_t));
        if (s_pcmBuf) {
            s_pcmCapacity = fallbackSamples;
            HAL_UART_Printf("[Mic] PCM buffer in heap (30 s). No PSRAM.\r\n");
        } else {
            HAL_UART_SendLine("[Mic] FATAL: PCM buffer allocation failed!");
            return false;
        }
    }

    memset(s_liveRing, 0, sizeof(s_liveRing));
    s_liveHead = 0;
    s_liveFill = 0;
    s_state    = MIC_STATE_IDLE;
    s_active   = false;

    HAL_UART_SendLine("[Mic] MCAL init OK (sensor idle).");
    return true;
}

/* ═══════════════════════════════════════════════════════════════════
   MCAL_Mic_SetActive
   ═══════════════════════════════════════════════════════════════════ */
void MCAL_Mic_SetActive(bool active) {
    if (active == s_active) return;
    s_active = active;

    if (!active) {
        /* If recording was running when screen closed, abort it */
        if (s_state == MIC_STATE_RECORDING) {
            s_state = MIC_STATE_IDLE;
            HAL_UART_SendLine("[Mic] Recording aborted (screen closed).");
        }
    }

    HAL_UART_Printf("[Mic] %s.\r\n", active ? "Active" : "Idle");
}

bool MCAL_Mic_IsActive(void) {
    return s_active;
}

/* ═══════════════════════════════════════════════════════════════════
   MCAL_Mic_StartRecording
   ═══════════════════════════════════════════════════════════════════ */
void MCAL_Mic_StartRecording(uint8_t maxSec) {
    if (s_state == MIC_STATE_RECORDING) return;
    if (!s_pcmBuf) return;

    if (maxSec == 0 || maxSec > MIC_MAX_RECORD_SEC) maxSec = MIC_MAX_RECORD_SEC;

    /* Clamp to actual buffer capacity */
    uint32_t limitSamples = (uint32_t)maxSec * MIC_SAMPLE_RATE_HZ;
    if (limitSamples > s_pcmCapacity) limitSamples = s_pcmCapacity;

    s_recIdx     = 0;
    s_recStartMs = (uint32_t)millis();
    s_recLimitMs = (uint32_t)maxSec * 1000UL;

    /* Clear old ML result */
    memset(&s_mlResult, 0, sizeof(s_mlResult));

    s_state = MIC_STATE_RECORDING;
    HAL_UART_Printf("[Mic] Recording started (%u s limit, %lu samples).\r\n",
                     maxSec, (unsigned long)limitSamples);
}

/* ═══════════════════════════════════════════════════════════════════
   MCAL_Mic_StopRecording
   ═══════════════════════════════════════════════════════════════════ */
void MCAL_Mic_StopRecording(void) {
    if (s_state != MIC_STATE_RECORDING) return;
    s_state = MIC_STATE_IDLE;
    s_recIdx = 0;
    HAL_UART_SendLine("[Mic] Recording stopped and discarded.");
}

/* ═══════════════════════════════════════════════════════════════════
   MCAL_Mic_Tick
   Called from Mic_Task at MIC_SAMPLE_RATE_HZ (every 250 µs).
   ═══════════════════════════════════════════════════════════════════ */
void MCAL_Mic_Tick(void) {
    if (!s_active) return;

    /* ── 1. Read ADC ── */
    uint16_t raw = HAL_MicAdc_ReadRaw();
    int16_t  amp = HAL_MicAdc_RawToAmplitude(raw);

    /* ── 2. Live ring buffer ── */
    s_liveRing[s_liveHead] = amp;
    s_liveHead = (s_liveHead + 1) % MIC_LIVE_WINDOW_SAMPLES;
    if (s_liveFill < MIC_LIVE_WINDOW_SAMPLES) s_liveFill++;

    /* ── 3. Accumulate stats for live window ── */
    int32_t absPeak = (amp < 0) ? -amp : amp;
    s_windowSumSq += (int64_t)amp * amp;
    if (absPeak > s_windowPeak) s_windowPeak = (int16_t)absPeak;
    s_windowIdx++;

    /* ── 4. Publish live reading every MIC_LIVE_WINDOW_SAMPLES ── */
    if (s_windowIdx >= MIC_LIVE_WINDOW_SAMPLES) {
        MicLiveReading_t live;
        live.dbSPL      = rmsToDB(s_windowSumSq, s_windowIdx);
        live.barPercent = dbToPercent(live.dbSPL);
        live.peakAmp    = s_windowPeak;
        live.clipping   = (s_windowPeak >= 2040);   /* near ADC saturation */

        s_lastLive = live;
        if (s_liveQueue) xQueueOverwrite(s_liveQueue, &live);

        /* Reset window */
        s_windowIdx   = 0;
        s_windowSumSq = 0;
        s_windowPeak  = 0;
    }

    /* ── 5. Recording fill ── */
    if (s_state == MIC_STATE_RECORDING) {
        if (s_recIdx < s_pcmCapacity) {
            s_pcmBuf[s_recIdx++] = amp;
        }

        /* Check time limit */
        uint32_t elapsed = (uint32_t)millis() - s_recStartMs;
        bool timeLimitHit    = (elapsed >= s_recLimitMs);
        bool bufferFull      = (s_recIdx >= s_pcmCapacity);

        if (timeLimitHit || bufferFull) {
            s_state = MIC_STATE_DONE;
            HAL_UART_Printf(
                "[Mic] Recording done: %lu samples, %lu ms.\r\n",
                (unsigned long)s_recIdx,
                (unsigned long)elapsed);
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════
   Getters
   ═══════════════════════════════════════════════════════════════════ */
MicState_t MCAL_Mic_GetState(void) {
    return s_state;
}

uint8_t MCAL_Mic_GetSecondsRemaining(void) {
    if (s_state != MIC_STATE_RECORDING) return 0;
    uint32_t elapsed = (uint32_t)millis() - s_recStartMs;
    if (elapsed >= s_recLimitMs) return 0;
    return (uint8_t)((s_recLimitMs - elapsed) / 1000UL);
}

uint8_t MCAL_Mic_GetSecondsElapsed(void) {
    if (s_state != MIC_STATE_RECORDING) return 0;
    return (uint8_t)(((uint32_t)millis() - s_recStartMs) / 1000UL);
}

bool MCAL_Mic_GetRecording(MicRecording_t *out) {
    if (!out || s_state != MIC_STATE_DONE || !s_pcmBuf || s_recIdx == 0) return false;
    out->samples    = s_pcmBuf;
    out->count      = s_recIdx;
    out->sampleRate = MIC_SAMPLE_RATE_HZ;
    out->durationMs = (s_recIdx * 1000UL) / MIC_SAMPLE_RATE_HZ;
    return true;
}

void MCAL_Mic_ResetRecording(void) {
    s_recIdx = 0;
    s_state  = MIC_STATE_IDLE;
    memset(&s_mlResult, 0, sizeof(s_mlResult));
    HAL_UART_SendLine("[Mic] Recording reset to IDLE.");
}

bool MCAL_Mic_GetLiveReading(MicLiveReading_t *out) {
    if (!out) return false;
    if (s_liveQueue && xQueuePeek(s_liveQueue, out, 0) == pdTRUE) return true;
    *out = s_lastLive;
    return true;
}

uint16_t MCAL_Mic_GetWaveformSnapshot(int16_t *out, uint16_t count) {
    if (!out || count == 0) return 0;
    if (count > MIC_LIVE_WINDOW_SAMPLES) count = MIC_LIVE_WINDOW_SAMPLES;
    uint16_t avail = s_liveFill < count ? s_liveFill : count;
    if (avail == 0) return 0;

    /* Oldest sample starts at (s_liveHead - avail + WINDOW) % WINDOW */
    uint16_t start = (s_liveHead + MIC_LIVE_WINDOW_SAMPLES - avail) % MIC_LIVE_WINDOW_SAMPLES;
    for (uint16_t i = 0; i < avail; i++) {
        out[i] = s_liveRing[(start + i) % MIC_LIVE_WINDOW_SAMPLES];
    }
    return avail;
}

/* ═══════════════════════════════════════════════════════════════════
   MCAL_Mic_UploadRecording
   Blocking HTTP POST with base64-encoded PCM.
   JSON body:
   {
     "sample_rate": 4000,
     "channels": 1,
     "encoding": "pcm_s16le",
     "duration_ms": <N>,
     "audio_b64": "<base64>"
   }
   ═══════════════════════════════════════════════════════════════════ */
void MCAL_Mic_UploadRecording(void) {
    if (s_state != MIC_STATE_DONE) return;
    if (!s_pcmBuf || s_recIdx == 0) {
        s_state = MIC_STATE_UPLOAD_ERR;
        return;
    }

    s_state = MIC_STATE_UPLOADING;
    HAL_UART_Printf("[Mic] Uploading %lu samples to ML API...\r\n",
                     (unsigned long)s_recIdx);

    /* Base64 encode the PCM buffer */
    size_t   pcmBytes  = s_recIdx * sizeof(int16_t);
    size_t   b64Len    = ((pcmBytes + 2) / 3) * 4 + 1;
    char    *b64Buf    = (char *)malloc(b64Len);
    if (!b64Buf) {
        HAL_UART_SendLine("[Mic] Upload FAILED: out of memory for base64.");
        s_state = MIC_STATE_UPLOAD_ERR;
        return;
    }

    base64Encode((const uint8_t *)s_pcmBuf, pcmBytes, b64Buf, b64Len);

    uint32_t durMs = (s_recIdx * 1000UL) / MIC_SAMPLE_RATE_HZ;

    /* Build JSON — allocate on heap because it can be large */
    /* Header is ~120 chars; base64 is pcmBytes*4/3 bytes  */
    size_t jsonLen = 160 + b64Len;
    char  *json    = (char *)malloc(jsonLen);
    if (!json) {
        free(b64Buf);
        HAL_UART_SendLine("[Mic] Upload FAILED: out of memory for JSON.");
        s_state = MIC_STATE_UPLOAD_ERR;
        return;
    }

    snprintf(json, jsonLen,
             "{\"sample_rate\":%d,\"channels\":1,"
             "\"encoding\":\"pcm_s16le\","
             "\"duration_ms\":%lu,"
             "\"audio_b64\":\"%s\"}",
             MIC_SAMPLE_RATE_HZ,
             (unsigned long)durMs,
             b64Buf);

    free(b64Buf);
    b64Buf = nullptr;

    /* HTTP POST */
    HTTPClient http;
    http.begin(MIC_ML_API_URL);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(30000);   /* 30 s — large payload */

    int httpCode = http.POST((uint8_t *)json, strlen(json));
    free(json);
    json = nullptr;

    if (httpCode == 200 || httpCode == 201) {
        String body = http.getString();
        http.end();

        HAL_UART_Printf("[Mic] Upload OK (%d). Response: %s\r\n",
                         httpCode, body.c_str());

        /* Parse minimal JSON: expect {"label":"Normal","confidence":0.95} */
        int labelStart = body.indexOf("\"label\":\"");
        int confStart  = body.indexOf("\"confidence\":");

        if (labelStart >= 0) {
            labelStart += 9;
            int labelEnd = body.indexOf('"', labelStart);
            String label = body.substring(labelStart, labelEnd);
            strncpy(s_mlResult.diagnosis, label.c_str(),
                    sizeof(s_mlResult.diagnosis) - 1);
        } else {
            strncpy(s_mlResult.diagnosis, "See server",
                    sizeof(s_mlResult.diagnosis) - 1);
        }

        if (confStart >= 0) {
            confStart += 13;
            s_mlResult.confidence = body.substring(confStart).toFloat();
        } else {
            s_mlResult.confidence = 0.0f;
        }

        s_mlResult.valid = true;
        s_state = MIC_STATE_UPLOAD_OK;

    } else {
        http.end();
        HAL_UART_Printf("[Mic] Upload FAILED (HTTP %d).\r\n", httpCode);
        strncpy(s_mlResult.diagnosis, "Upload error",
                sizeof(s_mlResult.diagnosis) - 1);
        s_mlResult.confidence = 0.0f;
        s_mlResult.valid      = false;
        s_state = MIC_STATE_UPLOAD_ERR;
    }
}

bool MCAL_Mic_GetMLResult(MicMLResult_t *out) {
    if (!out || !s_mlResult.valid) return false;
    *out = s_mlResult;
    return true;
}