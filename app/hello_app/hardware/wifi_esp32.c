/****************************************************************************
 * FOCUS AIoT - 真实 WiFi 驱动 (hardware/wifi_esp32.c)
 *
 * 负责人: 张沐泽
 * 职责: STA 连接、连接状态/RSSI 查询、HTTP POST (lwIP POSIX socket)。
 *
 * 说明:
 *   - HTTP POST 基于 lwIP 的 POSIX socket API, 超时 3s + 重试 1 次 (≤6s)。
 *   - 连接/状态/RSSI 走 netdev wireless ioctl (wlan0)。
 *   - 板子镜像内置 wapi 命令, 也可在启动脚本里先 wapi connect 联网,
 *     本驱动的 wifi_http_post 只依赖已联网的 wlan0。
 ****************************************************************************/

#include "../api/wifi.h"
#include "../api/error.h"

#include <nuttx/config.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <netdb.h>
#include <nuttx/net/ioctl.h>
#include <nuttx/wireless/wireless.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define WIFI_IFNAME    "wlan0"
#define HTTP_TIMEOUT_SEC  3

/****************************************************************************
 * Name: wifi_connect
 *
 * 用 wireless ioctl 连 WPA2-PSK。若板子已用 wapi 连过网, 可直接返回成功。
 * ⚠️ 上板核验: 若 esp32s3 wifi 驱动不支持 SIOCSIWENCODEEXT, 改用 wapi。
 ****************************************************************************/
int wifi_connect(const char *ssid, const char *password)
{
  struct iwreq iwr;
  int sock;

  if (ssid == NULL || password == NULL)
    {
      return FOCUS_ERR_PARAM;
    }

  if (wifi_is_connected())
    {
      return FOCUS_OK;
    }

  sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0)
    {
      return FOCUS_ERR_NET_DISCONN;
    }

  memset(&iwr, 0, sizeof(iwr));
  strncpy(iwr.ifr_name, WIFI_IFNAME, IFNAMSIZ);

  /* 设置 SSID */
  iwr.u.essid.flags = 1;               /* active */
  iwr.u.essid.length = strlen(ssid);
  iwr.u.essid.pointer = (FAR void *)ssid;
  if (ioctl(sock, SIOCSIWESSID, (unsigned long)&iwr) < 0)
    {
      close(sock);
      return FOCUS_ERR_NET_DISCONN;
    }

  /* 设置 WPA2 密码 (key) */
  iwr.u.essid.flags = IW_ENCODE_DISABLED;
  iwr.u.essid.length = strlen(password);
  iwr.u.essid.pointer = (FAR void *)password;
  if (ioctl(sock, SIOCSIWENCODEEXT, (unsigned long)&iwr) < 0)
    {
      close(sock);
      return FOCUS_ERR_NET_DISCONN;
    }

  /* 提交连接 */
  if (ioctl(sock, SIOCSIWCOMMIT, (unsigned long)&iwr) < 0)
    {
      close(sock);
      return FOCUS_ERR_NET_DISCONN;
    }

  close(sock);
  return FOCUS_OK;
}

/****************************************************************************
 * Name: wifi_is_connected
 *
 * 通过 wlan0 的 IFF_UP + IFF_RUNNING 判断是否已联网。
 ****************************************************************************/
bool wifi_is_connected(void)
{
  struct ifreq ifr;
  int sock = socket(AF_INET, SOCK_DGRAM, 0);

  if (sock < 0)
    {
      return false;
    }

  memset(&ifr, 0, sizeof(ifr));
  strncpy(ifr.ifr_name, WIFI_IFNAME, IFNAMSIZ);
  if (ioctl(sock, SIOCGIFFLAGS, (unsigned long)&ifr) < 0)
    {
      close(sock);
      return false;
    }

  close(sock);
  return (ifr.ifr_flags & IFF_UP) && (ifr.ifr_flags & IFF_RUNNING);
}

/****************************************************************************
 * Name: wifi_get_rssi
 *
 * 通过 wireless ioctl SIOCGIWSTATS 取信号强度 (dBm)。
 ****************************************************************************/
int wifi_get_rssi(void)
{
  struct iwreq iwr;
  int sock = socket(AF_INET, SOCK_DGRAM, 0);

  if (sock < 0)
    {
      return 0;
    }

  memset(&iwr, 0, sizeof(iwr));
  strncpy(iwr.ifr_name, WIFI_IFNAME, IFNAMSIZ);
  if (ioctl(sock, SIOCGIWSTATS, (unsigned long)&iwr) < 0)
    {
      close(sock);
      return 0;
    }

  close(sock);
  return (int)iwr.u.stats.qual.level;
}

/****************************************************************************
 * Name: parse_url
 *
 * 从 "http://host[:port]/path" 解析出 host/port/path。
 ****************************************************************************/
