// SPDX-License-Identifier: MIT

#include "bc_hrbl.h"
#include "bc_allocators.h"

#include <bc/bc_core_io.h>

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cmocka.h>

#include <xxhash.h>

#define HRBL_HEADER_SIZE 128u
#define HRBL_FOOTER_SIZE 32u

static void hrbl_write_u16(uint8_t* b, size_t off, uint16_t v)
{
    memcpy(b + off, &v, sizeof(v));
}
static void hrbl_write_u32(uint8_t* b, size_t off, uint32_t v)
{
    memcpy(b + off, &v, sizeof(v));
}
static void hrbl_write_u64(uint8_t* b, size_t off, uint64_t v)
{
    memcpy(b + off, &v, sizeof(v));
}

static void hrbl_finalize(uint8_t* buffer, size_t file_size, uint64_t root_count, uint64_t root_index_size, uint64_t nodes_offset,
                          uint64_t nodes_size, uint64_t strings_offset, uint64_t strings_count, uint64_t strings_size,
                          uint64_t footer_offset)
{
    hrbl_write_u32(buffer, 0u, 0x4C425248u);
    hrbl_write_u16(buffer, 4u, 1u);
    hrbl_write_u16(buffer, 6u, 0u);
    hrbl_write_u64(buffer, 8u, file_size);
    hrbl_write_u64(buffer, 16u, (uint64_t)(0x1u | 0x2u | 0x4u | 0x8u));
    hrbl_write_u64(buffer, 24u, root_count);
    hrbl_write_u64(buffer, 32u, 128u);
    hrbl_write_u64(buffer, 40u, root_index_size);
    hrbl_write_u64(buffer, 48u, nodes_offset);
    hrbl_write_u64(buffer, 56u, nodes_size);
    hrbl_write_u64(buffer, 64u, strings_offset);
    hrbl_write_u64(buffer, 72u, strings_size);
    hrbl_write_u64(buffer, 80u, strings_count);
    hrbl_write_u64(buffer, 88u, footer_offset);

    size_t payload_length = (size_t)(footer_offset - 128u);
    uint64_t checksum = (uint64_t)XXH3_64bits(buffer + 128u, payload_length);
    hrbl_write_u64(buffer, 96u, checksum);

    hrbl_write_u64(buffer, footer_offset, checksum);
    hrbl_write_u64(buffer, footer_offset + 8u, file_size);
    hrbl_write_u32(buffer, footer_offset + 16u, 0x4C425248u);
}

static bc_allocators_context_t* make_memory(void)
{
    bc_allocators_context_config_t config;
    memset(&config, 0, sizeof(config));
    bc_allocators_context_t* memory = NULL;
    assert_true(bc_allocators_context_create(&config, &memory));
    return memory;
}

static char* capture_export(const bc_hrbl_reader_t* reader, const bc_hrbl_export_options_t* options)
{
    /* Use a buffer-only writer (no fd) so we can capture output for byte-equivalence assertions. */
    static char sink_buffer[65536];
    bc_core_writer_t writer;
    assert_true(bc_core_writer_init_buffer_only(&writer, sink_buffer, sizeof(sink_buffer)));
    bool ok = (options != NULL) ? bc_hrbl_export_json_ex(reader, &writer, options) : bc_hrbl_export_json(reader, &writer);
    assert_true(ok);
    const char* data = NULL;
    size_t length = 0u;
    assert_true(bc_core_writer_buffer_data(&writer, &data, &length));
    char* result = (char*)malloc(length + 1u);
    assert_non_null(result);
    if (length != 0u) {
        memcpy(result, data, length);
    }
    result[length] = '\0';
    bc_core_writer_destroy(&writer);
    return result;
}

static void test_export_empty_document(void** state)
{
    (void)state;
    uint8_t buffer[160];
    memset(buffer, 0, sizeof(buffer));
    hrbl_finalize(buffer, sizeof(buffer), 0u, 0u, 128u, 0u, 128u, 0u, 0u, 128u);

    bc_allocators_context_t* memory = make_memory();
    bc_hrbl_reader_t* reader = NULL;
    assert_true(bc_hrbl_reader_open_buffer(memory, buffer, sizeof(buffer), &reader));

    char* out = capture_export(reader, NULL);
    assert_string_equal(out, "{}\n");
    free(out);

    bc_hrbl_reader_close(reader);
    bc_allocators_context_destroy(memory);
}

