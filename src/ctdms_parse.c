/*
 * ctdms - TDMS file parser
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "ctdms_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ---- Helper: find or create group ---- */
static ctdms_group *find_or_create_group(ctdms_file *file, const char *name,
                                          const char *path) {
    for (int i = 0; i < file->num_groups; i++) {
        if (strcmp(file->groups[i]->path, path) == 0)
            return file->groups[i];
    }

    /* Create new group */
    if (file->num_groups >= file->cap_groups) {
        int newcap = file->cap_groups ? file->cap_groups * 2 : 8;
        ctdms_group **tmp = ctdms_realloc(file->groups,
                                           (size_t)newcap * sizeof(*tmp));
        if (!tmp) return NULL;
        file->groups = tmp;
        file->cap_groups = newcap;
    }

    ctdms_group *g = calloc(1, sizeof(*g));
    if (!g) return NULL;
    g->name = strdup(name);
    g->path = strdup(path);
    if (!g->name || !g->path) {
        free(g->name);
        free(g->path);
        free(g);
        return NULL;
    }
    file->groups[file->num_groups++] = g;
    return g;
}

/* ---- Helper: find or create channel ---- */
static ctdms_channel *find_or_create_channel(ctdms_file *file,
                                              ctdms_group *group,
                                              const char *name,
                                              const char *path) {
    for (int i = 0; i < group->num_channels; i++) {
        if (strcmp(group->channels[i]->path, path) == 0)
            return group->channels[i];
    }

    /* Create new channel */
    if (group->num_channels >= group->cap_channels) {
        int newcap = group->cap_channels ? group->cap_channels * 2 : 8;
        ctdms_channel **tmp = ctdms_realloc(group->channels,
                                             (size_t)newcap * sizeof(*tmp));
        if (!tmp) return NULL;
        group->channels = tmp;
        group->cap_channels = newcap;
    }

    ctdms_channel *ch = calloc(1, sizeof(*ch));
    if (!ch) return NULL;
    ch->name = strdup(name);
    ch->path = strdup(path);
    ch->fp = file->fp;
    if (!ch->name || !ch->path) {
        free(ch->name);
        free(ch->path);
        free(ch);
        return NULL;
    }
    group->channels[group->num_channels++] = ch;
    return ch;
}

/* ---- Parse object path into group/channel names ---- */
/* Paths look like: /               (file)
 *                  /'GroupName'     (group)
 *                  /'GroupName'/'ChannelName' (channel)
 */
typedef enum {
    PATH_TYPE_FILE,
    PATH_TYPE_GROUP,
    PATH_TYPE_CHANNEL,
    PATH_TYPE_INVALID,
} path_type;

typedef struct {
    path_type type;
    char group_name[512];
    char channel_name[512];
} parsed_path;

static parsed_path parse_object_path(const char *path) {
    parsed_path pp;
    memset(&pp, 0, sizeof(pp));

    if (!path || path[0] != '/') {
        pp.type = PATH_TYPE_INVALID;
        return pp;
    }

    /* File object */
    if (path[1] == '\0') {
        pp.type = PATH_TYPE_FILE;
        return pp;
    }

    /* Expect /'name' pattern */
    const char *p = path + 1;
    if (*p != '\'') {
        pp.type = PATH_TYPE_INVALID;
        return pp;
    }
    p++; /* skip opening quote */

    /* Extract group name, handling '' as escaped single quote */
    int gi = 0;
    while (*p && gi < (int)sizeof(pp.group_name) - 1) {
        if (*p == '\'') {
            if (*(p + 1) == '\'') {
                /* Escaped quote */
                pp.group_name[gi++] = '\'';
                p += 2;
            } else {
                /* End of group name */
                p++;
                break;
            }
        } else {
            pp.group_name[gi++] = *p;
            p++;
        }
    }
    pp.group_name[gi] = '\0';

    /* Check if there's a channel part */
    if (*p == '\0') {
        pp.type = PATH_TYPE_GROUP;
        return pp;
    }

    if (*p != '/' || *(p + 1) != '\'') {
        pp.type = PATH_TYPE_INVALID;
        return pp;
    }
    p += 2; /* skip /' */

    int ci = 0;
    while (*p && ci < (int)sizeof(pp.channel_name) - 1) {
        if (*p == '\'') {
            if (*(p + 1) == '\'') {
                pp.channel_name[ci++] = '\'';
                p += 2;
            } else {
                p++;
                break;
            }
        } else {
            pp.channel_name[ci++] = *p;
            p++;
        }
    }
    pp.channel_name[ci] = '\0';
    pp.type = PATH_TYPE_CHANNEL;
    return pp;
}

