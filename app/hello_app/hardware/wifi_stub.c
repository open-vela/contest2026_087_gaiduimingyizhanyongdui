/****************************************************************************
 * Temporary network backend for offline UI validation. The real Wi-Fi
 * module overrides these weak symbols when it is linked.
 ****************************************************************************/

#include "../api/error.h"
#include "../api/wifi.h"

#if defined(__GNUC__) && !defined(_WIN32)
#  define FOCUS_WEAK __attribute__((weak))
#else
#  define FOCUS_WEAK
#endif

FOCUS_WEAK bool wifi_is_connected(void)
{
  return false;
}

FOCUS_WEAK int wifi_http_post(const char *url, const char *body,
                              char *resp, size_t maxlen)
{
  (void)url;
  (void)body;
  (void)resp;
  (void)maxlen;
  return FOCUS_ERR_NET_DISCONN;
}
