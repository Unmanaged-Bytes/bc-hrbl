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

static bc_hrbl_writer_t* fresh_writer(bc_allocators_context_t* memory)
{
    bc_hrbl_writer_t* writer = NULL;
    assert_true(bc_hrbl_writer_create(memory, NULL, &writer));
    return writer;
}

static void test_set_string_value_length_overflow_marks_error(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();
    bc_hrbl_writer_t* writer = fresh_writer(memory);

    const size_t too_long = (size_t)UINT32_MAX + (size_t)1;
    assert_false(bc_hrbl_writer_set_string(writer, "key", 3u, "x", too_long));

    void* buffer = NULL;
    size_t size = 0u;
    assert_false(bc_hrbl_writer_finalize_to_buffer(writer, &buffer, &size));
    assert_int_equal((int)bc_hrbl_writer_last_error(writer), (int)BC_HRBL_WRITER_ERROR_CONSTRUCTION);

    bc_hrbl_writer_destroy(writer);
    bc_allocators_context_destroy(memory);
}

static void test_append_string_value_length_overflow_marks_error(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();
    bc_hrbl_writer_t* writer = fresh_writer(memory);

    assert_true(bc_hrbl_writer_begin_array(writer, "values", 6u));
    const size_t too_long = (size_t)UINT32_MAX + (size_t)1;
    assert_false(bc_hrbl_writer_append_string(writer, "x", too_long));

    void* buffer = NULL;
    size_t size = 0u;
    assert_false(bc_hrbl_writer_finalize_to_buffer(writer, &buffer, &size));
    assert_int_equal((int)bc_hrbl_writer_last_error(writer), (int)BC_HRBL_WRITER_ERROR_CONSTRUCTION);

    bc_hrbl_writer_destroy(writer);
    bc_allocators_context_destroy(memory);
}

static void test_set_int64_inside_array_violates_scope(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();
    bc_hrbl_writer_t* writer = fresh_writer(memory);

    assert_true(bc_hrbl_writer_begin_array(writer, "items", 5u));
    assert_false(bc_hrbl_writer_set_int64(writer, "key", 3u, 42));

    void* buffer = NULL;
    size_t size = 0u;
    assert_false(bc_hrbl_writer_finalize_to_buffer(writer, &buffer, &size));
    assert_int_equal((int)bc_hrbl_writer_last_error(writer), (int)BC_HRBL_WRITER_ERROR_CONSTRUCTION);

    bc_hrbl_writer_destroy(writer);
    bc_allocators_context_destroy(memory);
}

static void test_append_int64_inside_block_violates_scope(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();
    bc_hrbl_writer_t* writer = fresh_writer(memory);

    assert_true(bc_hrbl_writer_begin_block(writer, "config", 6u));
    assert_false(bc_hrbl_writer_append_int64(writer, 42));

    bc_hrbl_writer_destroy(writer);
    bc_allocators_context_destroy(memory);
}

static void test_append_int64_at_root_violates_scope(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();
    bc_hrbl_writer_t* writer = fresh_writer(memory);

    assert_false(bc_hrbl_writer_append_int64(writer, 42));

    bc_hrbl_writer_destroy(writer);
    bc_allocators_context_destroy(memory);
}

static void test_set_keys_inside_array_each_kind(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();
    bc_hrbl_writer_t* writer = fresh_writer(memory);

    assert_true(bc_hrbl_writer_begin_array(writer, "a", 1u));
    assert_false(bc_hrbl_writer_set_null(writer, "k", 1u));

    bc_hrbl_writer_destroy(writer);
    bc_allocators_context_destroy(memory);

    memory = make_memory();
    writer = fresh_writer(memory);
    assert_true(bc_hrbl_writer_begin_array(writer, "a", 1u));
    assert_false(bc_hrbl_writer_set_bool(writer, "k", 1u, true));
    bc_hrbl_writer_destroy(writer);
    bc_allocators_context_destroy(memory);

    memory = make_memory();
    writer = fresh_writer(memory);
    assert_true(bc_hrbl_writer_begin_array(writer, "a", 1u));
    assert_false(bc_hrbl_writer_set_uint64(writer, "k", 1u, 1u));
    bc_hrbl_writer_destroy(writer);
    bc_allocators_context_destroy(memory);

    memory = make_memory();
    writer = fresh_writer(memory);
    assert_true(bc_hrbl_writer_begin_array(writer, "a", 1u));
    assert_false(bc_hrbl_writer_set_float64(writer, "k", 1u, 1.0));
    bc_hrbl_writer_destroy(writer);
    bc_allocators_context_destroy(memory);

    memory = make_memory();
    writer = fresh_writer(memory);
    assert_true(bc_hrbl_writer_begin_array(writer, "a", 1u));
    assert_false(bc_hrbl_writer_set_string(writer, "k", 1u, "v", 1u));
    bc_hrbl_writer_destroy(writer);
    bc_allocators_context_destroy(memory);
}

