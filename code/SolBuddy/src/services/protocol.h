#ifndef SOLBUDDY_SERVICES_PROTOCOL_H
#define SOLBUDDY_SERVICES_PROTOCOL_H

/*
 * BLE protocol byte layouts (docs/ble_protocol.md, version 1).
 * Pure packing/parsing: no BLE, no hardware. All fields little-endian.
 */

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PROTO_VERSION           1u
#define PROTO_STATUS_LEN        36u
#define PROTO_TIME_LEN          4u
#define PROTO_CONFIG_LEN        8u
#define PROTO_END_LEN           6u
#define PROTO_MIN_MTU           67u          /* one 64-byte record + 3 ATT */
#define PROTO_UTC_MIN           1704067200u  /* 2024-01-01: earlier = phone clock unset */

typedef enum {
    PROTO_OK = 0,
    PROTO_ERR_LENGTH,   /* -> ATT 0x0D Invalid Attribute Value Length */
    PROTO_ERR_VALUE     /* -> ATT 0xFF Out of Range */
} proto_result_t;

/* ---- Status (§5.1) ------------------------------------------------------- */

typedef enum {
    PROTO_STATE_NORMAL   = 0,
    PROTO_STATE_SYNC     = 1,
    PROTO_STATE_FAST_ADV = 2,
    PROTO_STATE_CHARGING = 3,
    PROTO_STATE_LIVE     = 4
} proto_state_t;

#define PROTO_STATUS_FLAG_UTC_SET   0x01u
#define PROTO_STATUS_FLAG_USB       0x02u
#define PROTO_STATUS_FLAG_LOW_BATT  0x04u

typedef struct {
    uint8_t  state;         /* proto_state_t */
    uint8_t  flags;         /* PROTO_STATUS_FLAG_* */
    uint16_t fw_version;    /* major << 8 | minor */
    uint16_t boot_id;
    uint32_t uptime_s;
    uint32_t utc_s;         /* 0 if not set */
    uint16_t vcell_mv;
    uint16_t soc_x256;
    uint32_t oldest_seq;
    uint32_t next_seq;
    uint16_t err_i2c;
    uint16_t err_flash;
    uint16_t err_sensor;
} proto_status_t;

void proto_pack_status(const proto_status_t *s, uint8_t out[PROTO_STATUS_LEN]);

/* ---- Time (§5.2) --------------------------------------------------------- */

proto_result_t proto_parse_time(const uint8_t *data, size_t len, uint32_t *utc_s);

/* ---- Config (§5.4) ------------------------------------------------------- */

typedef struct {
    uint8_t  live_timeout_min;      /* 1-60 */
    uint16_t sample_interval_s;     /* 10-3600 */
    uint16_t adv_interval_ms;       /* 20-10240 */
} proto_config_t;

void           proto_config_defaults(proto_config_t *c);
void           proto_pack_config(const proto_config_t *c, uint8_t out[PROTO_CONFIG_LEN]);
/* All-or-nothing: on error *c is left unchanged. */
proto_result_t proto_parse_config(const uint8_t *data, size_t len, proto_config_t *c);

/* ---- Log Read (§5.3) ----------------------------------------------------- */

#define PROTO_LOG_OP_START  0x01u
#define PROTO_LOG_OP_STOP   0x02u
#define PROTO_PKT_END       0xE0u
#define PROTO_PKT_LIVE_END  0xE1u

typedef enum {
    PROTO_END_CAUGHT_UP     = 0,
    PROTO_END_STOPPED       = 1,
    PROTO_END_MTU_TOO_SMALL = 2,
    PROTO_END_BAD_REQUEST   = 3,
    PROTO_END_STORAGE_ERROR = 4
} proto_end_reason_t;

typedef struct {
    uint8_t  op;            /* PROTO_LOG_OP_* */
    uint32_t from_seq;      /* START only */
    uint32_t max_count;     /* START only; 0 = no limit */
} proto_log_cmd_t;

proto_result_t proto_parse_log_cmd(const uint8_t *data, size_t len, proto_log_cmd_t *cmd);
void           proto_pack_end(uint8_t reason, uint32_t next_seq, uint8_t out[PROTO_END_LEN]);

/* Whole 64-byte records per notification at this ATT MTU; 0 if < 67. */
uint32_t proto_records_per_notification(uint16_t mtu);

#ifdef __cplusplus
}
#endif

#endif
