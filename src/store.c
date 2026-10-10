// Blocklist + custom/allow/ban storage + query log.
// Blocklist file (/sp/blocklist.bin) = sorted 5-byte FNV-1a hashes, read via
// stdio pread-style seeks. A 4096-sample RAM index + 512-slot LRU cache keep
// lookups to a couple dozen flash reads.
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include "esp_log.h"
#include "state.h"
#include "store.h"

static const char *TAG = "store";

static uint8_t s_index[INDEX_ENTRIES][HASH_BYTES];
static uint8_t s_cache_key[CACHE_SIZE][HASH_BYTES];
static uint8_t s_cache_res[CACHE_SIZE];
static uint8_t s_cache_valid[CACHE_SIZE];
static uint8_t s_range[MAX_RANGE * HASH_BYTES];
static FILE *s_bl = NULL;

static uint64_t fnv40(const char *s, size_t n) {
  uint64_t h = 0xcbf29ce484222325ULL;
  for (size_t i = 0; i < n; i++) { h ^= (uint8_t)s[i]; h *= 0x100000001b3ULL; }
  return h & 0xFFFFFFFFFFULL;
}
static uint64_t unpack(const uint8_t *b) {
  uint64_t v = 0;
  for (int k = 0; k < HASH_BYTES; k++) v |= (uint64_t)b[k] << (8 * k);
  return v;
}
static void pack(uint64_t h, uint8_t *b) {
  for (int k = 0; k < HASH_BYTES; k++) { b[k] = (uint8_t)h; h >>= 8; }
}

static bool bl_seek_read(uint32_t pos, uint8_t *out, uint32_t len) {
  if (!s_bl) return false;
  if (fseek(s_bl, (long)pos * HASH_BYTES, SEEK_SET) != 0) return false;
  return fread(out, 1, len, s_bl) == len;
}

void store_close_blocklist(void) {
  if (s_bl) { fclose(s_bl); s_bl = NULL; }
  g_num_hashes = 0;
  memset(s_cache_valid, 0, sizeof(s_cache_valid));
}

bool store_open_blocklist(void) {
  store_close_blocklist();
  struct stat st;
  if (stat("/sp/blocklist.bin", &st) != 0 || st.st_size == 0 ||
      (st.st_size % HASH_BYTES) != 0) {
    ESP_LOGW(TAG, "no valid /sp/blocklist.bin (fail-open, no blocking)");
    return false;
  }
  s_bl = fopen("/sp/blocklist.bin", "rb");
  if (!s_bl) return false;
  g_num_hashes = (uint32_t)(st.st_size / HASH_BYTES);
  for (int i = 0; i < INDEX_ENTRIES; i++) {
    uint32_t pos = (uint32_t)((uint64_t)i * (g_num_hashes - 1) / (INDEX_ENTRIES - 1));
    if (!bl_seek_read(pos, s_index[i], HASH_BYTES)) { store_close_blocklist(); return false; }
  }
  ESP_LOGI(TAG, "blocklist: %lu hashes indexed", (unsigned long)g_num_hashes);
  return true;
}

static bool in_flash(uint64_t h) {
  if (g_num_hashes == 0) return false;
  uint64_t first = unpack(s_index[0]);
  uint64_t last = unpack(s_index[INDEX_ENTRIES - 1]);
  if (h < first || h > last) return false;
  int lo = 0, hi = INDEX_ENTRIES - 2, seg = 0;
  while (lo <= hi) {
    int mid = (lo + hi) >> 1;
    uint64_t midv = unpack(s_index[mid]);
    if (midv < h) { seg = mid; lo = mid + 1; }
    else if (midv > h) { hi = mid - 1; }
    else return true;
  }
  uint32_t start = (uint32_t)((uint64_t)seg * (g_num_hashes - 1) / (INDEX_ENTRIES - 1));
  uint32_t end = (uint32_t)((uint64_t)(seg + 1) * (g_num_hashes - 1) / (INDEX_ENTRIES - 1));
  if (end >= g_num_hashes) end = g_num_hashes - 1;
  uint32_t n = end - start + 1;
  if (n > (uint32_t)MAX_RANGE) n = MAX_RANGE;
  if (!bl_seek_read(start, s_range, n * HASH_BYTES)) return false;
  for (uint32_t i = 0; i < n; i++) {
    uint64_t v = unpack(s_range + i * HASH_BYTES);
    if (v == h) return true;
    if (v > h) break;
  }
  return false;
}

