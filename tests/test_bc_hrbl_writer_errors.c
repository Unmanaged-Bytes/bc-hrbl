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

static void test_last_error_initial_state_ok(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();

    bc_hrbl_writer_t* writer = NULL;
    assert_true(bc_hrbl_writer_create(memory, NULL, &writer));

    assert_int_equal((int)bc_hrbl_writer_last_error(writer), (int)BC_HRBL_WRITER_OK);
    assert_int_equal((int)bc_hrbl_writer_last_error(writer), (int)BC_HRBL_WRITER_ERROR_NONE);

    bc_hrbl_writer_destroy(writer);
    bc_allocators_context_destroy(memory);
}

static void test_last_error_unclosed_scope(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();

    bc_hrbl_writer_t* writer = NULL;
    assert_true(bc_hrbl_writer_create(memory, NULL, &writer));

    assert_true(bc_hrbl_writer_begin_block(writer, "open", 4u));

    void* buffer = NULL;
    size_t size = 0u;
    assert_false(bc_hrbl_writer_finalize_to_buffer(writer, &buffer, &size));
    assert_int_equal((int)bc_hrbl_writer_last_error(writer), (int)BC_HRBL_WRITER_ERROR_UNCLOSED_SCOPE);
    assert_null(buffer);
    assert_int_equal((int)size, 0);

    bc_hrbl_writer_destroy(writer);
    bc_allocators_context_destroy(memory);
}

static void test_last_error_invalid_argument(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();

    bc_hrbl_writer_t* writer = NULL;
    assert_true(bc_hrbl_writer_create(memory, NULL, &writer));

    size_t size = 0u;
    assert_false(bc_hrbl_writer_finalize_to_buffer(writer, NULL, &size));
    assert_int_equal((int)bc_hrbl_writer_last_error(writer), (int)BC_HRBL_WRITER_ERROR_INVALID_ARGUMENT);

    void* buffer = NULL;
    assert_false(bc_hrbl_writer_finalize_to_buffer(writer, &buffer, NULL));
    assert_int_equal((int)bc_hrbl_writer_last_error(writer), (int)BC_HRBL_WRITER_ERROR_INVALID_ARGUMENT);

    bc_hrbl_writer_destroy(writer);
    bc_allocators_context_destroy(memory);
}

static void test_last_error_null_writer(void** state)
{
    (void)state;
    assert_int_equal((int)bc_hrbl_writer_last_error(NULL), (int)BC_HRBL_WRITER_ERROR_INVALID_ARGUMENT);
}

static void test_last_error_construction_invalid_scope(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();

    bc_hrbl_writer_t* writer = NULL;
    assert_true(bc_hrbl_writer_create(memory, NULL, &writer));

    assert_false(bc_hrbl_writer_end_block(writer));

    void* buffer = NULL;
    size_t size = 0u;
    assert_false(bc_hrbl_writer_finalize_to_buffer(writer, &buffer, &size));
    assert_int_equal((int)bc_hrbl_writer_last_error(writer), (int)BC_HRBL_WRITER_ERROR_CONSTRUCTION);

    bc_hrbl_writer_destroy(writer);
    bc_allocators_context_destroy(memory);
}

static void test_last_error_ok_after_successful_finalize(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();

    bc_hrbl_writer_t* writer = NULL;
    assert_true(bc_hrbl_writer_create(memory, NULL, &writer));

    assert_true(bc_hrbl_writer_set_int64(writer, "k", 1u, 7));

    void* buffer = NULL;
    size_t size = 0u;
    assert_true(bc_hrbl_writer_finalize_to_buffer(writer, &buffer, &size));
    assert_int_equal((int)bc_hrbl_writer_last_error(writer), (int)BC_HRBL_WRITER_OK);

    bc_hrbl_writer_free_buffer(memory, buffer);
    bc_hrbl_writer_destroy(writer);
    bc_allocators_context_destroy(memory);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_last_error_initial_state_ok),           cmocka_unit_test(test_last_error_unclosed_scope),
        cmocka_unit_test(test_last_error_invalid_argument),           cmocka_unit_test(test_last_error_null_writer),
        cmocka_unit_test(test_last_error_construction_invalid_scope), cmocka_unit_test(test_last_error_ok_after_successful_finalize),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
