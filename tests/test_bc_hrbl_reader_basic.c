// SPDX-License-Identifier: MIT

#include "bc_hrbl.h"
#include "bc_allocators.h"

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <cmocka.h>

#include <xxhash.h>

#define HRBL_HEADER_SIZE 128u
#define HRBL_FOOTER_SIZE 32u

static void hrbl_write_u16(uint8_t* buffer, size_t offset, uint16_t value)
{
    memcpy(buffer + offset, &value, sizeof(value));
}

static void hrbl_write_u32(uint8_t* buffer, size_t offset, uint32_t value)
{
    memcpy(buffer + offset, &value, sizeof(value));
}

static void hrbl_write_u64(uint8_t* buffer, size_t offset, uint64_t value)
{
    memcpy(buffer + offset, &value, sizeof(value));
}

static void hrbl_finalize_header_and_footer(uint8_t* buffer, size_t file_size, uint64_t root_count, uint64_t root_index_size,
                                            uint64_t nodes_offset, uint64_t nodes_size, uint64_t strings_offset, uint64_t strings_count,
                                            uint64_t strings_size, uint64_t footer_offset)
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

static void test_reader_opens_empty_document(void** state)
{
    (void)state;
    uint8_t buffer[160];
    memset(buffer, 0, sizeof(buffer));
    hrbl_finalize_header_and_footer(buffer, sizeof(buffer), 0u, 0u, 128u, 0u, 128u, 0u, 0u, 128u);

    assert_int_equal((int)bc_hrbl_verify_buffer(buffer, sizeof(buffer)), (int)BC_HRBL_VERIFY_OK);

    bc_allocators_context_t* memory = make_memory();
    bc_hrbl_reader_t* reader = NULL;
    assert_true(bc_hrbl_reader_open_buffer(memory, buffer, sizeof(buffer), &reader));

    uint64_t count = 42u;
    assert_true(bc_hrbl_reader_root_count(reader, &count));
    assert_int_equal((int)count, 0);

    bc_hrbl_value_ref_t value;
    assert_false(bc_hrbl_reader_find(reader, "missing", 7u, &value));

    bc_hrbl_reader_close(reader);
    bc_allocators_context_destroy(memory);
}

static void test_reader_opens_int64_root(void** state)
{
    (void)state;
    const char key_name[] = "count";
    size_t key_length = sizeof(key_name) - 1u;

    size_t strings_string_length = sizeof(uint32_t) + key_length;
    size_t strings_entry_aligned = (strings_string_length + 3u) & ~(size_t)3u;
    size_t strings_size = strings_entry_aligned;

    size_t node_alignment = 8u;
    uint64_t nodes_offset = 128u + 24u;
    uint64_t kind_offset_relative = (1u + (node_alignment - 1u)) & ~(node_alignment - 1u);
    kind_offset_relative -= 1u;
    uint64_t body_offset_relative = kind_offset_relative + 1u;
    body_offset_relative = (body_offset_relative + (node_alignment - 1u)) & ~(node_alignment - 1u);
    uint64_t nodes_size = body_offset_relative + 8u;

    uint64_t strings_offset = nodes_offset + nodes_size;
    uint64_t footer_offset = strings_offset + strings_size;
    size_t file_size = (size_t)(footer_offset + HRBL_FOOTER_SIZE);

    uint8_t* buffer = calloc(file_size, 1u);
    assert_non_null(buffer);

    int64_t value_stored = -123456789LL;
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

    hrbl_finalize_header_and_footer(buffer, file_size, 1u, 24u, nodes_offset, nodes_size, strings_offset, 1u, strings_size, footer_offset);

    assert_int_equal((int)bc_hrbl_verify_buffer(buffer, file_size), (int)BC_HRBL_VERIFY_OK);

    bc_allocators_context_t* memory = make_memory();
    bc_hrbl_reader_t* reader = NULL;
    assert_true(bc_hrbl_reader_open_buffer(memory, buffer, file_size, &reader));

    uint64_t count = 0u;
    assert_true(bc_hrbl_reader_root_count(reader, &count));
    assert_int_equal((int)count, 1);

    bc_hrbl_value_ref_t value;
    assert_true(bc_hrbl_reader_find(reader, "count", 5u, &value));

    bc_hrbl_kind_t kind = BC_HRBL_KIND_NULL;
    assert_true(bc_hrbl_reader_value_kind(&value, &kind));
    assert_int_equal((int)kind, (int)BC_HRBL_KIND_INT64);

    int64_t loaded = 0;
    assert_true(bc_hrbl_reader_get_int64(&value, &loaded));
    assert_true(loaded == value_stored);

    assert_false(bc_hrbl_reader_find(reader, "missing", 7u, &value));

    bc_hrbl_reader_close(reader);
    bc_allocators_context_destroy(memory);
    free(buffer);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_reader_opens_empty_document),
        cmocka_unit_test(test_reader_opens_int64_root),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
