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

static bc_allocators_context_t* make_memory(void)
{
    bc_allocators_context_config_t config;
    memset(&config, 0, sizeof(config));
    bc_allocators_context_t* memory = NULL;
    assert_true(bc_allocators_context_create(&config, &memory));
    return memory;
}

static void test_writer_roundtrip_empty(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();

    bc_hrbl_writer_t* writer = NULL;
    assert_true(bc_hrbl_writer_create(memory, &writer));

    void* buffer = NULL;
    size_t size = 0u;
    assert_true(bc_hrbl_writer_finalize_to_buffer(writer, &buffer, &size));
    assert_non_null(buffer);
    assert_int_equal((int)size, 160);

    assert_int_equal((int)bc_hrbl_verify_buffer(buffer, size), (int)BC_HRBL_VERIFY_OK);

    bc_hrbl_reader_t* reader = NULL;
    assert_true(bc_hrbl_reader_open_buffer(memory, buffer, size, &reader));
    uint64_t count = 42u;
    assert_true(bc_hrbl_reader_root_count(reader, &count));
    assert_int_equal((int)count, 0);

    bc_hrbl_reader_destroy(reader);
    bc_hrbl_writer_destroy(writer);

    static char sink_empty[1024];
    bc_core_writer_t writer_empty;
    assert_true(bc_core_writer_init_buffer_only(&writer_empty, sink_empty, sizeof(sink_empty)));
    bc_hrbl_reader_t* reader_again = NULL;
    assert_true(bc_hrbl_reader_open_buffer(memory, buffer, size, &reader_again));
    assert_true(bc_hrbl_export_json(reader_again, &writer_empty));
    const char* empty_data = NULL;
    size_t empty_length = 0u;
    assert_true(bc_core_writer_buffer_data(&writer_empty, &empty_data, &empty_length));
    assert_int_equal((int)empty_length, 3);
    assert_memory_equal(empty_data, "{}\n", 3);
    bc_core_writer_destroy(&writer_empty);
    bc_hrbl_reader_destroy(reader_again);

    bc_hrbl_free_buffer(memory, buffer);
    bc_allocators_context_destroy(memory);
}

static void test_writer_roundtrip_scalars(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();

    bc_hrbl_writer_t* writer = NULL;
    assert_true(bc_hrbl_writer_create(memory, &writer));
    assert_true(bc_hrbl_writer_set_int64(writer, "count", 5u, -42));
    assert_true(bc_hrbl_writer_set_bool(writer, "enabled", 7u, true));
    assert_true(bc_hrbl_writer_set_string(writer, "name", 4u, "hello", 5u));
    assert_true(bc_hrbl_writer_set_null(writer, "maybe", 5u));
    assert_true(bc_hrbl_writer_set_float64(writer, "pi", 2u, 3.14));

    void* buffer = NULL;
    size_t size = 0u;
    assert_true(bc_hrbl_writer_finalize_to_buffer(writer, &buffer, &size));
    bc_hrbl_writer_destroy(writer);

    assert_int_equal((int)bc_hrbl_verify_buffer(buffer, size), (int)BC_HRBL_VERIFY_OK);

    bc_hrbl_reader_t* reader = NULL;
    assert_true(bc_hrbl_reader_open_buffer(memory, buffer, size, &reader));
    uint64_t count = 0u;
    assert_true(bc_hrbl_reader_root_count(reader, &count));
    assert_int_equal((int)count, 5);

    bc_hrbl_value_ref_t value;
    int64_t loaded_int = 0;
    bool loaded_bool = false;
    const char* loaded_str = NULL;
    size_t loaded_str_len = 0u;
    double loaded_float = 0.0;
    bc_hrbl_kind_t loaded_kind = BC_HRBL_KIND_NULL;

    assert_true(bc_hrbl_reader_find(reader, "count", 5u, &value));
    assert_true(bc_hrbl_reader_get_int64(&value, &loaded_int));
    assert_true(loaded_int == -42);

    assert_true(bc_hrbl_reader_find(reader, "enabled", 7u, &value));
    assert_true(bc_hrbl_reader_get_bool(&value, &loaded_bool));
    assert_true(loaded_bool);

    assert_true(bc_hrbl_reader_find(reader, "name", 4u, &value));
    assert_true(bc_hrbl_reader_get_string(&value, &loaded_str, &loaded_str_len));
    assert_int_equal((int)loaded_str_len, 5);
    assert_memory_equal(loaded_str, "hello", 5u);

    assert_true(bc_hrbl_reader_find(reader, "maybe", 5u, &value));
    assert_true(bc_hrbl_reader_value_kind(&value, &loaded_kind));
    assert_int_equal((int)loaded_kind, (int)BC_HRBL_KIND_NULL);

    assert_true(bc_hrbl_reader_find(reader, "pi", 2u, &value));
    assert_true(bc_hrbl_reader_get_float64(&value, &loaded_float));
    assert_true(loaded_float > 3.13 && loaded_float < 3.15);

    bc_hrbl_reader_destroy(reader);
    bc_hrbl_free_buffer(memory, buffer);
    bc_allocators_context_destroy(memory);
}

