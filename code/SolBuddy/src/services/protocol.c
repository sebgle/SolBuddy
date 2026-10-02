#include "services/protocol.h"

/* Config field limits (§5.4). */
#define LIVE_TIMEOUT_MIN_MIN    1u
#define LIVE_TIMEOUT_MIN_MAX    60u
#define SAMPLE_INTERVAL_S_MIN   10u
#define SAMPLE_INTERVAL_S_MAX   3600u
#define ADV_INTERVAL_MS_MIN     20u      /* BLE advertising interval limits */
#define ADV_INTERVAL_MS_MAX     10240u

#define LOG_START_LEN           9u
#define LOG_STOP_LEN            1u
#define RECORD_BYTES            64u
#define ATT_HEADER_BYTES        3u

/* ---- little-endian helpers ---------------------------------------------- */

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)(v >> 8);
}

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* ---- Status ------------------------------------------------------------- */

void proto_pack_status(const proto_status_t *s, uint8_t out[PROTO_STATUS_LEN])
{
    out[0] = PROTO_VERSION;
    out[1] = s->state;
    out[2] = s->flags;
    out[3] = 0;                     /* reserved */
    put_u16(&out[4],  s->fw_version);
    put_u16(&out[6],  s->boot_id);
    put_u32(&out[8],  s->uptime_s);
    put_u32(&out[12], s->utc_s);
    put_u16(&out[16], s->vcell_mv);
    put_u16(&out[18], s->soc_x256);
    put_u32(&out[20], s->oldest_seq);
    put_u32(&out[24], s->next_seq);
    put_u16(&out[28], s->err_i2c);
    put_u16(&out[30], s->err_flash);
    put_u16(&out[32], s->err_sensor);
    put_u16(&out[34], 0);           /* reserved */
}

/* ---- Time --------------------------------------------------------------- */

proto_result_t proto_parse_time(const uint8_t *data, size_t len, uint32_t *utc_s)
{
    if (len != PROTO_TIME_LEN) return PROTO_ERR_LENGTH;
    uint32_t t = get_u32(data);
    if (t < PROTO_UTC_MIN) return PROTO_ERR_VALUE;
    *utc_s = t;
    return PROTO_OK;
}

/* ---- Config ------------------------------------------------------------- */

void proto_config_defaults(proto_config_t *c)
{
    c->live_timeout_min  = 5;
    c->sample_interval_s = 30;
    c->adv_interval_ms   = 1285;
}

void proto_pack_config(const proto_config_t *c, uint8_t out[PROTO_CONFIG_LEN])
{
    out[0] = PROTO_VERSION;
    out[1] = c->live_timeout_min;
    put_u16(&out[2], c->sample_interval_s);
    put_u16(&out[4], c->adv_interval_ms);
    put_u16(&out[6], 0);            /* reserved */
}

proto_result_t proto_parse_config(const uint8_t *data, size_t len, proto_config_t *c)
{
    if (len != PROTO_CONFIG_LEN) return PROTO_ERR_LENGTH;

    /* Decode into a local copy; touch *c only if every field is valid. */
    proto_config_t n;
    n.live_timeout_min  = data[1];
    n.sample_interval_s = get_u16(&data[2]);
    n.adv_interval_ms   = get_u16(&data[4]);

    if (data[0] != PROTO_VERSION) return PROTO_ERR_VALUE;
    if (n.live_timeout_min  < LIVE_TIMEOUT_MIN_MIN  || n.live_timeout_min  > LIVE_TIMEOUT_MIN_MAX)  return PROTO_ERR_VALUE;
    if (n.sample_interval_s < SAMPLE_INTERVAL_S_MIN || n.sample_interval_s > SAMPLE_INTERVAL_S_MAX) return PROTO_ERR_VALUE;
    if (n.adv_interval_ms   < ADV_INTERVAL_MS_MIN   || n.adv_interval_ms   > ADV_INTERVAL_MS_MAX)   return PROTO_ERR_VALUE;

    *c = n;
    return PROTO_OK;
}

/* ---- Log Read ----------------------------------------------------------- */

proto_result_t proto_parse_log_cmd(const uint8_t *data, size_t len, proto_log_cmd_t *cmd)
{
    if (len == 0u) return PROTO_ERR_LENGTH;

    switch (data[0]) {
        case PROTO_LOG_OP_START:
            if (len != LOG_START_LEN) return PROTO_ERR_LENGTH;
            cmd->op        = PROTO_LOG_OP_START;
            cmd->from_seq  = get_u32(&data[1]);
            cmd->max_count = get_u32(&data[5]);
            return PROTO_OK;

        case PROTO_LOG_OP_STOP:
            if (len != LOG_STOP_LEN) return PROTO_ERR_LENGTH;
            cmd->op        = PROTO_LOG_OP_STOP;
            cmd->from_seq  = 0;
            cmd->max_count = 0;
            return PROTO_OK;

        default:
            return PROTO_ERR_VALUE;
    }
}

void proto_pack_end(uint8_t reason, uint32_t next_seq, uint8_t out[PROTO_END_LEN])
{
    out[0] = PROTO_PKT_END;
    out[1] = reason;
    put_u32(&out[2], next_seq);
}

uint32_t proto_records_per_notification(uint16_t mtu)
{
    if (mtu < PROTO_MIN_MTU) return 0;
    return (uint32_t)(mtu - ATT_HEADER_BYTES) / RECORD_BYTES;
}
