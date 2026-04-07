/*
 * ctdms - Example: Read a TDMS file
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "ctdms/ctdms.h"
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <file.tdms>\n", argv[0]);
        return 1;
    }

    ctdms_file *file = ctdms_open(argv[1]);
    if (!file) {
        fprintf(stderr, "Failed to open file: %s\n", argv[1]);
        return 1;
    }

    const char *err = ctdms_get_error(file);
    if (err) {
        fprintf(stderr, "Error: %s\n", err);
        ctdms_close(file);
        return 1;
    }

    /* Print file-level properties */
    int nprops = ctdms_get_property_count(file);
    if (nprops > 0) {
        printf("File properties (%d):\n", nprops);
        for (int i = 0; i < nprops; i++) {
            ctdms_property prop;
            if (ctdms_get_property(file, i, &prop) == CTDMS_OK) {
                printf("  %s = ", prop.name);
                switch (prop.type) {
                    case CTDMS_TYPE_STRING:
                        printf("\"%s\"", prop.value.str);
                        break;
                    case CTDMS_TYPE_I32:
                        printf("%" PRId32, prop.value.i32);
                        break;
                    case CTDMS_TYPE_DOUBLE_FLOAT:
                        printf("%f", prop.value.f64);
                        break;
                    default:
                        printf("(type=0x%x)", prop.type);
                        break;
                }
                printf("\n");
            }
        }
    }

    /* Iterate groups */
    int ngroups = ctdms_get_group_count(file);
    printf("\nGroups: %d\n", ngroups);

    for (int gi = 0; gi < ngroups; gi++) {
        ctdms_group *group = ctdms_get_group(file, gi);
        printf("\n  Group: %s\n", ctdms_group_get_name(group));

        /* Group properties */
        int gnprops = ctdms_group_get_property_count(group);
        for (int pi = 0; pi < gnprops; pi++) {
            ctdms_property prop;
            if (ctdms_group_get_property(group, pi, &prop) == CTDMS_OK) {
                printf("    Property: %s\n", prop.name);
            }
        }

        /* Channels */
        int nchannels = ctdms_group_get_channel_count(group);
        printf("    Channels: %d\n", nchannels);

        for (int ci = 0; ci < nchannels; ci++) {
            ctdms_channel *ch = ctdms_group_get_channel(group, ci);
            const char *name = ctdms_channel_get_name(ch);
            ctdms_data_type dt = ctdms_channel_get_data_type(ch);
            uint64_t nvals = ctdms_channel_get_num_values(ch);

            printf("      Channel: %s  type=0x%x  values=%" PRIu64 "\n",
                   name, dt, nvals);

            /* Print first few values */
            if (nvals > 0 && dt == CTDMS_TYPE_DOUBLE_FLOAT) {
                uint64_t nprint = nvals < 5 ? nvals : 5;
                double *buf = malloc((size_t)nprint * sizeof(double));
                if (buf) {
                    int64_t nread = ctdms_channel_read_data(ch, buf, 0,
                                                             nprint);
                    if (nread > 0) {
                        printf("        Data: ");
                        for (int64_t vi = 0; vi < nread; vi++)
                            printf("%f ", buf[vi]);
                        if (nvals > nprint)
                            printf("...");
                        printf("\n");
                    }
                    free(buf);
                }
            } else if (nvals > 0 && dt == CTDMS_TYPE_I32) {
                uint64_t nprint = nvals < 5 ? nvals : 5;
                int32_t *buf = malloc((size_t)nprint * sizeof(int32_t));
                if (buf) {
                    int64_t nread = ctdms_channel_read_data(ch, buf, 0,
                                                             nprint);
                    if (nread > 0) {
                        printf("        Data: ");
                        for (int64_t vi = 0; vi < nread; vi++)
                            printf("%" PRId32 " ", buf[vi]);
                        if (nvals > nprint)
                            printf("...");
                        printf("\n");
                    }
                    free(buf);
                }
            }

            /* Channel properties */
            int cnprops = ctdms_channel_get_property_count(ch);
            for (int pi = 0; pi < cnprops; pi++) {
                ctdms_property prop;
                if (ctdms_channel_get_property(ch, pi, &prop) == CTDMS_OK) {
                    printf("        Property: %s\n", prop.name);
                }
            }
        }
    }

    ctdms_close(file);
    return 0;
}
