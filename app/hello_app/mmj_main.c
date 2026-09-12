/****************************************************************************
 * mmj_main.c - 喵喵机(MiaoMiaoJi) 板端主程序 · M1 主页框架
 *
 * 形态: 纯英语学习机(独占系统)。主页 = 大时钟/英文日期 + WiFi 状态 +
 *       学习/闯关/词库/设置 四个矩形入口(后续里程碑逐步落地各页)。
 *
 * 板端接入:
 *   - 显示/输入 : lv_nuttx_init() (/dev/fb0 + /dev/input0)
 *   - 中文字体  : 运行时加载 /etc/fonts/font_puhui_{12,16,16b,21,28}_4.bin
 *                 (按模拟器实际文案精简生成, 5 档合计约 230KB;
 *                  新增中文文案后需重跑 tools/gen_board_fonts.js)
 *   - 触摸      : LVGL 按钮事件(不再用整画布手势)
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/compiler.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>

#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/boardctl.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <errno.h>

#include <lvgl/lvgl.h>

#include <wireless/wapi.h>     /* WiFi 扫描(wapi 库, CONFIG_WIRELESS_WAPI=y) */
#include <netutils/netlib.h>   /* DNS 服务器设置(NTP 域名解析需要) */
#include <sched.h>             /* task_create(后台校时任务) */
#include <strings.h>           /* strncasecmp */

#include "mmj_words.h"

/* 时间同步见下方 mmj_ntp_task(不再使用 ntpc_start: daemon 创建易失败) */

#define SCR_W        320
#define SCR_H        480

/* 主页布局坐标(320x480 像素风: 大块直角卡片) */
#define TIME_X       18
#define TIME_Y       22
#define DATE_X       18
#define WIFI_R       16
#define TILE_X       18
#define TILE_W       138
#define TILE_H       88
#define TILE_GAP     12
#define TILE_TOP     250

enum {
  PAGE_HOME,
  PAGE_LEARN,
  PAGE_QUEST,
  PAGE_BOOK,
  PAGE_SET,
  PAGE_WIFI,
  PAGE_AI,
};

static const lv_font_t *g_font = NULL;   /* 默认中文(16px) */
static const lv_font_t *g_fipa = NULL;   /* 音标(IPA, MiSans; 普惠体无 IPA 字形) */
static const lv_font_t *g_f12  = NULL;   /* 小标签 / 底部 / 例句中文 */
static const lv_font_t *g_f16  = NULL;   /* 正文 / 设置项 / 按钮 */
static const lv_font_t *g_f16b = NULL;   /* 粗体: 入口卡片中文 */
static const lv_font_t *g_f21  = NULL;   /* 中文释义 */
static const lv_font_t *g_f28  = NULL;   /* 大标题 / 时间 */
static lv_obj_t *scr = NULL;

/* 页面切换 / 学习评级 / 统计(定义在文件后部) */
static void show_page(int page);

/* AI/Web 管理(定义在文件后部): 设置页引用 */
static char g_ai_key[80];
static char g_tts_voice[24];               /* TTS 音色(默认冰糖) */
static int  wifi_ip_str(char *out, int len);
static void ai_cfg_load(void);
static void tts_speak(const char *text);   /* 单词发音(学习页/AI 页引用) */
static void learn_cb(lv_event_t *e);
static int  json_int(const char *buf, const char *key, int def);
static int  cnt_learned(void);        /* 已学词数 */
static void build_home_stats(void);   /* 主页统计条(需访问 NWORD, 定义在后部) */

static lv_obj_t *lbl_time   = NULL;
static lv_obj_t *lbl_date   = NULL;
static lv_obj_t *lbl_wifi   = NULL;

static const char *WD[] = {"SUNDAY","MONDAY","TUESDAY","WEDNESDAY",
                           "THURSDAY","FRIDAY","SATURDAY"};
static const char *MO[] = {"JANUARY","FEBRUARY","MARCH","APRIL","MAY","JUNE",
                           "JULY","AUGUST","SEPTEMBER","OCTOBER","NOVEMBER",
                           "DECEMBER"};

/* 主题色(运行时切换: 纸色/夜幕/掌机) —— 宏指向变量, 故各页面零改动即支持换肤 */
static lv_color_t TH_PAPER, TH_INK, TH_LINE, TH_MUTED, TH_PANEL;
static int g_theme = 0;              /* 0=纸色 1=夜幕 2=掌机 */

#define C_PAPER  TH_PAPER
#define C_INK    TH_INK
#define C_LINE   TH_LINE
#define C_MUTED  TH_MUTED
#define C_PANEL  TH_PANEL
#define C_GREEN  lv_color_hex(0x5fbf61)
#define C_ORANGE lv_color_hex(0xf09a3e)
#define C_BLUE   lv_color_hex(0x5b93e0)
#define C_PURPLE lv_color_hex(0x9c83c8)
#define C_WHITE  lv_color_hex(0xffffff)

/* 应用主题(三套配色取自模拟器 tools/mmj_sim/index.html) */
static void theme_apply(int t)
{
  switch (t)
    {
      case 1:                        /* 夜幕 Night */
        TH_PAPER = lv_color_hex(0x1f2a40);
        TH_PANEL = lv_color_hex(0x28354f);
        TH_INK   = lv_color_hex(0xf0ead9);
        TH_MUTED = lv_color_hex(0x8fa1c0);
        TH_LINE  = lv_color_hex(0x0c1322);
        break;
      case 2:                        /* 掌机 GB */
        TH_PAPER = lv_color_hex(0xc6ddb4);
        TH_PANEL = lv_color_hex(0xb2d0a0);
        TH_INK   = lv_color_hex(0x243b22);
        TH_MUTED = lv_color_hex(0x557a4e);
        TH_LINE  = lv_color_hex(0x243b22);
        break;
      default:                       /* 纸色 Paper */
        TH_PAPER = lv_color_hex(0xf8f3e6);
        TH_PANEL = lv_color_hex(0xefe8d5);
        TH_INK   = lv_color_hex(0x1d2027);
        TH_MUTED = lv_color_hex(0x938d7d);
        TH_LINE  = lv_color_hex(0x1d2027);
        break;
    }
  g_theme = t;
}

/* 是否已关联 AP(AP MAC 非 0)
 * 仅查 IP 会误判 —— 驱动/netinit 启动时就给 wlan0 配了地址 */
static bool wifi_associated(void)
{
  struct iwreq iwr;
  const unsigned char *mac;
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  int i;

  if (fd < 0) return false;
  memset(&iwr, 0, sizeof(iwr));
  strncpy(iwr.ifr_name, "wlan0", IFNAMSIZ - 1);
  bool ok = (ioctl(fd, SIOCGIWAP, (unsigned long)&iwr) == 0);
  close(fd);
  if (!ok) return false;
  mac = (const unsigned char *)iwr.u.ap_addr.sa_data;
  for (i = 0; i < 6; i++)
    {
      if (mac[i] != 0) return true;
    }
  return false;
}

/* wlan0 是否拿到有效 IP(排除 0.0.0.0) */
static bool wifi_has_ip(void)
{
  struct ifreq ifr;
  struct sockaddr_in *sa;
  int fd = socket(AF_INET, SOCK_DGRAM, 0);

  if (fd < 0) return false;
  memset(&ifr, 0, sizeof(ifr));
  strncpy(ifr.ifr_name, "wlan0", IFNAMSIZ - 1);
  bool ok = (ioctl(fd, SIOCGIFADDR, (unsigned long)&ifr) == 0);
  close(fd);
  if (!ok) return false;
  sa = (struct sockaddr_in *)&ifr.ifr_addr;
  return sa->sin_addr.s_addr != 0;
}

/* 真正联网 = 已关联 AP 且拿到 IP */
static bool wifi_up(void)
{
  return wifi_associated() && wifi_has_ip();
}

/* 取当前关联的 SSID(联网后由驱动上报) */
static bool wifi_get_current_ssid(char *out, int cap)
{
  int sock = wapi_make_socket();
  char ess[WAPI_ESSID_MAX_SIZE + 1];
  bool ok;

  out[0] = '\0';
  if (sock < 0) return false;
  ok = (wapi_get_essid(sock, "wlan0", ess, sizeof(ess)) == 0);
  close(sock);
  if (!ok) return false;
  strncpy(out, ess, cap - 1);
  out[cap - 1] = '\0';
  return out[0] != '\0';
}

/* ==================== WiFi 扫描/连接 ==================== */

static char g_wifi_ssid[33];
static char g_wifi_pass[64];
static char g_wifi_ap[16][33];          /* 扫描到的热点名 */
static int  g_wifi_rssi[16];
static int  g_wifi_n;                   /* 热点数 */
static volatile bool g_wifi_busy = false; /* 等待 wifi_manager 完成连接 */
static lv_obj_t *lbl_wifi_st = NULL;    /* WiFi 页状态行 */
static bool g_ntpc_started = false;     /* 校时只启动一次 */

static void wifi_st_update(void);       /* 前向声明(upd_clock 引用) */

/* 读取 wifi_manager 约定的 /data/wifi.cfg (SSID=/PASSWORD=) */
static void wifi_conf_load(void)
{
  FILE *fp = fopen("/data/wifi.cfg", "r");
  char line[128];

  g_wifi_ssid[0] = '\0';
  g_wifi_pass[0] = '\0';
  if (fp == NULL) return;
  while (fgets(line, sizeof(line), fp) != NULL)
    {
      line[strcspn(line, "\n")] = '\0';
      if (strncmp(line, "SSID=", 5) == 0)
        {
          strncpy(g_wifi_ssid, line + 5, sizeof(g_wifi_ssid) - 1);
          g_wifi_ssid[sizeof(g_wifi_ssid) - 1] = '\0';
        }
      else if (strncmp(line, "PASSWORD=", 9) == 0)
        {
          strncpy(g_wifi_pass, line + 9, sizeof(g_wifi_pass) - 1);
          g_wifi_pass[sizeof(g_wifi_pass) - 1] = '\0';
        }
    }
  fclose(fp);
}

/* ---- 时间同步: 后台任务直连 NTP(固定 IP, 免 DNS) + HTTP Date 兜底 ----
 * 不用 ntpc_start: 其 daemon 线程在内存紧张时创建失败(真机日志 "ntp start failed")。
 * curl 输出写 /data(yaffs 已挂载; 板上无 /tmp)。 */

#define MMJ_NTP_IP "203.107.6.88"        /* ntp.aliyun.com, 固定 IP 免域名解析 */

static int ntp_query_once(void)
{
  struct sockaddr_in srv;
  struct timeval tv;
  unsigned char pkt[48];
  unsigned char rsp[48];
  socklen_t slen = sizeof(srv);
  uint32_t secs;
  time_t epoch;
  struct timespec ts;
  ssize_t n;
  int fd;

  fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) return -1;

  tv.tv_sec  = 4;                        /* 阻塞上限 4s, 仅在后台任务里跑 */
  tv.tv_usec = 0;
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  memset(&srv, 0, sizeof(srv));
  srv.sin_family      = AF_INET;
  srv.sin_port        = htons(123);
  srv.sin_addr.s_addr = inet_addr(MMJ_NTP_IP);

  memset(pkt, 0, sizeof(pkt));
  pkt[0] = 0x1b;                         /* LI=0 VN=3 Mode=3(client) */
  if (sendto(fd, pkt, sizeof(pkt), 0, (struct sockaddr *)&srv, sizeof(srv)) < 0)
    {
      close(fd);
      return -2;
    }

  n = recvfrom(fd, rsp, sizeof(rsp), 0, (struct sockaddr *)&srv, &slen);
  close(fd);
  if (n < 48) return -3;

  /* NTP 64 位时间戳: 偏移 40 处 32 位秒, 自 1900 起 */
  secs = ((uint32_t)rsp[40] << 24) | ((uint32_t)rsp[41] << 16) |
         ((uint32_t)rsp[42] << 8) | rsp[43];
  if (secs == 0) return -4;

  epoch = (time_t)(secs - 2208988800u);  /* 1900 → 1970 */
  if (epoch < 1600000000) return -5;     /* 时间不合理(2020 年前) */

  ts.tv_sec  = epoch;
  ts.tv_nsec = 0;
  clock_settime(CLOCK_REALTIME, &ts);
  return 0;
}

