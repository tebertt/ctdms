/*
 * ctdms - Public API implementation
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "ctdms_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ---- Helpers ---- */

static void free_properties(ctdms_prop_internal *props, int count) {
    for (int i = 0; i < count; i++) {
        free(props[i].name);
        if (props[i].type == CTDMS_TYPE_STRING)
            free(props[i].value.str);
    }
    free(props);
}

static void fill_property_out(const ctdms_prop_internal *src,
                               ctdms_property *out) {
    out->name = src->name;
    out->type = src->type;
    memcpy(&out->value, &src->value, sizeof(out->value));
}

/* ---- File operations ---- */

ctdms_file *ctdms_open(const char *path) {
    if (!path) return NULL;

    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;

    ctdms_file *file = calloc(1, sizeof(*file));
    if (!file) {
        fclose(fp);
        return NULL;
    }
    file->fp = fp;

    ctdms_error err = ctdms_parse_file(file);
    if (err != CTDMS_OK) {
        if (file->error_msg[0] == '\0') {
            snprintf(file->error_msg, sizeof(file->error_msg),
                     "%s", ctdms_error_string(err));
        }
        /* Keep the file object so the user can call ctdms_get_error() */
    }

    return file;
}

void ctdms_close(ctdms_file *file) {
    if (!file) return;

    /* Free groups and their channels */
    for (int gi = 0; gi < file->num_groups; gi++) {
        ctdms_group *g = file->groups[gi];
        for (int ci = 0; ci < g->num_channels; ci++) {
            ctdms_channel *ch = g->channels[ci];
            free(ch->name);
            free(ch->path);
            free_properties(ch->properties, ch->num_properties);
            free(ch->chunks);
            free(ch);
        }
        free(g->channels);
        free(g->name);
        free(g->path);
        free_properties(g->properties, g->num_properties);
        free(g);
    }
    free(file->groups);

    /* Free file-level properties */
    free_properties(file->properties, file->num_properties);

    /* Free active channels list (channels themselves freed above) */
    free(file->active_channels);

    if (file->fp) fclose(file->fp);
    free(file);
}

const char *ctdms_get_error(const ctdms_file *file) {
    if (!file) return "NULL file handle";
    if (file->error_msg[0] == '\0') return NULL;
    return file->error_msg;
}

/* ---- File-level properties ---- */

int ctdms_get_property_count(const ctdms_file *file) {
    return file ? file->num_properties : 0;
}

ctdms_error ctdms_get_property(const ctdms_file *file, int index,
                                ctdms_property *out) {
    if (!file || !out) return CTDMS_ERR_NULL_ARG;
    if (index < 0 || index >= file->num_properties)
        return CTDMS_ERR_OUT_OF_RANGE;
    fill_property_out(&file->properties[index], out);
    return CTDMS_OK;
}

/* ---- Groups ---- */

int ctdms_get_group_count(const ctdms_file *file) {
    return file ? file->num_groups : 0;
}

ctdms_group *ctdms_get_group(const ctdms_file *file, int index) {
    if (!file || index < 0 || index >= file->num_groups) return NULL;
    return file->groups[index];
}

ctdms_group *ctdms_get_group_by_name(const ctdms_file *file,
                                      const char *name) {
    if (!file || !name) return NULL;
    for (int i = 0; i < file->num_groups; i++) {
        if (strcmp(file->groups[i]->name, name) == 0)
            return file->groups[i];
    }
    return NULL;
}

const char *ctdms_group_get_name(const ctdms_group *group) {
    return group ? group->name : NULL;
}

int ctdms_group_get_property_count(const ctdms_group *group) {
    return group ? group->num_properties : 0;
}

ctdms_error ctdms_group_get_property(const ctdms_group *group, int index,
                                      ctdms_property *out) {
    if (!group || !out) return CTDMS_ERR_NULL_ARG;
    if (index < 0 || index >= group->num_properties)
        return CTDMS_ERR_OUT_OF_RANGE;
    fill_property_out(&group->properties[index], out);
    return CTDMS_OK;
}

/* ---- Channels ---- */

int ctdms_group_get_channel_count(const ctdms_group *group) {
    return group ? group->num_channels : 0;
}

