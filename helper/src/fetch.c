/*
 * Art Swap helper - fetching pictures.
 *
 * A picture is either one of the person's own (named mine:<name>, read from
 * /data/artswap/pictures: pictures.c, never cached) or a PlayStation Store
 * logo from Sony's image server (store.c), which is only fetched when the
 * person has turned Store logos on, and is kept in /data/artswap/cache
 * (trimmed at start-up). Nothing else is ever downloaded.
 *
 * Copyright (C) 2026 Gary Riches
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "artswap.h"
#include "pictures.h"
#include "store.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* ---- pictures, through the cache ---- */

bool net_fetch(const char *url, buf_t *out, char *err, size_t errlen)
{
    out->len = 0;
    /* one of the person's own pictures */
    if (strncmp(url, PICTURE_SCHEME, strlen(PICTURE_SCHEME)) == 0)
        return pictures_read(url + strlen(PICTURE_SCHEME), out, err, errlen);
    if (!store_picture(url)) {
        snprintf(err, errlen, "only your own pictures and PlayStation Store logos are used");
        return false;
    }
    if (!config_store_logos()) {
        snprintf(err, errlen, "PlayStation Store logos are off: turn them on in Settings, Games");
        return false;
    }
    char name[64], path[512];
    snprintf(name, sizeof(name), "cache/%016llx.bin", (unsigned long long)fnv1a64(url));
    data_path(name, path, sizeof(path));
    if (file_read(path, out) && out->len > 0)
        return true;
    int status = 0;
    if (!http_get_plain(url, out, &status, err, errlen))
        return false;
    if (status != 200 || out->len == 0) {
        snprintf(err, errlen, "the picture did not download (%d)", status);
        return false;
    }
    if (!file_write_replace(path, out->data, out->len, NULL))
        log_line("could not keep %s in the cache", url);
    return true;
}

/* Oldest first, down to 192 MiB, when the cache is over 256 MiB */
void net_cache_trim(void)
{
    char folder[512];
    data_path("cache", folder, sizeof(folder));
    DIR *dir = opendir(folder);
    if (dir == NULL)
        return;
    typedef struct {
        char name[64];
        time_t when;
        off_t size;
    } entry_t;
    entry_t *entries = NULL;
    size_t count = 0, cap = 0;
    off_t total = 0;
    struct dirent *e;
    while ((e = readdir(dir)) != NULL) {
        if (e->d_name[0] == '.' || strlen(e->d_name) >= sizeof(entries->name))
            continue;
        char path[640];
        snprintf(path, sizeof(path), "%s/%s", folder, e->d_name);
        struct stat st;
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode))
            continue;
        if (count == cap) {
            cap = cap ? cap * 2 : 256;
            entry_t *grown = realloc(entries, cap * sizeof(*entries));
            if (grown == NULL)
                break;
            entries = grown;
        }
        copy_str(entries[count].name, sizeof(entries[count].name), e->d_name);
        entries[count].when = st.st_mtime;
        entries[count].size = st.st_size;
        total += st.st_size;
        count++;
    }
    closedir(dir);
    const off_t high = (off_t)256 << 20, low = (off_t)192 << 20;
    if (total > high) {
        /* the oldest out first (a simple selection: the cache holds hundreds, not millions) */
        while (total > low) {
            size_t oldest = count;
            for (size_t i = 0; i < count; i++)
                if (entries[i].size >= 0 && (oldest == count || entries[i].when < entries[oldest].when))
                    oldest = i;
            if (oldest == count)
                break;
            char path[640];
            snprintf(path, sizeof(path), "%s/%s", folder, entries[oldest].name);
            unlink(path);
            total -= entries[oldest].size;
            entries[oldest].size = -1;
        }
        log_line("cache trimmed to %lld MiB", (long long)(total >> 20));
    }
    free(entries);
}