static void test_append_kinds_at_root_each_violate_scope(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();
    bc_hrbl_writer_t* writer = fresh_writer(memory);

    assert_false(bc_hrbl_writer_append_null(writer));
    bc_hrbl_writer_destroy(writer);
    bc_allocators_context_destroy(memory);

    memory = make_memory();
    writer = fresh_writer(memory);
    assert_false(bc_hrbl_writer_append_bool(writer, true));
    bc_hrbl_writer_destroy(writer);
    bc_allocators_context_destroy(memory);

    memory = make_memory();
    writer = fresh_writer(memory);
    assert_false(bc_hrbl_writer_append_uint64(writer, 1u));
    bc_hrbl_writer_destroy(writer);
    bc_allocators_context_destroy(memory);

    memory = make_memory();
    writer = fresh_writer(memory);
    assert_false(bc_hrbl_writer_append_float64(writer, 3.14));
    bc_hrbl_writer_destroy(writer);
    bc_allocators_context_destroy(memory);

    memory = make_memory();
    writer = fresh_writer(memory);
    assert_false(bc_hrbl_writer_append_string(writer, "v", 1u));
    bc_hrbl_writer_destroy(writer);
    bc_allocators_context_destroy(memory);
}

static void test_end_block_at_root_returns_false(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();
    bc_hrbl_writer_t* writer = fresh_writer(memory);

    assert_false(bc_hrbl_writer_end_block(writer));

    bc_hrbl_writer_destroy(writer);
    bc_allocators_context_destroy(memory);
}

static void test_end_array_at_root_returns_false(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();
    bc_hrbl_writer_t* writer = fresh_writer(memory);

    assert_false(bc_hrbl_writer_end_array(writer));

    bc_hrbl_writer_destroy(writer);
    bc_allocators_context_destroy(memory);
}

static void test_end_block_on_array_scope_mismatch(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();
    bc_hrbl_writer_t* writer = fresh_writer(memory);

    assert_true(bc_hrbl_writer_begin_array(writer, "list", 4u));
    assert_false(bc_hrbl_writer_end_block(writer));

    bc_hrbl_writer_destroy(writer);
    bc_allocators_context_destroy(memory);
}

static void test_end_array_on_block_scope_mismatch(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();
    bc_hrbl_writer_t* writer = fresh_writer(memory);

    assert_true(bc_hrbl_writer_begin_block(writer, "config", 6u));
    assert_false(bc_hrbl_writer_end_array(writer));

    bc_hrbl_writer_destroy(writer);
    bc_allocators_context_destroy(memory);
}

static void test_begin_block_inside_array_with_key_fails(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();
    bc_hrbl_writer_t* writer = fresh_writer(memory);

    assert_true(bc_hrbl_writer_begin_array(writer, "items", 5u));
    assert_false(bc_hrbl_writer_begin_block(writer, "key", 3u));

    bc_hrbl_writer_destroy(writer);
    bc_allocators_context_destroy(memory);
}

static void test_begin_array_inside_array_with_key_fails(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();
    bc_hrbl_writer_t* writer = fresh_writer(memory);

    assert_true(bc_hrbl_writer_begin_array(writer, "items", 5u));
    assert_false(bc_hrbl_writer_begin_array(writer, "nested", 6u));

    bc_hrbl_writer_destroy(writer);
    bc_allocators_context_destroy(memory);
}

