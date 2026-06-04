/**
 * @file    qrcode_mcal.h
 * @brief   Small QR Code generator for OLED setup links
 * @layer   MCAL
 *
 * Supports QR Version 3-L, byte mode, enough for the setup WiFi payload.
 */

#ifndef QRCODE_MCAL_H
#define QRCODE_MCAL_H

#include <Arduino.h>

/* ------ Config ------ */
#define QR_UTIL_SIZE 29

/* ------ Types ------ */
typedef struct {
    uint8_t modules[QR_UTIL_SIZE][QR_UTIL_SIZE];
} MCAL_QRCode_t;

/* ------ API ------ */
bool MCAL_QRCode_GenerateText(MCAL_QRCode_t *qr, const char *text);
bool MCAL_QRCode_GenerateSetupUrl(MCAL_QRCode_t *qr, const char *url);
bool MCAL_QRCode_GetModule(const MCAL_QRCode_t *qr, uint8_t x, uint8_t y);

#endif /* QRCODE_MCAL_H */