ctdms_channel *ctdms_group_get_channel(const ctdms_group *group, int index) {
    if (!group || index < 0 || index >= group->num_channels) return NULL;
    return group->channels[index];
}

ctdms_channel *ctdms_group_get_channel_by_name(const ctdms_group *group,
                                                const char *name) {
    if (!group || !name) return NULL;
    for (int i = 0; i < group->num_channels; i++) {
        if (strcmp(group->channels[i]->name, name) == 0)
            return group->channels[i];
    }
    return NULL;
}

const char *ctdms_channel_get_name(const ctdms_channel *ch) {
    return ch ? ch->name : NULL;
}

ctdms_data_type ctdms_channel_get_data_type(const ctdms_channel *ch) {
    return ch ? ch->data_type : CTDMS_TYPE_VOID;
}

uint64_t ctdms_channel_get_num_values(const ctdms_channel *ch) {
    return ch ? ch->total_values : 0;
}

int ctdms_channel_get_property_count(const ctdms_channel *ch) {
    return ch ? ch->num_properties : 0;
}

ctdms_error ctdms_channel_get_property(const ctdms_channel *ch, int index,
                                        ctdms_property *out) {
    if (!ch || !out) return CTDMS_ERR_NULL_ARG;
    if (index < 0 || index >= ch->num_properties)
        return CTDMS_ERR_OUT_OF_RANGE;
    fill_property_out(&ch->properties[index], out);
    return CTDMS_OK;
}

/* ---- Data reading ---- */

int64_t ctdms_channel_read_data(const ctdms_channel *ch, void *buffer,
                                 uint64_t offset, uint64_t num_values) {
    if (!ch || !buffer) return CTDMS_ERR_NULL_ARG;
    if (ch->data_type == CTDMS_TYPE_STRING) return CTDMS_ERR_TYPE_MISMATCH;

    size_t type_sz = ctdms_type_size(ch->data_type);
    if (type_sz == 0) return CTDMS_ERR_TYPE_MISMATCH;

    uint8_t *out = (uint8_t *)buffer;
    uint64_t values_read = 0;
    uint64_t skip = offset;

    for (int ci = 0; ci < ch->num_chunks && values_read < num_values; ci++) {
        const ctdms_data_chunk *chunk = &ch->chunks[ci];

        if (skip >= chunk->num_values) {
            skip -= chunk->num_values;
            continue;
        }

        uint64_t to_read = chunk->num_values - skip;
        if (to_read > num_values - values_read)
            to_read = num_values - values_read;

        if (!chunk->interleaved) {
            /* Contiguous data */
            uint64_t byte_offset = chunk->file_offset +
                                    skip * type_sz * ch->dimension;
            fseek(ch->fp, (long)byte_offset, SEEK_SET);
            size_t bytes = (size_t)(to_read * type_sz * ch->dimension);
            size_t read = fread(out + values_read * type_sz * ch->dimension,
                                1, bytes, ch->fp);
            values_read += read / (type_sz * ch->dimension);
        } else {
            /* Interleaved data - read value by value */
            uint64_t stride = chunk->interleaved_stride;
            for (uint64_t vi = 0; vi < to_read; vi++) {
                uint64_t byte_offset = chunk->file_offset +
                                        (skip + vi) * stride;
                fseek(ch->fp, (long)byte_offset, SEEK_SET);
                size_t bytes = type_sz * ch->dimension;
                fread(out + (values_read + vi) * bytes, 1, bytes, ch->fp);
            }
            values_read += to_read;
        }
        skip = 0;
    }

    return (int64_t)values_read;
}

