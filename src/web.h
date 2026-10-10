#pragma once
#include "esp_http_server.h"

void web_start(bool portal_mode);
void web_fetch_task(void *arg);   // on-demand remote blocklist fetch
void web_fetch_start(void);       // spawn fetch task (from handler)
void main_fetch_done(void);       // implemented in main.c: clears fetch guard