/* ---- Add or update a property on an object ---- */
static ctdms_error add_property(ctdms_prop_internal **props, int *num, int *cap,
                                 const ctdms_prop_internal *prop) {
    /* Check if property already exists - update it */
    for (int i = 0; i < *num; i++) {
        if (strcmp((*props)[i].name, prop->name) == 0) {
            /* Free old string value if applicable */
            if ((*props)[i].type == CTDMS_TYPE_STRING) {
                free((*props)[i].value.str);
            }
            (*props)[i].type = prop->type;
            (*props)[i].value = prop->value;
            return CTDMS_OK;
        }
    }

    /* Add new property */
    if (*num >= *cap) {
        int newcap = *cap ? *cap * 2 : 8;
        ctdms_prop_internal *tmp = ctdms_realloc(*props,
                                                  (size_t)newcap * sizeof(**props));
        if (!tmp) return CTDMS_ERR_ALLOC;
        *props = tmp;
        *cap = newcap;
    }
    (*props)[*num] = *prop;
    (*num)++;
    return CTDMS_OK;
}

/* ---- Read a property value from file ---- */
static ctdms_error read_property_value(FILE *fp, int big_endian,
                                        ctdms_data_type type,
                                        ctdms_prop_internal *prop) {
    prop->type = type;
    switch (type) {
        case CTDMS_TYPE_I8:
            prop->value.i8 = ctdms_read_i8(fp);
            break;
        case CTDMS_TYPE_I16:
            prop->value.i16 = ctdms_read_i16(fp, big_endian);
            break;
        case CTDMS_TYPE_I32:
            prop->value.i32 = ctdms_read_i32(fp, big_endian);
            break;
        case CTDMS_TYPE_I64:
            prop->value.i64 = ctdms_read_i64(fp, big_endian);
            break;
        case CTDMS_TYPE_U8:
            prop->value.u8 = ctdms_read_u8(fp);
            break;
        case CTDMS_TYPE_U16:
            prop->value.u16 = ctdms_read_u16(fp, big_endian);
            break;
        case CTDMS_TYPE_U32:
            prop->value.u32 = ctdms_read_u32(fp, big_endian);
            break;
        case CTDMS_TYPE_U64:
            prop->value.u64 = ctdms_read_u64(fp, big_endian);
            break;
        case CTDMS_TYPE_SINGLE_FLOAT:
        case CTDMS_TYPE_SINGLE_FLOAT_UNIT:
            prop->value.f32 = ctdms_read_f32(fp, big_endian);
            break;
        case CTDMS_TYPE_DOUBLE_FLOAT:
        case CTDMS_TYPE_DOUBLE_FLOAT_UNIT:
            prop->value.f64 = ctdms_read_f64(fp, big_endian);
            break;
        case CTDMS_TYPE_STRING:
            prop->value.str = ctdms_read_string(fp, big_endian);
            if (!prop->value.str) return CTDMS_ERR_ALLOC;
            break;
        case CTDMS_TYPE_BOOLEAN:
            prop->value.boolean = ctdms_read_u8(fp);
            break;
        case CTDMS_TYPE_TIMESTAMP:
            /* fractions first (u64), then seconds (i64) -- NI byte order */
            prop->value.timestamp.fractions = ctdms_read_u64(fp, big_endian);
            prop->value.timestamp.seconds = ctdms_read_i64(fp, big_endian);
            break;
        default:
            return CTDMS_ERR_CORRUPT;
    }
    return CTDMS_OK;
}

