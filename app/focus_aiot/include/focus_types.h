#ifndef FOCUS_AIOT_TYPES_H
#define FOCUS_AIOT_TYPES_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
  DEVICE_IDLE,
  DEVICE_MODE_SELECT,
  DEVICE_MONITORING,
  DEVICE_REPORT
} device_status_t;

typedef struct
{
  uint32_t session_start;
  uint32_t total_duration_sec;
  uint32_t effective_duration_sec;
  uint8_t distraction_count;
  uint8_t focus_score;
  uint8_t current_mode;
} session_stats_t;

typedef struct
{
  session_stats_t stats;
  uint8_t distraction_by_type[4];
  char advice[512];
} session_report_t;

typedef enum
{
  FOCUSED,
  GLANCING_PHONE,
  PLAYING_PHONE,
  AWAY,
  DROWSY
} status_t;

typedef enum
{
  NONE,
  REMIND,
  ENCOURAGE
} action_t;

typedef struct
{
  status_t status;
  action_t action;
  char message[128];
  bool milestone_reached;
  int milestone_minutes;
  uint8_t focus_score_delta;
} study_state_t;

#endif
