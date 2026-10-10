// Dashboard + JSON API + OTA (blocklist & firmware) + captive portal pages.
// Auth: HTTP Basic (admin/WEB_PASS from /sp/auth.cfg) + custom CSRF header
// (X-Requested-With: c3-adblock) on every mutating endpoint — same threat
// model as the Arduino version.
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_random.h"
#include "esp_system.h"
#include "mbedtls/base64.h"
#include "state.h"
#include "store.h"
#include "page.h"
#include "web.h"

float web_temp(void);
uint32_t web_fsu(void);
uint32_t web_fst(void);

static const char *TAG = "web";
static httpd_handle_t s_server = NULL;

uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

// ---------- small helpers ----------
static void urldecode(char *s) {
  char *w = s;
  for (char *p = s; *p; p++) {
    if (*p == '%' && p[1] && p[2]) {
      char h[3] = {p[1], p[2], 0};
      *w++ = (char)strtol(h, NULL, 16); p += 2;
    } else if (*p == '+') *w++ = ' ';
    else *w++ = *p;
  }
  *w = 0;
}

static int qval(httpd_req_t *req, const char *key, char *out, size_t n) {
  size_t qlen = httpd_req_get_url_query_len(req) + 1;
  if (qlen <= 1 || qlen > 2048) return 0;
  char *q = malloc(qlen);
  if (!q) return 0;
  int r = 0;
  if (httpd_req_get_url_query_str(req, q, qlen) == ESP_OK &&
      httpd_query_key_value(q, key, out, n) == ESP_OK) {
    urldecode(out);
    r = 1;
  }
  free(q);
  return r;
}

static void json_escape(const char *s, char *o, size_t n) {
  size_t j = 0;
  for (; *s && j + 6 < n; s++) {
    if (*s == '"' || *s == '\\') { o[j++] = '\\'; o[j++] = *s; }
    else if ((unsigned char)*s < 0x20) { j += snprintf(o + j, n - j, "\\u%04x", *s); }
    else o[j++] = *s;
  }
  o[j] = 0;
}

static void html_escape(const char *s, char *o, size_t n) {
  size_t j = 0;
  for (; *s && j + 6 < n; s++) {
    const char *rep = NULL;
    switch (*s) {
      case '&': rep = "&amp;"; break;
      case '<': rep = "&lt;"; break;
      case '>': rep = "&gt;"; break;
      case '"': rep = "&quot;"; break;
      case '\'': rep = "&#39;"; break;
    }
    if (rep) { size_t l = strlen(rep); if (j + l >= n) break; strcpy(o + j, rep); j += l; }
    else o[j++] = *s;
  }
  o[j] = 0;
}

static void basic_expected(char *out, size_t n) {
  char cred[128];
  snprintf(cred, sizeof(cred), "%s:%s", g_web_user, g_web_pass);
  size_t olen = 0;
  mbedtls_base64_encode((unsigned char *)out, n, &olen,
                        (const unsigned char *)cred, strlen(cred));
}

// real auth check
static bool check_auth(httpd_req_t *req) {
  size_t len = httpd_req_get_hdr_value_len(req, "Authorization") + 1;
  if (len <= 1) return false;
  char *v = malloc(len);
  bool ok = false;
  if (v && httpd_req_get_hdr_value_str(req, "Authorization", v, len) == ESP_OK) {
    char exp[192]; basic_expected(exp, sizeof(exp));
    char want[200]; snprintf(want, sizeof(want), "Basic %s", exp);
    ok = (strcmp(v, want) == 0);
  }
  free(v);
  return ok;
}

static bool check_csrf(httpd_req_t *req) {
  char v[32] = {0};
  if (httpd_req_get_hdr_value_str(req, "X-Requested-With", v, sizeof(v)) != ESP_OK)
    return false;
  return strcmp(v, "c3-adblock") == 0;
}

// mutating endpoints: auth + CSRF. Sends 401 (with Basic challenge) or 403.
static bool require_auth(httpd_req_t *req) {
  if (!check_csrf(req)) {
    httpd_resp_set_status(req, "403 Forbidden");
    httpd_resp_send(req, "missing CSRF header", HTTPD_RESP_USE_STRLEN);
    return false;
  }
  if (check_auth(req)) return true;
  httpd_resp_set_status(req, "401 Unauthorized");
  httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"c3adblock\"");
  httpd_resp_send(req, "auth required", HTTPD_RESP_USE_STRLEN);
  return false;
}

// ---------- /stats.json ----------
static const char *mac_str(const uint8_t *m, char *o) {
  snprintf(o, 18, "%02x:%02x:%02x:%02x:%02x:%02x", m[0], m[1], m[2], m[3], m[4], m[5]);
  return o;
}