/* ---- Add a data chunk to a channel ---- */
static ctdms_error add_data_chunk(ctdms_channel *ch,
                                   const ctdms_data_chunk *chunk) {
    if (ch->num_chunks >= ch->cap_chunks) {
        int newcap = ch->cap_chunks ? ch->cap_chunks * 2 : 16;
        ctdms_data_chunk *tmp = ctdms_realloc(ch->chunks,
                                               (size_t)newcap * sizeof(*tmp));
        if (!tmp) return CTDMS_ERR_ALLOC;
        ch->chunks = tmp;
        ch->cap_chunks = newcap;
    }
    ch->chunks[ch->num_chunks++] = *chunk;
    ch->total_values += chunk->num_values;
    return CTDMS_OK;
}

/* ---- Active channel list management ---- */
static ctdms_error add_active_channel(ctdms_file *file, ctdms_channel *ch) {
    if (file->num_active_channels >= file->cap_active_channels) {
        int newcap = file->cap_active_channels
                         ? file->cap_active_channels * 2
                         : 16;
        ctdms_channel **tmp = ctdms_realloc(
            file->active_channels,
            (size_t)newcap * sizeof(*tmp));
        if (!tmp) return CTDMS_ERR_ALLOC;
        file->active_channels = tmp;
        file->cap_active_channels = newcap;
    }
    file->active_channels[file->num_active_channels++] = ch;
    return CTDMS_OK;
}

