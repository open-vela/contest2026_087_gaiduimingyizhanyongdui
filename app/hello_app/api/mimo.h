/****************************************************************************
 * FOCUS AIoT - MiMo 学习建议接口
 ****************************************************************************/

#ifndef FOCUS_AIOT_API_MIMO_H
#define FOCUS_AIOT_API_MIMO_H

#include <stddef.h>
#include <stdint.h>

#include "state_machine.h"

#ifdef __cplusplus
extern "C" {
#endif

int mimo_get_advice(session_stats_t *stats,
                    uint8_t distraction_by_type[4],
                    char *advice_out, size_t max_len);

#ifdef __cplusplus
}
#endif

#endif