static esp_err_t h_stats(httpd_req_t *req) {
  // refresh RSSI (best effort)
  wifi_ap_record_t ap;
  if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) g_last_rssi = ap.rssi;

  uint32_t up = now_ms() / 1000;
  char ut[24]; snprintf(ut, sizeof(ut), "%lud %luh %lum",
      (unsigned long)(up / 86400), (unsigned long)((up % 86400) / 3600),
      (unsigned long)((up % 3600) / 60));

  size_t cap = 24576;
  char *j = malloc(cap);
  if (!j) { httpd_resp_send_500(req); return ESP_FAIL; }
  size_t o = 0;
  char esc[512];
  json_escape(g_update_url, esc, sizeof(esc));
  char escst[160]; json_escape(g_update_status, escst, sizeof(escst));
  bool defcreds = (strcmp(g_web_pass, "admin") == 0 || strcmp(g_ota_pass, "admin") == 0);
  o += snprintf(j + o, cap - o,
      "{\"fw\":\"%s\",\"ip\":\"%s\",\"blocked\":%lu,\"allowed\":%lu,"
      "\"domains\":%lu,\"listbytes\":%lu,"
      "\"rssi\":%d,\"temp\":%.1f,"
      "\"heap\":%lu,\"uptime\":\"%s\","
      "\"fsu\":%lu,\"fst\":%lu,"
      "\"upurl\":\"%s\",\"upiv\":%lu,\"upstat\":\"%s\","
      "\"blocking\":%s,"
      "\"resumeIn\":%lu,"
      "\"defcreds\":%s,"
      "\"clients\":[",
      FW_VERSION, g_lan_ip,
      (unsigned long)g_total_blocked, (unsigned long)g_total_allowed,
      (unsigned long)g_num_hashes, (unsigned long)g_num_hashes * HASH_BYTES,
      g_last_rssi, (double)web_temp(),
      (unsigned long)esp_get_free_heap_size(), ut,
      (unsigned long)web_fsu(), (unsigned long)web_fst(),
      esc, (unsigned long)g_update_interval_h, escst,
      g_blocking_on ? "true" : "false",
      (!g_blocking_on && g_resume_at_ms) ? (unsigned long)(g_resume_at_ms - now_ms()) / 1000 : 0,
      defcreds ? "true" : "false");

  xSemaphoreTake(g_lock, portMAX_DELAY);
  for (int i = 0; i < g_num_clients && o + 160 < cap; i++) {
    client_t *c = &g_clients[i];
    struct in_addr a; a.s_addr = c->ip;
    char mac[18]; mac_str(c->mac, mac);
    o += snprintf(j + o, cap - o, "%s{\"ip\":\"%s\",\"mac\":\"%s\",\"blocked\":%lu,\"allowed\":%lu,\"banned\":%s}",
                  i ? "," : "", inet_ntoa(a), mac, (unsigned long)c->blocked, (unsigned long)c->allowed,
                  c->banned ? "true" : "false");
  }
  o += snprintf(j + o, cap - o, "],\"custom\":[");
  for (int i = 0; i < g_num_custom && o + 80 < cap; i++) {
    json_escape(g_custom_dom[i], esc, sizeof(esc));
    o += snprintf(j + o, cap - o, "%s\"%s\"", i ? "," : "", esc);
  }
  o += snprintf(j + o, cap - o, "],\"allow\":[");
  for (int i = 0; i < g_num_allow && o + 80 < cap; i++) {
    json_escape(g_allow_dom[i], esc, sizeof(esc));
    o += snprintf(j + o, cap - o, "%s\"%s\"", i ? "," : "", esc);
  }
  o += snprintf(j + o, cap - o, "],\"log\":[");
  bool first = true;
  for (int k = 0; k < g_log_count && o + 200 < cap; k++) {
    int idx = (g_log_head + LOG_N - 1 - k) % LOG_N;
    if (!g_log[idx].domain[0]) continue;
    struct in_addr a; a.s_addr = g_log[idx].ip;
    uint32_t sec = (now_ms() - g_log[idx].age_ms) / 1000;
    json_escape(g_log[idx].domain, esc, sizeof(esc));
    o += snprintf(j + o, cap - o, "%s{\"d\":\"%s\",\"ip\":\"%s\",\"t\":%lu}",
                  first ? "" : ",", esc, inet_ntoa(a), (unsigned long)sec);
    first = false;
  }
  o += snprintf(j + o, cap - o, "]}");
  xSemaphoreGive(g_lock);

  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, j, o);
  free(j);
  return ESP_OK;
}

