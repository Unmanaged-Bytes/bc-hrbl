// SPDX-License-Identifier: MIT

#include "bc_hrbl.h"
#include "bc_allocators.h"

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cmocka.h>

static bc_allocators_context_t* make_memory(void)
{
    bc_allocators_context_config_t config;
    memset(&config, 0, sizeof(config));
    bc_allocators_context_t* memory = NULL;
    assert_true(bc_allocators_context_create(&config, &memory));
    return memory;
}

static void test_string_pool_dedup_repeated_value(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();
    bc_hrbl_writer_t* writer = NULL;
    assert_true(bc_hrbl_writer_create(memory, NULL, &writer));

    assert_true(bc_hrbl_writer_begin_array(writer, "ids", 3u));
    for (int i = 0; i < 50; i++) {
        assert_true(bc_hrbl_writer_append_string(writer, "shared", 6u));
    }
    assert_true(bc_hrbl_writer_end_array(writer));

    void* buffer = NULL;
    size_t size = 0u;
    assert_true(bc_hrbl_writer_finalize_to_buffer(writer, &buffer, &size));
    assert_int_equal((int)bc_hrbl_verify_buffer(buffer, size), (int)BC_HRBL_VERIFY_OK);

    bc_hrbl_writer_free_buffer(memory, buffer);
    bc_hrbl_writer_destroy(writer);
    bc_allocators_context_destroy(memory);
}

static void test_string_pool_many_unique_values_grows_slots(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();
    bc_hrbl_writer_t* writer = NULL;
    assert_true(bc_hrbl_writer_create(memory, NULL, &writer));

    char key[16];
    char value[32];
    for (int i = 0; i < 1000; i++) {
        int key_length = snprintf(key, sizeof(key), "k%d", i);
        int value_length = snprintf(value, sizeof(value), "value-%d-text", i);
        assert_true(bc_hrbl_writer_set_string(writer, key, (size_t)key_length, value, (size_t)value_length));
    }

    void* buffer = NULL;
    size_t size = 0u;
    assert_true(bc_hrbl_writer_finalize_to_buffer(writer, &buffer, &size));
    assert_int_equal((int)bc_hrbl_verify_buffer(buffer, size), (int)BC_HRBL_VERIFY_OK);

    bc_hrbl_writer_free_buffer(memory, buffer);
    bc_hrbl_writer_destroy(writer);
    bc_allocators_context_destroy(memory);
}

static void test_block_with_many_keys_triggers_radix_sort_serial(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();
    bc_hrbl_writer_t* writer = NULL;
    assert_true(bc_hrbl_writer_create(memory, NULL, &writer));

    assert_true(bc_hrbl_writer_begin_block(writer, "wide", 4u));
    char key[16];
    for (int i = 0; i < 500; i++) {
        int key_length = snprintf(key, sizeof(key), "field_%04d", i);
        assert_true(bc_hrbl_writer_set_int64(writer, key, (size_t)key_length, i));
    }
    assert_true(bc_hrbl_writer_end_block(writer));

    void* buffer = NULL;
    size_t size = 0u;
    assert_true(bc_hrbl_writer_finalize_to_buffer(writer, &buffer, &size));
    assert_int_equal((int)bc_hrbl_verify_buffer(buffer, size), (int)BC_HRBL_VERIFY_OK);

    bc_hrbl_reader_t* reader = NULL;
    assert_true(bc_hrbl_reader_open_buffer(memory, buffer, size, &reader));
    bc_hrbl_value_ref_t value;
    int64_t loaded = 0;
    assert_true(bc_hrbl_reader_find(reader, "wide.field_0042", 15u, &value));
    assert_true(bc_hrbl_reader_get_int64(&value, &loaded));
    assert_true(loaded == 42);
    assert_true(bc_hrbl_reader_find(reader, "wide.field_0499", 15u, &value));
    assert_true(bc_hrbl_reader_get_int64(&value, &loaded));
    assert_true(loaded == 499);
    bc_hrbl_reader_close(reader);

    bc_hrbl_writer_free_buffer(memory, buffer);
    bc_hrbl_writer_destroy(writer);
    bc_allocators_context_destroy(memory);
}

