// SPDX-License-Identifier: MIT

#include "bc_hrbl.h"
#include "bc_allocators.h"
#include "bc_allocators_pool.h"
#include "bc_allocators_arena.h"

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cmocka.h>

static int pool_allocate_call_count = 0;
static int pool_allocate_fail_on_call = 0;

bool __real_bc_allocators_pool_allocate(bc_allocators_context_t* ctx, size_t size, void** out_ptr);

bool __wrap_bc_allocators_pool_allocate(bc_allocators_context_t* ctx, size_t size, void** out_ptr)
{
    pool_allocate_call_count++;
    if (pool_allocate_fail_on_call > 0 && pool_allocate_call_count == pool_allocate_fail_on_call) {
        if (out_ptr != NULL) {
            *out_ptr = NULL;
        }
        return false;
    }
    return __real_bc_allocators_pool_allocate(ctx, size, out_ptr);
}

static int pool_reallocate_call_count = 0;
static int pool_reallocate_fail_on_call = 0;

bool __real_bc_allocators_pool_reallocate(bc_allocators_context_t* ctx, void* ptr, size_t new_size, void** out_ptr);

bool __wrap_bc_allocators_pool_reallocate(bc_allocators_context_t* ctx, void* ptr, size_t new_size, void** out_ptr)
{
    pool_reallocate_call_count++;
    if (pool_reallocate_fail_on_call > 0 && pool_reallocate_call_count == pool_reallocate_fail_on_call) {
        if (out_ptr != NULL) {
            *out_ptr = NULL;
        }
        return false;
    }
    return __real_bc_allocators_pool_reallocate(ctx, ptr, new_size, out_ptr);
}

static int arena_allocate_call_count = 0;
static int arena_allocate_fail_on_call = 0;

bool __real_bc_allocators_arena_allocate(bc_allocators_arena_t* arena, size_t size, size_t alignment, void** out_ptr);

bool __wrap_bc_allocators_arena_allocate(bc_allocators_arena_t* arena, size_t size, size_t alignment, void** out_ptr)
{
    arena_allocate_call_count++;
    if (arena_allocate_fail_on_call > 0 && arena_allocate_call_count == arena_allocate_fail_on_call) {
        if (out_ptr != NULL) {
            *out_ptr = NULL;
        }
        return false;
    }
    return __real_bc_allocators_arena_allocate(arena, size, alignment, out_ptr);
}

static void reset_wraps(void)
{
    pool_allocate_call_count = 0;
    pool_allocate_fail_on_call = 0;
    pool_reallocate_call_count = 0;
    pool_reallocate_fail_on_call = 0;
    arena_allocate_call_count = 0;
    arena_allocate_fail_on_call = 0;
}

static bc_allocators_context_t* make_memory(void)
{
    bc_allocators_context_config_t config;
    memset(&config, 0, sizeof(config));
    bc_allocators_context_t* memory = NULL;
    assert_true(bc_allocators_context_create(&config, &memory));
    return memory;
}

static void test_writer_create_fails_when_pool_allocate_fails(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();
    reset_wraps();

    pool_allocate_fail_on_call = 1;
    bc_hrbl_writer_t* writer = NULL;
    assert_false(bc_hrbl_writer_create(memory, NULL, &writer));
    assert_null(writer);

    reset_wraps();
    bc_allocators_context_destroy(memory);
}

static void test_arena_clone_fails_when_arena_allocate_fails(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();
    reset_wraps();

    bc_hrbl_writer_t* writer = NULL;
    assert_true(bc_hrbl_writer_create(memory, NULL, &writer));

    arena_allocate_fail_on_call = arena_allocate_call_count + 1;
    assert_false(bc_hrbl_writer_set_string(writer, "key", 3u, "value", 5u));

    reset_wraps();
    bc_hrbl_writer_destroy(writer);
    bc_allocators_context_destroy(memory);
}

static void test_arena_clone_fails_for_key(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();
    reset_wraps();

    bc_hrbl_writer_t* writer = NULL;
    assert_true(bc_hrbl_writer_create(memory, NULL, &writer));

    int call_count_before = arena_allocate_call_count;
    arena_allocate_fail_on_call = call_count_before + 2;
    assert_false(bc_hrbl_writer_set_int64(writer, "key", 3u, 1));

    reset_wraps();
    bc_hrbl_writer_destroy(writer);
    bc_allocators_context_destroy(memory);
}

