// C3 AdBlock v3 (native ESP-IDF): DNS sinkhole + dashboard.
// Boots with an IDF-built bootloader + IDF flash stack, which handle this
// board's 16MB XTX flash correctly (the Arduino prebuilt bootloader and its
// IDF-5.x flash stack read back 0xFF for everything).
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "nvs_flash.h"
#include "esp_vfs_fat.h"
#include "esp_partition.h"
#include <sys/statvfs.h>
#include "driver/gpio.h"
#include "driver/temperature_sensor.h"
#include "mdns.h"
#include "state.h"
#include "store.h"
#include "dns.h"
#include "web.h"

static const char *TAG = "main";

// ---- globals ----
SemaphoreHandle_t g_lock;
client_t g_clients[MAX_CLIENTS];
int g_num_clients = 0;
uint32_t g_banned_ip[MAX_BAN];
int g_num_banned = 0;
char g_custom_dom[MAX_CUSTOM][64];
uint64_t g_custom_hash[MAX_CUSTOM];
int g_num_custom = 0;
char g_allow_dom[MAX_ALLOW][64];
uint64_t g_allow_hash[MAX_ALLOW];
int g_num_allow = 0;
logent_t g_log[LOG_N];
int g_log_head = 0, g_log_count = 0;
uint32_t g_total_blocked = 0, g_total_allowed = 0;
uint32_t g_num_hashes = 0;
bool g_blocking_on = true;
uint32_t g_resume_at_ms = 0;
bool g_portal_mode = false;
uint32_t g_ap_ip = 0;
char g_update_url[256] = {0};
uint32_t g_update_interval_h = 24;
char g_update_status[96] = "never";
char g_web_user[32] = "admin";
char g_web_pass[64] = "admin";
char g_ota_pass[64] = "admin";
int g_last_rssi = -99;
char g_lan_ip[16] = "0.0.0.0";

static temperature_sensor_handle_t s_temp = NULL;
static bool s_fetching = false;
static uint32_t s_last_update_ms = 0;

void main_fetch_done(void) {
  xSemaphoreTake(g_lock, portMAX_DELAY);
  s_fetching = false;
  xSemaphoreGive(g_lock);
}

float web_temp(void) {
  if (!s_temp) return -99;
  float t = -99;
  if (temperature_sensor_get_celsius(s_temp, &t) != ESP_OK) return -99;
  return t;
}

uint32_t web_fsu(void) {
  struct statvfs s;
  if (statvfs("/sp", &s) == 0)
    return (uint32_t)((uint64_t)(s.f_blocks - s.f_bfree) * s.f_frsize);
  return 0;
}
uint32_t web_fst(void) {
  struct statvfs s;
  if (statvfs("/sp", &s) == 0)
    return (uint32_t)((uint64_t)s.f_blocks * s.f_frsize);
  return 0;
}

static void read_line2(const char *path, char *l1, size_t n1, char *l2, size_t n2) {
  if (l1) l1[0] = 0;
  if (l2) l2[0] = 0;
  FILE *f = fopen(path, "r");
  if (!f) return;
  if (l1 && !fgets(l1, n1, f)) l1[0] = 0;
  if (l2 && !fgets(l2, n2, f)) l2[0] = 0;
  fclose(f);
  if (l1) l1[strcspn(l1, "\r\n")] = 0;
  if (l2) l2[strcspn(l2, "\r\n")] = 0;
}