static void test_root_with_many_keys(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();
    bc_hrbl_writer_t* writer = NULL;
    assert_true(bc_hrbl_writer_create(memory, NULL, &writer));

    char key[16];
    for (int i = 0; i < 200; i++) {
        int key_length = snprintf(key, sizeof(key), "k%04d", i);
        assert_true(bc_hrbl_writer_set_uint64(writer, key, (size_t)key_length, (uint64_t)i));
    }

    void* buffer = NULL;
    size_t size = 0u;
    assert_true(bc_hrbl_writer_finalize_to_buffer(writer, &buffer, &size));
    assert_int_equal((int)bc_hrbl_verify_buffer(buffer, size), (int)BC_HRBL_VERIFY_OK);

    bc_hrbl_reader_t* reader = NULL;
    assert_true(bc_hrbl_reader_open_buffer(memory, buffer, size, &reader));
    uint64_t count = 0u;
    assert_true(bc_hrbl_reader_root_count(reader, &count));
    assert_true(count == 200u);
    bc_hrbl_reader_close(reader);

    bc_hrbl_writer_free_buffer(memory, buffer);
    bc_hrbl_writer_destroy(writer);
    bc_allocators_context_destroy(memory);
}

static void test_parallel_radix_sort_path_with_workers(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();

    bc_hrbl_writer_options_t options;
    options.worker_count = 4u;
    options.deduplicate_strings = true;

    bc_hrbl_writer_t* writer = NULL;
    assert_true(bc_hrbl_writer_create(memory, &options, &writer));

    assert_true(bc_hrbl_writer_begin_block(writer, "huge", 4u));
    char key[20];
    const int total = 12000;
    for (int i = 0; i < total; i++) {
        int key_length = snprintf(key, sizeof(key), "k%08d", i);
        assert_true(bc_hrbl_writer_set_int64(writer, key, (size_t)key_length, i));
    }
    assert_true(bc_hrbl_writer_end_block(writer));

    void* buffer = NULL;
    size_t size = 0u;
    assert_true(bc_hrbl_writer_finalize_to_buffer(writer, &buffer, &size));
    assert_int_equal((int)bc_hrbl_verify_buffer(buffer, size), (int)BC_HRBL_VERIFY_OK);

    bc_hrbl_reader_t* reader = NULL;
    assert_true(bc_hrbl_reader_open_buffer(memory, buffer, size, &reader));
    bc_hrbl_value_ref_t value;
    int64_t loaded = 0;
    assert_true(bc_hrbl_reader_find(reader, "huge.k00006789", 14u, &value));
    assert_true(bc_hrbl_reader_get_int64(&value, &loaded));
    assert_true(loaded == 6789);
    bc_hrbl_reader_close(reader);

    bc_hrbl_writer_free_buffer(memory, buffer);
    bc_hrbl_writer_destroy(writer);
    bc_allocators_context_destroy(memory);
}

static void test_buffer_growth_via_large_strings(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();
    bc_hrbl_writer_t* writer = NULL;
    assert_true(bc_hrbl_writer_create(memory, NULL, &writer));

    char large_value[8192];
    for (size_t i = 0; i < sizeof(large_value); i++) {
        large_value[i] = (char)('a' + (i % 26));
    }

    char key[16];
    for (int i = 0; i < 30; i++) {
        int key_length = snprintf(key, sizeof(key), "big_%d", i);
        assert_true(bc_hrbl_writer_set_string(writer, key, (size_t)key_length, large_value, sizeof(large_value)));
    }

    void* buffer = NULL;
    size_t size = 0u;
    assert_true(bc_hrbl_writer_finalize_to_buffer(writer, &buffer, &size));
    assert_int_equal((int)bc_hrbl_verify_buffer(buffer, size), (int)BC_HRBL_VERIFY_OK);

    bc_hrbl_writer_free_buffer(memory, buffer);
    bc_hrbl_writer_destroy(writer);
    bc_allocators_context_destroy(memory);
}