// ---------- simple state endpoints ----------
static esp_err_t h_root(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  httpd_resp_send(req, PAGE, HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

static esp_err_t h_ban(httpd_req_t *req) {
  if (!require_auth(req)) return ESP_OK;
  char v[32] = {0};
  if (qval(req, "ip", v, sizeof(v))) {
    unsigned a, b, c, d;
    if (sscanf(v, "%u.%u.%u.%u", &a, &b, &c, &d) == 4) {
      uint32_t ip = ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)c << 8) | d;
      xSemaphoreTake(g_lock, portMAX_DELAY);
      bool found = false;
      for (int i = 0; i < g_num_banned; i++)
        if (g_banned_ip[i] == ip) {
          for (int k = i; k < g_num_banned - 1; k++) g_banned_ip[k] = g_banned_ip[k + 1];
          g_num_banned--; found = true; break;
        }
      if (!found && g_num_banned < MAX_BAN) g_banned_ip[g_num_banned++] = ip;
      for (int i = 0; i < g_num_clients; i++)
        if (g_clients[i].ip == ip) g_clients[i].banned = !found;
      store_save_banned();
      xSemaphoreGive(g_lock);
    }
  }
  httpd_resp_send(req, "ok", HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

static esp_err_t h_addblock(httpd_req_t *req) {
  if (!require_auth(req)) return ESP_OK;
  char v[128] = {0};
  if (qval(req, "d", v, sizeof(v))) {
    xSemaphoreTake(g_lock, portMAX_DELAY);
    store_add_custom(v);
    xSemaphoreGive(g_lock);
  }
  httpd_resp_send(req, "ok", HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

static esp_err_t h_unblock(httpd_req_t *req) {
  if (!require_auth(req)) return ESP_OK;
  char v[128] = {0};
  if (qval(req, "d", v, sizeof(v))) {
    xSemaphoreTake(g_lock, portMAX_DELAY);
    store_del_custom(v);
    xSemaphoreGive(g_lock);
  }
  httpd_resp_send(req, "ok", HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

static esp_err_t h_addallow(httpd_req_t *req) {
  if (!require_auth(req)) return ESP_OK;
  char v[128] = {0};
  if (qval(req, "d", v, sizeof(v))) {
    xSemaphoreTake(g_lock, portMAX_DELAY);
    store_add_allow(v);
    xSemaphoreGive(g_lock);
  }
  httpd_resp_send(req, "ok", HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

static esp_err_t h_removeallow(httpd_req_t *req) {
  if (!require_auth(req)) return ESP_OK;
  char v[128] = {0};
  if (qval(req, "d", v, sizeof(v))) {
    xSemaphoreTake(g_lock, portMAX_DELAY);
    store_del_allow(v);
    xSemaphoreGive(g_lock);
  }
  httpd_resp_send(req, "ok", HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

static esp_err_t h_pause(httpd_req_t *req) {
  if (!require_auth(req)) return ESP_OK;
  char v[16] = {0};
  long s = 0;
  if (qval(req, "s", v, sizeof(v))) s = atol(v);
  xSemaphoreTake(g_lock, portMAX_DELAY);
  g_blocking_on = false;
  g_resume_at_ms = (s > 0) ? now_ms() + (uint32_t)s * 1000UL : 0;
  xSemaphoreGive(g_lock);
  httpd_resp_send(req, "paused", HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

static esp_err_t h_resume(httpd_req_t *req) {
  if (!require_auth(req)) return ESP_OK;
  xSemaphoreTake(g_lock, portMAX_DELAY);
  g_blocking_on = true; g_resume_at_ms = 0;
  xSemaphoreGive(g_lock);
  httpd_resp_send(req, "resumed", HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

// read-only domain check (no auth needed)
static esp_err_t h_check(httpd_req_t *req) {
  char v[128] = {0};
  qval(req, "d", v, sizeof(v));
  for (char *p = v; *p; p++) if (*p >= 'A' && *p <= 'Z') *p += 32;
  if (strncmp(v, "www.", 4) == 0) memmove(v, v + 4, strlen(v + 4) + 1);
  bool valid = strlen(v) > 2 && strlen(v) < 120 && strchr(v, '.') != NULL;
  bool al = false, bl = false;
  if (valid) {
    xSemaphoreTake(g_lock, portMAX_DELAY);
    al = store_is_allowed(v);
    bl = !al && store_is_blocked(v);
    xSemaphoreGive(g_lock);
  }
  char esc[160]; json_escape(v, esc, sizeof(esc));
  char j[256];
  snprintf(j, sizeof(j), "{\"d\":\"%s\",\"valid\":%s,\"allowed\":%s,\"blocked\":%s}",
           esc, valid ? "true" : "false", al ? "true" : "false", bl ? "true" : "false");
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, j, HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

static esp_err_t h_forgetwifi(httpd_req_t *req) {
  if (!require_auth(req)) return ESP_OK;
  remove("/sp/wifi.cfg");
  httpd_resp_send(req, "cleared - rebooting into setup portal", HTTPD_RESP_USE_STRLEN);
  vTaskDelay(pdMS_TO_TICKS(500));
  esp_restart();
  return ESP_OK;
}

static esp_err_t h_setupdate(httpd_req_t *req) {
  if (!require_auth(req)) return ESP_OK;
  char u[256] = {0}, h[16] = {0};
  xSemaphoreTake(g_lock, portMAX_DELAY);
  if (qval(req, "u", u, sizeof(u))) {
    strncpy(g_update_url, u, sizeof(g_update_url) - 1);
  }
  if (qval(req, "h", h, sizeof(h))) {
    long v = atol(h); if (v < 1) v = 1;
    g_update_interval_h = (uint32_t)v;
  }
  FILE *f = fopen("/sp/update.cfg", "w");
  if (f) { fprintf(f, "%s\n%lu\n", g_update_url, (unsigned long)g_update_interval_h); fclose(f); }
  xSemaphoreGive(g_lock);
  httpd_resp_send(req, "ok", HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

static esp_err_t h_setauth(httpd_req_t *req) {
  if (!require_auth(req)) return ESP_OK;
  char u[32] = {0}, w[64] = {0}, o[64] = {0};
  xSemaphoreTake(g_lock, portMAX_DELAY);
  if (qval(req, "u", u, sizeof(u)) && strlen(u) >= 1 && strlen(u) < sizeof(g_web_user))
    strncpy(g_web_user, u, sizeof(g_web_user) - 1);
  if (qval(req, "w", w, sizeof(w)) && strlen(w) >= 4 && strlen(w) < sizeof(g_web_pass))
    strncpy(g_web_pass, w, sizeof(g_web_pass) - 1);
  if (qval(req, "o", o, sizeof(o)) && strlen(o) >= 4 && strlen(o) < sizeof(g_ota_pass))
    strncpy(g_ota_pass, o, sizeof(g_ota_pass) - 1);
  FILE *f = fopen("/sp/auth.cfg", "w");
  if (f) { fprintf(f, "%s\n%s\n%s\n", g_web_user, g_web_pass, g_ota_pass); fclose(f); }
  xSemaphoreGive(g_lock);
  httpd_resp_send(req, "ok", HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

static esp_err_t h_fetchnow(httpd_req_t *req) {
  if (!require_auth(req)) return ESP_OK;
  web_fetch_start();
  httpd_resp_send(req, "fetch started, refresh in ~30s", HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

// ---------- multipart streaming upload (blocklist + firmware) ----------
// Minimal streaming parser: finds the file part and streams its body.
typedef struct {
  char boundary[128];
  size_t blen;
  FILE *out;                       // blocklist destination (or NULL)
  esp_ota_handle_t ota;            // firmware destination (or 0)
  bool ota_active;
  bool file_part;                  // currently inside the file part
  bool done;
  uint8_t tail[160];
  size_t tail_len;
  size_t written;
} mp_t;

static void mp_emit(mp_t *m, const uint8_t *data, size_t len) {
  if (len == 0) return;
  if (m->out) fwrite(data, 1, len, m->out);
  else if (m->ota_active) esp_ota_write(m->ota, data, len);
  m->written += len;
}

// Streaming multipart/form-data parser for a single file part.
// Keeps an unprocessed tail so a boundary split across chunks is still found.
static esp_err_t mp_consume(mp_t *m, const uint8_t *data, size_t len) {
  size_t total = m->tail_len + len;
  if (total > sizeof(m->tail) + 2048) return ESP_FAIL;  // sanity
  uint8_t *buf = malloc(total);
  if (!buf) return ESP_FAIL;
  memcpy(buf, m->tail, m->tail_len);
  memcpy(buf + m->tail_len, data, len);

  char pat[136];
  snprintf(pat, sizeof(pat), "--%s", m->boundary);
  size_t plen = strlen(pat);

  size_t pos = 0;   // consumed offset within buf
  // A boundary at the very start has no leading CRLF.
  if (total >= plen && memcmp(buf, pat, plen) == 0) {
    // skip the boundary line
    size_t eol = plen;
    while (eol + 1 < total && !(buf[eol] == '\r' && buf[eol + 1] == '\n')) eol++;
    if (eol + 1 >= total) { free(buf); return ESP_FAIL; }
    pos = eol + 2;
  }

  while (1) {
    // find next "\r\n--boundary" at/after pos
    int at = -1;
    for (size_t i = pos; i + 2 + plen <= total; i++) {
      if (buf[i] == '\r' && buf[i + 1] == '\n' && memcmp(buf + i + 2, pat, plen) == 0) {
        at = (int)i; break;
      }
    }
    if (at < 0) {
      // no boundary in sight: emit all but a safe overlap, keep the rest
      size_t keep = 2 + plen + 8;
      if (total - pos <= keep) {
        memmove(m->tail, buf + pos, total - pos);
        m->tail_len = total - pos;
        free(buf);
        return ESP_OK;
      }
      size_t out_end = total - keep;
      if (m->written == 0) {
        // first emit: skip part headers (ends with \r\n\r\n)
        int hh = -1;
        for (size_t i = pos; i + 4 <= out_end; i++) {
          if (memcmp(buf + i, "\r\n\r\n", 4) == 0) { hh = (int)(i + 4); break; }
        }
        if (hh < 0) {
          // headers incomplete: hold everything
          memmove(m->tail, buf + pos, total - pos);
          m->tail_len = total - pos;
          free(buf);
          return ESP_OK;
        }
        mp_emit(m, buf + hh, out_end - hh);
      } else {
        mp_emit(m, buf + pos, out_end - pos);
      }
      memmove(m->tail, buf + out_end, total - out_end);
      m->tail_len = total - out_end;
      free(buf);
      return ESP_OK;
    }
    // boundary at `at`: emit [pos, at) as file data (skipping headers once)
    if (m->written == 0) {
      int hh = -1;
      for (size_t i = pos; i + 4 <= (size_t)at; i++) {
        if (memcmp(buf + i, "\r\n\r\n", 4) == 0) { hh = (int)(i + 4); break; }
      }
      if (hh < 0) { free(buf); return ESP_FAIL; }
      // only the part with filename= is our file; others are skipped
      bool is_file = false;
      for (int i = (int)pos; i + 9 <= hh; i++) {
        if (memcmp(buf + i, "filename=", 9) == 0) { is_file = true; break; }
      }
      if (!is_file) {
        // skip this field: resume scanning after the boundary line
        size_t eol = at + 2 + plen;
        while (eol + 1 < total && !(buf[eol] == '\r' && buf[eol + 1] == '\n')) eol++;
        pos = (eol + 1 < total) ? eol + 2 : total;
        continue;
      }
      if ((size_t)at > (size_t)hh) mp_emit(m, buf + hh, at - hh);
    } else {
      if ((size_t)at > pos) mp_emit(m, buf + pos, at - pos);
    }
    // final boundary ends with "--"
    size_t after = at + 2 + plen;
    if (after + 1 < total && buf[after] == '-' && buf[after + 1] == '-') {
      m->done = true;
      m->tail_len = 0;
      free(buf);
      return ESP_OK;
    }
    // not final (shouldn't happen for single-file forms): stop
    m->done = true;
    m->tail_len = 0;
    free(buf);
    return ESP_OK;
  }
}

static esp_err_t handle_upload_common(httpd_req_t *req, bool firmware) {
  if (!check_csrf(req) || !check_auth(req)) {
    if (!check_auth(req)) {
      httpd_resp_set_status(req, "401 Unauthorized");
      httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"c3adblock\"");
      httpd_resp_send(req, "auth required", HTTPD_RESP_USE_STRLEN);
    } else {
      httpd_resp_set_status(req, "403 Forbidden");
      httpd_resp_send(req, "missing CSRF header", HTTPD_RESP_USE_STRLEN);
    }
    return ESP_OK;
  }
  char ctype[160] = {0};
  if (httpd_req_get_hdr_value_str(req, "Content-Type", ctype, sizeof(ctype)) != ESP_OK ||
      strstr(ctype, "multipart/form-data") == NULL) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }
  const char *b = strstr(ctype, "boundary=");
  if (!b) { httpd_resp_send_500(req); return ESP_FAIL; }

  mp_t m; memset(&m, 0, sizeof(m));
  snprintf(m.boundary, sizeof(m.boundary), "%s", b + 9);
  m.boundary[strcspn(m.boundary, ";\r\n \"")] = 0;
  m.file_part = true;

  const esp_partition_t *upd = NULL;
  if (firmware) {
    upd = esp_ota_get_next_update_partition(NULL);
    if (!upd) { httpd_resp_send_500(req); return ESP_FAIL; }
    if (esp_ota_begin(upd, OTA_SIZE_UNKNOWN, &m.ota) != ESP_OK) {
      httpd_resp_send_500(req); return ESP_FAIL;
    }
    m.ota_active = true;
  } else {
    store_close_blocklist();
    remove("/sp/blocklist.new");
    m.out = fopen("/sp/blocklist.new", "wb");
    if (!m.out) { httpd_resp_send_500(req); return ESP_FAIL; }
  }

  char chunk[2048];
  size_t remaining = req->content_len;
  esp_err_t err = ESP_OK;
  while (remaining > 0) {
    int got = httpd_req_recv(req, chunk, remaining > sizeof(chunk) ? sizeof(chunk) : remaining);
    if (got <= 0) { err = ESP_FAIL; break; }
    remaining -= got;
    if (mp_consume(&m, (uint8_t *)chunk, got) != ESP_OK) { err = ESP_FAIL; break; }
    if (m.done) break;
  }
  // drain any unread body
  while (remaining > 0) {
    int got = httpd_req_recv(req, chunk, remaining > sizeof(chunk) ? sizeof(chunk) : remaining);
    if (got <= 0) break;
    remaining -= got;
  }

  if (firmware) {
    if (err == ESP_OK && m.written > 0 && esp_ota_end(m.ota) == ESP_OK &&
        esp_ota_set_boot_partition(upd) == ESP_OK) {
      ESP_LOGI(TAG, "firmware OTA ok (%u bytes), rebooting", (unsigned)m.written);
      httpd_resp_send(req, "ok, rebooting", HTTPD_RESP_USE_STRLEN);
      vTaskDelay(pdMS_TO_TICKS(300));
      esp_restart();
    } else {
      esp_ota_abort(m.ota);
      httpd_resp_set_status(req, "500 Internal Server Error");
      httpd_resp_send(req, "firmware update failed", HTTPD_RESP_USE_STRLEN);
    }
  } else {
    if (m.out) fclose(m.out);
    struct stat st;
    bool ok = err == ESP_OK && m.written > 0 &&
              stat("/sp/blocklist.new", &st) == 0 &&
              st.st_size > 0 && (st.st_size % HASH_BYTES) == 0;
    if (ok) {
      remove("/sp/blocklist.bin");
      rename("/sp/blocklist.new", "/sp/blocklist.bin");
    } else {
      remove("/sp/blocklist.new");
    }
    xSemaphoreTake(g_lock, portMAX_DELAY);
    store_open_blocklist();
    uint32_t n = g_num_hashes;
    xSemaphoreGive(g_lock);
    ESP_LOGI(TAG, "blocklist upload %s -> %lu domains", ok ? "OK" : "REJECTED", (unsigned long)n);
    if (ok) httpd_resp_send(req, "ok", HTTPD_RESP_USE_STRLEN);
    else {
      httpd_resp_set_status(req, "500 Internal Server Error");
      httpd_resp_send(req, "rejected: empty or size not a multiple of 5", HTTPD_RESP_USE_STRLEN);
    }
  }
  return ESP_OK;
}

static esp_err_t h_upload_bl(httpd_req_t *req) { return handle_upload_common(req, false); }
static esp_err_t h_upload_fw(httpd_req_t *req) { return handle_upload_common(req, true); }

// ---------- captive portal pages ----------

static esp_err_t h_portal(httpd_req_t *req) {
  // scanned network options
  char opts[2048] = {0};
  wifi_ap_record_t recs[15];
  uint16_t n = 15;
  if (esp_wifi_scan_start(NULL, true) == ESP_OK &&
      esp_wifi_scan_get_ap_records(&n, recs) == ESP_OK) {
    for (int i = 0; i < n; i++) {
      char esc[72], ssid[34] = {0};
      memcpy(ssid, recs[i].ssid, 32);
      html_escape(ssid, esc, sizeof(esc));
      char o[96]; snprintf(o, sizeof(o), "<option value='%s'>", esc);
      if (strlen(opts) + strlen(o) < sizeof(opts) - 1) strcat(opts, o);
    }
  }
  static const char P1[] =
    "<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>C3 AdBlock setup</title>"
    "<body style='font:16px system-ui,sans-serif;max-width:420px;margin:36px auto;padding:0 16px;background:#0d1117;color:#c9d1d9'>"
    "<h2>&#128737; C3 AdBlock &mdash; WiFi setup</h2>"
    "<p style='color:#8b949e'>Pick your network and enter its password. The device restarts and joins it.</p>"
    "<form method=POST action=/wifisave>"
    "<input list=nets name=s placeholder='WiFi name' required style='width:100%;box-sizing:border-box;padding:11px;margin:6px 0;border-radius:6px;border:1px solid #30363d;background:#161b22;color:#c9d1d9'>"
    "<datalist id=nets>";
  static const char P2[] =
    "</datalist>"
    "<input name=p type=password placeholder='Password' style='width:100%;box-sizing:border-box;padding:11px;margin:6px 0;border-radius:6px;border:1px solid #30363d;background:#161b22;color:#c9d1d9'>"
    "<button style='width:100%;padding:12px;margin-top:8px;border-radius:6px;border:0;background:#3fb950;color:#000;font-weight:600;cursor:pointer'>Connect</button>"
    "</form></body>";
  httpd_resp_set_type(req, "text/html");
  httpd_resp_send_chunk(req, P1, HTTPD_RESP_USE_STRLEN);
  if (strlen(opts)) httpd_resp_send_chunk(req, opts, HTTPD_RESP_USE_STRLEN);
  httpd_resp_send_chunk(req, P2, HTTPD_RESP_USE_STRLEN);
  httpd_resp_send_chunk(req, NULL, 0);
  return ESP_OK;
}

static esp_err_t h_404(httpd_req_t *req, httpd_err_code_t err) {
  (void)err;
  return h_portal(req);   // captive portal: everything shows the setup form
}

static esp_err_t h_wifisave(httpd_req_t *req) {
  char body[512] = {0};
  size_t remaining = req->content_len > sizeof(body) - 1 ? sizeof(body) - 1 : req->content_len;
  size_t got = 0;
  while (got < remaining) {
    int r = httpd_req_recv(req, body + got, remaining - got);
    if (r <= 0) break;
    got += r;
  }
  body[got] = 0;
  char ss[64] = {0}, pw[64] = {0};
  // parse s=..&p=..
  char *s = strstr(body, "s=");
  char *p = strstr(body, "p=");
  if (s) { strncpy(ss, s + 2, sizeof(ss) - 1); ss[strcspn(ss, "&")] = 0; urldecode(ss); }
  if (p) { strncpy(pw, p + 2, sizeof(pw) - 1); pw[strcspn(pw, "&")] = 0; urldecode(pw); }
  if (!strlen(ss)) { httpd_resp_send_500(req); return ESP_FAIL; }
  FILE *f = fopen("/sp/wifi.cfg", "w");
  if (f) { fprintf(f, "%s\n%s\n", ss, pw); fclose(f); }
  char esc[96]; html_escape(ss, esc, sizeof(esc));
  char resp[512];
  snprintf(resp, sizeof(resp),
      "<!doctype html><meta charset=utf-8><body style='font:16px system-ui;text-align:center;margin-top:60px'>"
      "&#9989; Saved. Restarting and joining <b>%s</b>&hellip;<br><br>"
      "Reconnect your phone to your normal WiFi, then open the device dashboard at its new IP "
      "(see your router's client list, hostname <b>c3adblock</b>).</body>",
      esc);
  httpd_resp_set_type(req, "text/html");
  httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
  vTaskDelay(pdMS_TO_TICKS(900));
  esp_restart();
  return ESP_OK;
}

// ---------- remote blocklist fetch (background task) ----------
static bool fetch_url_to(const char *url, const char *tmp_path, int depth) {
  if (depth > 5) return false;
  bool is_https = strncasecmp(url, "https://", 8) == 0;
  esp_http_client_config_t ccfg;
  memset(&ccfg, 0, sizeof(ccfg));
  ccfg.url = url;
  ccfg.timeout_ms = 20000;
  ccfg.buffer_size = 2048;
  if (is_https) ccfg.crt_bundle_attach = esp_crt_bundle_attach;
  esp_http_client_handle_t c = esp_http_client_init(&ccfg);
  if (!c) return false;
  bool ok = false;
  if (esp_http_client_open(c, 0) == ESP_OK) {
    int code = esp_http_client_fetch_headers(c);
    ESP_LOGI(TAG, "fetch %s -> %d", url, code);
    if (code == 200) {
      FILE *f = fopen(tmp_path, "wb");
      if (f) {
        char b[1024]; size_t total = 0;
        while (1) {
          int n = esp_http_client_read(c, b, sizeof(b));
          if (n < 0) break;
          if (n == 0) { ok = true; break; }
          if (fwrite(b, 1, n, f) != (size_t)n) break;
          total += n;
        }
        fclose(f);
        if (!ok) remove(tmp_path);
        else ESP_LOGI(TAG, "fetched %u bytes", (unsigned)total);
      }
    } else if (code == 301 || code == 302 || code == 303 || code == 307 || code == 308) {
      char *loc = NULL;
      char redir[512] = {0};
      if (esp_http_client_get_header(c, "Location", &loc) >= 0 && loc) {
        strncpy(redir, loc, sizeof(redir) - 1);
      }
      esp_http_client_close(c);
      esp_http_client_cleanup(c);
      if (strlen(redir)) {
        ESP_LOGI(TAG, "redirect -> %s", redir);
        return fetch_url_to(redir, tmp_path, depth + 1);
      }
      return false;
    }
    esp_http_client_close(c);
  } else {
    ESP_LOGW(TAG, "fetch open failed: %s", url);
  }
  esp_http_client_cleanup(c);
  return ok;
}

void web_fetch_task(void *arg) {
  (void)arg;
  char url[256];
  xSemaphoreTake(g_lock, portMAX_DELAY);
  strncpy(url, g_update_url, sizeof(url) - 1);
  xSemaphoreGive(g_lock);
  if (!strlen(url)) {
    xSemaphoreTake(g_lock, portMAX_DELAY);
    strncpy(g_update_status, "no url set", sizeof(g_update_status) - 1);
    xSemaphoreGive(g_lock);
    main_fetch_done();
    vTaskDelete(NULL);
  }
  ESP_LOGI(TAG, "remote fetch: %s", url);
  bool ok = fetch_url_to(url, "/sp/blocklist.new", 0);
  xSemaphoreTake(g_lock, portMAX_DELAY);
  if (ok) {
    struct stat st;
    if (stat("/sp/blocklist.new", &st) == 0 && st.st_size > 0 &&
        (st.st_size % HASH_BYTES) == 0) {
      remove("/sp/blocklist.bin");
      rename("/sp/blocklist.new", "/sp/blocklist.bin");
      store_open_blocklist();
      snprintf(g_update_status, sizeof(g_update_status), "ok: %lu domains", (unsigned long)g_num_hashes);
    } else {
      remove("/sp/blocklist.new");
      store_open_blocklist();
      snprintf(g_update_status, sizeof(g_update_status), "bad data");
    }
  } else {
    snprintf(g_update_status, sizeof(g_update_status), "fetch failed");
  }
  ESP_LOGI(TAG, "fetch result: %s", g_update_status);
  xSemaphoreGive(g_lock);
  main_fetch_done();
  vTaskDelete(NULL);
}

void web_fetch_start(void) {
  xTaskCreate(web_fetch_task, "fetch", 6144, NULL, 5, NULL);
}

// ---------- server setup ----------

void web_start(bool portal_mode) {
  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.max_uri_handlers = 24;
  cfg.stack_size = 6144;
  if (httpd_start(&s_server, &cfg) != ESP_OK) {
    ESP_LOGE(TAG, "httpd_start failed");
    return;
  }
  if (portal_mode) {
    httpd_uri_t r = {.uri = "/", .method = HTTP_GET, .handler = h_portal};
    httpd_register_uri_handler(s_server, &r);
    r.uri = "/wifisave"; r.method = HTTP_POST; r.handler = h_wifisave;
    httpd_register_uri_handler(s_server, &r);
    httpd_register_err_handler(s_server, HTTPD_404_NOT_FOUND, h_404);
    ESP_LOGI(TAG, "portal web on :80");
    return;
  }
  static const struct { const char *uri; int method; esp_err_t (*h)(httpd_req_t *); } routes[] = {
    {"/", HTTP_GET, h_root},
    {"/stats.json", HTTP_GET, h_stats},
    {"/check", HTTP_GET, h_check},
    {"/ban", HTTP_GET, h_ban},
    {"/addblock", HTTP_GET, h_addblock},
    {"/unblock", HTTP_GET, h_unblock},
    {"/addallow", HTTP_GET, h_addallow},
    {"/removeallow", HTTP_GET, h_removeallow},
    {"/pause", HTTP_GET, h_pause},
    {"/resume", HTTP_GET, h_resume},
    {"/forgetwifi", HTTP_GET, h_forgetwifi},
    {"/setupdate", HTTP_GET, h_setupdate},
    {"/setauth", HTTP_GET, h_setauth},
    {"/fetchnow", HTTP_GET, h_fetchnow},
    {"/upload", HTTP_POST, h_upload_bl},
    {"/update", HTTP_POST, h_upload_fw},
  };
  for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
    httpd_uri_t r = {.uri = routes[i].uri, .method = routes[i].method, .handler = routes[i].h};
    httpd_register_uri_handler(s_server, &r);
  }
  ESP_LOGI(TAG, "dashboard web on :80");
}
