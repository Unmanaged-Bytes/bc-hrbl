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

static bc_allocators_context_t* make_memory(void)
{
    bc_allocators_context_config_t config;
    memset(&config, 0, sizeof(config));
    bc_allocators_context_t* memory = NULL;
    assert_true(bc_allocators_context_create(&config, &memory));
    return memory;
}

static void test_array_append_each_kind(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();

    bc_hrbl_writer_t* writer = NULL;
    assert_true(bc_hrbl_writer_create(memory, NULL, &writer));

    assert_true(bc_hrbl_writer_begin_array(writer, "mixed", 5u));
    assert_true(bc_hrbl_writer_append_null(writer));
    assert_true(bc_hrbl_writer_append_bool(writer, true));
    assert_true(bc_hrbl_writer_append_bool(writer, false));
    assert_true(bc_hrbl_writer_append_int64(writer, -123));
    assert_true(bc_hrbl_writer_append_uint64(writer, 9876543210u));
    assert_true(bc_hrbl_writer_append_float64(writer, 2.71828));
    assert_true(bc_hrbl_writer_append_string(writer, "alpha", 5u));
    assert_true(bc_hrbl_writer_append_string(writer, "", 0u));
    assert_true(bc_hrbl_writer_end_array(writer));

    void* buffer = NULL;
    size_t size = 0u;
    assert_true(bc_hrbl_writer_finalize_to_buffer(writer, &buffer, &size));
    assert_int_equal((int)bc_hrbl_verify_buffer(buffer, size), (int)BC_HRBL_VERIFY_OK);

    bc_hrbl_reader_t* reader = NULL;
    assert_true(bc_hrbl_reader_open_buffer(memory, buffer, size, &reader));

    bc_hrbl_value_ref_t value;
    bool boolean_value = false;
    int64_t int_value = 0;
    uint64_t uint_value = 0u;
    double float_value = 0.0;
    const char* string_value = NULL;
    size_t string_length = 0u;
    bc_hrbl_kind_t kind = BC_HRBL_KIND_NULL;

    assert_true(bc_hrbl_reader_find(reader, "mixed[0]", 8u, &value));
    assert_true(bc_hrbl_reader_value_kind(&value, &kind));
    assert_int_equal((int)kind, (int)BC_HRBL_KIND_NULL);

    assert_true(bc_hrbl_reader_find(reader, "mixed[1]", 8u, &value));
    assert_true(bc_hrbl_reader_get_bool(&value, &boolean_value));
    assert_true(boolean_value);

    assert_true(bc_hrbl_reader_find(reader, "mixed[2]", 8u, &value));
    assert_true(bc_hrbl_reader_get_bool(&value, &boolean_value));
    assert_false(boolean_value);

    assert_true(bc_hrbl_reader_find(reader, "mixed[3]", 8u, &value));
    assert_true(bc_hrbl_reader_get_int64(&value, &int_value));
    assert_true(int_value == -123);

    assert_true(bc_hrbl_reader_find(reader, "mixed[4]", 8u, &value));
    assert_true(bc_hrbl_reader_get_uint64(&value, &uint_value));
    assert_true(uint_value == 9876543210u);

    assert_true(bc_hrbl_reader_find(reader, "mixed[5]", 8u, &value));
    assert_true(bc_hrbl_reader_get_float64(&value, &float_value));
    assert_true(float_value > 2.71 && float_value < 2.72);

    assert_true(bc_hrbl_reader_find(reader, "mixed[6]", 8u, &value));
    assert_true(bc_hrbl_reader_get_string(&value, &string_value, &string_length));
    assert_int_equal((int)string_length, 5);
    assert_memory_equal(string_value, "alpha", 5u);

    assert_true(bc_hrbl_reader_find(reader, "mixed[7]", 8u, &value));
    assert_true(bc_hrbl_reader_get_string(&value, &string_value, &string_length));
    assert_int_equal((int)string_length, 0);

    bc_hrbl_reader_close(reader);
    bc_hrbl_writer_free_buffer(memory, buffer);
    bc_hrbl_writer_destroy(writer);
    bc_allocators_context_destroy(memory);
}

