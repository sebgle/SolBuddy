#ifndef SOLBUDDY_SERVICES_CRC16_H
#define SOLBUDDY_SERVICES_CRC16_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * CRC-16/CCITT-FALSE: polynomial 0x1021, initial value 0xFFFF, no
 * reflection, no final XOR. Check value: "123456789" -> 0x29B1.
 */
uint16_t crc16_ccitt(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif
