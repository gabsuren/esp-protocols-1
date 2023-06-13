/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "freertos/task.h"


TimerHandle_t xTimerCreate( const char *const pcTimerName,  /*lint !e971 Unqualified char types are allowed for strings and single characters only. */
                            const TickType_t xTimerPeriodInTicks,
                            const UBaseType_t uxAutoReload,
                            void *const pvTimerID,
                            TimerCallbackFunction_t pxCallbackFunction )
{
    Timer_t *pxNewTimer = ( Timer_t * ) malloc( sizeof( 1) );

    return pxNewTimer;
}
