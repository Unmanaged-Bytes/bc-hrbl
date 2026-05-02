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
#include <sys/stat.h>
#include <unistd.h>
#include <cmocka.h>

static bc_allocators_context_t* make_memory(void)
{
    bc_allocators_context_config_t config;
    memset(&config, 0, sizeof(config));
    bc_allocators_context_t* memory = NULL;
    assert_true(bc_allocators_context_create(&config, &memory));
    return memory;
}

static void make_temp_path(char* buffer, size_t capacity, const char* slug)
{
    int written = snprintf(buffer, capacity, "/tmp/bc_hrbl_writer_finalize_%d_%s.hrbl", (int)getpid(), slug);
    assert_true(written > 0 && (size_t)written < capacity);
    (void)unlink(buffer);
}

static void test_finalize_to_file_succeeds_and_writes_buffer(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();
    bc_hrbl_writer_t* writer = NULL;
    assert_true(bc_hrbl_writer_create(memory, NULL, &writer));

    assert_true(bc_hrbl_writer_set_int64(writer, "value", 5u, 12345));

    char path[256];
    make_temp_path(path, sizeof(path), "ok");

    assert_true(bc_hrbl_writer_finalize_to_file(writer, path));

    struct stat statistics;
    assert_int_equal(stat(path, &statistics), 0);
    assert_true(statistics.st_size > 0);

    bc_hrbl_reader_t* reader = NULL;
    assert_true(bc_hrbl_reader_open(memory, path, &reader));
    bc_hrbl_value_ref_t value;
    int64_t loaded = 0;
    assert_true(bc_hrbl_reader_find(reader, "value", 5u, &value));
    assert_true(bc_hrbl_reader_get_int64(&value, &loaded));
    assert_true(loaded == 12345);
    bc_hrbl_reader_close(reader);

    (void)unlink(path);
    bc_hrbl_writer_destroy(writer);
    bc_allocators_context_destroy(memory);
}

static void test_finalize_to_file_fails_on_invalid_path(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();
    bc_hrbl_writer_t* writer = NULL;
    assert_true(bc_hrbl_writer_create(memory, NULL, &writer));

    assert_true(bc_hrbl_writer_set_int64(writer, "k", 1u, 1));

    assert_false(bc_hrbl_writer_finalize_to_file(writer, "/nonexistent_directory_42/file.hrbl"));

    bc_hrbl_writer_destroy(writer);
    bc_allocators_context_destroy(memory);
}

static void test_finalize_to_file_propagates_unclosed_scope(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();
    bc_hrbl_writer_t* writer = NULL;
    assert_true(bc_hrbl_writer_create(memory, NULL, &writer));

    assert_true(bc_hrbl_writer_begin_block(writer, "open", 4u));

    char path[256];
    make_temp_path(path, sizeof(path), "unclosed");
    assert_false(bc_hrbl_writer_finalize_to_file(writer, path));
    assert_int_equal((int)bc_hrbl_writer_last_error(writer), (int)BC_HRBL_WRITER_ERROR_UNCLOSED_SCOPE);

    struct stat statistics;
    assert_int_not_equal(stat(path, &statistics), 0);

    bc_hrbl_writer_destroy(writer);
    bc_allocators_context_destroy(memory);
}

static void test_finalize_to_file_propagates_construction_error(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();
    bc_hrbl_writer_t* writer = NULL;
    assert_true(bc_hrbl_writer_create(memory, NULL, &writer));

    assert_false(bc_hrbl_writer_end_block(writer));

    char path[256];
    make_temp_path(path, sizeof(path), "construction");
    assert_false(bc_hrbl_writer_finalize_to_file(writer, path));
    assert_int_equal((int)bc_hrbl_writer_last_error(writer), (int)BC_HRBL_WRITER_ERROR_CONSTRUCTION);

    bc_hrbl_writer_destroy(writer);
    bc_allocators_context_destroy(memory);
}