static int parse_url(const char *url, char *host, size_t hostlen,
                     uint16_t *port, char *path, size_t pathlen)
{
  const char *p;
  const char *host_start;
  const char *path_start;
  const char *colon;
  size_t host_len;

  if (strncmp(url, "http://", 7) != 0)
    {
      return -1;
    }
  p = url + 7;

  host_start = p;
  path_start = strchr(p, '/');
  if (path_start == NULL)
    {
      path_start = p + strlen(p);
    }

  host_len = (size_t)(path_start - host_start);
  if (host_len == 0 || host_len >= hostlen)
    {
      return -1;
    }

  colon = memchr(host_start, ':', host_len);
  if (colon != NULL)
    {
      *port = (uint16_t)atoi(colon + 1);
      host_len = (size_t)(colon - host_start);
    }
  else
    {
      *port = 80;
    }

  memcpy(host, host_start, host_len);
  host[host_len] = '\0';

  snprintf(path, pathlen, "%s", path_start);

  return 0;
}

/****************************************************************************
 * Name: wifi_http_post
 *
 * HTTP POST (阻塞), 超时 3s + 重试 1 次, 总计 ≤6s。
 * 返回: 0=成功, FOCUS_ERR_NET_DISCONN=连接失败, FOCUS_ERR_TIMEOUT=超时。
 ****************************************************************************/
int wifi_http_post(const char *url, const char *body,
                   char *resp, size_t maxlen)
{
  char host[64];
  char path[256];
  char req[2048];
  uint16_t port;
  int attempt;

  if (url == NULL || body == NULL || resp == NULL || maxlen == 0)
    {
      return FOCUS_ERR_PARAM;
    }

  if (parse_url(url, host, sizeof(host), &port, path, sizeof(path)) < 0)
    {
      return FOCUS_ERR_PARAM;
    }

  for (attempt = 0; attempt < 2; attempt++)
    {
      struct sockaddr_in addr;
      struct timeval tv;
      struct hostent *he;
      int sock;
      int req_len;
      int n;

      he = gethostbyname(host);
      if (he == NULL)
        {
          return FOCUS_ERR_NET_DNS;
        }

      sock = socket(AF_INET, SOCK_STREAM, 0);
      if (sock < 0)
        {
          return FOCUS_ERR_NET_DISCONN;
        }

      tv.tv_sec = HTTP_TIMEOUT_SEC;
      tv.tv_usec = 0;
      setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
      setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

      memset(&addr, 0, sizeof(addr));
      addr.sin_family = AF_INET;
      addr.sin_port = htons(port);
      memcpy(&addr.sin_addr, he->h_addr, he->h_length);

      if (connect(sock, (FAR struct sockaddr *)&addr, sizeof(addr)) < 0)
        {
          close(sock);
          if (attempt == 0)
            {
              continue;
            }
          return FOCUS_ERR_NET_DISCONN;
        }

      req_len = snprintf(req, sizeof(req),
                         "POST %s HTTP/1.1\r\n"
                         "Host: %s\r\n"
                         "Content-Type: application/json\r\n"
                         "Content-Length: %d\r\n"
                         "Connection: close\r\n\r\n"
                         "%s",
                         path, host, (int)strlen(body), body);
      if (req_len < 0 || (size_t)req_len >= sizeof(req))
        {
          close(sock);
          return FOCUS_ERR_PARAM;
        }

      if (send(sock, req, req_len, 0) < 0)
        {
          close(sock);
          if (attempt == 0)
            {
              continue;
            }
          return FOCUS_ERR_NET_DISCONN;
        }

      n = recv(sock, resp, maxlen - 1, 0);
      close(sock);

      if (n > 0)
        {
          resp[n] = '\0';
          return FOCUS_OK;
        }

      if (n == 0)
        {
          return FOCUS_ERR_NET_DISCONN;
        }

      /* n < 0: 超时(EAGAIN/EWOULDBLOCK) 或错误 */
      if (attempt == 0)
        {
          continue;
        }
    }

  return FOCUS_ERR_TIMEOUT;
}

/****************************************************************************
 * Name: 独立测试 (编译时 -DTEST_WIFI)
 ****************************************************************************/
#ifdef TEST_WIFI
int main(void)
{
  char resp[4096];
  int ret;

  ret = wifi_connect("SSID", "PASSWORD");
  printf("wifi_connect=%d connected=%d rssi=%d dBm\n",
         ret, wifi_is_connected(), wifi_get_rssi());

  ret = wifi_http_post("http://httpbin.org/post",
                       "{\"test\":1}", resp, sizeof(resp));
  printf("HTTP POST=%d\n%s\n", ret, resp);
  return 0;
}
#endif