static void test_begin_block_inside_array_without_key_succeeds(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();
    bc_hrbl_writer_t* writer = fresh_writer(memory);

    assert_true(bc_hrbl_writer_begin_array(writer, "rows", 4u));
    assert_true(bc_hrbl_writer_begin_block(writer, NULL, 0u));
    assert_true(bc_hrbl_writer_set_int64(writer, "col", 3u, 1));
    assert_true(bc_hrbl_writer_end_block(writer));
    assert_true(bc_hrbl_writer_end_array(writer));

    void* buffer = NULL;
    size_t size = 0u;
    assert_true(bc_hrbl_writer_finalize_to_buffer(writer, &buffer, &size));
    assert_int_equal((int)bc_hrbl_verify_buffer(buffer, size), (int)BC_HRBL_VERIFY_OK);

    bc_hrbl_writer_free_buffer(memory, buffer);
    bc_hrbl_writer_destroy(writer);
    bc_allocators_context_destroy(memory);
}

static void test_begin_array_inside_array_without_key_succeeds(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();
    bc_hrbl_writer_t* writer = fresh_writer(memory);

    assert_true(bc_hrbl_writer_begin_array(writer, "matrix", 6u));
    assert_true(bc_hrbl_writer_begin_array(writer, NULL, 0u));
    assert_true(bc_hrbl_writer_append_int64(writer, 1));
    assert_true(bc_hrbl_writer_append_int64(writer, 2));
    assert_true(bc_hrbl_writer_end_array(writer));
    assert_true(bc_hrbl_writer_begin_array(writer, NULL, 0u));
    assert_true(bc_hrbl_writer_append_int64(writer, 3));
    assert_true(bc_hrbl_writer_end_array(writer));
    assert_true(bc_hrbl_writer_end_array(writer));

    void* buffer = NULL;
    size_t size = 0u;
    assert_true(bc_hrbl_writer_finalize_to_buffer(writer, &buffer, &size));
    assert_int_equal((int)bc_hrbl_verify_buffer(buffer, size), (int)BC_HRBL_VERIFY_OK);

    bc_hrbl_writer_free_buffer(memory, buffer);
    bc_hrbl_writer_destroy(writer);
    bc_allocators_context_destroy(memory);
}

static void test_begin_block_in_block_null_key_with_length_fails(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();
    bc_hrbl_writer_t* writer = fresh_writer(memory);

    assert_true(bc_hrbl_writer_begin_block(writer, "outer", 5u));
    assert_false(bc_hrbl_writer_begin_block(writer, NULL, 5u));

    bc_hrbl_writer_destroy(writer);
    bc_allocators_context_destroy(memory);
}

static void test_begin_block_at_root_null_key_with_length_fails(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();
    bc_hrbl_writer_t* writer = fresh_writer(memory);

    assert_false(bc_hrbl_writer_begin_block(writer, NULL, 5u));

    bc_hrbl_writer_destroy(writer);
    bc_allocators_context_destroy(memory);
}

static void test_set_string_with_null_key_and_length_fails(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();
    bc_hrbl_writer_t* writer = fresh_writer(memory);

    assert_false(bc_hrbl_writer_set_string(writer, NULL, 3u, "value", 5u));

    bc_hrbl_writer_destroy(writer);
    bc_allocators_context_destroy(memory);
}

static void test_post_error_subsequent_calls_all_fail(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();
    bc_hrbl_writer_t* writer = fresh_writer(memory);

    assert_false(bc_hrbl_writer_end_block(writer));

    assert_false(bc_hrbl_writer_set_int64(writer, "k", 1u, 1));
    assert_false(bc_hrbl_writer_set_uint64(writer, "k", 1u, 1u));
    assert_false(bc_hrbl_writer_set_bool(writer, "k", 1u, true));
    assert_false(bc_hrbl_writer_set_float64(writer, "k", 1u, 1.0));
    assert_false(bc_hrbl_writer_set_null(writer, "k", 1u));
    assert_false(bc_hrbl_writer_set_string(writer, "k", 1u, "v", 1u));
    assert_false(bc_hrbl_writer_begin_block(writer, "b", 1u));
    assert_false(bc_hrbl_writer_begin_array(writer, "a", 1u));
    assert_false(bc_hrbl_writer_end_block(writer));
    assert_false(bc_hrbl_writer_end_array(writer));

    bc_hrbl_writer_destroy(writer);
    bc_allocators_context_destroy(memory);
}

