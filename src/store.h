#pragma once
#include <stdint.h>
#include <stdbool.h>

bool store_open_blocklist(void);
void store_close_blocklist(void);
bool store_is_blocked(const char *domain);
bool store_is_allowed(const char *domain);
void store_load_lists(void);
bool store_add_custom(const char *d);
void store_del_custom(const char *d);
bool store_add_allow(const char *d);
void store_del_allow(const char *d);
void store_save_banned(void);
void store_log_block(const char *domain, uint32_t ip);
