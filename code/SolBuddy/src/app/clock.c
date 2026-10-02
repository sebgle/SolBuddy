#include "app/clock.h"
#include "services/timekeeper.h"

#include <FreeRTOS.h>
#include <task.h>

/* Read by the sampling task, written by the BLE task (Time characteristic).
 * Each call updates several fields, so it runs in a critical section: no
 * task switch can see the timekeeper half-updated. The work inside is a
 * few arithmetic operations. */
static timekeeper_t s_tk;

void clock_init(void)
{
    taskENTER_CRITICAL();
    timekeeper_init(&s_tk, configTICK_RATE_HZ, xTaskGetTickCount());
    taskEXIT_CRITICAL();
}

uint32_t clock_uptime_s(void)
{
    taskENTER_CRITICAL();
    uint32_t s = timekeeper_uptime_s(&s_tk, xTaskGetTickCount());
    taskEXIT_CRITICAL();
    return s;
}

void clock_set_utc(uint32_t utc_s)
{
    taskENTER_CRITICAL();
    timekeeper_set_utc(&s_tk, utc_s, xTaskGetTickCount());
    taskEXIT_CRITICAL();
}

uint32_t clock_timestamp(bool *is_utc)
{
    taskENTER_CRITICAL();
    uint32_t t = timekeeper_timestamp(&s_tk, xTaskGetTickCount(), is_utc);
    taskEXIT_CRITICAL();
    return t;
}