static void test_root_each_kind(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();
    bc_hrbl_writer_t* writer = NULL;
    assert_true(bc_hrbl_writer_create(memory, NULL, &writer));

    assert_true(bc_hrbl_writer_set_null(writer, "n", 1u));
    assert_true(bc_hrbl_writer_set_bool(writer, "t", 1u, true));
    assert_true(bc_hrbl_writer_set_bool(writer, "f", 1u, false));
    assert_true(bc_hrbl_writer_set_int64(writer, "i", 1u, INT64_MIN));
    assert_true(bc_hrbl_writer_set_int64(writer, "j", 1u, INT64_MAX));
    assert_true(bc_hrbl_writer_set_uint64(writer, "u", 1u, UINT64_MAX));
    assert_true(bc_hrbl_writer_set_float64(writer, "x", 1u, -1.5));
    assert_true(bc_hrbl_writer_set_string(writer, "s", 1u, "hello world", 11u));

    void* buffer = NULL;
    size_t size = 0u;
    assert_true(bc_hrbl_writer_finalize_to_buffer(writer, &buffer, &size));
    assert_int_equal((int)bc_hrbl_verify_buffer(buffer, size), (int)BC_HRBL_VERIFY_OK);

    bc_hrbl_reader_t* reader = NULL;
    assert_true(bc_hrbl_reader_open_buffer(memory, buffer, size, &reader));

    bc_hrbl_value_ref_t value;
    int64_t int_value = 0;
    uint64_t uint_value = 0u;
    double float_value = 0.0;

    assert_true(bc_hrbl_reader_find(reader, "i", 1u, &value));
    assert_true(bc_hrbl_reader_get_int64(&value, &int_value));
    assert_true(int_value == INT64_MIN);

    assert_true(bc_hrbl_reader_find(reader, "j", 1u, &value));
    assert_true(bc_hrbl_reader_get_int64(&value, &int_value));
    assert_true(int_value == INT64_MAX);

    assert_true(bc_hrbl_reader_find(reader, "u", 1u, &value));
    assert_true(bc_hrbl_reader_get_uint64(&value, &uint_value));
    assert_true(uint_value == UINT64_MAX);

    assert_true(bc_hrbl_reader_find(reader, "x", 1u, &value));
    assert_true(bc_hrbl_reader_get_float64(&value, &float_value));
    assert_true(float_value < -1.49 && float_value > -1.51);

    bc_hrbl_reader_close(reader);
    bc_hrbl_writer_free_buffer(memory, buffer);
    bc_hrbl_writer_destroy(writer);
    bc_allocators_context_destroy(memory);
}

static void test_block_with_each_kind_inside(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();
    bc_hrbl_writer_t* writer = NULL;
    assert_true(bc_hrbl_writer_create(memory, NULL, &writer));

    assert_true(bc_hrbl_writer_begin_block(writer, "block", 5u));
    assert_true(bc_hrbl_writer_set_null(writer, "n", 1u));
    assert_true(bc_hrbl_writer_set_bool(writer, "t", 1u, true));
    assert_true(bc_hrbl_writer_set_int64(writer, "i", 1u, 100));
    assert_true(bc_hrbl_writer_set_uint64(writer, "u", 1u, 200u));
    assert_true(bc_hrbl_writer_set_float64(writer, "x", 1u, 1.25));
    assert_true(bc_hrbl_writer_set_string(writer, "s", 1u, "test", 4u));
    assert_true(bc_hrbl_writer_end_block(writer));

    void* buffer = NULL;
    size_t size = 0u;
    assert_true(bc_hrbl_writer_finalize_to_buffer(writer, &buffer, &size));
    assert_int_equal((int)bc_hrbl_verify_buffer(buffer, size), (int)BC_HRBL_VERIFY_OK);

    bc_hrbl_writer_free_buffer(memory, buffer);
    bc_hrbl_writer_destroy(writer);
    bc_allocators_context_destroy(memory);
}