static void load_configs(void) {
  char u[32], w[64], o[64];
  read_line2("/sp/auth.cfg", u, sizeof(u), w, sizeof(w));
  // third line: ota pass
  FILE *f = fopen("/sp/auth.cfg", "r");
  if (f) {
    char l1[64], l2[96], l3[96];
    if (fgets(l1, sizeof(l1), f)) {
      if (fgets(l2, sizeof(l2), f)) {
        if (fgets(l3, sizeof(l3), f)) {
          l3[strcspn(l3, "\r\n")] = 0;
          if (strlen(l3) >= 4 && strlen(l3) < sizeof(g_ota_pass))
            strncpy(g_ota_pass, l3, sizeof(g_ota_pass) - 1);
        }
      }
    }
    fclose(f);
  }
  if (strlen(u) >= 1 && strlen(u) < sizeof(g_web_user)) strncpy(g_web_user, u, sizeof(g_web_user) - 1);
  if (strlen(w) >= 4 && strlen(w) < sizeof(g_web_pass)) strncpy(g_web_pass, w, sizeof(g_web_pass) - 1);
  (void)o;

  char uu[256], hh[16];
  read_line2("/sp/update.cfg", uu, sizeof(uu), hh, sizeof(hh));
  if (strlen(uu) < sizeof(g_update_url)) strncpy(g_update_url, uu, sizeof(g_update_url) - 1);
  long h = atol(hh);
  g_update_interval_h = (h >= 1) ? (uint32_t)h : 24;
}

// ---------- WiFi ----------
static bool s_wifi_ok = false;

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    ESP_LOGI(TAG, "wifi disconnected, retrying");
    esp_wifi_connect();
  } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *e = data;
    snprintf(g_lan_ip, sizeof(g_lan_ip), IPSTR, IP2STR(&e->ip_info.ip));
    ESP_LOGI(TAG, "WiFi up: %s", g_lan_ip);
    s_wifi_ok = true;
  }
}

static bool wifi_join(const char *ssid, const char *pass, uint32_t timeout_ms) {
  s_wifi_ok = false;
  wifi_config_t cfg;
  memset(&cfg, 0, sizeof(cfg));
  strncpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid) - 1);
  strncpy((char *)cfg.sta.password, pass ? pass : "", sizeof(cfg.sta.password) - 1);
  cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
  ESP_ERROR_CHECK(esp_wifi_start());
  ESP_ERROR_CHECK(esp_wifi_connect());
  uint32_t t0 = now_ms();
  while (!s_wifi_ok && now_ms() - t0 < timeout_ms) vTaskDelay(pdMS_TO_TICKS(250));
  return s_wifi_ok;
}

static void wifi_ap(const char *ssid) {
  esp_wifi_stop();
  wifi_config_t cfg;
  memset(&cfg, 0, sizeof(cfg));
  strncpy((char *)cfg.ap.ssid, ssid, sizeof(cfg.ap.ssid) - 1);
  cfg.ap.authmode = WIFI_AUTH_OPEN;
  cfg.ap.max_connection = 4;
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &cfg));
  ESP_ERROR_CHECK(esp_wifi_start());
  esp_netif_ip_info_t info;
  esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_AP_DEF"), &info);
  g_ap_ip = info.ip.addr;
  ESP_LOGI(TAG, "portal AP \"%s\" at " IPSTR, ssid, IP2STR(&info.ip));
}

// ---------- background tasks ----------
extern void dns_task(void *arg);

static void updater_task(void *arg) {
  (void)arg;
  while (1) {
    vTaskDelay(pdMS_TO_TICKS(10000));
    if (g_portal_mode) continue;
    // skip an immediate fetch on boot; then fetch on schedule
    bool due = false;
    xSemaphoreTake(g_lock, portMAX_DELAY);
    if (s_last_update_ms == 0) {
      s_last_update_ms = now_ms();
    } else if (strlen(g_update_url) > 0 && !s_fetching &&
               now_ms() - s_last_update_ms >= g_update_interval_h * 3600000UL) {
      s_last_update_ms = now_ms();
      s_fetching = true;
      due = true;
    }
    xSemaphoreGive(g_lock);
    if (due) web_fetch_start();   // completion clears s_fetching via main_fetch_done()
  }
}