static void test_finalize_to_file_overwrites_existing(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();

    char path[256];
    make_temp_path(path, sizeof(path), "overwrite");

    {
        bc_hrbl_writer_t* writer = NULL;
        assert_true(bc_hrbl_writer_create(memory, NULL, &writer));
        assert_true(bc_hrbl_writer_set_int64(writer, "first", 5u, 1));
        assert_true(bc_hrbl_writer_finalize_to_file(writer, path));
        bc_hrbl_writer_destroy(writer);
    }

    {
        bc_hrbl_writer_t* writer = NULL;
        assert_true(bc_hrbl_writer_create(memory, NULL, &writer));
        assert_true(bc_hrbl_writer_set_int64(writer, "second", 6u, 2));
        assert_true(bc_hrbl_writer_finalize_to_file(writer, path));
        bc_hrbl_writer_destroy(writer);
    }

    bc_hrbl_reader_t* reader = NULL;
    assert_true(bc_hrbl_reader_open(memory, path, &reader));
    bc_hrbl_value_ref_t value;
    int64_t loaded = 0;
    assert_false(bc_hrbl_reader_find(reader, "first", 5u, &value));
    assert_true(bc_hrbl_reader_find(reader, "second", 6u, &value));
    assert_true(bc_hrbl_reader_get_int64(&value, &loaded));
    assert_true(loaded == 2);
    bc_hrbl_reader_close(reader);

    (void)unlink(path);
    bc_allocators_context_destroy(memory);
}

static void test_finalize_to_buffer_then_destroy_buffer_separately(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();
    bc_hrbl_writer_t* writer = NULL;
    assert_true(bc_hrbl_writer_create(memory, NULL, &writer));
    assert_true(bc_hrbl_writer_set_int64(writer, "k", 1u, 1));

    void* buffer = NULL;
    size_t size = 0u;
    assert_true(bc_hrbl_writer_finalize_to_buffer(writer, &buffer, &size));
    assert_non_null(buffer);
    assert_true(size > 0u);

    bc_hrbl_writer_destroy(writer);

    bc_hrbl_reader_t* reader = NULL;
    assert_true(bc_hrbl_reader_open_buffer(memory, buffer, size, &reader));
    bc_hrbl_value_ref_t value;
    int64_t loaded = 0;
    assert_true(bc_hrbl_reader_find(reader, "k", 1u, &value));
    assert_true(bc_hrbl_reader_get_int64(&value, &loaded));
    assert_true(loaded == 1);
    bc_hrbl_reader_close(reader);

    bc_hrbl_writer_free_buffer(memory, buffer);
    bc_allocators_context_destroy(memory);
}

static void test_finalize_to_file_with_complex_payload(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();
    bc_hrbl_writer_t* writer = NULL;
    assert_true(bc_hrbl_writer_create(memory, NULL, &writer));

    assert_true(bc_hrbl_writer_begin_block(writer, "config", 6u));
    assert_true(bc_hrbl_writer_set_string(writer, "host", 4u, "example.com", 11u));
    assert_true(bc_hrbl_writer_set_int64(writer, "port", 4u, 443));
    assert_true(bc_hrbl_writer_begin_array(writer, "tags", 4u));
    assert_true(bc_hrbl_writer_append_string(writer, "alpha", 5u));
    assert_true(bc_hrbl_writer_append_string(writer, "beta", 4u));
    assert_true(bc_hrbl_writer_append_string(writer, "gamma", 5u));
    assert_true(bc_hrbl_writer_end_array(writer));
    assert_true(bc_hrbl_writer_end_block(writer));

    char path[256];
    make_temp_path(path, sizeof(path), "complex");
    assert_true(bc_hrbl_writer_finalize_to_file(writer, path));

    bc_hrbl_reader_t* reader = NULL;
    assert_true(bc_hrbl_reader_open(memory, path, &reader));
    bc_hrbl_value_ref_t value;
    int64_t port_value = 0;
    assert_true(bc_hrbl_reader_find(reader, "config.port", 11u, &value));
    assert_true(bc_hrbl_reader_get_int64(&value, &port_value));
    assert_true(port_value == 443);

    const char* tag_value = NULL;
    size_t tag_length = 0u;
    assert_true(bc_hrbl_reader_find(reader, "config.tags[1]", 14u, &value));
    assert_true(bc_hrbl_reader_get_string(&value, &tag_value, &tag_length));
    assert_int_equal((int)tag_length, 4);
    assert_memory_equal(tag_value, "beta", 4u);

    bc_hrbl_reader_close(reader);
    (void)unlink(path);
    bc_hrbl_writer_destroy(writer);
    bc_allocators_context_destroy(memory);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_finalize_to_file_succeeds_and_writes_buffer),
        cmocka_unit_test(test_finalize_to_file_fails_on_invalid_path),
        cmocka_unit_test(test_finalize_to_file_propagates_unclosed_scope),
        cmocka_unit_test(test_finalize_to_file_propagates_construction_error),
        cmocka_unit_test(test_finalize_to_file_overwrites_existing),
        cmocka_unit_test(test_finalize_to_buffer_then_destroy_buffer_separately),
        cmocka_unit_test(test_finalize_to_file_with_complex_payload),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
