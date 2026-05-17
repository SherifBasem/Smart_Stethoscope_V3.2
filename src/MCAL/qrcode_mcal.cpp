/**
 * @file    qrcode_mcal.cpp
 * @brief   Minimal QR Version 3-L encoder
 * @layer   MCAL
 */

#include "qrcode_mcal.h"
#include <string.h>

#define QR_DATA_CODEWORDS 55
#define QR_EC_CODEWORDS   15
#define QR_TOTAL_CODEWORDS (QR_DATA_CODEWORDS + QR_EC_CODEWORDS)

static uint8_t s_reserved[QR_UTIL_SIZE][QR_UTIL_SIZE];

static void qrSet(MCAL_QRCode_t *qr, int x, int y, bool value, bool reserved) {
    if (x < 0 || y < 0 || x >= QR_UTIL_SIZE || y >= QR_UTIL_SIZE) return;
    qr->modules[y][x] = value ? 1 : 0;
    if (reserved) s_reserved[y][x] = 1;
}

static void addFinder(MCAL_QRCode_t *qr, int x, int y) {
    for (int dy = -1; dy <= 7; dy++) {
        for (int dx = -1; dx <= 7; dx++) {
            int xx = x + dx;
            int yy = y + dy;
            bool on = (dx >= 0 && dx <= 6 && dy >= 0 && dy <= 6 &&
                      (dx == 0 || dx == 6 || dy == 0 || dy == 6 ||
                       (dx >= 2 && dx <= 4 && dy >= 2 && dy <= 4)));
            qrSet(qr, xx, yy, on, true);
        }
    }
}

static void addAlignment(MCAL_QRCode_t *qr, int cx, int cy) {
    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            bool on = (abs(dx) == 2 || abs(dy) == 2 || (dx == 0 && dy == 0));
            qrSet(qr, cx + dx, cy + dy, on, true);
        }
    }
}

static void addFunctionPatterns(MCAL_QRCode_t *qr) {
    addFinder(qr, 0, 0);
    addFinder(qr, QR_UTIL_SIZE - 7, 0);
    addFinder(qr, 0, QR_UTIL_SIZE - 7);
    addAlignment(qr, 22, 22);

    for (int i = 8; i < QR_UTIL_SIZE - 8; i++) {
        qrSet(qr, 6, i, (i % 2) == 0, true);
        qrSet(qr, i, 6, (i % 2) == 0, true);
    }

    qrSet(qr, 8, 21, true, true); /* dark module */

    for (int i = 0; i < 9; i++) {
        if (i != 6) {
            qrSet(qr, 8, i, false, true);
            qrSet(qr, i, 8, false, true);
        }
    }
    for (int i = QR_UTIL_SIZE - 8; i < QR_UTIL_SIZE; i++) {
        qrSet(qr, 8, i, false, true);
        qrSet(qr, i, 8, false, true);
    }
}

static uint8_t gfMul(uint8_t x, uint8_t y) {
    uint16_t z = 0;
    for (int i = 7; i >= 0; i--) {
        z = (z << 1) ^ ((z & 0x80) ? 0x11D : 0);
        if ((y >> i) & 1) z ^= x;
    }
    return (uint8_t)z;
}

static uint8_t gfPow(uint8_t x, int power) {
    uint8_t y = 1;
    while (power-- > 0) y = gfMul(y, x);
    return y;
}

static void reedSolomonGenerator(uint8_t gen[QR_EC_CODEWORDS + 1]) {
    memset(gen, 0, QR_EC_CODEWORDS + 1);
    gen[0] = 1;
    for (int i = 0; i < QR_EC_CODEWORDS; i++) {
        uint8_t root = gfPow(2, i);
        for (int j = i; j >= 0; j--) {
            gen[j + 1] ^= gfMul(gen[j], root);
        }
    }
}

static void reedSolomonRemainder(const uint8_t data[QR_DATA_CODEWORDS],
                                 uint8_t ec[QR_EC_CODEWORDS]) {
    uint8_t gen[QR_EC_CODEWORDS + 1];
    reedSolomonGenerator(gen);
    memset(ec, 0, QR_EC_CODEWORDS);

    for (int i = 0; i < QR_DATA_CODEWORDS; i++) {
        uint8_t factor = data[i] ^ ec[0];
        memmove(&ec[0], &ec[1], QR_EC_CODEWORDS - 1);
        ec[QR_EC_CODEWORDS - 1] = 0;
        for (int j = 0; j < QR_EC_CODEWORDS; j++) {
            ec[j] ^= gfMul(gen[j + 1], factor);
        }
    }
}