static void test_error_string_each_code(void** state)
{
    (void)state;
    assert_string_equal(bc_hrbl_writer_error_string(BC_HRBL_WRITER_OK), "ok");
    assert_string_equal(bc_hrbl_writer_error_string(BC_HRBL_WRITER_ERROR_OOM), "out of memory");
    assert_string_equal(bc_hrbl_writer_error_string(BC_HRBL_WRITER_ERROR_UNCLOSED_SCOPE), "unclosed block scope at finalize");
    assert_string_equal(bc_hrbl_writer_error_string(BC_HRBL_WRITER_ERROR_CONSTRUCTION),
                        "writer error flag set during construction");
    assert_string_equal(bc_hrbl_writer_error_string(BC_HRBL_WRITER_ERROR_STRING_LENGTH_OVERFLOW_4GB),
                        "string length exceeds 4 GiB");
    assert_string_equal(bc_hrbl_writer_error_string(BC_HRBL_WRITER_ERROR_STRING_POOL_OVERFLOW_4GB),
                        "string pool buffer exceeds 4 GiB");
    assert_string_equal(bc_hrbl_writer_error_string(BC_HRBL_WRITER_ERROR_FILE_SIZE_OVERFLOW_4GB), "file size exceeds 4 GiB");
    assert_string_equal(bc_hrbl_writer_error_string(BC_HRBL_WRITER_ERROR_INVALID_ARGUMENT), "invalid argument");
    assert_string_equal(bc_hrbl_writer_error_string(BC_HRBL_WRITER_ERROR_INTERNAL), "internal error");
    assert_string_equal(bc_hrbl_writer_error_string((bc_hrbl_writer_error_t)9999), "unknown error");
}

static void test_unclosed_scope_at_finalize_with_block(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();
    bc_hrbl_writer_t* writer = fresh_writer(memory);

    assert_true(bc_hrbl_writer_begin_block(writer, "open", 4u));
    assert_true(bc_hrbl_writer_set_int64(writer, "x", 1u, 1));

    void* buffer = NULL;
    size_t size = 0u;
    assert_false(bc_hrbl_writer_finalize_to_buffer(writer, &buffer, &size));
    assert_int_equal((int)bc_hrbl_writer_last_error(writer), (int)BC_HRBL_WRITER_ERROR_UNCLOSED_SCOPE);

    bc_hrbl_writer_destroy(writer);
    bc_allocators_context_destroy(memory);
}

static void test_unclosed_scope_at_finalize_with_array(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();
    bc_hrbl_writer_t* writer = fresh_writer(memory);

    assert_true(bc_hrbl_writer_begin_array(writer, "items", 5u));
    assert_true(bc_hrbl_writer_append_int64(writer, 1));
    assert_true(bc_hrbl_writer_append_int64(writer, 2));

    void* buffer = NULL;
    size_t size = 0u;
    assert_false(bc_hrbl_writer_finalize_to_buffer(writer, &buffer, &size));
    assert_int_equal((int)bc_hrbl_writer_last_error(writer), (int)BC_HRBL_WRITER_ERROR_UNCLOSED_SCOPE);

    bc_hrbl_writer_destroy(writer);
    bc_allocators_context_destroy(memory);
}

static void test_unclosed_scope_deeply_nested(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();
    bc_hrbl_writer_t* writer = fresh_writer(memory);

    assert_true(bc_hrbl_writer_begin_block(writer, "a", 1u));
    assert_true(bc_hrbl_writer_begin_block(writer, "b", 1u));
    assert_true(bc_hrbl_writer_begin_array(writer, "c", 1u));
    assert_true(bc_hrbl_writer_append_int64(writer, 1));

    void* buffer = NULL;
    size_t size = 0u;
    assert_false(bc_hrbl_writer_finalize_to_buffer(writer, &buffer, &size));
    assert_int_equal((int)bc_hrbl_writer_last_error(writer), (int)BC_HRBL_WRITER_ERROR_UNCLOSED_SCOPE);

    bc_hrbl_writer_destroy(writer);
    bc_allocators_context_destroy(memory);
}

