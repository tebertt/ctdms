/*
 * ctdms - I/O utility helpers
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "ctdms_internal.h"
#include <stdlib.h>
#include <string.h>

void *ctdms_realloc(void *ptr, size_t size) {
    if (size == 0) {
        free(ptr);
        return NULL;
    }
    return realloc(ptr, size);
}

/* Byte-swap helpers */
static uint16_t swap16(uint16_t v) {
    return (uint16_t)((v >> 8) | (v << 8));
}

static uint32_t swap32(uint32_t v) {
    return ((v >> 24) & 0x000000FF) |
           ((v >>  8) & 0x0000FF00) |
           ((v <<  8) & 0x00FF0000) |
           ((v << 24) & 0xFF000000);
}

static uint64_t swap64(uint64_t v) {
    return ((v >> 56) & 0x00000000000000FFULL) |
           ((v >> 40) & 0x000000000000FF00ULL) |
           ((v >> 24) & 0x0000000000FF0000ULL) |
           ((v >>  8) & 0x00000000FF000000ULL) |
           ((v <<  8) & 0x000000FF00000000ULL) |
           ((v << 24) & 0x0000FF0000000000ULL) |
           ((v << 40) & 0x00FF000000000000ULL) |
           ((v << 56) & 0xFF00000000000000ULL);
}

/* Detect host endianness */
static int host_is_big_endian(void) {
    uint32_t x = 1;
    return *((uint8_t *)&x) == 0;
}

static int need_swap(int big_endian) {
    return big_endian != host_is_big_endian();
}

uint8_t ctdms_read_u8(FILE *fp) {
    uint8_t v = 0;
    fread(&v, 1, 1, fp);
    return v;
}

int8_t ctdms_read_i8(FILE *fp) {
    int8_t v = 0;
    fread(&v, 1, 1, fp);
    return v;
}

uint16_t ctdms_read_u16(FILE *fp, int big_endian) {
    uint16_t v = 0;
    fread(&v, 2, 1, fp);
    if (need_swap(big_endian)) v = swap16(v);
    return v;
}

int16_t ctdms_read_i16(FILE *fp, int big_endian) {
    uint16_t v = ctdms_read_u16(fp, big_endian);
    int16_t r;
    memcpy(&r, &v, 2);
    return r;
}

uint32_t ctdms_read_u32(FILE *fp, int big_endian) {
    uint32_t v = 0;
    fread(&v, 4, 1, fp);
    if (need_swap(big_endian)) v = swap32(v);
    return v;
}

int32_t ctdms_read_i32(FILE *fp, int big_endian) {
    uint32_t v = ctdms_read_u32(fp, big_endian);
    int32_t r;
    memcpy(&r, &v, 4);
    return r;
}

uint64_t ctdms_read_u64(FILE *fp, int big_endian) {
    uint64_t v = 0;
    fread(&v, 8, 1, fp);
    if (need_swap(big_endian)) v = swap64(v);
    return v;
}

int64_t ctdms_read_i64(FILE *fp, int big_endian) {
    uint64_t v = ctdms_read_u64(fp, big_endian);
    int64_t r;
    memcpy(&r, &v, 8);
    return r;
}

float ctdms_read_f32(FILE *fp, int big_endian) {
    uint32_t v = ctdms_read_u32(fp, big_endian);
    float r;
    memcpy(&r, &v, 4);
    return r;
}

double ctdms_read_f64(FILE *fp, int big_endian) {
    uint64_t v = ctdms_read_u64(fp, big_endian);
    double r;
    memcpy(&r, &v, 8);
    return r;
}

char *ctdms_read_string(FILE *fp, int big_endian) {
    uint32_t len = ctdms_read_u32(fp, big_endian);
    if (len == 0) {
        char *s = malloc(1);
        if (s) s[0] = '\0';
        return s;
    }
    /* Sanity check: reject unreasonably large strings (>64 MB) */
    if (len > 64 * 1024 * 1024) {
        return NULL;
    }
    char *s = malloc((size_t)len + 1);
    if (!s) return NULL;
    size_t read = fread(s, 1, len, fp);
    if (read != len) {
        free(s);
        return NULL;
    }
    s[len] = '\0';
    return s;
}