/* ---- Parse a single segment ---- */
static ctdms_error parse_segment(ctdms_file *file, uint64_t seg_start,
                                  uint64_t *next_seg_pos) {
    FILE *fp = file->fp;

    /* Read lead-in */
    if (fseek(fp, (long)seg_start, SEEK_SET) != 0)
        return CTDMS_ERR_READ;

    /* Tag: "TDSm" */
    char tag[4];
    if (fread(tag, 1, 4, fp) != 4)
        return CTDMS_ERR_READ;
    if (memcmp(tag, "TDSm", 4) != 0)
        return CTDMS_ERR_INVALID_FILE;

    /* ToC mask - always little-endian */
    uint32_t toc = ctdms_read_u32(fp, 0);
    int big_endian = (toc & CTDMS_TOC_BIG_ENDIAN) != 0;

    /* Version number */
    uint32_t version = ctdms_read_u32(fp, big_endian);
    if (version != 4712 && version != 4713) {
        snprintf(file->error_msg, sizeof(file->error_msg),
                 "Unsupported TDMS version: %u", version);
        return CTDMS_ERR_UNSUPPORTED_VERSION;
    }

    /* Next segment offset */
    uint64_t next_seg_offset = ctdms_read_u64(fp, big_endian);
    /* Raw data offset */
    uint64_t raw_data_offset = ctdms_read_u64(fp, big_endian);

    uint64_t meta_start = seg_start + CTDMS_LEAD_IN_SIZE;
    uint64_t raw_start = meta_start + raw_data_offset;

    /* Calculate next segment position */
    if (next_seg_offset == 0xFFFFFFFFFFFFFFFFULL) {
        /* Last segment, possibly incomplete - get file size */
        long cur = ftell(fp);
        fseek(fp, 0, SEEK_END);
        *next_seg_pos = (uint64_t)ftell(fp);
        fseek(fp, cur, SEEK_SET);
    } else {
        *next_seg_pos = seg_start + CTDMS_LEAD_IN_SIZE + next_seg_offset;
    }

    /* If new object list flag is set, clear active channels */
    if (toc & CTDMS_TOC_NEW_OBJ_LIST) {
        file->num_active_channels = 0;
    }

    /* ---- Parse meta data ---- */
    if (toc & CTDMS_TOC_METADATA) {
        if (fseek(fp, (long)meta_start, SEEK_SET) != 0)
            return CTDMS_ERR_READ;

        uint32_t num_objects = ctdms_read_u32(fp, big_endian);

        for (uint32_t obj_i = 0; obj_i < num_objects; obj_i++) {
            /* Object path */
            char *obj_path = ctdms_read_string(fp, big_endian);
            if (!obj_path) return CTDMS_ERR_ALLOC;

            parsed_path pp = parse_object_path(obj_path);

            /* Raw data index */
            uint32_t raw_index_marker = ctdms_read_u32(fp, big_endian);

            ctdms_channel *channel = NULL;
            ctdms_data_type data_type = CTDMS_TYPE_VOID;
            uint32_t dimension = 1;
            int has_raw_data = 0;

            if (raw_index_marker == CTDMS_RAW_DATA_INDEX_NONE) {
                /* No raw data */
            } else if (raw_index_marker == CTDMS_RAW_DATA_INDEX_SAME) {
                /* Same as previous segment - channel should already exist */
                has_raw_data = 1;
            } else if (raw_index_marker == CTDMS_RAW_DATA_INDEX_DAQMX_FCS ||
                       raw_index_marker == CTDMS_RAW_DATA_INDEX_DAQMX_DL) {
                /* DAQmx raw data - skip for now */
                uint32_t daqmx_type = ctdms_read_u32(fp, big_endian);
                (void)daqmx_type;
                dimension = ctdms_read_u32(fp, big_endian);
                (void)ctdms_read_u64(fp, big_endian); /* num_values */
                /* Format changing scalers vector */
                uint32_t num_scalers = ctdms_read_u32(fp, big_endian);
                for (uint32_t s = 0; s < num_scalers; s++) {
                    ctdms_read_u32(fp, big_endian); /* DAQmx data type */
                    ctdms_read_u32(fp, big_endian); /* raw buffer index */
                    ctdms_read_u32(fp, big_endian); /* raw byte offset */
                    ctdms_read_u32(fp, big_endian); /* sample format bitmap */
                    ctdms_read_u32(fp, big_endian); /* scale ID */
                }
                /* Raw data width vector */
                uint32_t num_widths = ctdms_read_u32(fp, big_endian);
                for (uint32_t w = 0; w < num_widths; w++) {
                    ctdms_read_u32(fp, big_endian);
                }
                data_type = CTDMS_TYPE_DAQMX;
                has_raw_data = 1;
            } else {
                /* New raw data index */
                /* raw_index_marker is the length of the index info */
                data_type = (ctdms_data_type)ctdms_read_u32(fp, big_endian);
                dimension = ctdms_read_u32(fp, big_endian);
                (void)ctdms_read_u64(fp, big_endian); /* num_values - tracked in re-parse */
                /* Total size only for variable-length types */
                if (data_type == CTDMS_TYPE_STRING) {
                    (void)ctdms_read_u64(fp, big_endian); /* total_size */
                }
                has_raw_data = 1;
            }

            /* Determine target for properties */
            ctdms_prop_internal **target_props = NULL;
            int *target_num = NULL;
            int *target_cap = NULL;

            switch (pp.type) {
                case PATH_TYPE_FILE:
                    target_props = &file->properties;
                    target_num = &file->num_properties;
                    target_cap = &file->cap_properties;
                    break;
                case PATH_TYPE_GROUP: {
                    ctdms_group *group = find_or_create_group(
                        file, pp.group_name, obj_path);
                    if (!group) { free(obj_path); return CTDMS_ERR_ALLOC; }
                    target_props = &group->properties;
                    target_num = &group->num_properties;
                    target_cap = &group->cap_properties;
                    break;
                }
                case PATH_TYPE_CHANNEL: {
                    /* Build group path */
                    char group_path[1024];
                    snprintf(group_path, sizeof(group_path), "/'%s'",
                             pp.group_name);
                    ctdms_group *group = find_or_create_group(
                        file, pp.group_name, group_path);
                    if (!group) { free(obj_path); return CTDMS_ERR_ALLOC; }
                    channel = find_or_create_channel(
                        file, group, pp.channel_name, obj_path);
                    if (!channel) { free(obj_path); return CTDMS_ERR_ALLOC; }

                    if (has_raw_data) {
                        if (raw_index_marker != CTDMS_RAW_DATA_INDEX_SAME) {
                            channel->data_type = data_type;
                            channel->dimension = dimension;
                        }
                        /* Add to active channels if not already present */
                        int found = 0;
                        for (int i = 0; i < file->num_active_channels; i++) {
                            if (file->active_channels[i] == channel) {
                                found = 1;
                                /* Update with new num_values if provided */
                                break;
                            }
                        }
                        if (!found) {
                            ctdms_error err = add_active_channel(file, channel);
                            if (err != CTDMS_OK) {
                                free(obj_path);
                                return err;
                            }
                        }
                    }
                    target_props = &channel->properties;
                    target_num = &channel->num_properties;
                    target_cap = &channel->cap_properties;
                    break;
                }
                default:
                    break;
            }

            /* Read properties */
            uint32_t num_props = ctdms_read_u32(fp, big_endian);
            for (uint32_t pi = 0; pi < num_props; pi++) {
                ctdms_prop_internal prop;
                memset(&prop, 0, sizeof(prop));
                prop.name = ctdms_read_string(fp, big_endian);
                if (!prop.name) { free(obj_path); return CTDMS_ERR_ALLOC; }

                uint32_t prop_type = ctdms_read_u32(fp, big_endian);
                ctdms_error err = read_property_value(
                    fp, big_endian, (ctdms_data_type)prop_type, &prop);
                if (err != CTDMS_OK) {
                    free(prop.name);
                    free(obj_path);
                    return err;
                }

                if (target_props) {
                    err = add_property(target_props, target_num, target_cap,
                                       &prop);
                    if (err != CTDMS_OK) {
                        free(prop.name);
                        if (prop.type == CTDMS_TYPE_STRING)
                            free(prop.value.str);
                        free(obj_path);
                        return err;
                    }
                } else {
                    /* Discard property for invalid paths */
                    free(prop.name);
                    if (prop.type == CTDMS_TYPE_STRING)
                        free(prop.value.str);
                }
            }

            /* Store num_values for this segment's channel (for chunk calc) */
            if (channel && has_raw_data &&
                raw_index_marker != CTDMS_RAW_DATA_INDEX_SAME &&
                raw_index_marker != CTDMS_RAW_DATA_INDEX_NONE) {
                /* Store the per-segment value count temporarily;
                   we'll build chunks below after meta parsing */
            }

            free(obj_path);
        }
    }

    /* ---- Build raw data chunks ---- */
    if ((toc & CTDMS_TOC_RAW_DATA) && file->num_active_channels > 0) {
        int interleaved = (toc & CTDMS_TOC_INTERLEAVED) != 0;
        uint64_t total_raw_size;

        if (next_seg_offset == 0xFFFFFFFFFFFFFFFFULL) {
            long cur = ftell(fp);
            fseek(fp, 0, SEEK_END);
            uint64_t file_size = (uint64_t)ftell(fp);
            fseek(fp, cur, SEEK_SET);
            total_raw_size = file_size - raw_start;
        } else {
            total_raw_size = (seg_start + CTDMS_LEAD_IN_SIZE + next_seg_offset)
                             - raw_start;
        }

        /* We need to compute per-channel values for the chunk.
         * We don't have the latest num_values easily accessible here for the
         * first time, so let's iterate and use the known raw data info.
         *
         * Rebuild: for each active channel, we need to know how many values
         * per chunk. We stored data_type and we'll infer from meta.
         * For the approach: scan active channels, compute per-value sizes,
         * compute chunk data size, then figure out number of chunks.
         */

        /* For each active channel, we need the latest num_values from meta.
         * Since we processed meta above, the channel objects have been updated.
         * We need a temporary array to track per-segment num_values.
         *
         * Simpler approach: re-read from the meta data for this segment's
         * channels by tracking it during meta parsing. Let's use a simpler
         * approach and compute from the raw data offset.
         */

        /* Re-parse meta to extract per-channel num_values for this segment */
        /* We'll track in a parallel structure */
        typedef struct {
            ctdms_channel *ch;
            uint64_t num_values;
            uint64_t total_size; /* for strings */
        } seg_channel_info;

        seg_channel_info *seg_info = calloc((size_t)file->num_active_channels,
                                             sizeof(seg_channel_info));
        if (!seg_info) return CTDMS_ERR_ALLOC;

        /* Re-read meta to get num_values per channel in this segment */
        if (toc & CTDMS_TOC_METADATA) {
            fseek(fp, (long)meta_start, SEEK_SET);
            uint32_t num_objects = ctdms_read_u32(fp, big_endian);

            for (uint32_t obj_i = 0; obj_i < num_objects; obj_i++) {
                char *obj_path = ctdms_read_string(fp, big_endian);
                if (!obj_path) { free(seg_info); return CTDMS_ERR_ALLOC; }
                parsed_path pp = parse_object_path(obj_path);

                uint32_t raw_index_marker = ctdms_read_u32(fp, big_endian);

                uint64_t nv = 0;
                uint64_t ts = 0;

                if (raw_index_marker == CTDMS_RAW_DATA_INDEX_NONE) {
                    /* no raw data */
                } else if (raw_index_marker == CTDMS_RAW_DATA_INDEX_SAME) {
                    /* inherit from previous - handled below */
                } else if (raw_index_marker == CTDMS_RAW_DATA_INDEX_DAQMX_FCS ||
                           raw_index_marker == CTDMS_RAW_DATA_INDEX_DAQMX_DL) {
                    ctdms_read_u32(fp, big_endian); /* type */
                    ctdms_read_u32(fp, big_endian); /* dimension */
                    nv = ctdms_read_u64(fp, big_endian);
                    uint32_t ns = ctdms_read_u32(fp, big_endian);
                    for (uint32_t s = 0; s < ns; s++) {
                        for (int k = 0; k < 5; k++)
                            ctdms_read_u32(fp, big_endian);
                    }
                    uint32_t nw = ctdms_read_u32(fp, big_endian);
                    for (uint32_t w = 0; w < nw; w++)
                        ctdms_read_u32(fp, big_endian);
                } else {
                    ctdms_data_type dt = (ctdms_data_type)ctdms_read_u32(fp, big_endian);
                    ctdms_read_u32(fp, big_endian); /* dimension */
                    nv = ctdms_read_u64(fp, big_endian);
                    if (dt == CTDMS_TYPE_STRING) {
                        ts = ctdms_read_u64(fp, big_endian);
                    }
                }

                /* Store in seg_info if it's a channel */
                if (pp.type == PATH_TYPE_CHANNEL) {
                    for (int i = 0; i < file->num_active_channels; i++) {
                        if (strcmp(file->active_channels[i]->path,
                                  obj_path) == 0) {
                            seg_info[i].ch = file->active_channels[i];
                            if (raw_index_marker != CTDMS_RAW_DATA_INDEX_NONE) {
                                seg_info[i].num_values = nv;
                                seg_info[i].total_size = ts;
                            }
                            break;
                        }
                    }
                }

                /* Skip properties */
                uint32_t num_props = ctdms_read_u32(fp, big_endian);
                for (uint32_t pi = 0; pi < num_props; pi++) {
                    char *pname = ctdms_read_string(fp, big_endian);
                    free(pname);
                    uint32_t ptype = ctdms_read_u32(fp, big_endian);
                    ctdms_prop_internal dummy;
                    memset(&dummy, 0, sizeof(dummy));
                    read_property_value(fp, big_endian,
                                        (ctdms_data_type)ptype, &dummy);
                    if (dummy.type == CTDMS_TYPE_STRING)
                        free(dummy.value.str);
                }

                free(obj_path);
            }
        }

        /* Fill in missing seg_info from previous chunks */
        for (int i = 0; i < file->num_active_channels; i++) {
            if (!seg_info[i].ch) {
                seg_info[i].ch = file->active_channels[i];
            }
            if (seg_info[i].num_values == 0 &&
                seg_info[i].ch->num_chunks > 0) {
                /* Use values from last chunk */
                int last = seg_info[i].ch->num_chunks - 1;
                seg_info[i].num_values =
                    seg_info[i].ch->chunks[last].num_values;
                seg_info[i].total_size =
                    seg_info[i].ch->chunks[last].total_size;
            }
        }

        /* Calculate single chunk raw data size */
        uint64_t one_chunk_size = 0;
        for (int i = 0; i < file->num_active_channels; i++) {
            ctdms_channel *ch = seg_info[i].ch;
            size_t type_sz = ctdms_type_size(ch->data_type);
            if (ch->data_type == CTDMS_TYPE_STRING) {
                /* String: offset array + string bytes */
                one_chunk_size += seg_info[i].num_values * 4 +
                                  seg_info[i].total_size;
            } else if (type_sz > 0) {
                one_chunk_size += type_sz * ch->dimension *
                                  seg_info[i].num_values;
            }
        }

        if (one_chunk_size > 0) {
            uint64_t num_chunks_in_seg = total_raw_size / one_chunk_size;
            if (num_chunks_in_seg == 0) num_chunks_in_seg = 1;

            /* Calculate interleaved stride */
            uint64_t stride = 0;
            if (interleaved) {
                for (int i = 0; i < file->num_active_channels; i++) {
                    size_t type_sz = ctdms_type_size(
                        seg_info[i].ch->data_type);
                    stride += type_sz * seg_info[i].ch->dimension;
                }
            }

            /* Add data chunks for each active channel, for each chunk */
            uint64_t chunk_offset = raw_start;
            for (uint64_t ci = 0; ci < num_chunks_in_seg; ci++) {
                uint64_t channel_offset = chunk_offset;
                for (int i = 0; i < file->num_active_channels; i++) {
                    ctdms_channel *ch = seg_info[i].ch;
                    ctdms_data_chunk dc;
                    memset(&dc, 0, sizeof(dc));
                    dc.file_offset = channel_offset;
                    dc.num_values = seg_info[i].num_values;
                    dc.total_size = seg_info[i].total_size;
                    dc.interleaved = interleaved;
                    dc.interleaved_stride = stride;

                    add_data_chunk(ch, &dc);

                    size_t type_sz = ctdms_type_size(ch->data_type);
                    if (ch->data_type == CTDMS_TYPE_STRING) {
                        channel_offset += seg_info[i].num_values * 4 +
                                          seg_info[i].total_size;
                    } else if (!interleaved && type_sz > 0) {
                        channel_offset += type_sz * ch->dimension *
                                          seg_info[i].num_values;
                    } else if (interleaved && type_sz > 0) {
                        channel_offset += type_sz * ch->dimension;
                    }
                }
                if (!interleaved) {
                    chunk_offset = channel_offset;
                } else {
                    /* For interleaved, all channels share the same data block */
                    chunk_offset += stride * seg_info[0].num_values;
                }
            }
        }

        free(seg_info);
    }

    return CTDMS_OK;
}

/* ---- Main parse entry point ---- */
ctdms_error ctdms_parse_file(ctdms_file *file) {
    FILE *fp = file->fp;

    /* Get file size */
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (file_size < CTDMS_LEAD_IN_SIZE) {
        snprintf(file->error_msg, sizeof(file->error_msg),
                 "File too small to contain a TDMS segment");
        return CTDMS_ERR_INVALID_FILE;
    }

    /* Verify TDMS tag */
    char tag[4];
    if (fread(tag, 1, 4, fp) != 4)
        return CTDMS_ERR_READ;
    if (memcmp(tag, "TDSm", 4) != 0) {
        snprintf(file->error_msg, sizeof(file->error_msg),
                 "Not a TDMS file (missing TDSm tag)");
        return CTDMS_ERR_INVALID_FILE;
    }
    fseek(fp, 0, SEEK_SET);

    /* Parse all segments */
    uint64_t pos = 0;
    while (pos < (uint64_t)file_size) {
        uint64_t next_pos = 0;
        ctdms_error err = parse_segment(file, pos, &next_pos);
        if (err != CTDMS_OK)
            return err;

        if (next_pos <= pos) break; /* prevent infinite loop */
        pos = next_pos;
    }

    return CTDMS_OK;
}
