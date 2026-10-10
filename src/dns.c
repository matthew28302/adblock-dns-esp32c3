// DNS sinkhole task: parse queries, match blocklist, sinkhole or forward.
// Same behavior as the Arduino version: fail-open when the list is missing,
// custom allow beats blocks, banned clients get 0.0.0.0 for everything.
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include "esp_log.h"
#include "esp_random.h"
#include "lwip/etharp.h"
#include "lwip/netif.h"
#include "state.h"
#include "store.h"

static const char *TAG = "dns";

static const char *UPSTREAM1 = "1.1.1.1";
static const char *UPSTREAM2 = "8.8.8.8";
static const int UPSTREAM_PORT = 53;

static uint8_t s_buf[1536];

static size_t parse_query(const uint8_t *pkt, int len, char *out,
                          uint16_t *qtype, int *qend) {
  if (len < 13) return 0;
  int i = 12; size_t o = 0;
  while (i < len) {
    uint8_t l = pkt[i++];
    if (l == 0) break;
    if (l & 0xC0) return 0;
    if (o + l + 1 >= 250 || i + l > len) return 0;
    if (o) out[o++] = '.';
    for (uint8_t k = 0; k < l; k++) {
      char c = pkt[i++];
      if (c >= 'A' && c <= 'Z') c += 32;
      out[o++] = c;
    }
  }
  out[o] = 0;
  if (i + 4 > len) return 0;
  *qtype = (uint16_t)((pkt[i] << 8) | pkt[i + 1]);
  *qend = i + 4;
  if (o > 4 && strncmp(out, "www.", 4) == 0) {
    memmove(out, out + 4, o - 3); o -= 4;
  }
  return o;
}

static int build_blocked(int qend, uint16_t qtype) {
  s_buf[2] = 0x81; s_buf[3] = 0x80;
  s_buf[6] = 0; s_buf[7] = (qtype == 1) ? 1 : 0;
  s_buf[8] = 0; s_buf[9] = 0; s_buf[10] = 0; s_buf[11] = 0;
  if (qtype != 1) return qend;
  static const uint8_t ans[] = {0xC0,0x0C, 0,1, 0,1, 0,0,1,0x2C, 0,4, 0,0,0,0};
  memcpy(s_buf + qend, ans, sizeof(ans));
  return qend + sizeof(ans);
}

static void get_mac(uint32_t ip_net_order, uint8_t *mac) {
  memset(mac, 0, 6);
  ip4_addr_t ipa; ipa.addr = ip_net_order;
  struct eth_addr *eth = NULL;
  const ip4_addr_t *ipret = NULL;
  for (struct netif *nif = netif_list; nif; nif = nif->next) {
    if (etharp_find_addr(nif, &ipa, &eth, &ipret) >= 0 && eth) {
      memcpy(mac, eth->addr, 6);
      return;
    }
  }
}

static client_t *get_client(uint32_t ip) {
  for (int i = 0; i < g_num_clients; i++)
    if (g_clients[i].ip == ip) { g_clients[i].last_seen_ms = now_ms(); return &g_clients[i]; }
  if (g_num_clients < MAX_CLIENTS) {
    client_t *c = &g_clients[g_num_clients++];
    c->ip = ip; c->blocked = c->allowed = 0;
    c->last_seen_ms = now_ms(); c->banned = false;
    for (int i = 0; i < g_num_banned; i++)
      if (g_banned_ip[i] == ip) { c->banned = true; break; }
    get_mac(ip, c->mac);
    return c;
  }
  return NULL;
}

static bool is_banned(uint32_t ip) {
  for (int i = 0; i < g_num_banned; i++) if (g_banned_ip[i] == ip) return true;
  return false;
}

