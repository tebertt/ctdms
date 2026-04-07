/*
 * ctdms - Internal structures (not part of public API)
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef CTDMS_INTERNAL_H
#define CTDMS_INTERNAL_H

#include "ctdms/ctdms.h"
#include <stdio.h>

/* ToC (Table of Contents) flags */
#define CTDMS_TOC_METADATA        (1U << 1)
#define CTDMS_TOC_NEW_OBJ_LIST    (1U << 2)
#define CTDMS_TOC_RAW_DATA        (1U << 3)
#define CTDMS_TOC_INTERLEAVED     (1U << 5)
#define CTDMS_TOC_BIG_ENDIAN      (1U << 6)
#define CTDMS_TOC_DAQMX_RAW_DATA (1U << 7)

/* Lead-in size in bytes: tag(4) + toc(4) + version(4) + next_seg_offset(8) + raw_data_offset(8) = 28 */
#define CTDMS_LEAD_IN_SIZE 28

/* Special raw data index markers */
#define CTDMS_RAW_DATA_INDEX_NONE      0xFFFFFFFF
#define CTDMS_RAW_DATA_INDEX_SAME      0x00000000
#define CTDMS_RAW_DATA_INDEX_DAQMX_FCS 0x00001269
#define CTDMS_RAW_DATA_INDEX_DAQMX_DL  0x00001369

/* Internal property storage */
typedef struct {
    char           *name;
    ctdms_data_type type;
    union {
        int8_t       i8;
        int16_t      i16;
        int32_t      i32;
        int64_t      i64;
        uint8_t      u8;
        uint16_t     u16;
        uint32_t     u32;
        uint64_t     u64;
        float        f32;
        double       f64;
        char        *str;
        uint8_t      boolean;
        ctdms_timestamp timestamp;
    } value;
} ctdms_prop_internal;

/* A segment of raw data for a channel */
typedef struct {
    uint64_t file_offset;   /* absolute file offset where raw data starts */
    uint64_t num_values;    /* number of values in this chunk */
    uint64_t total_size;    /* total bytes for variable-length types (e.g., strings) */
    int      interleaved;   /* whether data in this segment is interleaved */
    uint64_t interleaved_stride; /* total stride for interleaved data */
} ctdms_data_chunk;

/* Internal channel */
struct ctdms_channel {
    char              *name;        /* channel name (without group prefix) */
    char              *path;        /* full TDMS path */
    ctdms_data_type    data_type;
    uint32_t           dimension;
    uint64_t           total_values; /* total values across all chunks */

    ctdms_prop_internal *properties;
    int                  num_properties;
    int                  cap_properties;

    ctdms_data_chunk   *chunks;
    int                 num_chunks;
    int                 cap_chunks;

    /* backref to file for reading */
    FILE               *fp;
};

/* Internal group */
struct ctdms_group {
    char              *name;
    char              *path;

    ctdms_prop_internal *properties;
    int                  num_properties;
    int                  cap_properties;

    ctdms_channel     **channels;
    int                 num_channels;
    int                 cap_channels;
};

/* Internal file */
struct ctdms_file {
    FILE             *fp;
    char              error_msg[512];

    ctdms_prop_internal *properties;
    int                  num_properties;
    int                  cap_properties;

    ctdms_group       **groups;
    int                 num_groups;
    int                 cap_groups;

    /* Ordered list of channels for segment parsing */
    ctdms_channel     **active_channels;
    int                 num_active_channels;
    int                 cap_active_channels;
};

/* Segment lead-in parsed data */
typedef struct {
    uint32_t toc;
    uint32_t version;
    int64_t  next_segment_offset;   /* -1 if 0xFFFFFFFFFFFFFFFF */
    int64_t  raw_data_offset;
    uint64_t segment_start;         /* absolute file position of segment start */
} ctdms_segment_info;

/* ---- Internal helpers (ctdms_parse.c) ---- */

ctdms_error ctdms_parse_file(ctdms_file *file);

/* ---- Utility helpers ---- */

/* Safe realloc that sets error on failure */
void *ctdms_realloc(void *ptr, size_t size);

/* Read helpers with endian awareness */
uint8_t  ctdms_read_u8(FILE *fp);
uint16_t ctdms_read_u16(FILE *fp, int big_endian);
uint32_t ctdms_read_u32(FILE *fp, int big_endian);
uint64_t ctdms_read_u64(FILE *fp, int big_endian);
int8_t   ctdms_read_i8(FILE *fp);
int16_t  ctdms_read_i16(FILE *fp, int big_endian);
int32_t  ctdms_read_i32(FILE *fp, int big_endian);
int64_t  ctdms_read_i64(FILE *fp, int big_endian);
float    ctdms_read_f32(FILE *fp, int big_endian);
double   ctdms_read_f64(FILE *fp, int big_endian);
char    *ctdms_read_string(FILE *fp, int big_endian);

#endif /* CTDMS_INTERNAL_H */