static bool hash_in_list(uint64_t h, uint64_t *list, int n) {
  for (int i = 0; i < n; i++) if (list[i] == h) return true;
  return false;
}

// walks domain + parent suffixes (stops before the bare TLD), same as before
static bool suffix_match(const char *domain, uint64_t *list, int n) {
  const char *p = domain;
  while (p && *p) {
    if (hash_in_list(fnv40(p, strlen(p)), list, n)) return true;
    const char *dot = strchr(p, '.'); if (!dot) break;
    const char *next = dot + 1; if (!strchr(next, '.')) break; p = next;
  }
  return false;
}

bool store_is_blocked(const char *domain) {
  if (!domain || !*domain || g_num_hashes == 0) return false;
  const char *p = domain;
  while (p && *p) {
    uint64_t h = fnv40(p, strlen(p));
    bool hit = hash_in_list(h, g_custom_hash, g_num_custom);
    if (!hit) {
      uint32_t slot = (uint32_t)(h & (CACHE_SIZE - 1));
      uint8_t want[HASH_BYTES]; pack(h, want);
      if (s_cache_valid[slot] && memcmp(s_cache_key[slot], want, HASH_BYTES) == 0) {
        hit = s_cache_res[slot] != 0;
      } else {
        hit = in_flash(h);
        s_cache_valid[slot] = 1;
        s_cache_res[slot] = hit ? 1 : 0;
        memcpy(s_cache_key[slot], want, HASH_BYTES);
      }
    }
    if (hit) return true;
    const char *dot = strchr(p, '.'); if (!dot) break;
    const char *next = dot + 1; if (!strchr(next, '.')) break; p = next;
  }
  return false;
}

bool store_is_allowed(const char *domain) {
  if (!domain || !*domain || g_num_allow == 0) return false;
  return suffix_match(domain, g_allow_hash, g_num_allow);
}

static void normdom(const char *src, char *dst, size_t n) {
  size_t i = 0;
  for (const char *p = src; *p && i + 1 < n; p++) {
    char c = *p;
    if (c >= 'A' && c <= 'Z') c += 32;
    dst[i++] = c;
  }
  dst[i] = 0;
  if (strncmp(dst, "www.", 4) == 0) memmove(dst, dst + 4, strlen(dst + 4) + 1);
}

static bool valid_dom(const char *d) {
  size_t n = strlen(d);
  return n > 2 && n < 60 && strchr(d, '.') != NULL && strchr(d, ' ') == NULL;
}

static void load_list(const char *path, char doms[][64], uint64_t *hashes, int *count, int max) {
  *count = 0;
  FILE *f = fopen(path, "r");
  if (!f) return;
  char line[96];
  while (fgets(line, sizeof(line), f) && *count < max) {
    char d[64]; normdom(line, d, sizeof(d));
    // strip trailing newline/CR leftovers already handled by normdom? trim:
    d[strcspn(d, "\r\n")] = 0;
    if (!valid_dom(d)) continue;
    bool dup = false;
    for (int i = 0; i < *count; i++) if (strcmp(doms[i], d) == 0) { dup = true; break; }
    if (dup) continue;
    strncpy(doms[*count], d, 64);
    hashes[*count] = fnv40(d, strlen(d));
    (*count)++;
  }
  fclose(f);
}

static void save_list(const char *path, char doms[][64], int count) {
  FILE *f = fopen(path, "w");
  if (!f) return;
  for (int i = 0; i < count; i++) fprintf(f, "%s\n", doms[i]);
  fclose(f);
}