// forward with fresh txid; accept only the matching reply; fallback resolver
static int forward_upstream(int up_sock, int qlen, int qend) {
  // drain stale datagrams
  uint8_t tmp[64];
  int fl = fcntl(up_sock, F_GETFL, 0);
  fcntl(up_sock, F_SETFL, fl | O_NONBLOCK);
  while (recv(up_sock, tmp, sizeof(tmp), 0) > 0) {}
  fcntl(up_sock, F_SETFL, fl);

  uint8_t cid0 = s_buf[0], cid1 = s_buf[1];
  uint16_t wid = (uint16_t)esp_random();
  uint8_t q[260]; int ql = qend - 12;
  bool have_q = ql > 0 && ql <= (int)sizeof(q) && qend <= qlen;
  if (have_q) memcpy(q, s_buf + 12, ql);
  s_buf[0] = wid >> 8; s_buf[1] = wid & 0xFF;

  const char *hosts[2] = { UPSTREAM1, UPSTREAM2 };
  for (int attempt = 0; attempt < 2; attempt++) {
    struct sockaddr_in up;
    memset(&up, 0, sizeof(up));
    up.sin_family = AF_INET;
    up.sin_port = htons(UPSTREAM_PORT);
    inet_pton(AF_INET, hosts[attempt], &up.sin_addr);
    sendto(up_sock, s_buf, qlen, 0, (struct sockaddr *)&up, sizeof(up));

    struct timeval tv = { .tv_sec = 0, .tv_usec = 500 * 1000 };
    setsockopt(up_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    uint32_t t0 = now_ms();
    while (now_ms() - t0 < 500) {
      struct sockaddr_in from; socklen_t flen = sizeof(from);
      int n = recvfrom(up_sock, s_buf, sizeof(s_buf), 0,
                       (struct sockaddr *)&from, &flen);
      if (n < 12) continue;
      if (from.sin_addr.s_addr != up.sin_addr.s_addr) continue;
      if (s_buf[0] != (wid >> 8) || s_buf[1] != (wid & 0xFF)) continue;
      if (have_q && (n < 12 + ql || memcmp(s_buf + 12, q, ql) != 0)) continue;
      s_buf[0] = cid0; s_buf[1] = cid1;
      return n;
    }
  }
  return 0;
}

void dns_task(void *arg) {
  (void)arg;
  int srv = socket(AF_INET, SOCK_DGRAM, 0);
  int up = socket(AF_INET, SOCK_DGRAM, 0);
  if (srv < 0 || up < 0) { ESP_LOGE(TAG, "socket failed"); vTaskDelete(NULL); }

  struct sockaddr_in me;
  memset(&me, 0, sizeof(me));
  me.sin_family = AF_INET;
  me.sin_port = htons(53);
  me.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bind(srv, (struct sockaddr *)&me, sizeof(me)) < 0) {
    ESP_LOGE(TAG, "bind :53 failed"); vTaskDelete(NULL);
  }
  ESP_LOGI(TAG, "DNS sinkhole on :53");

  uint32_t last_slow_ms = 0;
  while (1) {
    fd_set rfds; FD_ZERO(&rfds); FD_SET(srv, &rfds);
    struct timeval tv = { .tv_sec = 0, .tv_usec = 100 * 1000 };
    int r = select(srv + 1, &rfds, NULL, NULL, &tv);

    // auto-resume + (slow tick uses last_slow_ms below)
    xSemaphoreTake(g_lock, portMAX_DELAY);
    if (!g_blocking_on && g_resume_at_ms &&
        (int32_t)(now_ms() - g_resume_at_ms) >= 0) {
      g_blocking_on = true; g_resume_at_ms = 0;
      ESP_LOGI(TAG, "blocking auto-resumed");
    }
    bool portal = g_portal_mode;
    uint32_t ap_ip = g_ap_ip;
    xSemaphoreGive(g_lock);

    if (r > 0) {
      for (int budget = 0; budget < 16; budget++) {
        struct sockaddr_in cli; socklen_t clen = sizeof(cli);
        int qlen = recvfrom(srv, s_buf, sizeof(s_buf), MSG_DONTWAIT,
                            (struct sockaddr *)&cli, &clen);
        if (qlen < 0) break;
        if (qlen < 13) continue;
        uint32_t cip = cli.sin_addr.s_addr;   // network byte order

        char domain[256]; uint16_t qtype = 0; int qend = qlen;
        size_t dl = parse_query(s_buf, qlen, domain, &qtype, &qend);

        xSemaphoreTake(g_lock, portMAX_DELAY);
        client_t *c = get_client(cip);
        bool ban = (c && c->banned) || is_banned(cip);
        bool blocked;
        int rlen;
        if (portal) {
          // captive portal: every A query -> the AP itself
          blocked = false;
          rlen = build_blocked(qend, qtype);
          if (qtype == 1 && dl) {
            // rewrite answer IP to AP
            rlen = qend + 16;
            static const uint8_t hdr[] = {0xC0,0x0C, 0,1, 0,1, 0,0,0,10, 0,4};
            s_buf[2] = 0x81; s_buf[3] = 0x80;
            s_buf[6] = 0; s_buf[7] = 1; s_buf[8] = 0; s_buf[9] = 0;
            s_buf[10] = 0; s_buf[11] = 0;
            memcpy(s_buf + qend, hdr, sizeof(hdr));
            memcpy(s_buf + qend + 12, &ap_ip, 4);
          } else {
            s_buf[2] = 0x81; s_buf[3] = 0x80;
            s_buf[6] = 0; s_buf[7] = 0; s_buf[8] = 0; s_buf[9] = 0;
            s_buf[10] = 0; s_buf[11] = 0;
            rlen = qend;
          }
          g_total_allowed++; if (c) c->allowed++;
        } else {
          blocked = ban || (g_blocking_on && dl && !store_is_allowed(domain) &&
                            g_num_hashes && store_is_blocked(domain));
          if (blocked) {
            rlen = build_blocked(qend, qtype);
            g_total_blocked++; if (c) c->blocked++;
            if (dl) store_log_block(domain, cip);
          } else {
            xSemaphoreGive(g_lock);
            rlen = forward_upstream(up, qlen, qend);
            xSemaphoreTake(g_lock, portMAX_DELAY);
            g_total_allowed++; if (c) { /* re-find under lock */ 
              for (int i = 0; i < g_num_clients; i++)
                if (g_clients[i].ip == cip) { g_clients[i].allowed++; break; }
            }
          }
        }
        xSemaphoreGive(g_lock);
        if (rlen > 0)
          sendto(srv, s_buf, rlen, 0, (struct sockaddr *)&cli, clen);
      }
    }

    // slow tick: nothing periodic needed inside DNS (updater is its own task)
    (void)last_slow_ms;
  }
}
