#include "lcd.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char *argv[])
{
  session_stats_t stats =
  {
    .total_duration_sec = 3661,
    .effective_duration_sec = 3000,
    .distraction_count = 2,
    .focus_score = 82,
    .current_mode = 0
  };
  study_state_t study =
  {
    .status = FOCUSED,
    .action = NONE
  };
  int ret;

  (void)argc;
  (void)argv;
  ret = lcd_init();
  if (ret < 0)
    {
      fprintf(stderr, "focus_aiot: lcd_init failed: %d\n", ret);
      return 1;
    }
  ret = lcd_show_status(DEVICE_MONITORING, &stats, &study);
  if (ret < 0)
    {
      fprintf(stderr, "focus_aiot: render failed: %d\n", ret);
      return 1;
    }
  printf("Focus AIoT UI initialized.\n");
  return 0;
}