void store_load_lists(void) {
  load_list("/sp/custom.txt", g_custom_dom, g_custom_hash, &g_num_custom, MAX_CUSTOM);
  load_list("/sp/allow.txt", g_allow_dom, g_allow_hash, &g_num_allow, MAX_ALLOW);
  // banned IPs
  g_num_banned = 0;
  FILE *f = fopen("/sp/banned.txt", "r");
  if (f) {
    char line[32];
    while (fgets(line, sizeof(line), f) && g_num_banned < MAX_BAN) {
      unsigned a, b, c, d;
      if (sscanf(line, "%u.%u.%u.%u", &a, &b, &c, &d) == 4 && a < 256 && b < 256 && c < 256 && d < 256) {
        // store network byte order to match recvfrom() addresses
        g_banned_ip[g_num_banned++] =
            ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)c << 8) | d;
      }
    }
    fclose(f);
  }
  ESP_LOGI(TAG, "custom=%d allow=%d banned=%d", g_num_custom, g_num_allow, g_num_banned);
}

bool store_add_custom(const char *d) {
  char n[64]; normdom(d, n, sizeof(n));
  if (!valid_dom(n) || g_num_custom >= MAX_CUSTOM) return false;
  for (int i = 0; i < g_num_custom; i++) if (strcmp(g_custom_dom[i], n) == 0) return false;
  strncpy(g_custom_dom[g_num_custom], n, 64);
  g_custom_hash[g_num_custom] = fnv40(n, strlen(n));
  g_num_custom++;
  save_list("/sp/custom.txt", g_custom_dom, g_num_custom);
  return true;
}

void store_del_custom(const char *d) {
  char n[64]; normdom(d, n, sizeof(n));
  for (int i = 0; i < g_num_custom; i++) if (strcmp(g_custom_dom[i], n) == 0) {
    for (int j = i; j < g_num_custom - 1; j++) {
      strcpy(g_custom_dom[j], g_custom_dom[j + 1]);
      g_custom_hash[j] = g_custom_hash[j + 1];
    }
    g_num_custom--;
    save_list("/sp/custom.txt", g_custom_dom, g_num_custom);
    return;
  }
}

bool store_add_allow(const char *d) {
  char n[64]; normdom(d, n, sizeof(n));
  if (!valid_dom(n) || g_num_allow >= MAX_ALLOW) return false;
  for (int i = 0; i < g_num_allow; i++) if (strcmp(g_allow_dom[i], n) == 0) return false;
  strncpy(g_allow_dom[g_num_allow], n, 64);
  g_allow_hash[g_num_allow] = fnv40(n, strlen(n));
  g_num_allow++;
  save_list("/sp/allow.txt", g_allow_dom, g_num_allow);
  return true;
}

void store_del_allow(const char *d) {
  char n[64]; normdom(d, n, sizeof(n));
  for (int i = 0; i < g_num_allow; i++) if (strcmp(g_allow_dom[i], n) == 0) {
    for (int j = i; j < g_num_allow - 1; j++) {
      strcpy(g_allow_dom[j], g_allow_dom[j + 1]);
      g_allow_hash[j] = g_allow_hash[j + 1];
    }
    g_num_allow--;
    save_list("/sp/allow.txt", g_allow_dom, g_num_allow);
    return;
  }
}

void store_save_banned(void) {
  FILE *f = fopen("/sp/banned.txt", "w");
  if (!f) return;
  for (int i = 0; i < g_num_banned; i++) {
    uint32_t ip = g_banned_ip[i];
    fprintf(f, "%u.%u.%u.%u\n",
            (unsigned)((ip >> 24) & 0xFF), (unsigned)((ip >> 16) & 0xFF),
            (unsigned)((ip >> 8) & 0xFF), (unsigned)(ip & 0xFF));
  }
  fclose(f);
}

void store_log_block(const char *domain, uint32_t ip) {
  logent_t *e = &g_log[g_log_head];
  strncpy(e->domain, domain, sizeof(e->domain) - 1);
  e->domain[sizeof(e->domain) - 1] = 0;
  e->ip = ip;
  e->age_ms = now_ms();
  g_log_head = (g_log_head + 1) % LOG_N;
  if (g_log_count < LOG_N) g_log_count++;
}
