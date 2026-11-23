/*
 * Art Swap helper - whether PlayStation Store logos are on, and the title's own choices.
 *
 * Copyright (C) 2026 Gary Riches
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "artswap.h"

#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

/* ---- PlayStation Store logos ----
 * Off unless the person turns them on: Sony's public catalog needs no account and is asked as a web
 * browser would ask it (http_get_plain), but it still sees the console's address and which game. */

bool config_store_logos(void)
{
    char path[512];
    return file_exists(data_path("store-logos", path, sizeof(path)));
}

bool config_set_store_logos(bool on)
{
    char path[512];
    data_path("store-logos", path, sizeof(path));
    if (!on)
        return remove(path) == 0 || !file_exists(path);
    return file_write_replace(path, "on\n", 3, NULL);
}

/* ---- the title's own choices ---- */

static bool pref_key_ok(const char *key)
{
    const size_t len = strlen(key);
    for (size_t i = 0; i < len; i++)
        if (!isalnum((unsigned char)key[i]))
            return false;
    return len > 0 && len < 32;
}

bool config_pref(const char *key, char *value, size_t size)
{
    char path[512];
    buf_t text = {0};
    bool found = false;
    if (pref_key_ok(key) && file_read(data_path("prefs.txt", path, sizeof(path)), &text) && text.len > 0) {
        const size_t len = strlen(key);
        char *save = NULL;
        for (char *line = strtok_r((char *)text.data, "\n", &save); line && !found;
             line = strtok_r(NULL, "\n", &save))
            if (strncmp(line, key, len) == 0 && line[len] == '\t') {
                copy_str(value, size, line + len + 1);
                found = true;
            }
    }
    buf_free(&text);
    return found;
}

bool config_set_pref(const char *key, const char *value)
{
    if (!pref_key_ok(key) || strlen(value) > 63)
        return false;
    for (const char *p = value; *p; p++)
        if ((unsigned char)*p < 0x20)
            return false;
    char path[512];
    buf_t text = {0}, out = {0};
    file_read(data_path("prefs.txt", path, sizeof(path)), &text);
    const size_t len = strlen(key);
    char *save = NULL;
    if (text.len > 0)
        for (char *line = strtok_r((char *)text.data, "\n", &save); line; line = strtok_r(NULL, "\n", &save))
            if (!(strncmp(line, key, len) == 0 && line[len] == '\t'))
                buf_printf(&out, "%s\n", line);
    buf_printf(&out, "%s\t%s\n", key, value);
    const bool ok = file_write_replace(path, out.data, out.len, NULL);
    buf_free(&text);
    buf_free(&out);
    return ok;
}