static void http_date_sync(void)
{
  FILE *fp;
  char line[256];
  static const char *const MONS[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                       "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  int i;

  system("curl -skI -m 6 http://www.baidu.com > /data/htd.txt 2>/dev/null");
  fp = fopen("/data/htd.txt", "r");
  if (fp == NULL)
    {
      printf("[mmj] http date: no output\n");
      return;
    }
  while (fgets(line, sizeof(line), fp) != NULL)
    {
      char mon_s[8];
      int day, yy, hh, mm, ss, mon;
      struct tm tmz;
      struct timespec ts;
      time_t epoch;

      if (strncasecmp(line, "Date:", 5) != 0) continue;
      if (sscanf(line + 5, " %*3s %d %3s %d %d:%d:%d",
                 &day, mon_s, &yy, &hh, &mm, &ss) != 6) continue;
      for (mon = -1, i = 0; i < 12; i++)
        {
          if (strncasecmp(mon_s, MONS[i], 3) == 0) { mon = i; break; }
        }
      if (mon < 0) break;

      memset(&tmz, 0, sizeof(tmz));
      tmz.tm_year = yy - 1900;
      tmz.tm_mon  = mon;
      tmz.tm_mday = day;
      tmz.tm_hour = hh;
      tmz.tm_min  = mm;
      tmz.tm_sec  = ss;
      epoch = mktime(&tmz);               /* TZ=CST-8: tm 被视为本地时间 */
      if (epoch <= 0) break;
      epoch -= 8 * 3600;                  /* HTTP Date 是 GMT → 换算 UTC epoch */
      ts.tv_sec  = epoch;
      ts.tv_nsec = 0;
      clock_settime(CLOCK_REALTIME, &ts);
      printf("[mmj] clock synced via HTTP Date\n");
      break;
    }
  fclose(fp);
  unlink("/data/htd.txt");
}

/* 后台校时任务(task_create 创建, 不占 LVGL 线程) */
static int mmj_ntp_task(int argc, char *argv[])
{
  (void)argc; (void)argv;

  sleep(2);                              /* 等网络栈就绪 */
  if (ntp_query_once() == 0)
    {
      printf("[mmj] clock synced via NTP\n");
      return 0;
    }
  printf("[mmj] ntp query failed, try HTTP Date\n");
  http_date_sync();
  return 0;
}

/* 联网后启动校时(仅一次) */
static void wifi_ntpc_kick(void)
{
  if (!g_ntpc_started && wifi_up())
    {
      struct in_addr dns;

      g_ntpc_started = true;

      /* DHCP 可能未下发 DNS —— 手动设公共 DNS(curl 域名解析需要) */
      dns.s_addr = inet_addr("223.5.5.5");
      netlib_set_ipv4dnsaddr(&dns);
      dns.s_addr = inet_addr("114.114.114.114");
      netlib_set_ipv4dnsaddr(&dns);

      if (task_create("mmj_ntp", 100, 2048, mmj_ntp_task, NULL) < 0)
        printf("[mmj] ntp task create failed\n");
      else
        printf("[mmj] time sync task started\n");
    }
}

/* 发起连接: 只写 /data/wifi.cfg, 固件 wifi_manager 守护(开机自启, 5s 轮询)
 * 检测到变化后自动执行 wapi 连接 + renew wlan0(DHCP)。
 * 这里写完配置后轮询联网状态, 最多 30s。 */
static bool wifi_do_connect(const char *ssid, const char *psk)
{
  FILE *fp;
  size_t len = (psk != NULL) ? strnlen(psk, 64) : 0;
  int i;

  if (len < 8 || len > 63)
    {
      return false;                    /* WPA2 密码 8..63(wifi_manager 同规则) */
    }

  fp = fopen("/data/wifi.cfg", "w");
  if (fp == NULL) return false;
  fprintf(fp, "SSID=%s\n", ssid);
  fprintf(fp, "PASSWORD=%s\n", psk);
  fclose(fp);

  strncpy(g_wifi_ssid, ssid, sizeof(g_wifi_ssid) - 1);
  g_wifi_ssid[sizeof(g_wifi_ssid) - 1] = '\0';
  strncpy(g_wifi_pass, psk, sizeof(g_wifi_pass) - 1);
  g_wifi_pass[sizeof(g_wifi_pass) - 1] = '\0';

  printf("[mmj] wifi config saved: %s (wifi_manager will connect)\n", ssid);

  g_wifi_busy = true;
  for (i = 0; i < 30; i++)
    {
      if (wifi_up())
        {
          g_wifi_busy = false;
          wifi_ntpc_kick();
          return true;
        }
      sleep(1);
    }
  g_wifi_busy = false;
  return false;
}

/* 阻塞扫描: 返回发现的热点数(结果写入 g_wifi_ap[]) */
static int wifi_do_scan(void)
{
  int sock = wapi_make_socket();
  struct wapi_list_s aps;
  struct wapi_scan_info_s *it;
  int i;

  g_wifi_n = 0;
  if (sock < 0) return 0;

  wapi_set_ifup(sock, "wlan0");
  wapi_set_mode(sock, "wlan0", WAPI_MODE_MANAGED);

  if (wapi_escan_init(sock, "wlan0", IW_SCAN_TYPE_ACTIVE, NULL) == 0)
    {
      for (i = 0; i < 40; i++)          /* 最长 ~8s 等扫描结束 */
        {
          int s = wapi_scan_stat(sock, "wlan0");
          if (s != 0) break;            /* 0=进行中, 非0=完成/出错 */
          usleep(200000);
        }

      memset(&aps, 0, sizeof(aps));
      if (wapi_scan_coll(sock, "wlan0", &aps) == 0)
        {
          for (it = aps.head.scan; it != NULL && g_wifi_n < 16; it = it->next)
            {
              if (!it->has_essid || it->essid[0] == '\0') continue;
              strncpy(g_wifi_ap[g_wifi_n], it->essid,
                      sizeof(g_wifi_ap[0]) - 1);
              g_wifi_ap[g_wifi_n][sizeof(g_wifi_ap[0]) - 1] = '\0';
              g_wifi_rssi[g_wifi_n] = it->has_rssi ? it->rssi : -99;
              g_wifi_n++;
            }
          wapi_scan_coll_free(&aps);
        }
    }
  close(sock);
  return g_wifi_n;
}

/* 统一控件观感: 去阴影/小圆角/去描边(与模拟器方角卡片风一致) */
static void style_card(lv_obj_t *o)
{
  lv_obj_set_style_shadow_width(o, 0, 0);
  lv_obj_set_style_radius(o, 2, 0);
  lv_obj_set_style_outline_width(o, 0, 0);
}

/* ---------- 小工具 ---------- */

/* 时钟渲染(建页与每秒刷新共用; 建页立即调用可消除 "--:--" 闪烁) */
static void clock_render(void)
{
  char buf[64];
  time_t now = time(NULL);
  struct tm *lt = localtime(&now);

  if (lt == NULL) return;
  snprintf(buf, sizeof(buf), "%02d:%02d", lt->tm_hour, lt->tm_min);
  if (lbl_time) lv_label_set_text(lbl_time, buf);
  snprintf(buf, sizeof(buf), "%s, %s %d",
           WD[lt->tm_wday], MO[lt->tm_mon], lt->tm_mday);
  if (lbl_date) lv_label_set_text(lbl_date, buf);
}

static void upd_clock(lv_timer_t *tmr)
{
  (void)tmr;
  clock_render();

  if (lbl_wifi) lv_label_set_text(lbl_wifi, wifi_up() ? "WIFI" : "OFF");

  wifi_ntpc_kick();            /* 联网后自动启动校时(仅一次) */
  wifi_st_update();            /* WiFi 页状态行 */
}

/* ---------- 入口按钮 ---------- */

static void btn_cb(lv_event_t *e)
{
  lv_obj_t *btn = lv_event_get_target(e);
  long id = (long)lv_obj_get_user_data(btn);
  show_page((int)id);
}

static lv_obj_t *mk_tile(int x, int y, const char *txt,
                         const char *cap, lv_color_t c, long id)
{
  (void)cap;  /* M2 起用于按钮副标题 */
  lv_obj_t *btn = lv_btn_create(scr);
  lv_obj_set_pos(btn, x, y);
  lv_obj_set_size(btn, TILE_W, TILE_H);
  lv_obj_set_style_bg_color(btn, c, 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(btn, 3, 0);
  lv_obj_set_style_border_color(btn, C_LINE, 0);
  lv_obj_set_user_data(btn, (void *)id);
  lv_obj_add_event_cb(btn, btn_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t *lbl = lv_label_create(btn);
  if (g_f16b) lv_obj_set_style_text_font(lbl, g_f16b, 0);
  lv_label_set_text(lbl, txt);
  lv_obj_set_style_text_color(lbl, C_WHITE, 0);
  lv_obj_center(lbl);
  return btn;
}

/* 统一左上角返回按钮: 各页面共用, 尺寸/样式/按下反馈一致 */
static lv_obj_t *mk_back(int target)
{
  lv_obj_t *btn = lv_btn_create(scr);
  lv_obj_t *lbl;

  lv_obj_set_size(btn, 64, 36);
  lv_obj_set_pos(btn, 8, 8);
  lv_obj_set_style_bg_color(btn, C_PANEL, 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(btn, 2, 0);
  lv_obj_set_style_border_color(btn, C_LINE, 0);
  lv_obj_set_style_bg_color(btn, C_MUTED, LV_STATE_PRESSED);
  lv_obj_set_user_data(btn, (void *)(long)target);
  lv_obj_add_event_cb(btn, btn_cb, LV_EVENT_CLICKED, NULL);
  lbl = lv_label_create(btn);
  if (g_f16) lv_obj_set_style_text_font(lbl, g_f16, 0);
  lv_label_set_text(lbl, "<");
  lv_obj_center(lbl);
  return btn;
}

/* ---------- 主页 ---------- */

static void build_home(void)
{
  lv_obj_set_style_bg_color(scr, C_PAPER, 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

  /* 左上: 时间(行1)/英文日期(行2) */
  lbl_time = lv_label_create(scr);
  lv_obj_set_pos(lbl_time, TIME_X, TIME_Y);
  if (g_f28) lv_obj_set_style_text_font(lbl_time, g_f28, 0);
  lv_obj_set_style_text_color(lbl_time, C_INK, 0);
  lv_label_set_text(lbl_time, "--:--");

  lbl_date = lv_label_create(scr);
  lv_obj_set_pos(lbl_date, DATE_X, TIME_Y + 32);
  lv_obj_set_style_text_font(lbl_date, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(lbl_date, C_MUTED, 0);
  lv_label_set_text(lbl_date, "-----");
  clock_render();              /* 建页即渲染当前时间, 消除横线闪烁 */

  /* 右上: WiFi 状态文字 */
  lbl_wifi = lv_label_create(scr);
  lv_obj_align(lbl_wifi, LV_ALIGN_TOP_RIGHT, -WIFI_R, TIME_Y + 8);
  if (g_f12) lv_obj_set_style_text_font(lbl_wifi, g_f12, 0);
  lv_obj_set_style_text_color(lbl_wifi, C_MUTED, 0);
  lv_label_set_text(lbl_wifi, "---");

  /* 统计条(已学 / 错词 / 已解锁) */
  build_home_stats();

  /* 2x2 入口(矩形像素卡片) */
  mk_tile(TILE_X,                      TILE_TOP, "学习", "LEARN", C_GREEN,  PAGE_LEARN);
  mk_tile(TILE_X + TILE_W + TILE_GAP,  TILE_TOP, "闯关", "QUEST", C_ORANGE, PAGE_QUEST);
  mk_tile(TILE_X,                      TILE_TOP + TILE_H + TILE_GAP,
          "词库", "BOOKS", C_BLUE,   PAGE_BOOK);
  mk_tile(TILE_X + TILE_W + TILE_GAP,  TILE_TOP + TILE_H + TILE_GAP,
          "设置", "SETUP", C_PURPLE, PAGE_SET);

  /* 底部小字 */
  lv_obj_t *foot = lv_label_create(scr);
  lv_obj_align(foot, LV_ALIGN_BOTTOM_MID, 0, -12);
  if (g_f12) lv_obj_set_style_text_font(foot, g_f12, 0);
  lv_obj_set_style_text_color(foot, C_MUTED, 0);
  lv_label_set_text(foot, "MMJ 0.1 · CET-4 WORD QUEST");

}

/* ==================== 数据层: /data/mmj ====================
 * 进度持久化(断电不丢)。格式为合法 JSON:
 *   {"v":1,"n":24,"w":[c0,e0, c1,e1, ...]}
 * 读写均流式进行, 不依赖大缓冲, 词库扩到数千词也不会撑爆栈。
 * 注意: 生成容器/词库文件时需同步扩大 gen_board_fonts.js 的字符集。
 */

#define MMJ_DIR  "/data/mmj"
#define MMJ_FILE MMJ_DIR "/words.json"

/* ==================== M2: 学习页 ==================== */

/* 词库数据: mmj_words.c (tools/gen_words.py 生成, CET-4 全量 7508 词) */
#define WORDS MMJ_WORDS
#define NWORD MMJ_NWORD

static int g_page      = PAGE_HOME;
static int g_cur       = 0;          /* 当前词索引 */
static int g_learn_cnt[NWORD];       /* 学习次数 */
static int g_err_cnt[NWORD];         /* 错误次数 */
static int g_lv[NWORD];              /* 复习等级 0..5 */
static int g_due[NWORD];             /* 到期日(epoch 天数) */
static int g_rep[NWORD];             /* 复习次数 */
static int g_streak[NWORD];          /* 连续答对次数 */

/* 分级间隔复习: 等级 0..5 对应 1/2/4/7/15/30 天 */
static const int REP_DAYS[6] = {1, 2, 4, 7, 15, 30};

static int today_days(void)
{
  return (int)(time(NULL) / 86400);
}

/* 保存: 写"学习次数,错误次数"扁平数组 */
static void data_save(void)
{
  FILE *fp;
  int i;

  mkdir(MMJ_DIR, 0777);          /* 已存在则返回 EEXIST, 忽略 */

  fp = fopen(MMJ_FILE, "w");
  if (fp == NULL)
    {
      printf("[mmj] save failed: %s\n", MMJ_FILE);
      return;
    }

  fprintf(fp, "{\"v\":2,\"n\":%d,\"w\":[", NWORD);
  for (i = 0; i < NWORD; i++)
    fprintf(fp, "%s%d,%d,%d,%d,%d,%d", i ? "," : "",
            g_learn_cnt[i], g_err_cnt[i], g_lv[i], g_due[i], g_rep[i],
            g_streak[i]);
  fprintf(fp, "]}\n");
  fclose(fp);
}

/* 落盘节流 -------------------------------------------------------------
 * 7508 词的进度文件约 40KB, 每点评级都写一次 NAND 会明显卡顿。
 * 策略: 评级只置脏标记并起 1.5s 一次性定时器, 期间连续操作只落盘一次;
 *       离开学习页时立即补写, 保证不丢。
 */
static int          g_dirty = 0;
static lv_timer_t  *g_save_timer = NULL;

static void save_timer_cb(lv_timer_t *t)
{
  lv_timer_del(t);
  g_save_timer = NULL;
  if (g_dirty)
    {
      g_dirty = 0;
      data_save();
    }
}

static void data_save_later(void)
{
  g_dirty = 1;
  if (g_save_timer == NULL)
    g_save_timer = lv_timer_create(save_timer_cb, 1500, NULL);
}

/* 后台落盘任务: 写 NAND 不再阻塞 UI(返回键卡顿的根因) */
static volatile int g_save_busy = 0;

static int save_bg_task(int argc, char *argv[])
{
  (void)argc; (void)argv;
  do
    {
      g_dirty = 0;
      data_save();
    }
  while (g_dirty);             /* 写盘期间又有新评级则补写 */
  g_save_busy = 0;
  printf("[mmj] progress saved (bg)\n");
  return 0;
}

/* 立即落盘(离开学习页时调用) —— 交给后台任务执行 */
static void data_save_now(void)
{
  if (g_save_timer != NULL)
    {
      lv_timer_del(g_save_timer);
      g_save_timer = NULL;
    }
  if (g_dirty)
    {
      g_dirty = 0;
      if (!g_save_busy)
        {
          g_save_busy = 1;
          if (task_create("mmj_save", 110, 8192, save_bg_task, NULL) < 0)
            {
              g_save_busy = 0;
              g_dirty = 1;     /* 任务创建失败, 保留脏标记稍后再写 */
            }
        }
      else
        {
          g_dirty = 1;         /* 上一轮还在写, 保留脏标记由其收尾 */
        }
    }
}

/* 加载: 逐字符扫描数字, 顺序为 c0,e0,c1,e1,... */
static void data_load(void)
{
  FILE *fp = fopen(MMJ_FILE, "r");
  int c, val = 0, innum = 0, started = 0, idx = 0;
  char hdr[64];
  size_t hn;

  if (fp == NULL)
    {
      printf("[mmj] no saved progress, start fresh\n");
      return;
    }

  /* 版本校验: 仅接受 v2(每词 6 字段); 旧 v1(2 字段)按位解析会错乱, 直接作废 */
  hn = fread(hdr, 1, sizeof(hdr) - 1, fp);
  hdr[hn] = '\0';
  if (strstr(hdr, "\"v\":2") == NULL)
    {
      printf("[mmj] words.json 版本不是 v2, 忽略旧进度(将重新开始)\n");
      fclose(fp);
      return;
    }

  /* 词表规模校验: 词库增删后下标会整体错位, 宁可重来也不要把进度错配给别人 */
  {
    int nsaved = json_int(hdr, "\"n\":", -1);
    if (nsaved != NWORD)
      {
        printf("[mmj] 词库规模已变(%d -> %d), 忽略旧进度\n", nsaved, NWORD);
        fclose(fp);
        return;
      }
  }
  fseek(fp, 0, SEEK_SET);

  while ((c = fgetc(fp)) != EOF)
    {
      if (!started)
        {
          if (c == '[') started = 1;   /* 跳过头部, 到数据数组 */
          continue;
        }
      if (c >= '0' && c <= '9')
        {
          val = val * 10 + (c - '0');
          innum = 1;
        }
      else if (innum)
        {
          int wi = idx / 6;
          if (wi < NWORD)
            {
              switch (idx % 6)
                {
                  case 0:  g_learn_cnt[wi] = val; break;
                  case 1:  g_err_cnt[wi]   = val; break;
                  case 2:  g_lv[wi]        = val; break;
                  case 3:  g_due[wi]       = val; break;
                  case 4:  g_rep[wi]       = val; break;
                  default: g_streak[wi]    = val; break;
                }
            }
          idx++;
          val = 0;
          innum = 0;
          if (c == ']') break;
        }
      else if (c == ']')
        {
          break;
        }
    }

  fclose(fp);
  printf("[mmj] progress loaded: %d values\n", idx);
}

/* 选下一个词, 优先级: 最早到期(含逾期) > 错词重学 > 新词 > 轮转 */
static void next_word(void)
{
  int i, k, today = today_days();
  int best = -1, bestdue = 0;

  for (i = 1; i <= NWORD; i++)              /* 1) 到期复习(取最早) */
    {
      k = (g_cur + i) % NWORD;
      if (g_learn_cnt[k] > 0 && g_due[k] <= today)
        if (best < 0 || g_due[k] < bestdue) { best = k; bestdue = g_due[k]; }
    }
  if (best >= 0) { g_cur = best; return; }

  for (i = 1; i <= NWORD; i++)              /* 2) 错词重学(未回到正常链) */
    {
      k = (g_cur + i) % NWORD;
      if (g_err_cnt[k] > 0 && g_lv[k] < 3) { g_cur = k; return; }
    }

  for (i = 1; i <= NWORD; i++)              /* 3) 新词 */
    {
      k = (g_cur + i) % NWORD;
      if (g_learn_cnt[k] == 0) { g_cur = k; return; }
    }

  g_cur = (g_cur + 1) % NWORD;              /* 4) 兜底轮转 */
}

static void learn_tts_cb(lv_event_t *e)
{
  (void)e;
  tts_speak(WORDS[g_cur].w);           /* 朗读当前单词 */
}

static void build_learn(void)
{
  const mmj_word_t *w = &WORDS[g_cur];
  char buf[160];
  lv_obj_t *o;
  int i;
  static const char *bt[3] = {"没记住", "模糊", "记住了"};
  lv_color_t bc[3];
  bc[0] = lv_color_hex(0xd4563f);   /* 没记住: 红 */
  bc[1] = lv_color_hex(0xf09a3e);   /* 模糊:   橙 */
  bc[2] = lv_color_hex(0x5fbf61);   /* 记住了: 绿 */

  lv_obj_set_style_bg_color(scr, C_PAPER, 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

  /* 顶栏: 返回 + 标题 + 进度 */
  lv_obj_t *back = mk_back(PAGE_HOME);

  o = lv_label_create(scr);
  lv_obj_set_pos(o, 80, 10);
  if (g_f16) lv_obj_set_style_text_font(o, g_f16, 0);
  {
    int td = today_days();
    if (g_learn_cnt[g_cur] == 0)
      snprintf(buf, sizeof(buf), "新词");
    else if (g_due[g_cur] <= td)
      snprintf(buf, sizeof(buf), "待复习 Lv%d", g_lv[g_cur]);
    else
      snprintf(buf, sizeof(buf), "Lv%d · %d 天后复习",
               g_lv[g_cur], g_due[g_cur] - td);
  }
  lv_label_set_text(o, buf);

  /* 右上: 发音按钮(MiMo TTS 朗读单词) */
  o = lv_btn_create(scr);
  lv_obj_set_size(o, 92, 28);
  lv_obj_set_pos(o, 220, 8);
  lv_obj_set_style_bg_color(o, C_PANEL, 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(o, 2, 0);
  lv_obj_set_style_border_color(o, C_LINE, 0);
  lv_obj_set_style_bg_color(o, C_MUTED, LV_STATE_PRESSED);
  lv_obj_add_event_cb(o, learn_tts_cb, LV_EVENT_CLICKED, NULL);
  o = lv_label_create(o);
  if (g_f16) lv_obj_set_style_text_font(o, g_f16, 0);
  lv_label_set_text(o, "发音");
  lv_obj_center(o);

  /* 单词(拉丁大字号) */
  o = lv_label_create(scr);
  lv_obj_set_pos(o, 16, 52);
  lv_obj_set_style_text_font(o, &lv_font_montserrat_30, 0);
  lv_obj_set_style_text_color(o, C_INK, 0);
  lv_label_set_text(o, w->w);

  /* 音标(含 IPA, MiSans 字体): 固定宽 + 自动换行, 长音标折到下一行 */
  o = lv_label_create(scr);
  lv_obj_set_pos(o, 16, 90);
  lv_obj_set_width(o, 288);
  lv_label_set_long_mode(o, LV_LABEL_LONG_WRAP);
  if (g_fipa) lv_obj_set_style_text_font(o, g_fipa, 0);
  lv_obj_set_style_text_color(o, C_MUTED, 0);
  snprintf(buf, sizeof(buf), "UK %s  US %s", w->uk, w->us);
  lv_label_set_text(o, buf);

  /* 词性 + 中文释义(28px 大字) */
  o = lv_label_create(scr);
  lv_obj_set_pos(o, 16, 132);
  lv_obj_set_width(o, 288);
  lv_label_set_long_mode(o, LV_LABEL_LONG_WRAP);
  if (g_f28) lv_obj_set_style_text_font(o, g_f28, 0);
  lv_obj_set_style_text_color(o, C_INK, 0);
  snprintf(buf, sizeof(buf), "%s %s", w->pos, w->cn);
  lv_label_set_text(o, buf);

  /* 例句(英) + 例句(中) */
  o = lv_label_create(scr);
  lv_obj_set_pos(o, 16, 224);
  lv_obj_set_width(o, 288);
  lv_label_set_long_mode(o, LV_LABEL_LONG_WRAP);
  if (g_f16) lv_obj_set_style_text_font(o, g_f16, 0);
  lv_label_set_text(o, w->ex);

  o = lv_label_create(scr);
  lv_obj_set_pos(o, 16, 268);
  lv_obj_set_width(o, 288);
  lv_label_set_long_mode(o, LV_LABEL_LONG_WRAP);
  if (g_f16) lv_obj_set_style_text_font(o, g_f16, 0);
  lv_obj_set_style_text_color(o, C_MUTED, 0);
  lv_label_set_text(o, w->exz);

  /* 底部三个评级按钮 */
  for (i = 0; i < 3; i++)
    {
      lv_obj_t *b = lv_btn_create(scr);
      lv_obj_set_size(b, 92, 46);
      lv_obj_set_pos(b, 12 + i * 99, 410);
      lv_obj_set_style_bg_color(b, bc[i], 0);
      lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
      lv_obj_set_style_border_width(b, 2, 0);
      lv_obj_set_style_border_color(b, C_LINE, 0);
      lv_obj_set_user_data(b, (void *)(long)(i + 1));
      lv_obj_add_event_cb(b, learn_cb, LV_EVENT_CLICKED, NULL);
      o = lv_label_create(b);
      if (g_f16) lv_obj_set_style_text_font(o, g_f16, 0);
      lv_obj_set_style_text_color(o, C_WHITE, 0);
      lv_label_set_text(o, bt[i]);
      lv_obj_center(o);
    }
}

/* 评级: 1=没记住 2=模糊 3=记住了 */
static void learn_cb(lv_event_t *e)
{
  lv_obj_t *b = lv_event_get_target(e);
  long score = (long)lv_obj_get_user_data(b);

  {
    int td = today_days();

    g_learn_cnt[g_cur]++;

    if (score == 1)                    /* 没记住: 降级到 0, 明天再来 */
      {
        g_err_cnt[g_cur]++;
        g_lv[g_cur]     = 0;
        g_streak[g_cur] = 0;
        g_due[g_cur]    = td + 1;
      }
    else if (score == 2)               /* 模糊: 降一级, 明天再来 */
      {
        if (g_lv[g_cur] > 0) g_lv[g_cur]--;
        g_streak[g_cur] = 0;
        g_due[g_cur]    = td + 1;
      }
    else                               /* 记住了 */
      {
        g_streak[g_cur]++;
        if (g_streak[g_cur] >= 2)      /* 连续 2 次对才升级 */
          {
            if (g_lv[g_cur] < 5) g_lv[g_cur]++;
            g_streak[g_cur] = 0;
          }
        if (g_err_cnt[g_cur] > 0) g_err_cnt[g_cur]--;   /* 销错 */
        g_due[g_cur] = td + REP_DAYS[g_lv[g_cur]];
        g_rep[g_cur]++;
      }
  }

  data_save_later();           /* 节流落盘(见 data_save_later) */
  next_word();
  lv_obj_clean(scr);           /* 原地重画当前页(不走 show_page, 避免立即落盘) */
  build_learn();
}

/* ==================== M3: 闯关页 ==================== */

#define UNIT_SIZE  20                     /* 每 Unit 词数(7508 词 -> 376 单元) */
#define QUEST_NUM  10                     /* 每关题数 */
#define NUNIT      ((NWORD + UNIT_SIZE - 1) / UNIT_SIZE)

static int g_q_unit   = 0;    /* 当前 Unit(0 起) */
static int g_q_hearts = 3;    /* 剩余心数 */
static int g_q_done   = 0;    /* 已答题数 */
static int g_q_right  = 0;    /* 答对数 */
static int g_q_cur    = 0;    /* 当前题的词索引 */
static int g_q_opts[4];       /* 4 个选项的词索引 */
static int g_q_wait   = 0;    /* 已作答, 等待切题(防连点) */
static int unit_unlocked(int u);      /* 前向声明(被 quest_pick_unit 调用) */
static void quest_pick_unit(void);
static int g_q_redo[64];      /* 本局答错待重测的词索引 */
static int g_q_redo_n = 0;
static int g_q_redo_i = 0;    /* 重测进行到第几个 */
static int g_q_in_redo = 0;   /* 是否处于重测阶段 */
static lv_timer_t *g_q_timer = NULL;  /* 切题定时器句柄(切页时需取消) */

static void build_quest(void);
static void build_quest_result(void);
static void q_opt_cb(lv_event_t *e);

/* 选第一个已解锁的 Unit(全都没解锁则停在 Unit 0, 显示未解锁页) */
static void quest_pick_unit(void)
{
  int i;
  for (i = 0; i < NUNIT; i++)
    if (unit_unlocked(i))
      {
        g_q_unit = i;
        return;
      }
  g_q_unit = 0;
}

/* Unit 是否解锁: 该 Unit 全部词都学过 */
static int unit_unlocked(int u)
{
  int i;
  for (i = u * UNIT_SIZE; i < NWORD && i < (u + 1) * UNIT_SIZE; i++)
    if (g_learn_cnt[i] == 0) return 0;
  return 1;
}

static int unit_learned(int u)
{
  int i, n = 0;
  for (i = u * UNIT_SIZE; i < NWORD && i < (u + 1) * UNIT_SIZE; i++)
    if (g_learn_cnt[i] > 0) n++;
  return n;
}

/* 为当前词 g_q_cur 生成 4 个选项(1 正确 + 3 干扰)并打乱 */
static void quest_make_opts(void)
{
  int i, j, t;

  g_q_opts[0] = g_q_cur;
  for (i = 1; i < 4; i++)
    {
      int dup;
      do
        {
          t = rand() % NWORD;
          dup = 0;
          for (j = 0; j < i; j++) if (g_q_opts[j] == t) dup = 1;
        }
      while (dup);
      g_q_opts[i] = t;
    }

  for (i = 3; i > 0; i--)          /* 洗牌 */
    {
      j = rand() % (i + 1);
      t = g_q_opts[i];
      g_q_opts[i] = g_q_opts[j];
      g_q_opts[j] = t;
    }
}

/* 出题: 本 Unit 已学词中随机选一词并生成选项
 * 错 >=3 次的词优先(更高权重), 其次全部已学词 */
static void quest_make(void)
{
  int i, n = 0, cand[64];
  int base = g_q_unit * UNIT_SIZE;
  int end  = base + UNIT_SIZE;

  if (end > NWORD) end = NWORD;

  for (i = base; i < end; i++)                      /* 高权重: 错 >=3 次 */
    if (g_learn_cnt[i] > 0 && g_err_cnt[i] >= 3) cand[n++] = i;

  if (n == 0)
    for (i = base; i < end; i++)                    /* 其次: 任一已学词 */
      if (g_learn_cnt[i] > 0) cand[n++] = i;

  if (n == 0)
    {
      g_q_cur = 0;
      quest_make_opts();
      return;
    }

  g_q_cur = cand[rand() % n];
  quest_make_opts();
}

static void q_next_cb(lv_timer_t *t)
{
  lv_timer_del(t);
  g_q_timer = NULL;
  g_q_wait = 0;

  if (g_page != PAGE_QUEST)            /* 已离开闯关页: 不要再动屏幕 */
    return;

  lv_obj_clean(scr);                   /* 关键: 清掉上一题残留控件
                                        * 否则每题叠加(对象累积)、结算页与题干重叠 */

  if (g_q_hearts <= 0)                 /* 心用完 -> 结束 */
    {
      build_quest_result();
      return;
    }

  if (!g_q_in_redo)
    {
      if (g_q_done >= QUEST_NUM)       /* 正常题答完 */
        {
          if (g_q_redo_n > 0)          /* 有错词 -> 进入当场重测 */
            {
              g_q_in_redo = 1;
              g_q_redo_i  = 0;
            }
          else
            {
              build_quest_result();
              return;
            }
        }
    }
  else
    {
      g_q_redo_i++;                    /* 重测逐题推进 */
      if (g_q_redo_i >= g_q_redo_n)
        {
          build_quest_result();
          return;
        }
    }

  build_quest();
}

/* 点击选项: 高亮 + 计分, 700ms 后自动切题 */
static void q_opt_cb(lv_event_t *e)
{
  lv_obj_t *b = lv_event_get_target(e);
  long slot = (long)lv_obj_get_user_data(b);
  int ok;

  if (g_q_wait) return;
  g_q_wait = 1;

  ok = (g_q_opts[slot] == g_q_cur);
  lv_obj_set_style_bg_color(b, ok ? C_GREEN : lv_color_hex(0xd4563f), 0);

  if (ok)
    {
      g_q_right++;
    }
  else
    {
      g_q_hearts--;                    /* 扣心 */

      /* 回流到复习调度: 降级回炉, 明天再来(否则离开本局错词即丢失) */
      g_err_cnt[g_q_cur]++;
      if (g_learn_cnt[g_q_cur] == 0) g_learn_cnt[g_q_cur] = 1;
      g_lv[g_q_cur]     = 0;
      g_streak[g_q_cur] = 0;
      g_due[g_q_cur]    = today_days() + 1;
      data_save_later();

      if (!g_q_in_redo && g_q_redo_n < 64)   /* 记入待重测(去重) */
        {
          int k, dup = 0;
          for (k = 0; k < g_q_redo_n; k++)
            if (g_q_redo[k] == g_q_cur) { dup = 1; break; }
          if (!dup) g_q_redo[g_q_redo_n++] = g_q_cur;
        }
    }

  g_q_done++;
  if (g_q_timer != NULL)               /* 防重复挂定时器 */
    {
      lv_timer_del(g_q_timer);
      g_q_timer = NULL;
    }
  g_q_timer = lv_timer_create(q_next_cb, 700, NULL);
}

/* 未解锁页 */
static void build_quest_locked(void)
{
  char buf[80];
  lv_obj_t *o;

  lv_obj_set_style_bg_color(scr, C_PAPER, 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

  lv_obj_t *back = mk_back(PAGE_HOME);

  o = lv_label_create(scr);
  lv_obj_set_pos(o, 80, 10);
  if (g_f16) lv_obj_set_style_text_font(o, g_f16, 0);
  snprintf(buf, sizeof(buf), "Unit %d 未解锁", g_q_unit + 1);
  lv_label_set_text(o, buf);

  o = lv_label_create(scr);
  lv_obj_align(o, LV_ALIGN_CENTER, 0, -30);
  if (g_f21) lv_obj_set_style_text_font(o, g_f21, 0);
  lv_label_set_text(o, "先学完本单元全部单词");

  o = lv_label_create(scr);
  lv_obj_align(o, LV_ALIGN_CENTER, 0, 10);
  if (g_f21) lv_obj_set_style_text_font(o, g_f21, 0);
  lv_obj_set_style_text_color(o, C_MUTED, 0);
  {
    int uend = (g_q_unit + 1) * UNIT_SIZE;   /* 末单元可能不足 UNIT_SIZE 词 */
    if (uend > NWORD) uend = NWORD;
    snprintf(buf, sizeof(buf), "已学 %d/%d", unit_learned(g_q_unit),
             uend - g_q_unit * UNIT_SIZE);
  }
  lv_label_set_text(o, buf);
}

static void build_quest_result(void)
{
  char buf[80];
  int stars, o_num, o_den, i;
  lv_obj_t *o;

  lv_obj_set_style_bg_color(scr, C_PAPER, 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

  o_num = g_q_right;
  o_den = g_q_done ? g_q_done : 1;
  stars = (o_num * 100 / o_den >= 90) ? 3 :
          (o_num * 100 / o_den >= 70) ? 2 :
          (o_num * 100 / o_den >= 50) ? 1 : 0;

  o = lv_label_create(scr);
  lv_obj_align(o, LV_ALIGN_TOP_MID, 0, 40);
  if (g_f21) lv_obj_set_style_text_font(o, g_f21, 0);
  snprintf(buf, sizeof(buf), "Unit %d 闯关结束", g_q_unit + 1);
  lv_label_set_text(o, buf);

  o = lv_label_create(scr);
  lv_obj_align(o, LV_ALIGN_TOP_MID, 0, 90);
  if (g_f28) lv_obj_set_style_text_font(o, g_f28, 0);  /* ★☆ 属中文字体字符集 */
  for (i = 0, buf[0] = '\0'; i < 3; i++)
    strcat(buf, i < stars ? "★" : "☆");
  lv_label_set_text(o, buf);

  o = lv_label_create(scr);
  lv_obj_align(o, LV_ALIGN_TOP_MID, 0, 140);
  if (g_f21) lv_obj_set_style_text_font(o, g_f21, 0);
  lv_obj_set_style_text_color(o, C_MUTED, 0);
  snprintf(buf, sizeof(buf), "答对 %d/%d", g_q_right, g_q_done);
  lv_label_set_text(o, buf);

  /* 再闯一次 / 返回主页 */
  lv_obj_t *b;
  b = lv_btn_create(scr);
  lv_obj_set_size(b, 130, 46);
  lv_obj_set_pos(b, 18, 300);
  lv_obj_set_style_bg_color(b, C_BLUE, 0);
  lv_obj_set_style_border_width(b, 2, 0);
  lv_obj_set_style_border_color(b, C_LINE, 0);
  lv_obj_set_user_data(b, (void *)PAGE_QUEST);
  lv_obj_add_event_cb(b, btn_cb, LV_EVENT_CLICKED, NULL);
  o = lv_label_create(b);
  if (g_f16) lv_obj_set_style_text_font(o, g_f16, 0);
  lv_obj_set_style_text_color(o, C_WHITE, 0);
  lv_label_set_text(o, "再闯一次");
  lv_obj_center(o);

  b = lv_btn_create(scr);
  lv_obj_set_size(b, 130, 46);
  lv_obj_set_pos(b, 172, 300);
  lv_obj_set_style_bg_color(b, C_PANEL, 0);
  lv_obj_set_style_border_width(b, 2, 0);
  lv_obj_set_style_border_color(b, C_LINE, 0);
  lv_obj_set_user_data(b, (void *)PAGE_HOME);
  lv_obj_add_event_cb(b, btn_cb, LV_EVENT_CLICKED, NULL);
  o = lv_label_create(b);
  if (g_f16) lv_obj_set_style_text_font(o, g_f16, 0);
  lv_label_set_text(o, "回主页");
  lv_obj_center(o);
}

static void build_quest(void)
{
  const mmj_word_t *w;
  char buf[80];
  lv_obj_t *o;
  int i;

  if (!unit_unlocked(g_q_unit))
    {
      build_quest_locked();
      return;
    }

  /* 每次进入都是一道新题: 重测阶段考错词, 正常阶段随机出题 */
  if (g_q_in_redo)
    {
      g_q_cur = g_q_redo[g_q_redo_i];
      quest_make_opts();
    }
  else
    {
      quest_make();
    }

  w = &WORDS[g_q_cur];

  lv_obj_set_style_bg_color(scr, C_PAPER, 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

  /* 顶栏: 返回 + 标题 + 心 */
  lv_obj_t *back = mk_back(PAGE_HOME);

  /* 右上: AI 挑战入口(大模型出题) */
  o = lv_btn_create(scr);
  lv_obj_set_size(o, 92, 28);
  lv_obj_set_pos(o, 220, 8);
  lv_obj_set_style_bg_color(o, C_PANEL, 0);
  lv_obj_set_style_border_width(o, 2, 0);
  lv_obj_set_style_border_color(o, C_LINE, 0);
  lv_obj_set_user_data(o, (void *)PAGE_AI);
  lv_obj_add_event_cb(o, btn_cb, LV_EVENT_CLICKED, NULL);
  o = lv_label_create(o);
  if (g_f16) lv_obj_set_style_text_font(o, g_f16, 0);
  lv_label_set_text(o, "AI 挑战");
  lv_obj_center(o);

  o = lv_label_create(scr);
  lv_obj_set_pos(o, 80, 10);
  if (g_f16) lv_obj_set_style_text_font(o, g_f16, 0);
  if (g_q_in_redo)
    snprintf(buf, sizeof(buf), "错词重测  %d/%d", g_q_redo_i + 1, g_q_redo_n);
  else
    snprintf(buf, sizeof(buf), "Unit %d 闯关  %d/%d", g_q_unit + 1,
             g_q_done + 1, QUEST_NUM);
  lv_label_set_text(o, buf);

  o = lv_label_create(scr);
  lv_obj_align(o, LV_ALIGN_TOP_RIGHT, -10, 10);
  if (g_f16) lv_obj_set_style_text_font(o, g_f16, 0);
  lv_obj_set_style_text_color(o, lv_color_hex(0xd4563f), 0);
  for (i = 0, buf[0] = '\0'; i < 3; i++)
    strcat(buf, i < g_q_hearts ? "♥" : "♡");
  lv_label_set_text(o, buf);

  /* 题干: 中文释义(中→英) */
  o = lv_label_create(scr);
  lv_obj_set_width(o, 288);
  lv_obj_set_pos(o, 16, 70);
  lv_label_set_long_mode(o, LV_LABEL_LONG_WRAP);
  if (g_f21) lv_obj_set_style_text_font(o, g_f21, 0);
  lv_obj_set_style_text_color(o, C_INK, 0);
  snprintf(buf, sizeof(buf), "%s  选出对应的英文", w->cn);
  lv_label_set_text(o, buf);

  /* 4 个选项(2x2), 文字为英文单词 */
  for (i = 0; i < 4; i++)
    {
      lv_obj_t *b = lv_btn_create(scr);
      lv_obj_set_size(b, 138, 56);
      lv_obj_set_pos(b, 18 + (i % 2) * 146, 240 + (i / 2) * 68);
      lv_obj_set_style_bg_color(b, C_PANEL, 0);
      lv_obj_set_style_border_width(b, 2, 0);
      lv_obj_set_style_border_color(b, C_LINE, 0);
      lv_obj_set_user_data(b, (void *)(long)i);
      lv_obj_add_event_cb(b, q_opt_cb, LV_EVENT_CLICKED, NULL);
      o = lv_label_create(b);
      lv_obj_set_style_text_font(o, &lv_font_montserrat_16, 0);
      lv_label_set_text(o, WORDS[g_q_opts[i]].w);
      lv_obj_center(o);
    }
}

/* ==================== 统计 / M4 词库页 ==================== */

static int cnt_learned(void)
{
  int i, n = 0;
  for (i = 0; i < NWORD; i++) if (g_learn_cnt[i] > 0) n++;
  return n;
}

/* 今日到期(含逾期): 已学过且到期日 <= 今天 */
static int cnt_due(void)
{
  int i, n = 0, td = today_days();
  for (i = 0; i < NWORD; i++)
    if (g_learn_cnt[i] > 0 && g_due[i] <= td) n++;
  return n;
}

/* 已逾期: 到期日早于今天 */
static int cnt_overdue(void)
{
  int i, n = 0, td = today_days();
  for (i = 0; i < NWORD; i++)
    if (g_learn_cnt[i] > 0 && g_due[i] < td) n++;
  return n;
}

/* 主页统计条: 今日到期 / 已逾期 / 已学 */
static void build_home_stats(void)
{
  static const char *labs[3] = {"今日到期", "已逾期", "已学"};
  char vb[3][16];
  lv_obj_t *o;
  int i;

  snprintf(vb[0], sizeof(vb[0]), "%d", cnt_due());
  snprintf(vb[1], sizeof(vb[1]), "%d", cnt_overdue());
  snprintf(vb[2], sizeof(vb[2]), "%d/%d", cnt_learned(), NWORD);

  for (i = 0; i < 3; i++)
    {
      o = lv_label_create(scr);
      lv_obj_set_pos(o, 20 + i * 98, 104);
      if (g_f21) lv_obj_set_style_text_font(o, g_f21, 0);
      lv_obj_set_style_text_color(o, C_INK, 0);
      lv_label_set_text(o, vb[i]);

      o = lv_label_create(scr);
      lv_obj_set_pos(o, 20 + i * 98, 134);
      if (g_f12) lv_obj_set_style_text_font(o, g_f12, 0);
      lv_obj_set_style_text_color(o, C_MUTED, 0);
      lv_label_set_text(o, labs[i]);
    }

  /* 分隔线 */
  o = lv_obj_create(scr);
  lv_obj_set_size(o, 284, 2);
  lv_obj_set_pos(o, 18, 166);
  lv_obj_set_style_bg_color(o, C_MUTED, 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_30, 0);
  lv_obj_set_style_border_width(o, 0, 0);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
}

/* 单词状态 */
static const char *word_status(int i)
{
  if (g_learn_cnt[i] == 0) return "未学";
  if (g_err_cnt[i] > 0)    return "错词";
  if (g_learn_cnt[i] >= 3) return "已掌握";
  return "学习中";
}

/* M4 词库页: 按 Unit 分页(每页 UNIT_SIZE 词)
 * 注意: 词库为全量 7508 词, 不能一次性创建所有行(会撑爆内存), 故分页。 */
static int g_book_unit = 0;

static void book_nav_cb(lv_event_t *e)
{
  long d = (long)lv_obj_get_user_data(lv_event_get_target(e));
  int u = g_book_unit + (int)d;

  if (u < 0) u = NUNIT - 1;
  if (u >= NUNIT) u = 0;
  g_book_unit = u;
  show_page(PAGE_BOOK);
}

static void build_book(void)
{
  char buf[64];
  lv_obj_t *o, *list, *b;
  int i, y = 6;
  int base = g_book_unit * UNIT_SIZE;
  int end  = base + UNIT_SIZE;

  if (end > NWORD) end = NWORD;

  lv_obj_set_style_bg_color(scr, C_PAPER, 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

  /* 返回 */
  b = mk_back(PAGE_HOME);

  /* 标题 */
  o = lv_label_create(scr);
  lv_obj_set_pos(o, 80, 10);
  if (g_f16) lv_obj_set_style_text_font(o, g_f16, 0);
  snprintf(buf, sizeof(buf), "词库 %d/%d", g_book_unit + 1, NUNIT);
  lv_label_set_text(o, buf);

  /* 上一页 */
  b = lv_btn_create(scr);
  lv_obj_set_size(b, 62, 28);
  lv_obj_set_pos(b, 182, 8);
  lv_obj_set_style_bg_color(b, C_PANEL, 0);
  lv_obj_set_style_border_width(b, 2, 0);
  lv_obj_set_style_border_color(b, C_LINE, 0);
  lv_obj_set_user_data(b, (void *)(long)-1);
  lv_obj_add_event_cb(b, book_nav_cb, LV_EVENT_CLICKED, NULL);
  o = lv_label_create(b);
  if (g_f12) lv_obj_set_style_text_font(o, g_f12, 0);
  lv_label_set_text(o, "上一页");
  lv_obj_center(o);

  /* 下一页 */
  b = lv_btn_create(scr);
  lv_obj_set_size(b, 62, 28);
  lv_obj_set_pos(b, 248, 8);
  lv_obj_set_style_bg_color(b, C_PANEL, 0);
  lv_obj_set_style_border_width(b, 2, 0);
  lv_obj_set_style_border_color(b, C_LINE, 0);
  lv_obj_set_user_data(b, (void *)(long)1);
  lv_obj_add_event_cb(b, book_nav_cb, LV_EVENT_CLICKED, NULL);
  o = lv_label_create(b);
  if (g_f12) lv_obj_set_style_text_font(o, g_f12, 0);
  lv_label_set_text(o, "下一页");
  lv_obj_center(o);

  /* 当前 Unit 的词列表 */
  list = lv_obj_create(scr);
  lv_obj_set_size(list, 304, 392);
  lv_obj_set_pos(list, 8, 46);
  lv_obj_set_style_bg_color(list, C_PAPER, 0);
  lv_obj_set_style_bg_opa(list, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(list, 0, 0);
  lv_obj_set_style_pad_all(list, 4, 0);
  lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);

  for (i = base; i < end; i++)
    {
      o = lv_label_create(list);
      lv_obj_set_pos(o, 6, y);
      lv_obj_set_style_text_font(o, &lv_font_montserrat_16, 0);
      lv_obj_set_style_text_color(o, C_INK, 0);
      lv_label_set_text(o, WORDS[i].w);

      o = lv_label_create(list);
      lv_obj_set_pos(o, 162, y);
      if (g_f12) lv_obj_set_style_text_font(o, g_f12, 0);
      lv_obj_set_style_text_color(o, C_MUTED, 0);
      snprintf(buf, sizeof(buf), "%s 学%d 错%d",
               word_status(i), g_learn_cnt[i], g_err_cnt[i]);
      lv_label_set_text(o, buf);

      y += 26;
    }
}

/* ==================== M5: 设置页 ==================== */

static int g_daily  = 10;        /* 每日新词目标 */
static int g_remind = 0;         /* 复习提醒开关 */
static lv_obj_t *lbl_set_tip = NULL;

#define SET_FILE MMJ_DIR "/settings.json"

/* 设置持久化(主题/每日目标/提醒) */
static void settings_save(void)
{
  FILE *fp;
  mkdir(MMJ_DIR, 0777);
  fp = fopen(SET_FILE, "w");
  if (fp == NULL) return;
  fprintf(fp, "{\"theme\":%d,\"daily\":%d,\"remind\":%d}\n",
          g_theme, g_daily, g_remind);
  fclose(fp);
}

/* 从 JSON 文本里取整数字段(找不到返回 def) */
static int json_int(const char *buf, const char *key, int def)
{
  const char *p = strstr(buf, key);
  if (p == NULL) return def;
  p += strlen(key);
  while (*p != '\0' && (*p < '0' || *p > '9') && *p != '-') p++;
  return atoi(p);
}

static void settings_load(void)
{
  char buf[256];
  size_t n;
  FILE *fp = fopen(SET_FILE, "r");

  if (fp == NULL) return;
  n = fread(buf, 1, sizeof(buf) - 1, fp);
  fclose(fp);
  buf[n] = '\0';

  g_theme  = json_int(buf, "\"theme\"",  0);
  g_daily  = json_int(buf, "\"daily\"",  10);
  g_remind = json_int(buf, "\"remind\"", 0);
  if (g_theme < 0 || g_theme > 2) g_theme = 0;
  if (g_daily < 5 || g_daily > 50) g_daily = 10;
  printf("[mmj] settings: theme=%d daily=%d remind=%d\n",
         g_theme, g_daily, g_remind);
}

static const char *theme_name(int t)
{
  return (t == 1) ? "夜幕" : (t == 2) ? "掌机" : "纸色";
}

static void set_cb(lv_event_t *e)
{
  lv_obj_t *b = lv_event_get_target(e);
  long id = (long)lv_obj_get_user_data(b);

  switch (id)
    {
      case 0:                                  /* 学习词汇 */
        if (lbl_set_tip)
          lv_label_set_text(lbl_set_tip, "当前仅 CET-4, 更多词书待导入");
        break;
      case 1:                                  /* 主题: 纸色->夜幕->掌机 */
        theme_apply((g_theme + 1) % 3);
        settings_save();
        show_page(PAGE_SET);
        break;
      case 2:                                  /* 无线网络 → 进入 WiFi 页 */
        show_page(PAGE_WIFI);
        break;
      case 3:                                  /* 每日目标: 5..25 */
        g_daily = (g_daily >= 25) ? 5 : g_daily + 5;
        settings_save();
        show_page(PAGE_SET);
        break;
      case 4:                                  /* 复习提醒 */
        g_remind = !g_remind;
        settings_save();
        show_page(PAGE_SET);
        break;
      case 5:                                  /* AI 服务: 提示配置方式 */
        if (lbl_set_tip)
          {
            if (g_ai_key[0] != '\0')
              lv_label_set_text(lbl_set_tip,
                                "AI 已就绪: 闯关页右上角进入 AI 挑战");
            else
              lv_label_set_text(lbl_set_tip,
                                "用电脑浏览器打开下方管理地址配置 MiMo Key");
          }
        break;
      case 6:                                  /* 管理地址(仅展示) */
        if (lbl_set_tip)
          lv_label_set_text(lbl_set_tip,
                            "电脑连同一 WiFi, 浏览器访问该地址管理板子");
        break;
      default:
        break;
    }
}

/* 一行设置项: 左标签 + 右值 */
static void set_row(int y, const char *label, const char *val, long id)
{
  lv_obj_t *o;
  lv_obj_t *row = lv_btn_create(scr);
  lv_obj_set_size(row, 288, 46);
  lv_obj_set_pos(row, 16, y);
  lv_obj_set_style_bg_color(row, C_PANEL, 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(row, 2, 0);
  lv_obj_set_style_border_color(row, C_LINE, 0);
  style_card(row);                       /* 去阴影/小圆角, 修复浅色主题下样式杂乱 */
  lv_obj_set_user_data(row, (void *)id);
  lv_obj_add_event_cb(row, set_cb, LV_EVENT_CLICKED, NULL);

  o = lv_label_create(row);
  lv_obj_align(o, LV_ALIGN_LEFT_MID, 4, 0);
  if (g_f16) lv_obj_set_style_text_font(o, g_f16, 0);
  lv_obj_set_style_text_color(o, C_INK, 0);
  lv_label_set_text(o, label);

  if (val != NULL && val[0] != '\0')
    {
      o = lv_label_create(row);
      lv_obj_align(o, LV_ALIGN_RIGHT_MID, -4, 0);
      if (g_f16) lv_obj_set_style_text_font(o, g_f16, 0);
      lv_obj_set_style_text_color(o, C_MUTED, 0);
      lv_label_set_text(o, val);
    }
}

static void build_set(void)
{
  char buf[40];
  lv_obj_t *o;

  lv_obj_set_style_bg_color(scr, C_PAPER, 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

  lv_obj_t *back = mk_back(PAGE_HOME);

  o = lv_label_create(scr);
  lv_obj_set_pos(o, 80, 10);
  if (g_f16) lv_obj_set_style_text_font(o, g_f16, 0);
  lv_label_set_text(o, "设置");

  set_row(56,  "学习词汇", "CET-4", 0);
  set_row(108, "主题",     theme_name(g_theme), 1);
  set_row(160, "无线网络", wifi_up() ? "已连接" : "未连接", 2);
  snprintf(buf, sizeof(buf), "%d 词/天", g_daily);
  set_row(212, "每日目标", buf, 3);
  set_row(264, "复习提醒", g_remind ? "开" : "关", 4);
  set_row(316, "AI 服务",  g_ai_key[0] != '\0' ? "已配置" : "未配置", 5);
  {
    char ipb[20];
    if (wifi_ip_str(ipb, sizeof(ipb)))
      snprintf(buf, sizeof(buf), "%s:8080", ipb);
    else
      snprintf(buf, sizeof(buf), "连接 WiFi 后显示");
    set_row(368, "管理地址", buf, 6);
  }

  /* 底部提示行 */
  lbl_set_tip = lv_label_create(scr);
  lv_obj_align(lbl_set_tip, LV_ALIGN_BOTTOM_MID, 0, -12);
  if (g_f12) lv_obj_set_style_text_font(lbl_set_tip, g_f12, 0);
  lv_obj_set_style_text_color(lbl_set_tip, C_MUTED, 0);
  lv_label_set_text(lbl_set_tip, "点击各项即可切换");
}

/* ==================== M6: WiFi 页 ==================== */

static lv_obj_t *wifi_list = NULL;      /* 热点列表容器 */
static lv_obj_t *wifi_ta   = NULL;      /* 密码输入框 */
static lv_obj_t *wifi_kb   = NULL;      /* 软键盘 */

static void wifi_ap_cb(lv_event_t *e);  /* 前向声明(wifi_list_render 引用) */

static void wifi_st_update(void)
{
  char s[80];
  char ess[40];

  if (lbl_wifi_st == NULL || !lv_obj_is_valid(lbl_wifi_st))
    {
      lbl_wifi_st = NULL;      /* 控件已被切页删除, 丢弃引用 */
      return;
    }
  if (g_wifi_busy)
    {
      lv_label_set_text(lbl_wifi_st, "连接中, 请稍候...");
    }
  else if (wifi_up())
    {
      /* 优先显示驱动上报的真实 SSID */
      if (g_wifi_ssid[0] != '\0')
        {
          strncpy(ess, g_wifi_ssid, sizeof(ess) - 1);
          ess[sizeof(ess) - 1] = '\0';
        }
      else if (!wifi_get_current_ssid(ess, sizeof(ess)))
        {
          strcpy(ess, "wireless");
        }
      snprintf(s, sizeof(s), "已连接: %s", ess);
      lv_label_set_text(lbl_wifi_st, s);
    }
  else if (g_wifi_ssid[0] != '\0')
    {
      snprintf(s, sizeof(s), "等待连接: %s", g_wifi_ssid);
      lv_label_set_text(lbl_wifi_st, s);
    }
  else
    {
      lv_label_set_text(lbl_wifi_st, "未连接");
    }
}

/* 用 g_wifi_ap[] 渲染热点列表(wifi_list 已存在) */
static void wifi_list_render(void)
{
  char s[64];
  lv_obj_t *o;
  int i;

  lv_obj_clean(wifi_list);
  if (g_wifi_n <= 0)
    {
      o = lv_label_create(wifi_list);
      if (g_f16) lv_obj_set_style_text_font(o, g_f16, 0);
      lv_obj_set_style_text_color(o, C_MUTED, 0);
      lv_obj_set_pos(o, 4, 4);
      lv_label_set_text(o, "点右上角[扫描]查找热点");
      return;
    }

  for (i = 0; i < g_wifi_n; i++)
    {
      lv_obj_t *row = lv_btn_create(wifi_list);
      lv_obj_set_size(row, 288, 40);
      lv_obj_set_pos(row, 0, i * 44);
      lv_obj_set_style_bg_color(row, C_PANEL, 0);
      lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
      lv_obj_set_style_border_width(row, 1, 0);
      lv_obj_set_style_border_color(row, C_LINE, 0);
      style_card(row);
      lv_obj_set_user_data(row, (void *)(long)i);
      lv_obj_add_event_cb(row, wifi_ap_cb, LV_EVENT_CLICKED, NULL);

      o = lv_label_create(row);
      lv_obj_align(o, LV_ALIGN_LEFT_MID, 6, 0);
      if (g_f16) lv_obj_set_style_text_font(o, g_f16, 0);
      lv_obj_set_style_text_color(o, C_INK, 0);
      {
        int r = g_wifi_rssi[i];              /* RSSI: 统一按绝对值分档 */
        if (r < 0) r = -r;
        const char *lvl = (r <= 55) ? "信号强" : (r <= 70) ? "信号中" : "信号弱";
        snprintf(s, sizeof(s), "%s  %s", g_wifi_ap[i], lvl);
      }
      lv_label_set_text(o, s);
    }
}

static void wifi_scan_cb(lv_event_t *e)
{
  lv_obj_t *tip;

  (void)e;
  if (wifi_list == NULL) return;

  lv_obj_clean(wifi_list);
  tip = lv_label_create(wifi_list);
  if (g_f16) lv_obj_set_style_text_font(tip, g_f16, 0);
  lv_obj_set_style_text_color(tip, C_MUTED, 0);
  lv_obj_set_pos(tip, 4, 4);
  lv_label_set_text(tip, "扫描中, 请稍候");
  lv_refr_now(NULL);                    /* 先渲染提示再阻塞扫描 */

  wifi_do_scan();
  wifi_list_render();
}

/* 输入密码后点击[连接]: 阻塞连接并回显结果 */
static void wifi_go_cb(lv_event_t *e)
{
  lv_obj_t *btn  = lv_event_get_target(e);
  lv_obj_t *lbl  = lv_obj_get_child(btn, 0);
  const char *psk = (wifi_ta != NULL) ? lv_textarea_get_text(wifi_ta) : "";
  bool ok;

  lv_label_set_text(lbl, "连接中");
  lv_refr_now(NULL);

  ok = wifi_do_connect(g_wifi_ssid, psk);
  printf("[mmj] wifi %s: %s\n", ok ? "connected" : "connect failed",
         g_wifi_ssid);
  show_page(PAGE_WIFI);
}

/* 密码输入覆盖层: 标题 + 密码框 + 软键盘 */
static void wifi_kb_build(void)
{
  lv_obj_t *o, *b;

  lv_obj_clean(scr);
  lbl_time = NULL;
  lbl_date = NULL;
  lbl_wifi = NULL;
  lbl_set_tip = NULL;
  lbl_wifi_st = NULL;
  wifi_list = NULL;

  /* 取消 */
  b = lv_btn_create(scr);
  lv_obj_set_size(b, 60, 28);
  lv_obj_set_pos(b, 8, 8);
  lv_obj_set_style_bg_color(b, C_PANEL, 0);
  lv_obj_set_style_border_width(b, 2, 0);
  lv_obj_set_style_border_color(b, C_LINE, 0);
  style_card(b);
  lv_obj_set_user_data(b, (void *)PAGE_WIFI);
  lv_obj_add_event_cb(b, btn_cb, LV_EVENT_CLICKED, NULL);
  o = lv_label_create(b);
  lv_label_set_text(o, "取消");
  if (g_f16) lv_obj_set_style_text_font(o, g_f16, 0);
  lv_obj_center(o);

  /* 标题: 连接目标 */
  o = lv_label_create(scr);
  lv_obj_set_pos(o, 78, 12);
  if (g_f16) lv_obj_set_style_text_font(o, g_f16, 0);
  lv_label_set_text(o, g_wifi_ssid);

  /* 密码输入框 */
  wifi_ta = lv_textarea_create(scr);
  lv_obj_set_size(wifi_ta, 304, 40);
  lv_obj_set_pos(wifi_ta, 8, 44);
  lv_textarea_set_one_line(wifi_ta, true);
  lv_textarea_set_password_mode(wifi_ta, true);
  lv_textarea_set_placeholder_text(wifi_ta, "输入密码(开放网络可留空)");
  if (g_f16) lv_obj_set_style_text_font(wifi_ta, g_f16, 0);
  lv_obj_set_style_bg_color(wifi_ta, C_PANEL, 0);
  lv_obj_set_style_border_color(wifi_ta, C_LINE, 0);
  lv_obj_set_style_border_width(wifi_ta, 1, 0);

  /* 连接按钮 */
  b = lv_btn_create(scr);
  lv_obj_set_size(b, 304, 38);
  lv_obj_set_pos(b, 8, 92);
  lv_obj_set_style_bg_color(b, C_GREEN, 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(b, 2, 0);
  lv_obj_set_style_border_color(b, C_LINE, 0);
  style_card(b);
  lv_obj_add_event_cb(b, wifi_go_cb, LV_EVENT_CLICKED, NULL);
  o = lv_label_create(b);
  lv_label_set_text(o, "连 接");
  if (g_f16) lv_obj_set_style_text_font(o, g_f16, 0);
  lv_obj_set_style_text_color(o, C_WHITE, 0);
  lv_obj_center(o);

  /* 软键盘: 固定小写字母布局; 吸附屏幕底部 + 高度自适应,
   * 避免键盘实际内容高度超过设定值而溢出到屏幕外 */
  wifi_kb = lv_keyboard_create(scr);
  lv_obj_set_width(wifi_kb, 320);
  lv_obj_align(wifi_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_keyboard_set_mode(wifi_kb, LV_KEYBOARD_MODE_TEXT_LOWER);
  lv_keyboard_set_textarea(wifi_kb, wifi_ta);
}

static void wifi_ap_cb(lv_event_t *e)
{
  long id = (long)lv_obj_get_user_data(lv_event_get_target(e));

  if (id < 0 || id >= g_wifi_n) return;
  strncpy(g_wifi_ssid, g_wifi_ap[id], sizeof(g_wifi_ssid) - 1);
  g_wifi_ssid[sizeof(g_wifi_ssid) - 1] = '\0';
  wifi_kb_build();
}

static void build_wifi(void)
{
  lv_obj_t *o, *back, *b;

  lv_obj_set_style_bg_color(scr, C_PAPER, 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

  back = mk_back(PAGE_SET);

  o = lv_label_create(scr);
  lv_obj_set_pos(o, 80, 10);
  if (g_f16) lv_obj_set_style_text_font(o, g_f16, 0);
  lv_label_set_text(o, "无线网络");

  /* 状态行(upd_clock 每秒刷新) */
  lbl_wifi_st = lv_label_create(scr);
  lv_obj_set_pos(lbl_wifi_st, 8, 48);
  if (g_f16) lv_obj_set_style_text_font(lbl_wifi_st, g_f16, 0);
  lv_obj_set_style_text_color(lbl_wifi_st, C_MUTED, 0);
  wifi_st_update();

  /* 重新扫描 */
  b = lv_btn_create(scr);
  lv_obj_set_size(b, 76, 30);
  lv_obj_set_pos(b, 236, 42);
  lv_obj_set_style_bg_color(b, C_PANEL, 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(b, 2, 0);
  lv_obj_set_style_border_color(b, C_LINE, 0);
  style_card(b);
  lv_obj_add_event_cb(b, wifi_scan_cb, LV_EVENT_CLICKED, NULL);
  o = lv_label_create(b);
  lv_label_set_text(o, "扫描");
  if (g_f16) lv_obj_set_style_text_font(o, g_f16, 0);
  lv_obj_center(o);

  /* 热点列表容器 */
  wifi_list = lv_obj_create(scr);
  lv_obj_set_size(wifi_list, 304, 384);
  lv_obj_set_pos(wifi_list, 8, 84);
  lv_obj_set_style_bg_color(wifi_list, C_PAPER, 0);
  lv_obj_set_style_bg_opa(wifi_list, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(wifi_list, 0, 0);
  lv_obj_set_style_pad_all(wifi_list, 4, 0);
  lv_obj_add_flag(wifi_list, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(wifi_list, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(wifi_list, LV_SCROLLBAR_MODE_OFF);

  wifi_list_render();
}

/* ==================== M7: Web 管理 + AI 挑战 ====================
 * 板子起 HTTP 服务(:8080): 电脑浏览器直连, 配置 AI 模型/查看学习数据;
 * AI 挑战页用 MiMo 大模型按已学单词出例句挖空四选一题(聊天式讲解)。 */

#define WEB_PORT     8080
#define AI_CFG_FILE  MMJ_DIR "/ai.json"
#define AI_REQ_FILE  MMJ_DIR "/ai_req.json"
#define AI_RSP_FILE  MMJ_DIR "/ai_rsp.json"
#define AI_META_FILE MMJ_DIR "/ai_meta.txt"
#define AI_WEB_HTML  "/etc/web/admin.html"

static char g_ai_base[96]  = "https://api.xiaomimimo.com/v1";
static char g_ai_model[48] = "mimo-v2.5-pro";

/* 取 key 对应的字符串值(带简单反转义); 未找到时不动 out */
static int json_str(const char *buf, const char *key, char *out, int outsz)
{
  const char *p = strstr(buf, key);
  int n = 0;

  out[0] = '\0';
  if (p == NULL) return 0;
  p = strchr(p + strlen(key), '"');
  if (p == NULL) return 0;
  p++;
  while (*p != '\0' && *p != '"' && n < outsz - 1)
    {
      if (*p == '\\' && p[1] != '\0')
        {
          p++;
          if (*p == 'n') { out[n++] = '\n'; p++; continue; }
        }
      out[n++] = *p++;
    }
  out[n] = '\0';
  return (p[0] == '"');
}

/* 板子 wlan0 的 IPv4 字符串(未联网返回 0) */
static int wifi_ip_str(char *out, int len)
{
  struct ifreq ifr;
  struct sockaddr_in *sa;
  int s = socket(AF_INET, SOCK_DGRAM, 0);

  if (s < 0) return 0;
  memset(&ifr, 0, sizeof(ifr));
  strncpy(ifr.ifr_name, "wlan0", IFNAMSIZ);
  if (ioctl(s, SIOCGIFADDR, (unsigned long)&ifr) < 0)
    {
      close(s);
      return 0;
    }
  close(s);
  sa = (struct sockaddr_in *)&ifr.ifr_addr;
  strncpy(out, inet_ntoa(sa->sin_addr), len - 1);
  out[len - 1] = '\0';
  return 1;
}

/* 读取 AI 配置(网页端写入 /data/mmj/ai.json) */
static void ai_cfg_load(void)
{
  FILE *fp = fopen(AI_CFG_FILE, "r");
  char buf[640];
  size_t n;

  if (fp == NULL) return;
  n = fread(buf, 1, sizeof(buf) - 1, fp);
  buf[n] = '\0';
  fclose(fp);
  json_str(buf, "\"api_key\"", g_ai_key, sizeof(g_ai_key));
  json_str(buf, "\"base_url\"", g_ai_base, sizeof(g_ai_base));
  json_str(buf, "\"model\"", g_ai_model, sizeof(g_ai_model));
  {
    char v[24];
    json_str(buf, "\"tts_voice\"", v, sizeof(v));
    if (v[0] != '\0')
      {
        strncpy(g_tts_voice, v, sizeof(g_tts_voice) - 1);
        g_tts_voice[sizeof(g_tts_voice) - 1] = '\0';
      }
  }
}

static void web_send(int fd, const char *ctype, const char *body)
{
  char h[160];
  int blen = (int)strlen(body);

  snprintf(h, sizeof(h),
           "HTTP/1.0 200 OK\r\nContent-Type: %s\r\n"
           "Content-Length: %d\r\nConnection: close\r\n\r\n", ctype, blen);
  write(fd, h, strlen(h));
  write(fd, body, blen);
}

/* 拼 JSON 字符串值前的转义(引号/反斜杠/控制符) */
static void json_escape(char *dst, int dstsz, const char *src)
{
  int n = 0;

  for (; *src != '\0' && n < dstsz - 1; src++)
    {
      if (*src == '"' || *src == '\\')
        {
          if (n < dstsz - 2) dst[n++] = '\\';
          dst[n++] = *src;
        }
      else if ((unsigned char)*src < 0x20)
        {
          dst[n++] = ' ';
        }
      else
        {
          dst[n++] = *src;
        }
    }
  dst[n] = '\0';
}

static void web_send_file(int fd, const char *path)
{
  FILE *fp = fopen(path, "r");
  char *buf;
  long sz;

  if (fp == NULL)
    {
      web_send(fd, "text/plain; charset=utf-8", "page not found");
      return;
    }
  fseek(fp, 0, SEEK_END);
  sz = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  buf = (sz > 0 && sz < 256 * 1024) ? malloc(sz + 1) : NULL;
  if (buf != NULL)
    {
      char h[160];
      fread(buf, 1, sz, fp);
      buf[sz] = '\0';
      snprintf(h, sizeof(h),
               "HTTP/1.0 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n"
               "Content-Length: %ld\r\nConnection: close\r\n\r\n", sz);
      write(fd, h, strlen(h));
      write(fd, buf, sz);
      free(buf);
    }
  else
    {
      web_send(fd, "text/plain; charset=utf-8", "out of memory");
    }
  fclose(fp);
}

static void web_handle(int fd, char *req)
{
  char method[8] = "", path[64] = "";
  char *body;
  int is_post;

  if (sscanf(req, "%7s %63s", method, path) != 2) return;
  is_post = (strcmp(method, "POST") == 0);
  body = strstr(req, "\r\n\r\n");
  body = (body != NULL) ? body + 4 : (char *)"";

  if (!is_post && strcmp(path, "/") == 0)
    {
      web_send_file(fd, AI_WEB_HTML);
    }
  else if (strcmp(path, "/api/config") == 0 && !is_post)
    {
      char j[576];
      char ke[96], be[112], me[56], ve[40];
      json_escape(ke, sizeof(ke), g_ai_key);
      json_escape(be, sizeof(be), g_ai_base);
      json_escape(me, sizeof(me), g_ai_model);
      json_escape(ve, sizeof(ve), g_tts_voice);
      snprintf(j, sizeof(j),
               "{\"api_key\":\"%s\",\"base_url\":\"%s\",\"model\":\"%s\","
               "\"tts_voice\":\"%s\"}",
               ke, be, me, ve);
      web_send(fd, "application/json", j);
    }
  else if (strcmp(path, "/api/config") == 0 && is_post)
    {
      char peek[64];
      strncpy(peek, body, sizeof(peek) - 1);
      peek[sizeof(peek) - 1] = '\0';
      printf("[mmj] cfg POST body: %s\n", peek);
      mkdir(MMJ_DIR, 0777);          /* 首次保存时目录可能尚未创建 */
      FILE *fp = fopen(AI_CFG_FILE, "w");
      if (fp != NULL)
        {
          fputs(body, fp);
          fclose(fp);
        }
      else
        {
          printf("[mmj] ai cfg write failed: %s errno=%d\n",
                 AI_CFG_FILE, errno);
        }
      ai_cfg_load();
      printf("[mmj] ai cfg loaded: key_len=%d base=%s model=%s\n",
             (int)strlen(g_ai_key), g_ai_base, g_ai_model);
      web_send(fd, "application/json", "{\"code\":0,\"msg\":\"saved\"}");
    }
  else if (strcmp(path, "/api/stats") == 0 && !is_post)
    {
      char j[256];
      int i, learned = 0, due = 0, od = 0, mast = 0;
      int today = today_days();
      for (i = 0; i < NWORD; i++)
        {
          if (g_learn_cnt[i] == 0) continue;
          learned++;
          if (g_due[i] <= today) due++;
          if (g_due[i] < today) od++;
          if (g_lv[i] >= 4) mast++;
        }
      snprintf(j, sizeof(j),
               "{\"total\":%d,\"learned\":%d,\"due\":%d,\"overdue\":%d,\"mastered\":%d}",
               NWORD, learned, due, od, mast);
      web_send(fd, "application/json", j);
    }
  else if (strcmp(path, "/api/reset") == 0 && is_post)
    {
      memset(g_learn_cnt, 0, sizeof(int) * NWORD);
      memset(g_err_cnt, 0, sizeof(int) * NWORD);
      memset(g_lv, 0, sizeof(int) * NWORD);
      memset(g_due, 0, sizeof(int) * NWORD);
      memset(g_rep, 0, sizeof(int) * NWORD);
      memset(g_streak, 0, sizeof(int) * NWORD);
      g_cur = 0;
      data_save();
      web_send(fd, "application/json", "{\"code\":0,\"msg\":\"reset ok\"}");
    }
  else if (strcmp(path, "/api/test") == 0 && is_post)
    {
      char msg[128] = "连接失败(检查网络或 Key)";
      int code = 1;

      ai_cfg_load();                 /* 以文件中的配置为准, 避免状态不同步 */
      printf("[mmj] web test: key_len=%d base=%s\n",
             (int)strlen(g_ai_key), g_ai_base);
      if (g_ai_key[0] == '\0')
        {
          code = 2;
          strcpy(msg, "请先保存 API Key");
        }
      else
        {
          FILE *fp;
          char cmd[640];
          char rbuf[2048];
          char meta[256] = "";
          char content[128];
          int ret;

          fp = fopen(AI_REQ_FILE, "w");
          if (fp != NULL)
            {
              fprintf(fp, "{\"model\":\"%s\",\"messages\":[{\"role\":\"user\","
                          "\"content\":\"reply with exactly: OK\"}],\"max_tokens\":32}",
                      g_ai_model);
              fclose(fp);
            }
          remove(AI_RSP_FILE);
          snprintf(cmd, sizeof(cmd),
                   "curl -sk -m 30 -o %s -w \"HTTPCODE:%%{http_code}\" "
                   "%s/chat/completions -H \"Authorization: Bearer %s\" "
                   "-H \"Content-Type: application/json\" -d @%s > %s 2>&1",
                   AI_RSP_FILE, g_ai_base, g_ai_key, AI_REQ_FILE, AI_META_FILE);
          ret = system(cmd);
          printf("[mmj] curl ret=0x%x\n", ret);

          fp = fopen(AI_META_FILE, "r");
          if (fp != NULL)
            {
              size_t rn = fread(meta, 1, sizeof(meta) - 1, fp);
              meta[rn] = '\0';
              fclose(fp);
              printf("[mmj] curl meta: %s\n", meta);
            }

          fp = fopen(AI_RSP_FILE, "r");
          if (fp == NULL)
            {
              snprintf(msg, sizeof(msg), "无响应: %.90s", meta);
            }
          else
            {
              size_t rn = fread(rbuf, 1, sizeof(rbuf) - 1, fp);
              rbuf[rn] = '\0';
              fclose(fp);
              printf("[mmj] rsp: %.160s\n", rbuf);
              if (json_str(rbuf, "\"content\"", content, sizeof(content)) &&
                  content[0] != '\0')
                {
                  code = 0;
                  snprintf(msg, sizeof(msg), "连接成功: %s", content);
                }
              else
                {
                  /* 服务端错误摘要转义后带给页面(401=Key无效/404=地址不对等) */
                  char safe[80];
                  char *ep = strstr(rbuf, "\"message\"");
                  json_escape(safe, sizeof(safe),
                              (ep != NULL) ? ep + 10 : "模型响应为空, 请重试");
                  snprintf(msg, sizeof(msg), "HTTP %.40s | %.70s", meta, safe);
                }
            }
        }
      {
        char j[192];
        snprintf(j, sizeof(j), "{\"code\":%d,\"msg\":\"%s\"}", code, msg);
        web_send(fd, "application/json", j);
      }
    }
  else
    {
      web_send(fd, "text/plain; charset=utf-8", "404 not found");
    }
}

/* Web 管理服务线程(开机常驻, :8080) */
static int mmj_web_task(int argc, char *argv[])
{
  struct sockaddr_in addr;
  int ls;
  int one = 1;

  (void)argc; (void)argv;

  ls = socket(AF_INET, SOCK_STREAM, 0);
  if (ls < 0) return 1;
  setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  memset(&addr, 0, sizeof(addr));
  addr.sin_family      = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port        = htons(WEB_PORT);
  if (bind(ls, (struct sockaddr *)&addr, sizeof(addr)) < 0 || listen(ls, 4) < 0)
    {
      printf("[mmj] web server bind/listen failed\n");
      close(ls);
      return 1;
    }
  printf("[mmj] web admin on port %d\n", WEB_PORT);

  for (;;)
    {
      static char req[4096];
      int fd = accept(ls, NULL, NULL);
      int n = 0, hlen = -1, clen = 0;

      if (fd < 0) continue;
      memset(req, 0, sizeof(req));
      while (n < (int)sizeof(req) - 1)
        {
          int r = read(fd, req + n, sizeof(req) - 1 - n);
          if (r <= 0) break;
          n += r;
          req[n] = '\0';
          if (hlen < 0)
            {
              char *hp = strstr(req, "\r\n\r\n");
              if (hp != NULL)
                {
                  char *cp;
                  hlen = (int)(hp - req) + 4;
                  cp = strstr(req, "Content-Length:");
                  if (cp != NULL && (cp - req) < hlen) clen = atoi(cp + 15);
                }
            }
          if (hlen >= 0 && n >= hlen + clen) break;
        }
      if (n > 0) web_handle(fd, req);
      close(fd);
    }
  return 0;
}

/* ---------- AI 挑战页 ---------- */

static void ai_retry_cb(lv_event_t *e);   /* 前向声明(build_ai 引用) */

#define AIQ_N 5
enum { AIQ_LOAD = 0, AIQ_QUIZ, AIQ_DONE, AIQ_ERR };

static volatile int g_ai_state = AIQ_LOAD;
static int  g_ai_idx    = 0;
static int  g_ai_right  = 0;
static int  g_ai_n      = 0;
static int  g_ai_picked = -1;
static char g_ai_q[AIQ_N][128];
static char g_ai_opt[AIQ_N][4][28];
static char g_ai_word[AIQ_N][24];
static char g_ai_cn[AIQ_N][48];
static char g_ai_expl[AIQ_N][96];
static int  g_ai_ans[AIQ_N];
static char g_ai_errmsg[96];
static lv_obj_t *g_ai_tip = NULL;      /* 讲解条(答题后更新) */
static lv_timer_t *g_ai_poll  = NULL;
static lv_timer_t *g_ai_next_t = NULL;

static void build_ai(void);

/* 从已学词中随机挑 n 个不重复的词索引 */
static int ai_pick_words(int *out, int n)
{
  int cnt = 0, tries = 0;

  while (cnt < n && tries < n * 400)
    {
      int k = rand() % NWORD;
      int i, dup = 0;
      tries++;
      if (g_learn_cnt[k] == 0) continue;
      for (i = 0; i < cnt; i++) if (out[i] == k) dup = 1;
      if (!dup) out[cnt++] = k;
    }
  return cnt;
}

/* 从单个题目对象文本里提取 4 个选项 */
static void ai_parse_opts(const char *obj, char out[4][28])
{
  const char *p = strstr(obj, "\"options\"");
  int i;

  for (i = 0; i < 4; i++) out[i][0] = '\0';
  if (p == NULL) return;
  p = strchr(p, '[');
  if (p == NULL) return;
  p++;
  for (i = 0; i < 4; i++)
    {
      int n = 0;
      p = strchr(p, '"');
      if (p == NULL) return;
      p++;
      while (*p != '\0' && *p != '"' && n < 27)
        {
          if (*p == '\\' && p[1] != '\0') p++;
          out[i][n++] = *p++;
        }
      out[i][n] = '\0';
      p++;
    }
}

/* 后台任务: 组请求 → curl 调 MiMo → 解析题目数组(完成后置状态) */
static int ai_fetch_task(int argc, char *argv[])
{
  int picked[AIQ_N];
  char wl[1024] = "";
  char body[2048];
  char cmd[512];
  char buf[8192];
  char content[3072];
  FILE *fp;
  int n, i;
  char *p;

  (void)argc; (void)argv;

  g_ai_n = 0;
  if (g_ai_key[0] == '\0')
    {
      strcpy(g_ai_errmsg, "AI 未配置: 电脑浏览器打开管理地址, 填入 MiMo API Key");
      g_ai_state = AIQ_ERR;
      return 0;
    }

  n = ai_pick_words(picked, AIQ_N);
  if (n == 0)
    {
      strcpy(g_ai_errmsg, "先去学习页学几个单词, AI 才有素材出题");
      g_ai_state = AIQ_ERR;
      return 0;
    }

  for (i = 0; i < n; i++)
    snprintf(wl + strlen(wl), sizeof(wl) - strlen(wl), "%d. %s (%s) %s\n",
             i + 1, WORDS[picked[i]].w, WORDS[picked[i]].pos, WORDS[picked[i]].cn);

  /* prompt 不含双引号, content 无需转义 */
  snprintf(body, sizeof(body),
           "{\"model\":\"%s\",\"messages\":[{\"role\":\"user\",\"content\":"
           "\"你是背单词应用的AI出题助手。根据以下CET-4单词出%d道四选一选择题。"
           "题干为英文例句挖空(空格用___表示), 选项为4个单词, 其中1个正确答案来自所给"
           "单词, 其余为所给单词或形近变形。只输出JSON数组, 不要输出任何其他文字。"
           "数组元素字段: q(英文例句,空格用___), options(4个单词), ans(正确选项下标0到3), "
           "word(正确单词), cn(词性加中文释义), explain(不超过30字的中文讲解)。"
           "单词列表:\\n%s\"}],\"temperature\":0.7,\"max_tokens\":1600}",
           g_ai_model, n, wl);

  fp = fopen(AI_REQ_FILE, "w");
  if (fp != NULL)
    {
      fputs(body, fp);
      fclose(fp);
    }
  snprintf(cmd, sizeof(cmd),
           "curl -sk -m 45 %s/chat/completions -H \"Authorization: Bearer %s\" "
           "-H \"Content-Type: application/json\" -d @%s -o %s 2>/dev/null",
           g_ai_base, g_ai_key, AI_REQ_FILE, AI_RSP_FILE);
  system(cmd);

  fp = fopen(AI_RSP_FILE, "r");
  if (fp == NULL)
    {
      strcpy(g_ai_errmsg, "AI 请求失败(检查 WiFi 与 API 配置)");
      g_ai_state = AIQ_ERR;
      return 0;
    }
  {
    size_t rn = fread(buf, 1, sizeof(buf) - 1, fp);
    buf[rn] = '\0';
  }
  fclose(fp);

  if (json_str(buf, "\"content\"", content, sizeof(content)) == 0)
    {
      strcpy(g_ai_errmsg, "AI 响应异常, 请重试");
      g_ai_state = AIQ_ERR;
      return 0;
    }

  p = strchr(content, '[');
  if (p == NULL)
    {
      strcpy(g_ai_errmsg, "AI 未返回题目数据, 请重试");
      g_ai_state = AIQ_ERR;
      return 0;
    }

  for (i = 0; i < AIQ_N && g_ai_n < AIQ_N; i++)
    {
      char *ob = strchr(p, '{');
      char *oe;
      int ans;
      if (ob == NULL) break;
      oe = strchr(ob, '}');
      if (oe == NULL) break;
      *oe = '\0';
      if (json_str(ob, "\"q\"", g_ai_q[g_ai_n], sizeof(g_ai_q[0])) &&
          g_ai_q[g_ai_n][0] != '\0')
        {
          ai_parse_opts(ob, g_ai_opt[g_ai_n]);
          ans = json_int(ob, "\"ans\"", 0);
          if (ans < 0 || ans > 3) ans = 0;
          g_ai_ans[g_ai_n] = ans;
          json_str(ob, "\"word\"", g_ai_word[g_ai_n], sizeof(g_ai_word[0]));
          json_str(ob, "\"cn\"", g_ai_cn[g_ai_n], sizeof(g_ai_cn[0]));
          json_str(ob, "\"explain\"", g_ai_expl[g_ai_n], sizeof(g_ai_expl[0]));
          g_ai_n++;
        }
      *oe = '}';
      p = oe + 1;
    }

  if (g_ai_n == 0)
    {
      strcpy(g_ai_errmsg, "AI 未生成有效题目, 请重试");
      g_ai_state = AIQ_ERR;
      return 0;
    }
  g_ai_state = AIQ_QUIZ;
  return 0;
}

static void ai_start(void)
{
  g_ai_state = AIQ_LOAD;
  g_ai_idx   = 0;
  g_ai_right = 0;
  g_ai_picked = -1;
  task_create("mmj_aiq", 95, 16384, ai_fetch_task, NULL);
}

static void ai_poll_cb(lv_timer_t *t)
{
  if (g_ai_state != AIQ_LOAD)
    {
      lv_timer_del(t);
      g_ai_poll = NULL;
      if (g_page == PAGE_AI) show_page(PAGE_AI);
    }
}

static void ai_next_cb(lv_timer_t *t)
{
  lv_timer_del(t);
  g_ai_next_t = NULL;
  g_ai_idx++;
  if (g_ai_idx >= g_ai_n) g_ai_state = AIQ_DONE;
  show_page(PAGE_AI);
}

static void ai_opt_cb(lv_event_t *e)
{
  lv_obj_t *b = lv_event_get_target(e);
  lv_obj_t *box = lv_obj_get_parent(b);
  long id = (long)lv_obj_get_user_data(b);
  int i;

  if (g_ai_picked >= 0 || g_page != PAGE_AI) return;
  g_ai_picked = (int)id;
  if (g_ai_picked == g_ai_ans[g_ai_idx]) g_ai_right++;

  for (i = 0; i < 4; i++)
    {
      lv_obj_t *ch = lv_obj_get_child(box, i);
      if (ch == NULL) continue;
      if (i == g_ai_ans[g_ai_idx])
        lv_obj_set_style_bg_color(ch, C_GREEN, 0);
      else if (i == g_ai_picked)
        lv_obj_set_style_bg_color(ch, lv_color_hex(0xd4563f), 0);
    }
  if (g_ai_tip != NULL)
    {
      char s[192];
      snprintf(s, sizeof(s), "%s  %s %s\n%s",
               (g_ai_picked == g_ai_ans[g_ai_idx]) ? "答对了" : "答错了",
               g_ai_word[g_ai_idx], g_ai_cn[g_ai_idx], g_ai_expl[g_ai_idx]);
      lv_label_set_text(g_ai_tip, s);
    }
  g_ai_next_t = lv_timer_create(ai_next_cb, 1600, NULL);
}

/* AI 挑战页: 状态机渲染(出题中 / 答题 / 结算 / 出错) */
static void ai_tts_cb(lv_event_t *e)
{
  (void)e;
  if (g_ai_state == AIQ_QUIZ && g_ai_n > 0)
    tts_speak(g_ai_q[g_ai_idx]);      /* 朗读当前题目例句 */
}

static void build_ai(void)
{
  char buf[96];
  lv_obj_t *o;
  int i;

  lv_obj_set_style_bg_color(scr, C_PAPER, 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

  o = mk_back(PAGE_QUEST);

  o = lv_label_create(scr);
  lv_obj_set_pos(o, 80, 10);
  if (g_f16) lv_obj_set_style_text_font(o, g_f16, 0);
  if (g_ai_state == AIQ_QUIZ && g_ai_n > 0)
    snprintf(buf, sizeof(buf), "AI 挑战  %d/%d", g_ai_idx + 1, g_ai_n);
  else
    snprintf(buf, sizeof(buf), "AI 挑战");
  lv_label_set_text(o, buf);

  /* 右上: 发音按钮(朗读当前题目例句) */
  o = lv_btn_create(scr);
  lv_obj_set_size(o, 92, 28);
  lv_obj_set_pos(o, 220, 8);
  lv_obj_set_style_bg_color(o, C_PANEL, 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(o, 2, 0);
  lv_obj_set_style_border_color(o, C_LINE, 0);
  lv_obj_set_style_bg_color(o, C_MUTED, LV_STATE_PRESSED);
  lv_obj_add_event_cb(o, ai_tts_cb, LV_EVENT_CLICKED, NULL);
  o = lv_label_create(o);
  if (g_f16) lv_obj_set_style_text_font(o, g_f16, 0);
  lv_label_set_text(o, "发音");
  lv_obj_center(o);

  if (g_ai_state == AIQ_LOAD)
    {
      o = lv_label_create(scr);
      lv_obj_align(o, LV_ALIGN_CENTER, 0, -30);
      if (g_f21) lv_obj_set_style_text_font(o, g_f21, 0);
      lv_label_set_text(o, "AI 正在出题, 请稍候...");

      o = lv_label_create(scr);
      lv_obj_align(o, LV_ALIGN_CENTER, 0, 16);
      if (g_f12) lv_obj_set_style_text_font(o, g_f12, 0);
      lv_obj_set_style_text_color(o, C_MUTED, 0);
      lv_label_set_text(o, "正在根据你已学的单词请求大模型出题");

      if (g_ai_poll == NULL)
        g_ai_poll = lv_timer_create(ai_poll_cb, 250, NULL);
      return;
    }

  if (g_ai_state == AIQ_ERR)
    {
      o = lv_label_create(scr);
      lv_obj_align(o, LV_ALIGN_CENTER, 0, -40);
      if (g_f16) lv_obj_set_style_text_font(o, g_f16, 0);
      lv_obj_set_style_text_color(o, lv_color_hex(0xd4563f), 0);
      lv_obj_set_width(o, 272);
      lv_label_set_long_mode(o, LV_LABEL_LONG_WRAP);
      lv_label_set_text(o, g_ai_errmsg);

      o = lv_btn_create(scr);
      lv_obj_set_size(o, 120, 44);
      lv_obj_align(o, LV_ALIGN_CENTER, -66, 40);
      lv_obj_set_style_bg_color(o, C_PANEL, 0);
      lv_obj_set_style_border_width(o, 2, 0);
      lv_obj_set_style_border_color(o, C_LINE, 0);
      lv_obj_add_event_cb(o, ai_retry_cb, LV_EVENT_CLICKED, NULL);
      o = lv_label_create(o);
      if (g_f16) lv_obj_set_style_text_font(o, g_f16, 0);
      lv_label_set_text(o, "重试");
      lv_obj_center(o);

      o = lv_btn_create(scr);
      lv_obj_set_size(o, 120, 44);
      lv_obj_align(o, LV_ALIGN_CENTER, 66, 40);
      lv_obj_set_style_bg_color(o, C_PANEL, 0);
      lv_obj_set_style_border_width(o, 2, 0);
      lv_obj_set_style_border_color(o, C_LINE, 0);
      lv_obj_set_user_data(o, (void *)PAGE_QUEST);
      lv_obj_add_event_cb(o, btn_cb, LV_EVENT_CLICKED, NULL);
      o = lv_label_create(o);
      if (g_f16) lv_obj_set_style_text_font(o, g_f16, 0);
      lv_label_set_text(o, "返回");
      lv_obj_center(o);
      return;
    }

  if (g_ai_state == AIQ_DONE)
    {
      o = lv_label_create(scr);
      lv_obj_align(o, LV_ALIGN_CENTER, 0, -40);
      if (g_f28) lv_obj_set_style_text_font(o, g_f28, 0);
      snprintf(buf, sizeof(buf), "答对 %d / %d", g_ai_right, g_ai_n);
      lv_label_set_text(o, buf);

      o = lv_btn_create(scr);
      lv_obj_set_size(o, 120, 44);
      lv_obj_align(o, LV_ALIGN_CENTER, -66, 40);
      lv_obj_set_style_bg_color(o, C_GREEN, 0);
      lv_obj_set_style_border_width(o, 2, 0);
      lv_obj_set_style_border_color(o, C_LINE, 0);
      lv_obj_add_event_cb(o, ai_retry_cb, LV_EVENT_CLICKED, NULL);
      o = lv_label_create(o);
      if (g_f16) lv_obj_set_style_text_font(o, g_f16, 0);
      lv_label_set_text(o, "再来一局");
      lv_obj_center(o);

      o = lv_btn_create(scr);
      lv_obj_set_size(o, 120, 44);
      lv_obj_align(o, LV_ALIGN_CENTER, 66, 40);
      lv_obj_set_style_bg_color(o, C_PANEL, 0);
      lv_obj_set_style_border_width(o, 2, 0);
      lv_obj_set_style_border_color(o, C_LINE, 0);
      lv_obj_set_user_data(o, (void *)PAGE_QUEST);
      lv_obj_add_event_cb(o, btn_cb, LV_EVENT_CLICKED, NULL);
      o = lv_label_create(o);
      if (g_f16) lv_obj_set_style_text_font(o, g_f16, 0);
      lv_label_set_text(o, "返回");
      lv_obj_center(o);
      return;
    }

  /* AIQ_QUIZ: 气泡题干 + 4 选项 + 讲解条 */
  o = lv_obj_create(scr);
  lv_obj_remove_style_all(o);
  lv_obj_set_pos(o, 16, 52);
  lv_obj_set_size(o, 288, 136);
  lv_obj_set_style_bg_color(o, C_PANEL, 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(o, 2, 0);
  lv_obj_set_style_border_color(o, C_LINE, 0);
  lv_obj_set_style_pad_all(o, 8, 0);

  o = lv_label_create(o);
  lv_obj_align(o, LV_ALIGN_TOP_LEFT, 0, 0);
  if (g_f12) lv_obj_set_style_text_font(o, g_f12, 0);
  lv_obj_set_style_text_color(o, C_MUTED, 0);
  lv_label_set_text(o, "AI 出题");

  o = lv_label_create(o);
  lv_obj_align(o, LV_ALIGN_TOP_LEFT, 0, 18);
  lv_obj_set_width(o, 268);
  lv_label_set_long_mode(o, LV_LABEL_LONG_WRAP);
  if (g_f16) lv_obj_set_style_text_font(o, g_f16, 0);
  lv_obj_set_style_text_color(o, C_INK, 0);
  lv_label_set_text(o, g_ai_q[g_ai_idx]);

  o = lv_obj_create(scr);
  lv_obj_remove_style_all(o);
  lv_obj_set_pos(o, 16, 198);
  lv_obj_set_size(o, 288, 200);

  for (i = 0; i < 4; i++)
    {
      lv_obj_t *b = lv_btn_create(o);
      lv_obj_set_size(b, 288, 44);
      lv_obj_set_pos(b, 0, i * 52);
      lv_obj_set_style_bg_color(b, C_PANEL, 0);
      lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
      lv_obj_set_style_border_width(b, 2, 0);
      lv_obj_set_style_border_color(b, C_LINE, 0);
      lv_obj_set_user_data(b, (void *)(long)i);
      lv_obj_add_event_cb(b, ai_opt_cb, LV_EVENT_CLICKED, NULL);
      o = lv_label_create(b);
      if (g_f16) lv_obj_set_style_text_font(o, g_f16, 0);
      lv_label_set_text(o, g_ai_opt[g_ai_idx][i]);
      lv_obj_center(o);
    }

  g_ai_tip = lv_label_create(scr);
  lv_obj_set_pos(g_ai_tip, 16, 410);
  lv_obj_set_width(g_ai_tip, 288);
  lv_label_set_long_mode(g_ai_tip, LV_LABEL_LONG_WRAP);
  if (g_f12) lv_obj_set_style_text_font(g_ai_tip, g_f12, 0);
  lv_obj_set_style_text_color(g_ai_tip, C_MUTED, 0);
  lv_label_set_text(g_ai_tip, "点击你认为正确的单词");
}

static void ai_retry_cb(lv_event_t *e)
{
  (void)e;
  ai_start();
  show_page(PAGE_AI);
}

/* ==================== M8: TTS 单词发音 ====================
 * MiMo mimo-v2.5-tts(预置音色): 请求里把要读的文本放 assistant 消息,
 * 响应 choices[0].message.audio.data = base64 编码的 WAV(24kHz)。
 * 板端流式解码写 /data/tts.wav, nxplayer 播放。 */

#define TTS_RSP_FILE  MMJ_DIR "/tts_rsp.json"
#define TTS_WAV_FILE  MMJ_DIR "/tts.wav"
#define TTS_SH_FILE   MMJ_DIR "/play.sh"

static char     g_tts_text[192] = "";    /* 待朗读文本 */
static volatile int g_tts_busy = 0;

/* 单个 base64 字符 → 0..63, 非法返回 -1 */
static int b64val(char c)
{
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

/* 从 MiMo 响应中提取 audio.data(base64 WAV) 并解码成 wav 文件 */
static int tts_wav_extract(const char *rspfile, const char *wavfile)
{
  FILE *in = fopen(rspfile, "r");
  FILE *out;
  char *buf;
  long sz;
  char *p;
  unsigned long acc = 0;
  int bits = 0;

  if (in == NULL) return -1;
  fseek(in, 0, SEEK_END);
  sz = ftell(in);
  fseek(in, 0, SEEK_SET);
  if (sz <= 0 || sz > 4 * 1024 * 1024) { fclose(in); return -2; }
  buf = (char *)malloc(sz + 1);
  if (buf == NULL) { fclose(in); return -3; }
  if (fread(buf, 1, sz, in) != (size_t)sz) { free(buf); fclose(in); return -4; }
  buf[sz] = '\0';
  fclose(in);

  p = strstr(buf, "\"audio\"");
  if (p != NULL) p = strstr(p + 7, "\"data\"");
  if (p == NULL) { free(buf); return -5; }
  p = strchr(p + 6, '"');
  if (p == NULL) { free(buf); return -5; }
  p++;

  out = fopen(wavfile, "wb");
  if (out == NULL) { free(buf); return -6; }

  if (strncmp(p, "data:", 5) == 0)         /* data:audio/wav;base64,xxx 形态 */
    {
      p = strchr(p, ',');
      if (p == NULL) { fclose(out); free(buf); return -7; }
      p++;
    }

  for (; *p != '\0' && *p != '"'; p++)
    {
      int v = b64val(*p);
      if (v < 0) continue;                 /* 跳过空白/填充容错 */
      acc = (acc << 6) | (unsigned long)v;
      bits += 6;
      if (bits >= 8)
        {
          bits -= 8;
          fputc((int)((acc >> bits) & 0xff), out);
        }
    }
  fclose(out);
  free(buf);
  return 0;
}

/* 后台任务: 请求 TTS → 解码 → 播放(全程阻塞本任务, UI 不卡) */
static int tts_task(int argc, char *argv[])
{
  FILE *fp;
  char body[512];
  char esc[256];
  char cmd[640];
  char meta[128] = "";

  (void)argc; (void)argv;

  if (g_ai_key[0] == '\0')
    {
      printf("[mmj] tts: no api key\n");
      g_tts_busy = 0;
      return 0;
    }

  json_escape(esc, sizeof(esc), g_tts_text);
  snprintf(body, sizeof(body),
           "{\"model\":\"mimo-v2.5-tts\",\"messages\":["
           "{\"role\":\"user\",\"content\":\"%s\"},"
           "{\"role\":\"assistant\",\"content\":\"%s\"}],"
           "\"audio\":{\"format\":\"wav\",\"voice\":\"%s\"}}",
           "请用自然清晰的语气朗读。", esc,
           g_tts_voice[0] ? g_tts_voice : "冰糖");

  fp = fopen(AI_REQ_FILE, "w");
  if (fp != NULL)
    {
      fputs(body, fp);
      fclose(fp);
    }
  remove(TTS_RSP_FILE);
  snprintf(cmd, sizeof(cmd),
           "curl -sk -m 60 -o %s -w \"HTTPCODE:%%{http_code}\" "
           "%s/chat/completions -H \"Authorization: Bearer %s\" "
           "-H \"Content-Type: application/json\" -d @%s > %s 2>&1",
           TTS_RSP_FILE, g_ai_base, g_ai_key, AI_REQ_FILE, AI_META_FILE);
  system(cmd);

  fp = fopen(AI_META_FILE, "r");
  if (fp != NULL)
    {
      size_t rn = fread(meta, 1, sizeof(meta) - 1, fp);
      meta[rn] = '\0';
      fclose(fp);
    }
  printf("[mmj] tts: %s\n", meta);

  if (strstr(meta, "HTTPCODE:200") == NULL)
    {
      printf("[mmj] tts: request failed\n");
      g_tts_busy = 0;
      return 0;
    }

  if (tts_wav_extract(TTS_RSP_FILE, TTS_WAV_FILE) != 0)
    {
      printf("[mmj] tts: wav extract failed\n");
      g_tts_busy = 0;
      return 0;
    }
  {
    FILE *f = fopen(TTS_WAV_FILE, "r");
    if (f != NULL)
      {
        fseek(f, 0, SEEK_END);
        printf("[mmj] tts: %ld bytes wav, playing\n", ftell(f));
        fclose(f);
      }
  }

  fp = fopen(TTS_SH_FILE, "w");
  if (fp != NULL)
    {
      fprintf(fp, "play %s\nq\n", TTS_WAV_FILE);
      fclose(fp);
    }
  system("nxplayer < " TTS_SH_FILE);
  g_tts_busy = 0;
  return 0;
}

/* 触发发音(防重入): 文本进后台队列 */
static void tts_speak(const char *text)
{
  if (g_tts_busy) return;                /* 正在合成/播放, 忽略连点 */
  if (g_ai_key[0] == '\0') return;
  strncpy(g_tts_text, text, sizeof(g_tts_text) - 1);
  g_tts_text[sizeof(g_tts_text) - 1] = '\0';
  g_tts_busy = 1;
  if (task_create("mmj_tts", 95, 8192, tts_task, NULL) < 0)
    g_tts_busy = 0;
}

static void show_page(int page)
{
  if (!scr) return;

  data_save_now();              /* 切页前补写未落盘的进度 */

  lv_obj_clean(scr);            /* 清掉上一页所有控件 */
  lbl_time = NULL;
  lbl_date = NULL;
  lbl_wifi = NULL;
  lbl_set_tip = NULL;
  lbl_wifi_st = NULL;           /* WiFi 页状态行(防悬空指针) */
  wifi_list = NULL;
  wifi_ta = NULL;
  wifi_kb = NULL;

  g_page = page;
  switch (page)
    {
      case PAGE_LEARN:
        build_learn();
        break;
      case PAGE_QUEST:
        if (g_q_timer != NULL)  /* 取消尚未触发的切题定时器 */
          {
            lv_timer_del(g_q_timer);
            g_q_timer = NULL;
          }
        g_q_hearts = 3;         /* 每关重置: 3 心 / 计分 / 防连点 */
        g_q_done   = 0;
        g_q_right  = 0;
        g_q_wait   = 0;
        g_q_redo_n = 0;         /* 清空重测队列 */
        g_q_redo_i = 0;
        g_q_in_redo = 0;
        quest_pick_unit();      /* 自动定位到第一个已解锁单元 */
        build_quest();
        break;
      case PAGE_BOOK:
        build_book();
        break;
      case PAGE_SET:
        build_set();
        break;
      case PAGE_WIFI:
        build_wifi();
        break;
      case PAGE_AI:
        if (g_ai_next_t != NULL)   /* 答题后切题定时器(切页需取消) */
          {
            lv_timer_del(g_ai_next_t);
            g_ai_next_t = NULL;
          }
        if (g_ai_poll != NULL)     /* 出题轮询定时器 */
          {
            lv_timer_del(g_ai_poll);
            g_ai_poll = NULL;
          }
        build_ai();
        break;
      default:
        build_home();
        g_page = PAGE_HOME;
        break;
    }
}

int main(int argc, FAR char *argv[])
{
  lv_nuttx_dsc_t    info;
  lv_nuttx_result_t result;

  (void)argc;
  (void)argv;

#ifdef CONFIG_BOARDCTL
  boardctl(BOARDIOC_INIT, 0);
#endif

  lv_init();

  /* 中文字体(运行时加载, 多档精简字库); 任一档缺失则退回内置 */
  g_f12  = lv_binfont_create("/etc/fonts/font_puhui_12_4.bin");
  g_f16  = lv_binfont_create("/etc/fonts/font_puhui_16_4.bin");
  g_f16b = lv_binfont_create("/etc/fonts/font_puhui_16b_4.bin");
  g_f21  = lv_binfont_create("/etc/fonts/font_puhui_21_4.bin");
  g_f28  = lv_binfont_create("/etc/fonts/font_puhui_28_4.bin");

  /* 音标字体(MiSans, 含全套 IPA; 普惠体无 IPA 字形故单独一档) */
  g_fipa = lv_binfont_create("/etc/fonts/font_ipa_16_4.bin");
  if (g_fipa == NULL) g_fipa = g_f16;   /* 音标字库缺失时退回通用字库(音标可能缺字形) */

  printf("[mmj] fonts 12=%p 16=%p 16b=%p 21=%p 28=%p ipa=%p\n",
         g_f12, g_f16, g_f16b, g_f21, g_f28, g_fipa);

  g_font = g_f16 ? g_f16 : &lv_font_montserrat_16;
  if (g_f16 == NULL)
    {
      printf("[mmj] Chinese font not found, using default\n");
    }

  settings_load();             /* 读取主题/每日目标(在 theme_apply 前) */

  /* AI 配置(网页端写入) + Web 管理服务(电脑浏览器直连板子) */
  ai_cfg_load();
  task_create("mmj_web", 90, 12288, mmj_web_task, NULL);
  theme_apply(g_theme);        /* 应用主题配色 */
  data_load();                 /* 载入 /data/mmj/words.json 学习进度 */
  srand((unsigned)time(NULL)); /* 闯关出题随机数种子 */

  /* WiFi: /data/wifi.cfg 由 wifi_manager 守护开机自动连接(含 DHCP);
   * 应用只读配置用于界面显示 */
  wifi_conf_load();

  lv_nuttx_dsc_init(&info);
  info.fb_path = "/dev/fb0";
  info.input_path = "/dev/input0";

  lv_nuttx_init(&info, &result);
  if (result.disp == NULL)
    {
      printf("[mmj] LVGL init failed!\n");
      return 1;
    }

  scr = lv_screen_active();
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);  /* 页面级禁滚动(防密码页上下晃) */

  /* 东八区: NTP/HTTP 对时写的是 UTC, localtime 按 TZ 显示 */
  setenv("TZ", "CST-8", 1);
  tzset();

  build_home();

  lv_timer_create(upd_clock, 1000, NULL);

  printf("[mmj] MiaoMiaoJi ready (320x480)\n");

  while (1)
    {
      uint32_t idle = lv_timer_handler();
      idle = idle ? idle : 1;
      usleep(idle * 1000);
    }

  lv_nuttx_deinit(&result);
  lv_deinit();
  return 0;
}
