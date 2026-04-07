/*
 * ctdms - Unit tests
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "ctdms/ctdms.h"
#include "ctdms_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static int tests_run = 0;
static int tests_passed = 0;

static int current_test_failed;

#define TEST(name) \
    do { \
        tests_run++; \
        current_test_failed = 0; \
        printf("  TEST: %-50s", #name); \
        name(); \
        if (!current_test_failed) { \
            tests_passed++; \
            printf(" PASS\n"); \
        } \
    } while (0)

#define ASSERT(cond) \
    do { \
        if (!(cond)) { \
            printf(" FAIL (%s:%d: %s)\n", __FILE__, __LINE__, #cond); \
            current_test_failed = 1; \
            return; \
        } \
    } while (0)

#define ASSERT_EQ(a, b) ASSERT((a) == (b))
#define ASSERT_NE(a, b) ASSERT((a) != (b))
#define ASSERT_STR_EQ(a, b) ASSERT(strcmp((a), (b)) == 0)

/* ---- Test: error strings ---- */
static void test_error_strings(void) {
    ASSERT_STR_EQ(ctdms_error_string(CTDMS_OK), "No error");
    ASSERT_STR_EQ(ctdms_error_string(CTDMS_ERR_NULL_ARG), "NULL argument");
    ASSERT_STR_EQ(ctdms_error_string(CTDMS_ERR_OPEN_FILE), "Failed to open file");
    ASSERT_NE(ctdms_error_string((ctdms_error)-999), NULL);
}

/* ---- Test: type sizes ---- */
static void test_type_sizes(void) {
    ASSERT_EQ(ctdms_type_size(CTDMS_TYPE_I8), 1);
    ASSERT_EQ(ctdms_type_size(CTDMS_TYPE_I16), 2);
    ASSERT_EQ(ctdms_type_size(CTDMS_TYPE_I32), 4);
    ASSERT_EQ(ctdms_type_size(CTDMS_TYPE_I64), 8);
    ASSERT_EQ(ctdms_type_size(CTDMS_TYPE_U8), 1);
    ASSERT_EQ(ctdms_type_size(CTDMS_TYPE_U16), 2);
    ASSERT_EQ(ctdms_type_size(CTDMS_TYPE_U32), 4);
    ASSERT_EQ(ctdms_type_size(CTDMS_TYPE_U64), 8);
    ASSERT_EQ(ctdms_type_size(CTDMS_TYPE_SINGLE_FLOAT), 4);
    ASSERT_EQ(ctdms_type_size(CTDMS_TYPE_DOUBLE_FLOAT), 8);
    ASSERT_EQ(ctdms_type_size(CTDMS_TYPE_BOOLEAN), 1);
    ASSERT_EQ(ctdms_type_size(CTDMS_TYPE_TIMESTAMP), 16);
    ASSERT_EQ(ctdms_type_size(CTDMS_TYPE_STRING), 0);
    ASSERT_EQ(ctdms_type_size(CTDMS_TYPE_VOID), 0);
}

/* ---- Test: NULL handling ---- */
static void test_null_handling(void) {
    ASSERT_EQ(ctdms_open(NULL), NULL);
    ctdms_close(NULL); /* should not crash */
    ASSERT_NE(ctdms_get_error(NULL), NULL);
    ASSERT_EQ(ctdms_get_group_count(NULL), 0);
    ASSERT_EQ(ctdms_get_group(NULL, 0), NULL);
    ASSERT_EQ(ctdms_get_group_by_name(NULL, "x"), NULL);
    ASSERT_EQ(ctdms_group_get_name(NULL), NULL);
    ASSERT_EQ(ctdms_group_get_channel_count(NULL), 0);
    ASSERT_EQ(ctdms_group_get_channel(NULL, 0), NULL);
    ASSERT_EQ(ctdms_channel_get_name(NULL), NULL);
    ASSERT_EQ(ctdms_channel_get_data_type(NULL), CTDMS_TYPE_VOID);
    ASSERT_EQ(ctdms_channel_get_num_values(NULL), 0);
}

/* ---- Test: open nonexistent file ---- */
static void test_open_nonexistent(void) {
    ctdms_file *f = ctdms_open("/nonexistent/file.tdms");
    ASSERT_EQ(f, NULL);
}