static void test_array_with_many_elements(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();
    bc_hrbl_writer_t* writer = NULL;
    assert_true(bc_hrbl_writer_create(memory, NULL, &writer));

    assert_true(bc_hrbl_writer_begin_array(writer, "long", 4u));
    for (int i = 0; i < 5000; i++) {
        assert_true(bc_hrbl_writer_append_int64(writer, i));
    }
    assert_true(bc_hrbl_writer_end_array(writer));

    void* buffer = NULL;
    size_t size = 0u;
    assert_true(bc_hrbl_writer_finalize_to_buffer(writer, &buffer, &size));
    assert_int_equal((int)bc_hrbl_verify_buffer(buffer, size), (int)BC_HRBL_VERIFY_OK);

    bc_hrbl_writer_free_buffer(memory, buffer);
    bc_hrbl_writer_destroy(writer);
    bc_allocators_context_destroy(memory);
}

static void test_string_pool_collision_distinct_values_same_hash_low_bits(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();
    bc_hrbl_writer_t* writer = NULL;
    assert_true(bc_hrbl_writer_create(memory, NULL, &writer));

    char value[32];
    for (int i = 0; i < 400; i++) {
        int value_length = snprintf(value, sizeof(value), "uniq-%05d", i);
        char key[16];
        int key_length = snprintf(key, sizeof(key), "k%d", i);
        assert_true(bc_hrbl_writer_set_string(writer, key, (size_t)key_length, value, (size_t)value_length));
    }

    void* buffer = NULL;
    size_t size = 0u;
    assert_true(bc_hrbl_writer_finalize_to_buffer(writer, &buffer, &size));
    assert_int_equal((int)bc_hrbl_verify_buffer(buffer, size), (int)BC_HRBL_VERIFY_OK);

    bc_hrbl_writer_free_buffer(memory, buffer);
    bc_hrbl_writer_destroy(writer);
    bc_allocators_context_destroy(memory);
}

static void test_repeated_keys_with_different_values_pool_dedup(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();
    bc_hrbl_writer_t* writer = NULL;
    assert_true(bc_hrbl_writer_create(memory, NULL, &writer));

    assert_true(bc_hrbl_writer_begin_array(writer, "rows", 4u));
    for (int i = 0; i < 100; i++) {
        assert_true(bc_hrbl_writer_begin_block(writer, NULL, 0u));
        assert_true(bc_hrbl_writer_set_string(writer, "name", 4u, "row", 3u));
        assert_true(bc_hrbl_writer_set_string(writer, "type", 4u, "shared", 6u));
        assert_true(bc_hrbl_writer_set_int64(writer, "index", 5u, i));
        assert_true(bc_hrbl_writer_end_block(writer));
    }
    assert_true(bc_hrbl_writer_end_array(writer));

    void* buffer = NULL;
    size_t size = 0u;
    assert_true(bc_hrbl_writer_finalize_to_buffer(writer, &buffer, &size));
    assert_int_equal((int)bc_hrbl_verify_buffer(buffer, size), (int)BC_HRBL_VERIFY_OK);

    bc_hrbl_writer_free_buffer(memory, buffer);
    bc_hrbl_writer_destroy(writer);
    bc_allocators_context_destroy(memory);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_string_pool_dedup_repeated_value),
        cmocka_unit_test(test_string_pool_many_unique_values_grows_slots),
        cmocka_unit_test(test_block_with_many_keys_triggers_radix_sort_serial),
        cmocka_unit_test(test_root_with_many_keys),
        cmocka_unit_test(test_parallel_radix_sort_path_with_workers),
        cmocka_unit_test(test_buffer_growth_via_large_strings),
        cmocka_unit_test(test_array_with_many_elements),
        cmocka_unit_test(test_string_pool_collision_distinct_values_same_hash_low_bits),
        cmocka_unit_test(test_repeated_keys_with_different_values_pool_dedup),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
