#include "app/clock.h"
#include "services/timekeeper.h"

#include <FreeRTOS.h>
#include <task.h>

static timekeeper_t s_tk;

void clock_init(void)
{
    timekeeper_init(&s_tk, configTICK_RATE_HZ, xTaskGetTickCount());
}

uint32_t clock_uptime_s(void)
{
    return timekeeper_uptime_s(&s_tk, xTaskGetTickCount());
}

void clock_set_utc(uint32_t utc_s)
{
    timekeeper_set_utc(&s_tk, utc_s, xTaskGetTickCount());
}

uint32_t clock_timestamp(bool *is_utc)
{
    return timekeeper_timestamp(&s_tk, xTaskGetTickCount(), is_utc);
}