static void test_alloc_node_fails_marks_error_flag(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();
    reset_wraps();

    bc_hrbl_writer_t* writer = NULL;
    assert_true(bc_hrbl_writer_create(memory, NULL, &writer));

    arena_allocate_fail_on_call = arena_allocate_call_count + 1;
    assert_false(bc_hrbl_writer_set_int64(writer, "key", 3u, 1));

    reset_wraps();

    void* buffer = NULL;
    size_t size = 0u;
    assert_false(bc_hrbl_writer_finalize_to_buffer(writer, &buffer, &size));
    assert_int_equal((int)bc_hrbl_writer_last_error(writer), (int)BC_HRBL_WRITER_ERROR_CONSTRUCTION);

    bc_hrbl_writer_destroy(writer);
    bc_allocators_context_destroy(memory);
}

static void test_finalize_oom_fail_at_each_pool_allocate(void** state)
{
    (void)state;

    for (int fail_index = 1; fail_index <= 30; fail_index++) {
        bc_allocators_context_t* memory = make_memory();
        reset_wraps();

        bc_hrbl_writer_t* writer = NULL;
        if (!bc_hrbl_writer_create(memory, NULL, &writer)) {
            bc_allocators_context_destroy(memory);
            continue;
        }

        bool setup_success = bc_hrbl_writer_set_int64(writer, "key1", 4u, 1);
        if (setup_success) {
            setup_success = bc_hrbl_writer_set_string(writer, "key2", 4u, "value", 5u);
        }
        if (setup_success) {
            setup_success = bc_hrbl_writer_begin_block(writer, "nested", 6u);
        }
        if (setup_success) {
            setup_success = bc_hrbl_writer_set_int64(writer, "inner", 5u, 42);
        }
        if (setup_success) {
            setup_success = bc_hrbl_writer_end_block(writer);
        }

        if (!setup_success) {
            bc_hrbl_writer_destroy(writer);
            bc_allocators_context_destroy(memory);
            continue;
        }

        int baseline = pool_allocate_call_count;
        pool_allocate_fail_on_call = baseline + fail_index;

        void* buffer = NULL;
        size_t size = 0u;
        bool finalized = bc_hrbl_writer_finalize_to_buffer(writer, &buffer, &size);
        if (finalized) {
            bc_hrbl_writer_free_buffer(memory, buffer);
        } else {
            assert_null(buffer);
            assert_int_equal((int)size, 0);
        }

        reset_wraps();
        bc_hrbl_writer_destroy(writer);
        bc_allocators_context_destroy(memory);
    }
}

static void test_finalize_oom_with_large_block(void** state)
{
    (void)state;

    for (int fail_index = 1; fail_index <= 20; fail_index++) {
        bc_allocators_context_t* memory = make_memory();
        reset_wraps();

        bc_hrbl_writer_t* writer = NULL;
        if (!bc_hrbl_writer_create(memory, NULL, &writer)) {
            bc_allocators_context_destroy(memory);
            continue;
        }

        bool setup_success = bc_hrbl_writer_begin_block(writer, "big", 3u);
        char key[16];
        for (int i = 0; setup_success && i < 100; i++) {
            int key_length = snprintf(key, sizeof(key), "k%03d", i);
            setup_success = bc_hrbl_writer_set_int64(writer, key, (size_t)key_length, i);
        }
        if (setup_success) {
            setup_success = bc_hrbl_writer_end_block(writer);
        }

        if (!setup_success) {
            bc_hrbl_writer_destroy(writer);
            bc_allocators_context_destroy(memory);
            continue;
        }

        int baseline = pool_allocate_call_count;
        pool_allocate_fail_on_call = baseline + fail_index;

        void* buffer = NULL;
        size_t size = 0u;
        bool finalized = bc_hrbl_writer_finalize_to_buffer(writer, &buffer, &size);
        if (finalized) {
            bc_hrbl_writer_free_buffer(memory, buffer);
        }

        reset_wraps();
        bc_hrbl_writer_destroy(writer);
        bc_allocators_context_destroy(memory);
    }
}