static void test_empty_array_at_root(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();
    bc_hrbl_writer_t* writer = NULL;
    assert_true(bc_hrbl_writer_create(memory, NULL, &writer));

    assert_true(bc_hrbl_writer_begin_array(writer, "empty", 5u));
    assert_true(bc_hrbl_writer_end_array(writer));

    void* buffer = NULL;
    size_t size = 0u;
    assert_true(bc_hrbl_writer_finalize_to_buffer(writer, &buffer, &size));
    assert_int_equal((int)bc_hrbl_verify_buffer(buffer, size), (int)BC_HRBL_VERIFY_OK);

    bc_hrbl_writer_free_buffer(memory, buffer);
    bc_hrbl_writer_destroy(writer);
    bc_allocators_context_destroy(memory);
}

static void test_empty_block_at_root(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();
    bc_hrbl_writer_t* writer = NULL;
    assert_true(bc_hrbl_writer_create(memory, NULL, &writer));

    assert_true(bc_hrbl_writer_begin_block(writer, "empty", 5u));
    assert_true(bc_hrbl_writer_end_block(writer));

    void* buffer = NULL;
    size_t size = 0u;
    assert_true(bc_hrbl_writer_finalize_to_buffer(writer, &buffer, &size));
    assert_int_equal((int)bc_hrbl_verify_buffer(buffer, size), (int)BC_HRBL_VERIFY_OK);

    bc_hrbl_writer_free_buffer(memory, buffer);
    bc_hrbl_writer_destroy(writer);
    bc_allocators_context_destroy(memory);
}

static void test_deeply_nested_blocks(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();
    bc_hrbl_writer_t* writer = NULL;
    assert_true(bc_hrbl_writer_create(memory, NULL, &writer));

    assert_true(bc_hrbl_writer_begin_block(writer, "level1", 6u));
    assert_true(bc_hrbl_writer_begin_block(writer, "level2", 6u));
    assert_true(bc_hrbl_writer_begin_block(writer, "level3", 6u));
    assert_true(bc_hrbl_writer_begin_block(writer, "level4", 6u));
    assert_true(bc_hrbl_writer_set_int64(writer, "leaf", 4u, 42));
    assert_true(bc_hrbl_writer_end_block(writer));
    assert_true(bc_hrbl_writer_end_block(writer));
    assert_true(bc_hrbl_writer_end_block(writer));
    assert_true(bc_hrbl_writer_end_block(writer));

    void* buffer = NULL;
    size_t size = 0u;
    assert_true(bc_hrbl_writer_finalize_to_buffer(writer, &buffer, &size));
    assert_int_equal((int)bc_hrbl_verify_buffer(buffer, size), (int)BC_HRBL_VERIFY_OK);

    bc_hrbl_reader_t* reader = NULL;
    assert_true(bc_hrbl_reader_open_buffer(memory, buffer, size, &reader));
    bc_hrbl_value_ref_t value;
    int64_t loaded = 0;
    assert_true(bc_hrbl_reader_find(reader, "level1.level2.level3.level4.leaf", 32u, &value));
    assert_true(bc_hrbl_reader_get_int64(&value, &loaded));
    assert_true(loaded == 42);
    bc_hrbl_reader_close(reader);

    bc_hrbl_writer_free_buffer(memory, buffer);
    bc_hrbl_writer_destroy(writer);
    bc_allocators_context_destroy(memory);
}

static void test_array_of_blocks(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();
    bc_hrbl_writer_t* writer = NULL;
    assert_true(bc_hrbl_writer_create(memory, NULL, &writer));

    assert_true(bc_hrbl_writer_begin_array(writer, "rows", 4u));
    for (int i = 0; i < 5; i++) {
        assert_true(bc_hrbl_writer_begin_block(writer, NULL, 0u));
        assert_true(bc_hrbl_writer_set_int64(writer, "id", 2u, i));
        assert_true(bc_hrbl_writer_set_int64(writer, "value", 5u, i * 10));
        assert_true(bc_hrbl_writer_end_block(writer));
    }
    assert_true(bc_hrbl_writer_end_array(writer));

    void* buffer = NULL;
    size_t size = 0u;
    assert_true(bc_hrbl_writer_finalize_to_buffer(writer, &buffer, &size));
    assert_int_equal((int)bc_hrbl_verify_buffer(buffer, size), (int)BC_HRBL_VERIFY_OK);

    bc_hrbl_reader_t* reader = NULL;
    assert_true(bc_hrbl_reader_open_buffer(memory, buffer, size, &reader));
    bc_hrbl_value_ref_t value;
    int64_t loaded = 0;
    assert_true(bc_hrbl_reader_find(reader, "rows[3].value", 13u, &value));
    assert_true(bc_hrbl_reader_get_int64(&value, &loaded));
    assert_true(loaded == 30);
    bc_hrbl_reader_close(reader);

    bc_hrbl_writer_free_buffer(memory, buffer);
    bc_hrbl_writer_destroy(writer);
    bc_allocators_context_destroy(memory);
}

