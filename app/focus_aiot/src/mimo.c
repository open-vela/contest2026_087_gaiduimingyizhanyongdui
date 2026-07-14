#include "mimo.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#ifndef MIMO_API_URL
#  define MIMO_API_URL "https://api.mimo.example/v1/advice"
#endif

#define MIMO_TIMEOUT_MS 5000U

#if defined(__GNUC__)
extern int mimo_http_post(const char *, const char *, char *, size_t,
                          unsigned int) __attribute__((weak));
#endif

static void local_advice(const session_stats_t *stats, char *output,
                         size_t output_size)
{
  unsigned long effective_minutes =
      (unsigned long)(stats->effective_duration_sec / 60U);
  if (stats->current_mode == 0)
    {
      snprintf(output, output_size,
               "严格模式下你坚持了 %lu 分钟，下次减少分心次数会更好！",
               effective_minutes);
    }
  else
    {
      snprintf(output, output_size,
               "你今天有效学习了 %lu 分钟，已经很棒了！继续加油！",
               effective_minutes);
    }
}

int mimo_get_advice(session_stats_t *stats,
                    uint8_t distraction_by_type[4],
                    char *advice_out, size_t max_len)
{
  char request[512];
  int ret = -ENOSYS;

  if (advice_out == NULL || max_len == 0) return -EINVAL;
  advice_out[0] = '\0';
  if (stats == NULL || distraction_by_type == NULL)
    {
      snprintf(advice_out, max_len, "学习已完成，请稍后查看详细建议。");
      return 0;
    }

  snprintf(request, sizeof(request),
           "{\"total_min\":%lu,\"effective_min\":%lu,"
           "\"distractions\":[%u,%u,%u,%u],\"focus_score\":%u,"
           "\"mode\":\"%s\"}",
           (unsigned long)(stats->total_duration_sec / 60U),
           (unsigned long)(stats->effective_duration_sec / 60U),
           distraction_by_type[0], distraction_by_type[1],
           distraction_by_type[2], distraction_by_type[3],
           stats->focus_score, stats->current_mode == 0 ? "strict" : "gentle");

  if (mimo_http_post != NULL)
    {
      ret = mimo_http_post(MIMO_API_URL, request, advice_out, max_len,
                           MIMO_TIMEOUT_MS);
      advice_out[max_len - 1] = '\0';
    }

  if (ret < 0 || advice_out[0] == '\0')
    {
      local_advice(stats, advice_out, max_len);
    }
  return 0;
}