static void test_finalize_realloc_failure_during_growth(void** state)
{
    (void)state;

    for (int fail_index = 1; fail_index <= 10; fail_index++) {
        bc_allocators_context_t* memory = make_memory();
        reset_wraps();

        bc_hrbl_writer_t* writer = NULL;
        if (!bc_hrbl_writer_create(memory, NULL, &writer)) {
            bc_allocators_context_destroy(memory);
            continue;
        }

        bool setup_success = true;
        char key[16];
        char value[64];
        for (int i = 0; setup_success && i < 200; i++) {
            int key_length = snprintf(key, sizeof(key), "key_%04d", i);
            int value_length = snprintf(value, sizeof(value), "unique-string-value-%05d-content", i);
            setup_success = bc_hrbl_writer_set_string(writer, key, (size_t)key_length, value, (size_t)value_length);
        }

        if (!setup_success) {
            bc_hrbl_writer_destroy(writer);
            bc_allocators_context_destroy(memory);
            continue;
        }

        pool_reallocate_fail_on_call = fail_index;

        void* buffer = NULL;
        size_t size = 0u;
        bool finalized = bc_hrbl_writer_finalize_to_buffer(writer, &buffer, &size);
        if (finalized) {
            bc_hrbl_writer_free_buffer(memory, buffer);
        }

        reset_wraps();
        bc_hrbl_writer_destroy(writer);
        bc_allocators_context_destroy(memory);
    }
}

static void test_set_string_arena_clone_value_failure(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();
    reset_wraps();

    bc_hrbl_writer_t* writer = NULL;
    assert_true(bc_hrbl_writer_create(memory, NULL, &writer));

    int call_count_before = arena_allocate_call_count;
    arena_allocate_fail_on_call = call_count_before + 3;
    assert_false(bc_hrbl_writer_set_string(writer, "key", 3u, "value", 5u));

    reset_wraps();

    void* buffer = NULL;
    size_t size = 0u;
    assert_false(bc_hrbl_writer_finalize_to_buffer(writer, &buffer, &size));
    assert_int_equal((int)bc_hrbl_writer_last_error(writer), (int)BC_HRBL_WRITER_ERROR_CONSTRUCTION);

    bc_hrbl_writer_destroy(writer);
    bc_allocators_context_destroy(memory);
}

static void test_oom_during_array_append_string(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();
    reset_wraps();

    bc_hrbl_writer_t* writer = NULL;
    assert_true(bc_hrbl_writer_create(memory, NULL, &writer));
    assert_true(bc_hrbl_writer_begin_array(writer, "items", 5u));

    int call_count_before = arena_allocate_call_count;
    arena_allocate_fail_on_call = call_count_before + 2;
    assert_false(bc_hrbl_writer_append_string(writer, "value", 5u));

    reset_wraps();
    bc_hrbl_writer_destroy(writer);
    bc_allocators_context_destroy(memory);
}

static void test_oom_during_begin_block(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();
    reset_wraps();

    bc_hrbl_writer_t* writer = NULL;
    assert_true(bc_hrbl_writer_create(memory, NULL, &writer));

    int call_count_before = arena_allocate_call_count;
    arena_allocate_fail_on_call = call_count_before + 1;
    assert_false(bc_hrbl_writer_begin_block(writer, "config", 6u));

    reset_wraps();
    bc_hrbl_writer_destroy(writer);
    bc_allocators_context_destroy(memory);
}

static void test_oom_during_begin_array(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();
    reset_wraps();

    bc_hrbl_writer_t* writer = NULL;
    assert_true(bc_hrbl_writer_create(memory, NULL, &writer));

    int call_count_before = arena_allocate_call_count;
    arena_allocate_fail_on_call = call_count_before + 2;
    assert_false(bc_hrbl_writer_begin_array(writer, "items", 5u));

    reset_wraps();
    bc_hrbl_writer_destroy(writer);
    bc_allocators_context_destroy(memory);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_writer_create_fails_when_pool_allocate_fails),
        cmocka_unit_test(test_arena_clone_fails_when_arena_allocate_fails),
        cmocka_unit_test(test_arena_clone_fails_for_key),
        cmocka_unit_test(test_alloc_node_fails_marks_error_flag),
        cmocka_unit_test(test_finalize_oom_fail_at_each_pool_allocate),
        cmocka_unit_test(test_finalize_oom_with_large_block),
        cmocka_unit_test(test_finalize_realloc_failure_during_growth),
        cmocka_unit_test(test_set_string_arena_clone_value_failure),
        cmocka_unit_test(test_oom_during_array_append_string),
        cmocka_unit_test(test_oom_during_begin_block),
        cmocka_unit_test(test_oom_during_begin_array),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