int64_t ctdms_channel_read_strings(const ctdms_channel *ch,
                                    const char **buffer,
                                    uint64_t offset,
                                    uint64_t num_values) {
    if (!ch || !buffer) return CTDMS_ERR_NULL_ARG;
    if (ch->data_type != CTDMS_TYPE_STRING) return CTDMS_ERR_TYPE_MISMATCH;

    /* String format: array of uint32 offsets, then concatenated string data.
     * Each offset points to the end of the string (exclusive). */
    uint64_t values_read = 0;
    uint64_t skip = offset;

    for (int ci = 0; ci < ch->num_chunks && values_read < num_values; ci++) {
        const ctdms_data_chunk *chunk = &ch->chunks[ci];

        if (skip >= chunk->num_values) {
            skip -= chunk->num_values;
            continue;
        }

        uint64_t to_read = chunk->num_values - skip;
        if (to_read > num_values - values_read)
            to_read = num_values - values_read;

        /* Read all offsets for this chunk */
        uint32_t *offsets = malloc((size_t)chunk->num_values * sizeof(uint32_t));
        if (!offsets) return CTDMS_ERR_ALLOC;

        fseek(ch->fp, (long)chunk->file_offset, SEEK_SET);
        fread(offsets, sizeof(uint32_t), (size_t)chunk->num_values, ch->fp);

        /* String data starts after offset array */
        uint64_t string_data_start = chunk->file_offset +
                                      chunk->num_values * sizeof(uint32_t);

        for (uint64_t vi = 0; vi < to_read; vi++) {
            uint64_t idx = skip + vi;
            uint32_t str_start = (idx == 0) ? 0 : offsets[idx - 1];
            uint32_t str_end = offsets[idx];
            uint32_t str_len = str_end - str_start;

            char *s = malloc((size_t)str_len + 1);
            if (!s) {
                free(offsets);
                return CTDMS_ERR_ALLOC;
            }
            fseek(ch->fp, (long)(string_data_start + str_start), SEEK_SET);
            fread(s, 1, str_len, ch->fp);
            s[str_len] = '\0';
            buffer[values_read + vi] = s;
        }

        values_read += to_read;
        skip = 0;
        free(offsets);
    }

    return (int64_t)values_read;
}

/* ---- Utility ---- */

const char *ctdms_error_string(ctdms_error err) {
    switch (err) {
        case CTDMS_OK:                  return "No error";
        case CTDMS_ERR_NULL_ARG:        return "NULL argument";
        case CTDMS_ERR_OPEN_FILE:       return "Failed to open file";
        case CTDMS_ERR_READ:            return "Read error";
        case CTDMS_ERR_INVALID_FILE:    return "Invalid TDMS file";
        case CTDMS_ERR_UNSUPPORTED_VERSION: return "Unsupported TDMS version";
        case CTDMS_ERR_CORRUPT:         return "Corrupt TDMS data";
        case CTDMS_ERR_ALLOC:           return "Memory allocation failed";
        case CTDMS_ERR_NOT_FOUND:       return "Object not found";
        case CTDMS_ERR_TYPE_MISMATCH:   return "Data type mismatch";
        case CTDMS_ERR_OUT_OF_RANGE:    return "Index out of range";
        default:                        return "Unknown error";
    }
}

size_t ctdms_type_size(ctdms_data_type type) {
    switch (type) {
        case CTDMS_TYPE_VOID:           return 0;
        case CTDMS_TYPE_I8:             return 1;
        case CTDMS_TYPE_I16:            return 2;
        case CTDMS_TYPE_I32:            return 4;
        case CTDMS_TYPE_I64:            return 8;
        case CTDMS_TYPE_U8:             return 1;
        case CTDMS_TYPE_U16:            return 2;
        case CTDMS_TYPE_U32:            return 4;
        case CTDMS_TYPE_U64:            return 8;
        case CTDMS_TYPE_SINGLE_FLOAT:   return 4;
        case CTDMS_TYPE_DOUBLE_FLOAT:   return 8;
        case CTDMS_TYPE_EXT_FLOAT:      return 10; /* platform-dependent */
        case CTDMS_TYPE_SINGLE_FLOAT_UNIT: return 4;
        case CTDMS_TYPE_DOUBLE_FLOAT_UNIT: return 8;
        case CTDMS_TYPE_EXT_FLOAT_UNIT:    return 10;
        case CTDMS_TYPE_STRING:         return 0; /* variable length */
        case CTDMS_TYPE_BOOLEAN:        return 1;
        case CTDMS_TYPE_TIMESTAMP:      return 16;
        case CTDMS_TYPE_FIXED_POINT:    return 0; /* variable */
        case CTDMS_TYPE_COMPLEX_SINGLE: return 8;
        case CTDMS_TYPE_COMPLEX_DOUBLE: return 16;
        default:                        return 0;
    }
}