// ---------- app_main ----------
void app_main(void) {
  ESP_LOGI(TAG, "[c3-adblock] %s booting", FW_VERSION);
  g_lock = xSemaphoreCreateMutex();

  // BOOT held at power-on -> wipe saved WiFi (force portal)
  gpio_set_direction(GPIO_NUM_9, GPIO_MODE_INPUT);
  gpio_set_pull_mode(GPIO_NUM_9, GPIO_PULLUP_ONLY);
  vTaskDelay(pdMS_TO_TICKS(50));
  if (gpio_get_level(GPIO_NUM_9) == 0) {
    vTaskDelay(pdMS_TO_TICKS(60));
    if (gpio_get_level(GPIO_NUM_9) == 0) {
      remove("/sp/wifi.cfg");
      ESP_LOGI(TAG, "BOOT held -> cleared saved WiFi");
    }
  }

  // FAT + wear leveling
  esp_vfs_fat_mount_config_t mcfg = {
    .format_if_mount_failed = true,
    .max_files = 8,
    .allocation_unit_size = CONFIG_WL_SECTOR_SIZE,
  };
  wl_handle_t wl = WL_INVALID_HANDLE;
  esp_err_t e = esp_vfs_fat_spiflash_mount_rw_wl("/sp", "storage", &mcfg, &wl);
  ESP_LOGI(TAG, "FAT mount: %s", esp_err_to_name(e));
  (void)wl;

  load_configs();
  ESP_LOGI(TAG, "auth user=%s defcreds=%d", g_web_user,
           (strcmp(g_web_pass, "admin") == 0 || strcmp(g_ota_pass, "admin") == 0));

  xSemaphoreTake(g_lock, portMAX_DELAY);
  store_open_blocklist();
  store_load_lists();
  xSemaphoreGive(g_lock);

  // temp sensor (best effort)
  temperature_sensor_config_t tcfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
  if (temperature_sensor_install(&tcfg, &s_temp) == ESP_OK)
    temperature_sensor_enable(s_temp);

  ESP_ERROR_CHECK(nvs_flash_init());
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_sta();
  esp_netif_create_default_wifi_ap();
  // friendly LAN hostname (shows in router client lists)
  esp_netif_set_hostname(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"), "c3adblock");
  wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&wcfg));
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL));
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

  xTaskCreate(dns_task, "dns", 4096, NULL, 10, NULL);

  char ssid[64] = {0}, pass[64] = {0};
  read_line2("/sp/wifi.cfg", ssid, sizeof(ssid), pass, sizeof(pass));
  bool joined = false;
  if (strlen(ssid) > 0) {
    ESP_LOGI(TAG, "WiFi: connecting to \"%s\"", ssid);
    joined = wifi_join(ssid, pass, 30000);
  }
  if (!joined) {
    // setup portal (blocks here until creds saved + reboot)
    g_portal_mode = true;
    uint8_t mac[6]; esp_wifi_get_mac(WIFI_IF_STA, mac);
    char ap[24]; snprintf(ap, sizeof(ap), "C3-AdBlock-%02X%02X", mac[4], mac[5]);
    wifi_ap(ap);
    web_start(true);
    ESP_LOGI(TAG, "[setup] Join open network \"%s\" to configure WiFi", ap);
    // portal runs forever; wifisave reboots. Reboot-and-retry if idle+configured.
    bool configured = strlen(ssid) > 0;
    uint32_t t0 = now_ms();
    while (1) {
      vTaskDelay(pdMS_TO_TICKS(500));
      wifi_sta_list_t st;
      if (esp_wifi_ap_get_sta_list(&st) == ESP_OK && st.num > 0) t0 = now_ms();
      if (configured && now_ms() - t0 > 180000UL) {
        ESP_LOGI(TAG, "[setup] retrying WiFi");
        esp_restart();
      }
    }
  }

  ESP_LOGI(TAG, "dashboard: http://%s (c3adblock)", g_lan_ip);
  // mDNS: http://c3adblock.local (Windows 10+, iOS, Android 12+; IP cho thiết bị cũ)
  if (mdns_init() == ESP_OK) {
    mdns_hostname_set("c3adblock");
    mdns_instance_name_set("C3 AdBlock");
    mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    ESP_LOGI(TAG, "mDNS up: http://c3adblock.local");
  } else {
    ESP_LOGW(TAG, "mDNS init failed (dung IP)");
  }
  web_start(false);
  xTaskCreate(updater_task, "updater", 4096, NULL, 5, NULL);
  ESP_LOGI(TAG, "DNS :53 + dashboard :80 up");
}