/* ---- Test: create and read a minimal TDMS file ---- */
static void write_u32_le(FILE *fp, uint32_t v) {
    uint8_t b[4] = { (uint8_t)(v), (uint8_t)(v >> 8),
                      (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
    fwrite(b, 1, 4, fp);
}

static void write_u64_le(FILE *fp, uint64_t v) {
    uint8_t b[8];
    for (int i = 0; i < 8; i++)
        b[i] = (uint8_t)(v >> (i * 8));
    fwrite(b, 1, 8, fp);
}

static void write_string(FILE *fp, const char *s) {
    uint32_t len = (uint32_t)strlen(s);
    write_u32_le(fp, len);
    fwrite(s, 1, len, fp);
}

/* Write lead-in with placeholder offsets; returns the file position of the
 * next_segment_offset field so it can be patched later. */
static long write_lead_in(FILE *fp, uint32_t toc) {
    fwrite("TDSm", 1, 4, fp);
    write_u32_le(fp, toc);
    write_u32_le(fp, 4713);
    long offset_pos = ftell(fp);
    write_u64_le(fp, 0); /* next_segment_offset placeholder */
    write_u64_le(fp, 0); /* raw_data_offset placeholder */
    return offset_pos;
}

/* Patch the lead-in offsets after writing meta and raw data.
 * seg_start: file position of the 'T' in "TDSm"
 * meta_end:  file position after all metadata
 * seg_end:   file position after all raw data
 */
static void patch_lead_in(FILE *fp, long seg_start, long meta_end,
                           long seg_end) {
    long lead_in_end = seg_start + 28; /* lead-in is always 28 bytes */
    uint64_t next_seg_offset = (uint64_t)(seg_end - lead_in_end);
    uint64_t raw_data_offset = (uint64_t)(meta_end - lead_in_end);
    long cur = ftell(fp);
    fseek(fp, seg_start + 12, SEEK_SET); /* skip tag(4)+toc(4)+version(4) */
    write_u64_le(fp, next_seg_offset);
    write_u64_le(fp, raw_data_offset);
    fseek(fp, cur, SEEK_SET);
}

static void test_read_minimal_tdms(void) {
    const char *tmpfile = "test_minimal.tdms";
    FILE *fp = fopen(tmpfile, "wb");
    ASSERT_NE(fp, NULL);

    /*
     * Build a minimal TDMS file with:
     * - One segment
     * - One group "Group" with one property (name="prop", value="value")
     * - One channel "Group"/"Channel1" with I32 data type, 3 values: 1, 2, 3
     */

    /* We'll build metadata in a buffer first to know its size */
    /* Metadata:
     * num_objects: 2
     * Obj 1: /'Group' - no raw data, 1 property (string "prop"="value")
     * Obj 2: /'Group'/'Channel1' - I32, dim=1, 3 values, 0 properties
     */

    /* Calculate metadata size manually */
    /* Object 1: path="/'Group'" (8 bytes + 4 len)
     *   raw_data_index: 0xFFFFFFFF (4)
     *   num_props: 1 (4)
     *   prop name: "prop" (4+4)
     *   prop type: 0x20 (4)
     *   prop value len: 5 (4)
     *   prop value: "value" (5)
     * = 4+8 + 4 + 4 + 4+4 + 4 + 4+5 = 41
     *
     * Object 2: path="/'Group'/'Channel1'" (19 bytes + 4 len)
     *   raw_data_index_len: 0x14 (4)
     *   data_type: 3 (I32) (4)
     *   dimension: 1 (4)
     *   num_values: 3 (8)
     *   num_props: 0 (4)
     * = 4+19 + 4 + 4+4+8 + 4 = 47
     */

    long seg_start = ftell(fp);
    write_lead_in(fp, 0x0E);

    /* Meta data */
    write_u32_le(fp, 2); /* num objects */

    /* Object 1: /'Group' */
    write_string(fp, "/'Group'");
    write_u32_le(fp, 0xFFFFFFFF); /* no raw data */
    write_u32_le(fp, 1); /* 1 property */
    write_string(fp, "prop");
    write_u32_le(fp, 0x20); /* tdsTypeString */
    write_string(fp, "value");

    /* Object 2: /'Group'/'Channel1' */
    write_string(fp, "/'Group'/'Channel1'");
    write_u32_le(fp, 0x14); /* index length = 20 */
    write_u32_le(fp, 0x03); /* tdsTypeI32 */
    write_u32_le(fp, 1);    /* dimension */
    write_u64_le(fp, 3);    /* num values */
    write_u32_le(fp, 0);    /* 0 properties */

    long meta_end = ftell(fp);

    /* Raw data: 1, 2, 3 as I32 LE */
    write_u32_le(fp, 1);
    write_u32_le(fp, 2);
    write_u32_le(fp, 3);

    long seg_end = ftell(fp);
    patch_lead_in(fp, seg_start, meta_end, seg_end);

    fclose(fp);

    /* Now read it back */
    ctdms_file *f = ctdms_open(tmpfile);
    ASSERT_NE(f, NULL);
    ASSERT_EQ(ctdms_get_error(f), NULL);

    /* Check groups */
    ASSERT_EQ(ctdms_get_group_count(f), 1);
    ctdms_group *g = ctdms_get_group(f, 0);
    ASSERT_NE(g, NULL);
    ASSERT_STR_EQ(ctdms_group_get_name(g), "Group");

    /* Check group properties */
    ASSERT_EQ(ctdms_group_get_property_count(g), 1);
    ctdms_property prop;
    ASSERT_EQ(ctdms_group_get_property(g, 0, &prop), CTDMS_OK);
    ASSERT_STR_EQ(prop.name, "prop");
    ASSERT_EQ(prop.type, CTDMS_TYPE_STRING);
    ASSERT_STR_EQ(prop.value.str, "value");

    /* Check group by name */
    ctdms_group *g2 = ctdms_get_group_by_name(f, "Group");
    ASSERT_EQ(g, g2);

    /* Check channels */
    ASSERT_EQ(ctdms_group_get_channel_count(g), 1);
    ctdms_channel *ch = ctdms_group_get_channel(g, 0);
    ASSERT_NE(ch, NULL);
    ASSERT_STR_EQ(ctdms_channel_get_name(ch), "Channel1");
    ASSERT_EQ(ctdms_channel_get_data_type(ch), CTDMS_TYPE_I32);
    ASSERT_EQ(ctdms_channel_get_num_values(ch), 3);

    /* Check channel by name */
    ctdms_channel *ch2 = ctdms_group_get_channel_by_name(g, "Channel1");
    ASSERT_EQ(ch, ch2);

    /* Read data */
    int32_t data[3] = {0};
    int64_t read = ctdms_channel_read_data(ch, data, 0, 3);
    ASSERT_EQ(read, 3);
    ASSERT_EQ(data[0], 1);
    ASSERT_EQ(data[1], 2);
    ASSERT_EQ(data[2], 3);

    /* Read partial data with offset */
    int32_t data2[2] = {0};
    read = ctdms_channel_read_data(ch, data2, 1, 2);
    ASSERT_EQ(read, 2);
    ASSERT_EQ(data2[0], 2);
    ASSERT_EQ(data2[1], 3);

    ctdms_close(f);
    remove(tmpfile);
}

/* ---- Test: multi-segment file ---- */
static void test_multi_segment(void) {
    const char *tmpfile = "test_multi.tdms";
    FILE *fp = fopen(tmpfile, "wb");
    ASSERT_NE(fp, NULL);

    /* Segment 1: channel with 2 I32 values: 10, 20 */
    long s1_start = ftell(fp);
    write_lead_in(fp, 0x0E);

    write_u32_le(fp, 1); /* 1 object */
    write_string(fp, "/'Group'/'Channel1'");
    write_u32_le(fp, 0x14); /* index len */
    write_u32_le(fp, 0x03); /* I32 */
    write_u32_le(fp, 1);
    write_u64_le(fp, 2);
    write_u32_le(fp, 0);

    long s1_meta_end = ftell(fp);
    write_u32_le(fp, 10);
    write_u32_le(fp, 20);
    long s1_end = ftell(fp);
    patch_lead_in(fp, s1_start, s1_meta_end, s1_end);

    /* Segment 2: same channel, 2 more values: 30, 40 */
    long s2_start = ftell(fp);
    write_lead_in(fp, 0x0E);

    write_u32_le(fp, 1);
    write_string(fp, "/'Group'/'Channel1'");
    write_u32_le(fp, 0x14);
    write_u32_le(fp, 0x03);
    write_u32_le(fp, 1);
    write_u64_le(fp, 2);
    write_u32_le(fp, 0);

    long s2_meta_end = ftell(fp);
    write_u32_le(fp, 30);
    write_u32_le(fp, 40);
    long s2_end = ftell(fp);
    patch_lead_in(fp, s2_start, s2_meta_end, s2_end);

    fclose(fp);

    ctdms_file *f = ctdms_open(tmpfile);
    ASSERT_NE(f, NULL);
    ASSERT_EQ(ctdms_get_error(f), NULL);

    ctdms_group *g = ctdms_get_group_by_name(f, "Group");
    ASSERT_NE(g, NULL);

    ctdms_channel *ch = ctdms_group_get_channel_by_name(g, "Channel1");
    ASSERT_NE(ch, NULL);
    ASSERT_EQ(ctdms_channel_get_num_values(ch), 4);

    int32_t data[4] = {0};
    int64_t count = ctdms_channel_read_data(ch, data, 0, 4);
    ASSERT_EQ(count, 4);
    ASSERT_EQ(data[0], 10);
    ASSERT_EQ(data[1], 20);
    ASSERT_EQ(data[2], 30);
    ASSERT_EQ(data[3], 40);

    ctdms_close(f);
    remove(tmpfile);
}

/* ---- Test: version info in header ---- */
static void test_version(void) {
    ASSERT_EQ(CTDMS_VERSION_MAJOR, 0);
    ASSERT_EQ(CTDMS_VERSION_MINOR, 1);
    ASSERT_EQ(CTDMS_VERSION_PATCH, 0);
}

/* ---- Test: multiple channels ---- */
static void test_two_channels(void) {
    const char *tmpfile = "test_two_ch.tdms";
    FILE *fp = fopen(tmpfile, "wb");
    ASSERT_NE(fp, NULL);

    /* One segment with two channels, 3 I32 values each */
    long seg_start = ftell(fp);
    write_lead_in(fp, 0x0E);

    write_u32_le(fp, 2);

    write_string(fp, "/'G'/'C1'");
    write_u32_le(fp, 0x14);
    write_u32_le(fp, 0x03); /* I32 */
    write_u32_le(fp, 1);
    write_u64_le(fp, 3);
    write_u32_le(fp, 0);

    write_string(fp, "/'G'/'C2'");
    write_u32_le(fp, 0x14);
    write_u32_le(fp, 0x03); /* I32 */
    write_u32_le(fp, 1);
    write_u64_le(fp, 3);
    write_u32_le(fp, 0);

    long meta_end = ftell(fp);

    /* Raw: C1 then C2 (contiguous/non-interleaved) */
    write_u32_le(fp, 1);
    write_u32_le(fp, 2);
    write_u32_le(fp, 3);
    write_u32_le(fp, 4);
    write_u32_le(fp, 5);
    write_u32_le(fp, 6);

    long seg_end = ftell(fp);
    patch_lead_in(fp, seg_start, meta_end, seg_end);

    fclose(fp);

    ctdms_file *f = ctdms_open(tmpfile);
    ASSERT_NE(f, NULL);

    ctdms_group *g = ctdms_get_group_by_name(f, "G");
    ASSERT_NE(g, NULL);
    ASSERT_EQ(ctdms_group_get_channel_count(g), 2);

    ctdms_channel *c1 = ctdms_group_get_channel_by_name(g, "C1");
    ctdms_channel *c2 = ctdms_group_get_channel_by_name(g, "C2");
    ASSERT_NE(c1, NULL);
    ASSERT_NE(c2, NULL);

    int32_t d1[3] = {0}, d2[3] = {0};
    ASSERT_EQ(ctdms_channel_read_data(c1, d1, 0, 3), 3);
    ASSERT_EQ(ctdms_channel_read_data(c2, d2, 0, 3), 3);

    ASSERT_EQ(d1[0], 1);
    ASSERT_EQ(d1[1], 2);
    ASSERT_EQ(d1[2], 3);
    ASSERT_EQ(d2[0], 4);
    ASSERT_EQ(d2[1], 5);
    ASSERT_EQ(d2[2], 6);

    ctdms_close(f);
    remove(tmpfile);
}

/* ---- Main ---- */
int main(void) {
    printf("ctdms tests:\n");

    TEST(test_error_strings);
    TEST(test_type_sizes);
    TEST(test_null_handling);
    TEST(test_open_nonexistent);
    TEST(test_version);
    TEST(test_read_minimal_tdms);
    TEST(test_multi_segment);
    TEST(test_two_channels);

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