static void test_export_single_int64_root(void** state)
{
    (void)state;
    const char key_name[] = "count";
    size_t key_length = sizeof(key_name) - 1u;

    size_t strings_entry_aligned = (sizeof(uint32_t) + key_length + 3u) & ~(size_t)3u;
    size_t strings_size = strings_entry_aligned;

    uint64_t nodes_offset = 128u + 24u;
    uint64_t kind_offset_relative = 7u;
    uint64_t body_offset_relative = 8u;
    uint64_t nodes_size = body_offset_relative + 8u;

    uint64_t strings_offset = nodes_offset + nodes_size;
    uint64_t footer_offset = strings_offset + strings_size;
    size_t file_size = (size_t)(footer_offset + HRBL_FOOTER_SIZE);

    uint8_t* buffer = calloc(file_size, 1u);
    assert_non_null(buffer);

    int64_t value_stored = -42LL;
    buffer[nodes_offset + kind_offset_relative] = (uint8_t)0x03;
    memcpy(&buffer[nodes_offset + body_offset_relative], &value_stored, sizeof(value_stored));

    uint32_t key_length_u32 = (uint32_t)key_length;
    memcpy(&buffer[strings_offset], &key_length_u32, sizeof(key_length_u32));
    memcpy(&buffer[strings_offset + sizeof(uint32_t)], key_name, key_length);

    uint64_t key_hash = (uint64_t)XXH3_64bits(key_name, key_length);
    uint64_t entry_offset = 128u;
    memcpy(&buffer[entry_offset], &key_hash, sizeof(key_hash));
    uint32_t key_pool_offset = (uint32_t)strings_offset;
    memcpy(&buffer[entry_offset + 8u], &key_pool_offset, sizeof(key_pool_offset));
    memcpy(&buffer[entry_offset + 12u], &key_length_u32, sizeof(key_length_u32));
    uint64_t value_offset = nodes_offset + kind_offset_relative;
    memcpy(&buffer[entry_offset + 16u], &value_offset, sizeof(value_offset));

    hrbl_finalize(buffer, file_size, 1u, 24u, nodes_offset, nodes_size, strings_offset, 1u, strings_size, footer_offset);

    bc_allocators_context_t* memory = make_memory();
    bc_hrbl_reader_t* reader = NULL;
    assert_true(bc_hrbl_reader_open_buffer(memory, buffer, file_size, &reader));

    bc_hrbl_export_options_t options;
    options.indent_spaces = 2u;
    options.sort_keys = true;
    options.ascii_only = false;
    char* out = capture_export(reader, &options);
    assert_string_equal(out, "{\n  \"count\": -42\n}\n");
    free(out);

    bc_hrbl_reader_close(reader);
    bc_allocators_context_destroy(memory);
    free(buffer);
}

static void test_export_compact_mode(void** state)
{
    (void)state;
    uint8_t buffer[160];
    memset(buffer, 0, sizeof(buffer));
    hrbl_finalize(buffer, sizeof(buffer), 0u, 0u, 128u, 0u, 128u, 0u, 0u, 128u);

    bc_allocators_context_t* memory = make_memory();
    bc_hrbl_reader_t* reader = NULL;
    assert_true(bc_hrbl_reader_open_buffer(memory, buffer, sizeof(buffer), &reader));

    bc_hrbl_export_options_t options;
    options.indent_spaces = 0u;
    options.sort_keys = true;
    options.ascii_only = false;
    char* out = capture_export(reader, &options);
    assert_string_equal(out, "{}");
    free(out);

    bc_hrbl_reader_close(reader);
    bc_allocators_context_destroy(memory);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_export_empty_document),
        cmocka_unit_test(test_export_single_int64_root),
        cmocka_unit_test(test_export_compact_mode),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