static void test_options_explicit_zero_workers(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();

    bc_hrbl_writer_options_t options;
    options.worker_count = 0u;
    options.deduplicate_strings = true;

    bc_hrbl_writer_t* writer = NULL;
    assert_true(bc_hrbl_writer_create(memory, &options, &writer));
    assert_true(bc_hrbl_writer_set_int64(writer, "k", 1u, 1));

    void* buffer = NULL;
    size_t size = 0u;
    assert_true(bc_hrbl_writer_finalize_to_buffer(writer, &buffer, &size));
    assert_int_equal((int)bc_hrbl_verify_buffer(buffer, size), (int)BC_HRBL_VERIFY_OK);

    bc_hrbl_writer_free_buffer(memory, buffer);
    bc_hrbl_writer_destroy(writer);
    bc_allocators_context_destroy(memory);
}

static void test_options_dedup_disabled(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();

    bc_hrbl_writer_options_t options;
    options.worker_count = 0u;
    options.deduplicate_strings = false;

    bc_hrbl_writer_t* writer = NULL;
    assert_true(bc_hrbl_writer_create(memory, &options, &writer));
    assert_true(bc_hrbl_writer_set_string(writer, "a", 1u, "value", 5u));
    assert_true(bc_hrbl_writer_set_string(writer, "b", 1u, "value", 5u));

    void* buffer = NULL;
    size_t size = 0u;
    assert_true(bc_hrbl_writer_finalize_to_buffer(writer, &buffer, &size));
    assert_int_equal((int)bc_hrbl_verify_buffer(buffer, size), (int)BC_HRBL_VERIFY_OK);

    bc_hrbl_writer_free_buffer(memory, buffer);
    bc_hrbl_writer_destroy(writer);
    bc_allocators_context_destroy(memory);
}

static void test_empty_string_key_succeeds(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();
    bc_hrbl_writer_t* writer = fresh_writer(memory);

    assert_true(bc_hrbl_writer_set_int64(writer, "", 0u, 7));

    void* buffer = NULL;
    size_t size = 0u;
    assert_true(bc_hrbl_writer_finalize_to_buffer(writer, &buffer, &size));
    assert_int_equal((int)bc_hrbl_verify_buffer(buffer, size), (int)BC_HRBL_VERIFY_OK);

    bc_hrbl_writer_free_buffer(memory, buffer);
    bc_hrbl_writer_destroy(writer);
    bc_allocators_context_destroy(memory);
}

static void test_empty_string_value_succeeds(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();
    bc_hrbl_writer_t* writer = fresh_writer(memory);

    assert_true(bc_hrbl_writer_set_string(writer, "k", 1u, "", 0u));

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
        cmocka_unit_test(test_set_string_value_length_overflow_marks_error),
        cmocka_unit_test(test_append_string_value_length_overflow_marks_error),
        cmocka_unit_test(test_set_int64_inside_array_violates_scope),
        cmocka_unit_test(test_append_int64_inside_block_violates_scope),
        cmocka_unit_test(test_append_int64_at_root_violates_scope),
        cmocka_unit_test(test_set_keys_inside_array_each_kind),
        cmocka_unit_test(test_append_kinds_at_root_each_violate_scope),
        cmocka_unit_test(test_end_block_at_root_returns_false),
        cmocka_unit_test(test_end_array_at_root_returns_false),
        cmocka_unit_test(test_end_block_on_array_scope_mismatch),
        cmocka_unit_test(test_end_array_on_block_scope_mismatch),
        cmocka_unit_test(test_begin_block_inside_array_with_key_fails),
        cmocka_unit_test(test_begin_array_inside_array_with_key_fails),
        cmocka_unit_test(test_begin_block_inside_array_without_key_succeeds),
        cmocka_unit_test(test_begin_array_inside_array_without_key_succeeds),
        cmocka_unit_test(test_begin_block_in_block_null_key_with_length_fails),
        cmocka_unit_test(test_begin_block_at_root_null_key_with_length_fails),
        cmocka_unit_test(test_set_string_with_null_key_and_length_fails),
        cmocka_unit_test(test_post_error_subsequent_calls_all_fail),
        cmocka_unit_test(test_error_string_each_code),
        cmocka_unit_test(test_unclosed_scope_at_finalize_with_block),
        cmocka_unit_test(test_unclosed_scope_at_finalize_with_array),
        cmocka_unit_test(test_unclosed_scope_deeply_nested),
        cmocka_unit_test(test_options_explicit_zero_workers),
        cmocka_unit_test(test_options_dedup_disabled),
        cmocka_unit_test(test_empty_string_key_succeeds),
        cmocka_unit_test(test_empty_string_value_succeeds),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