static void appendBits(uint8_t *data, int *bitLen, uint32_t value, int count) {
    for (int i = count - 1; i >= 0; i--) {
        if ((value >> i) & 1) {
            data[*bitLen >> 3] |= (uint8_t)(0x80 >> (*bitLen & 7));
        }
        (*bitLen)++;
    }
}

static bool makeCodewords(const char *text, uint8_t out[QR_TOTAL_CODEWORDS]) {
    size_t len = strlen(text);
    if (len > 53) return false;

    uint8_t data[QR_DATA_CODEWORDS];
    memset(data, 0, sizeof(data));
    int bitLen = 0;

    appendBits(data, &bitLen, 0x4, 4);       /* byte mode */
    appendBits(data, &bitLen, (uint32_t)len, 8);
    for (size_t i = 0; i < len; i++) appendBits(data, &bitLen, (uint8_t)text[i], 8);

    int capacityBits = QR_DATA_CODEWORDS * 8;
    int terminator = capacityBits - bitLen;
    if (terminator > 4) terminator = 4;
    appendBits(data, &bitLen, 0, terminator);
    while ((bitLen & 7) != 0) appendBits(data, &bitLen, 0, 1);

    for (int i = bitLen >> 3; i < QR_DATA_CODEWORDS; i++) {
        data[i] = (i & 1) ? 0x11 : 0xEC;
    }

    uint8_t ec[QR_EC_CODEWORDS];
    reedSolomonRemainder(data, ec);
    memcpy(out, data, QR_DATA_CODEWORDS);
    memcpy(out + QR_DATA_CODEWORDS, ec, QR_EC_CODEWORDS);
    return true;
}

static bool getCodewordBit(const uint8_t codewords[QR_TOTAL_CODEWORDS], int bitIndex) {
    if (bitIndex >= QR_TOTAL_CODEWORDS * 8) return false;
    return ((codewords[bitIndex >> 3] >> (7 - (bitIndex & 7))) & 1) != 0;
}

static void drawCodewords(MCAL_QRCode_t *qr, const uint8_t codewords[QR_TOTAL_CODEWORDS]) {
    int bitIndex = 0;
    int direction = -1;

    for (int right = QR_UTIL_SIZE - 1; right >= 1; right -= 2) {
        if (right == 6) right--;
        for (int vert = 0; vert < QR_UTIL_SIZE; vert++) {
            int y = (direction == -1) ? (QR_UTIL_SIZE - 1 - vert) : vert;
            for (int j = 0; j < 2; j++) {
                int x = right - j;
                if (s_reserved[y][x]) continue;
                bool bit = getCodewordBit(codewords, bitIndex++);
                if (((x + y) & 1) == 0) bit = !bit; /* mask 0 */
                qrSet(qr, x, y, bit, false);
            }
        }
        direction = -direction;
    }
}

static void drawFormatBits(MCAL_QRCode_t *qr) {
    const uint16_t format = 0x77C4; /* ECC L, mask 0 */
    for (int i = 0; i <= 5; i++) qrSet(qr, 8, i, ((format >> i) & 1) != 0, true);
    qrSet(qr, 8, 7, ((format >> 6) & 1) != 0, true);
    qrSet(qr, 8, 8, ((format >> 7) & 1) != 0, true);
    qrSet(qr, 7, 8, ((format >> 8) & 1) != 0, true);
    for (int i = 9; i < 15; i++) qrSet(qr, 14 - i, 8, ((format >> i) & 1) != 0, true);

    for (int i = 0; i < 8; i++) qrSet(qr, QR_UTIL_SIZE - 1 - i, 8, ((format >> i) & 1) != 0, true);
    for (int i = 8; i < 15; i++) qrSet(qr, 8, QR_UTIL_SIZE - 15 + i, ((format >> i) & 1) != 0, true);
}

bool MCAL_QRCode_GenerateText(MCAL_QRCode_t *qr, const char *text) {
    if (!qr || !text) return false;
    memset(qr, 0, sizeof(MCAL_QRCode_t));
    memset(s_reserved, 0, sizeof(s_reserved));

    uint8_t codewords[QR_TOTAL_CODEWORDS];
    if (!makeCodewords(text, codewords)) return false;

    addFunctionPatterns(qr);
    drawCodewords(qr, codewords);
    drawFormatBits(qr);
    return true;
}

bool MCAL_QRCode_GenerateSetupUrl(MCAL_QRCode_t *qr, const char *url) {
    return MCAL_QRCode_GenerateText(qr, url);
}

bool MCAL_QRCode_GetModule(const MCAL_QRCode_t *qr, uint8_t x, uint8_t y) {
    if (!qr || x >= QR_UTIL_SIZE || y >= QR_UTIL_SIZE) return false;
    return qr->modules[y][x] != 0;
}
