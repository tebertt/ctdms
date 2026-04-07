/*
 * ctdms - C library for reading NI TDMS files
 * Copyright (C) 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef CTDMS_H
#define CTDMS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Library version */
#define CTDMS_VERSION_MAJOR 0
#define CTDMS_VERSION_MINOR 1
#define CTDMS_VERSION_PATCH 0

/* Export macros */
#if defined(_WIN32) && defined(CTDMS_SHARED)
#  ifdef CTDMS_BUILDING
#    define CTDMS_API __declspec(dllexport)
#  else
#    define CTDMS_API __declspec(dllimport)
#  endif
#elif defined(__GNUC__) && defined(CTDMS_SHARED)
#  define CTDMS_API __attribute__((visibility("default")))
#else
#  define CTDMS_API
#endif

/* Error codes */
typedef enum {
    CTDMS_OK = 0,
    CTDMS_ERR_NULL_ARG = -1,
    CTDMS_ERR_OPEN_FILE = -2,
    CTDMS_ERR_READ = -3,
    CTDMS_ERR_INVALID_FILE = -4,
    CTDMS_ERR_UNSUPPORTED_VERSION = -5,
    CTDMS_ERR_CORRUPT = -6,
    CTDMS_ERR_ALLOC = -7,
    CTDMS_ERR_NOT_FOUND = -8,
    CTDMS_ERR_TYPE_MISMATCH = -9,
    CTDMS_ERR_OUT_OF_RANGE = -10,
} ctdms_error;

/* TDMS data types (matches NI enum values) */
typedef enum {
    CTDMS_TYPE_VOID          = 0x00,
    CTDMS_TYPE_I8            = 0x01,
    CTDMS_TYPE_I16           = 0x02,
    CTDMS_TYPE_I32           = 0x03,
    CTDMS_TYPE_I64           = 0x04,
    CTDMS_TYPE_U8            = 0x05,
    CTDMS_TYPE_U16           = 0x06,
    CTDMS_TYPE_U32           = 0x07,
    CTDMS_TYPE_U64           = 0x08,
    CTDMS_TYPE_SINGLE_FLOAT  = 0x09,
    CTDMS_TYPE_DOUBLE_FLOAT  = 0x0A,
    CTDMS_TYPE_EXT_FLOAT     = 0x0B,
    CTDMS_TYPE_SINGLE_FLOAT_UNIT = 0x19,
    CTDMS_TYPE_DOUBLE_FLOAT_UNIT = 0x1A,
    CTDMS_TYPE_EXT_FLOAT_UNIT    = 0x1B,
    CTDMS_TYPE_STRING        = 0x20,
    CTDMS_TYPE_BOOLEAN       = 0x21,
    CTDMS_TYPE_TIMESTAMP     = 0x44,
    CTDMS_TYPE_FIXED_POINT   = 0x4F,
    CTDMS_TYPE_COMPLEX_SINGLE = 0x08000C,
    CTDMS_TYPE_COMPLEX_DOUBLE = 0x10000D,
    CTDMS_TYPE_DAQMX         = 0x7FFFFFFF,
} ctdms_data_type;

/* TDMS timestamp: seconds since 1904-01-01 00:00:00 UTC + fractional part */
typedef struct {
    int64_t  seconds;          /* seconds since NI epoch (1904-01-01) */
    uint64_t fractions;        /* positive fractions of a second (2^-64) */
} ctdms_timestamp;

/* Opaque types */
typedef struct ctdms_file    ctdms_file;
typedef struct ctdms_group   ctdms_group;
typedef struct ctdms_channel ctdms_channel;

/* Property value union */
typedef struct {
    const char    *name;
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
        const char  *str;
        uint8_t      boolean;
        ctdms_timestamp timestamp;
    } value;
} ctdms_property;

/* ---- File operations ---- */

CTDMS_API ctdms_file   *ctdms_open(const char *path);
CTDMS_API void          ctdms_close(ctdms_file *file);
CTDMS_API const char   *ctdms_get_error(const ctdms_file *file);

/* ---- File-level properties ---- */

CTDMS_API int           ctdms_get_property_count(const ctdms_file *file);
CTDMS_API ctdms_error   ctdms_get_property(const ctdms_file *file, int index,
                                           ctdms_property *out);

/* ---- Groups ---- */

CTDMS_API int           ctdms_get_group_count(const ctdms_file *file);
CTDMS_API ctdms_group  *ctdms_get_group(const ctdms_file *file, int index);
CTDMS_API ctdms_group  *ctdms_get_group_by_name(const ctdms_file *file,
                                                 const char *name);
CTDMS_API const char   *ctdms_group_get_name(const ctdms_group *group);
CTDMS_API int           ctdms_group_get_property_count(const ctdms_group *group);
CTDMS_API ctdms_error   ctdms_group_get_property(const ctdms_group *group,
                                                  int index,
                                                  ctdms_property *out);

/* ---- Channels ---- */

CTDMS_API int              ctdms_group_get_channel_count(const ctdms_group *group);
CTDMS_API ctdms_channel   *ctdms_group_get_channel(const ctdms_group *group,
                                                    int index);
CTDMS_API ctdms_channel   *ctdms_group_get_channel_by_name(
                                const ctdms_group *group, const char *name);
CTDMS_API const char      *ctdms_channel_get_name(const ctdms_channel *ch);
CTDMS_API ctdms_data_type  ctdms_channel_get_data_type(const ctdms_channel *ch);
CTDMS_API uint64_t         ctdms_channel_get_num_values(const ctdms_channel *ch);
CTDMS_API int              ctdms_channel_get_property_count(
                                const ctdms_channel *ch);
CTDMS_API ctdms_error      ctdms_channel_get_property(const ctdms_channel *ch,
                                                       int index,
                                                       ctdms_property *out);

/* ---- Data reading ---- */

/*
 * Read raw data values for a channel into the provided buffer.
 * The buffer must be large enough to hold `num_values` elements of the
 * channel's data type. For string channels, use ctdms_channel_read_strings().
 * Returns the number of values actually read, or a negative ctdms_error code.
 */
CTDMS_API int64_t ctdms_channel_read_data(const ctdms_channel *ch,
                                           void *buffer,
                                           uint64_t offset,
                                           uint64_t num_values);

/*
 * Read string data. Caller provides an array of `const char*` pointers.
 * The pointers remain valid until ctdms_close() is called.
 * Returns the number of strings read, or a negative ctdms_error code.
 */
CTDMS_API int64_t ctdms_channel_read_strings(const ctdms_channel *ch,
                                              const char **buffer,
                                              uint64_t offset,
                                              uint64_t num_values);

/* ---- Utility ---- */

CTDMS_API const char *ctdms_error_string(ctdms_error err);
CTDMS_API size_t      ctdms_type_size(ctdms_data_type type);

#ifdef __cplusplus
}
#endif

#endif /* CTDMS_H */
