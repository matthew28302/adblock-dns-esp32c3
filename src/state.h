#pragma once
// Shared state for the C3 AdBlock IDF app.
#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define FW_VERSION "3.0-idf"
#define HASH_BYTES 5
#define INDEX_ENTRIES 4096
#define CACHE_SIZE 512          // must be power of 2
#define MAX_RANGE 512
#define MAX_CLIENTS 96
#define MAX_CUSTOM 200
#define MAX_ALLOW 200
#define MAX_BAN 32
#define LOG_N 24

typedef struct {
  uint32_t ip;                  // network byte order (as seen on the wire)
  uint8_t mac[6];
  uint32_t blocked, allowed;
  uint32_t last_seen_ms;
  bool banned;
} client_t;

typedef struct {
  char domain[128];
  uint32_t ip;
  uint32_t age_ms;              // when it was blocked (xTaskGetTickCount ms)
} logent_t;

extern SemaphoreHandle_t g_lock;
extern client_t g_clients[MAX_CLIENTS];
extern int g_num_clients;
extern uint32_t g_banned_ip[MAX_BAN];
extern int g_num_banned;

extern char g_custom_dom[MAX_CUSTOM][64];
extern uint64_t g_custom_hash[MAX_CUSTOM];
extern int g_num_custom;

extern char g_allow_dom[MAX_ALLOW][64];
extern uint64_t g_allow_hash[MAX_ALLOW];
extern int g_num_allow;

extern logent_t g_log[LOG_N];
extern int g_log_head, g_log_count;

extern uint32_t g_total_blocked, g_total_allowed;
extern uint32_t g_num_hashes;

extern bool g_blocking_on;
extern uint32_t g_resume_at_ms;   // 0 = no auto-resume
extern bool g_portal_mode;        // true while running the WiFi setup portal
extern uint32_t g_ap_ip;          // portal AP address (network byte order)

extern char g_update_url[256];
extern uint32_t g_update_interval_h;
extern char g_update_status[96];

extern char g_web_user[32];
extern char g_web_pass[64];
extern char g_ota_pass[64];

extern int g_last_rssi;
extern char g_lan_ip[16];

uint32_t now_ms(void);