static void test_writer_roundtrip_nested(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();

    bc_hrbl_writer_t* writer = NULL;
    assert_true(bc_hrbl_writer_create(memory, &writer));

    assert_true(bc_hrbl_writer_begin_block(writer, "server", 6u));
    assert_true(bc_hrbl_writer_set_string(writer, "host", 4u, "localhost", 9u));
    assert_true(bc_hrbl_writer_set_int64(writer, "port", 4u, 8080));
    assert_true(bc_hrbl_writer_end_block(writer));

    assert_true(bc_hrbl_writer_begin_array(writer, "ports", 5u));
    assert_true(bc_hrbl_writer_append_int64(writer, 80));
    assert_true(bc_hrbl_writer_append_int64(writer, 443));
    assert_true(bc_hrbl_writer_append_int64(writer, 8080));
    assert_true(bc_hrbl_writer_end_array(writer));

    void* buffer = NULL;
    size_t size = 0u;
    assert_true(bc_hrbl_writer_finalize_to_buffer(writer, &buffer, &size));
    bc_hrbl_writer_destroy(writer);

    assert_int_equal((int)bc_hrbl_verify_buffer(buffer, size), (int)BC_HRBL_VERIFY_OK);

    bc_hrbl_reader_t* reader = NULL;
    assert_true(bc_hrbl_reader_open_buffer(memory, buffer, size, &reader));

    bc_hrbl_value_ref_t value;
    int64_t loaded = 0;
    const char* loaded_str = NULL;
    size_t loaded_len = 0u;

    assert_true(bc_hrbl_reader_find(reader, "server.port", 11u, &value));
    assert_true(bc_hrbl_reader_get_int64(&value, &loaded));
    assert_true(loaded == 8080);

    assert_true(bc_hrbl_reader_find(reader, "server.host", 11u, &value));
    assert_true(bc_hrbl_reader_get_string(&value, &loaded_str, &loaded_len));
    assert_int_equal((int)loaded_len, 9);
    assert_memory_equal(loaded_str, "localhost", 9u);

    assert_true(bc_hrbl_reader_find(reader, "ports[0]", 8u, &value));
    assert_true(bc_hrbl_reader_get_int64(&value, &loaded));
    assert_true(loaded == 80);
    assert_true(bc_hrbl_reader_find(reader, "ports[1]", 8u, &value));
    assert_true(bc_hrbl_reader_get_int64(&value, &loaded));
    assert_true(loaded == 443);
    assert_true(bc_hrbl_reader_find(reader, "ports[2]", 8u, &value));
    assert_true(bc_hrbl_reader_get_int64(&value, &loaded));
    assert_true(loaded == 8080);

    static char sink_full[4096];
    bc_core_writer_t writer_full;
    assert_true(bc_core_writer_init_buffer_only(&writer_full, sink_full, sizeof(sink_full)));
    bc_hrbl_export_options_t options;
    options.indent_spaces = 2u;
    options.sort_keys = true;
    options.ascii_only = false;
    assert_true(bc_hrbl_export_json_ex(reader, &writer_full, &options));
    const char* full_data = NULL;
    size_t full_length = 0u;
    assert_true(bc_core_writer_buffer_data(&writer_full, &full_data, &full_length));
    const char* expected = "{\n"
                           "  \"ports\": [\n"
                           "    80,\n"
                           "    443,\n"
                           "    8080\n"
                           "  ],\n"
                           "  \"server\": {\n"
                           "    \"host\": \"localhost\",\n"
                           "    \"port\": 8080\n"
                           "  }\n"
                           "}\n";
    size_t expected_length = strlen(expected);
    assert_int_equal((int)full_length, (int)expected_length);
    assert_memory_equal(full_data, expected, expected_length);
    bc_core_writer_destroy(&writer_full);

    bc_hrbl_reader_destroy(reader);
    bc_hrbl_free_buffer(memory, buffer);
    bc_allocators_context_destroy(memory);
}

static void test_writer_roundtrip_quoted_path_segments(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();

    bc_hrbl_writer_t* writer = NULL;
    assert_true(bc_hrbl_writer_create(memory, &writer));

    assert_true(bc_hrbl_writer_begin_block(writer, "files", 5u));
    assert_true(bc_hrbl_writer_set_int64(writer, "a.txt", 5u, 11));
    assert_true(bc_hrbl_writer_set_int64(writer, "sub/b.log", 9u, 22));
    assert_true(bc_hrbl_writer_set_int64(writer, "weird\"name", 10u, 33));
    assert_true(bc_hrbl_writer_end_block(writer));

    void* buffer = NULL;
    size_t size = 0u;
    assert_true(bc_hrbl_writer_finalize_to_buffer(writer, &buffer, &size));
    bc_hrbl_writer_destroy(writer);

    bc_hrbl_reader_t* reader = NULL;
    assert_true(bc_hrbl_reader_open_buffer(memory, buffer, size, &reader));

    bc_hrbl_value_ref_t value;
    int64_t loaded = 0;

    assert_true(bc_hrbl_reader_find(reader, "files.'a.txt'", 13u, &value));
    assert_true(bc_hrbl_reader_get_int64(&value, &loaded));
    assert_true(loaded == 11);

    assert_true(bc_hrbl_reader_find(reader, "files.'sub/b.log'", 17u, &value));
    assert_true(bc_hrbl_reader_get_int64(&value, &loaded));
    assert_true(loaded == 22);

    assert_true(bc_hrbl_reader_find(reader, "files.'weird\"name'", 18u, &value));
    assert_true(bc_hrbl_reader_get_int64(&value, &loaded));
    assert_true(loaded == 33);

    assert_false(bc_hrbl_reader_find(reader, "files.'missing'", 15u, &value));
    assert_false(bc_hrbl_reader_find(reader, "files.'unterminated", 19u, &value));
    assert_false(bc_hrbl_reader_find(reader, "files.''", 8u, &value));

    bc_hrbl_reader_destroy(reader);
    bc_hrbl_free_buffer(memory, buffer);
    bc_allocators_context_destroy(memory);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_writer_roundtrip_empty),
        cmocka_unit_test(test_writer_roundtrip_scalars),
        cmocka_unit_test(test_writer_roundtrip_nested),
        cmocka_unit_test(test_writer_roundtrip_quoted_path_segments),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