static void test_array_of_arrays(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();
    bc_hrbl_writer_t* writer = NULL;
    assert_true(bc_hrbl_writer_create(memory, NULL, &writer));

    assert_true(bc_hrbl_writer_begin_array(writer, "matrix", 6u));
    for (int row = 0; row < 3; row++) {
        assert_true(bc_hrbl_writer_begin_array(writer, NULL, 0u));
        for (int col = 0; col < 4; col++) {
            assert_true(bc_hrbl_writer_append_int64(writer, row * 4 + col));
        }
        assert_true(bc_hrbl_writer_end_array(writer));
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

static void test_block_in_array_in_block_in_array(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();
    bc_hrbl_writer_t* writer = NULL;
    assert_true(bc_hrbl_writer_create(memory, NULL, &writer));

    assert_true(bc_hrbl_writer_begin_array(writer, "outer", 5u));
    assert_true(bc_hrbl_writer_begin_block(writer, NULL, 0u));
    assert_true(bc_hrbl_writer_begin_array(writer, "inner", 5u));
    assert_true(bc_hrbl_writer_begin_block(writer, NULL, 0u));
    assert_true(bc_hrbl_writer_set_int64(writer, "deep", 4u, 999));
    assert_true(bc_hrbl_writer_end_block(writer));
    assert_true(bc_hrbl_writer_end_array(writer));
    assert_true(bc_hrbl_writer_end_block(writer));
    assert_true(bc_hrbl_writer_end_array(writer));

    void* buffer = NULL;
    size_t size = 0u;
    assert_true(bc_hrbl_writer_finalize_to_buffer(writer, &buffer, &size));
    assert_int_equal((int)bc_hrbl_verify_buffer(buffer, size), (int)BC_HRBL_VERIFY_OK);

    bc_hrbl_reader_t* reader = NULL;
    assert_true(bc_hrbl_reader_open_buffer(memory, buffer, size, &reader));
    bc_hrbl_value_ref_t value;
    int64_t loaded = 0;
    assert_true(bc_hrbl_reader_find(reader, "outer[0].inner[0].deep", 22u, &value));
    assert_true(bc_hrbl_reader_get_int64(&value, &loaded));
    assert_true(loaded == 999);
    bc_hrbl_reader_close(reader);

    bc_hrbl_writer_free_buffer(memory, buffer);
    bc_hrbl_writer_destroy(writer);
    bc_allocators_context_destroy(memory);
}

static void test_float_special_values(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();
    bc_hrbl_writer_t* writer = NULL;
    assert_true(bc_hrbl_writer_create(memory, NULL, &writer));

    assert_true(bc_hrbl_writer_set_float64(writer, "zero", 4u, 0.0));
    assert_true(bc_hrbl_writer_set_float64(writer, "neg_zero", 8u, -0.0));
    assert_true(bc_hrbl_writer_set_float64(writer, "tiny", 4u, 1e-300));
    assert_true(bc_hrbl_writer_set_float64(writer, "huge", 4u, 1e300));

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
        cmocka_unit_test(test_array_append_each_kind),
        cmocka_unit_test(test_root_each_kind),
        cmocka_unit_test(test_block_with_each_kind_inside),
        cmocka_unit_test(test_empty_array_at_root),
        cmocka_unit_test(test_empty_block_at_root),
        cmocka_unit_test(test_deeply_nested_blocks),
        cmocka_unit_test(test_array_of_blocks),
        cmocka_unit_test(test_array_of_arrays),
        cmocka_unit_test(test_block_in_array_in_block_in_array),
        cmocka_unit_test(test_float_special_values),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
