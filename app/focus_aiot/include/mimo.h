#ifndef FOCUS_AIOT_MIMO_H
#define FOCUS_AIOT_MIMO_H

#include <stddef.h>
#include <stdint.h>

#include "focus_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

int mimo_get_advice(session_stats_t *stats,
                    uint8_t distraction_by_type[4],
                    char *advice_out, size_t max_len);

/* Optional HTTP adapter supplied by the networking module. */
int mimo_http_post(const char *url, const char *json,
                   char *response, size_t response_size,
                   unsigned int timeout_ms);

#ifdef __cplusplus
}
#endif

#endif
