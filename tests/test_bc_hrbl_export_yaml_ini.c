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

static void build_simple(bc_allocators_context_t* memory, void** out_buffer, size_t* out_size)
{
    bc_hrbl_writer_t* writer = NULL;
    assert_true(bc_hrbl_writer_create(memory, NULL, &writer));
    assert_true(bc_hrbl_writer_set_string(writer, "name", 4u, "demo", 4u));
    assert_true(bc_hrbl_writer_set_int64(writer, "port", 4u, 8080));
    assert_true(bc_hrbl_writer_set_bool(writer, "tls", 3u, true));
    assert_true(bc_hrbl_writer_finalize_to_buffer(writer, out_buffer, out_size));
    bc_hrbl_writer_destroy(writer);
}

static void build_nested(bc_allocators_context_t* memory, void** out_buffer, size_t* out_size)
{
    bc_hrbl_writer_t* writer = NULL;
    assert_true(bc_hrbl_writer_create(memory, NULL, &writer));
    assert_true(bc_hrbl_writer_set_string(writer, "name", 4u, "app", 3u));
    assert_true(bc_hrbl_writer_begin_block(writer, "server", 6u));
    assert_true(bc_hrbl_writer_set_string(writer, "host", 4u, "localhost", 9u));
    assert_true(bc_hrbl_writer_set_int64(writer, "port", 4u, 8080));
    assert_true(bc_hrbl_writer_end_block(writer));
    assert_true(bc_hrbl_writer_begin_array(writer, "ports", 5u));
    assert_true(bc_hrbl_writer_append_int64(writer, 80));
    assert_true(bc_hrbl_writer_append_int64(writer, 443));
    assert_true(bc_hrbl_writer_end_array(writer));
    assert_true(bc_hrbl_writer_finalize_to_buffer(writer, out_buffer, out_size));
    bc_hrbl_writer_destroy(writer);
}

static char* capture(const bc_hrbl_reader_t* reader, bool (*export_fn)(const bc_hrbl_reader_t*, bc_core_writer_t*))
{
    /* Buffer-only writer so we can capture output without going through a file descriptor. */
    static char sink_buffer[65536];
    bc_core_writer_t writer;
    assert_true(bc_core_writer_init_buffer_only(&writer, sink_buffer, sizeof(sink_buffer)));
    assert_true(export_fn(reader, &writer));
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

static void test_yaml_simple(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();
    void* buffer = NULL;
    size_t size = 0u;
    build_simple(memory, &buffer, &size);

    bc_hrbl_reader_t* reader = NULL;
    assert_true(bc_hrbl_reader_open_buffer(memory, buffer, size, &reader));
    char* yaml = capture(reader, bc_hrbl_export_yaml);
    assert_non_null(strstr(yaml, "\"name\""));
    assert_non_null(strstr(yaml, "\"demo\""));
    assert_non_null(strstr(yaml, "\"port\""));
    assert_non_null(strstr(yaml, "8080"));
    assert_non_null(strstr(yaml, "\"tls\""));
    assert_non_null(strstr(yaml, "true"));
    free(yaml);
    bc_hrbl_reader_close(reader);
    bc_hrbl_writer_free_buffer(memory, buffer);
    bc_allocators_context_destroy(memory);
}

static void test_yaml_nested(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();
    void* buffer = NULL;
    size_t size = 0u;
    build_nested(memory, &buffer, &size);

    bc_hrbl_reader_t* reader = NULL;
    assert_true(bc_hrbl_reader_open_buffer(memory, buffer, size, &reader));
    char* yaml = capture(reader, bc_hrbl_export_yaml);
    assert_non_null(strstr(yaml, "\"server\""));
    assert_non_null(strstr(yaml, "\"host\""));
    assert_non_null(strstr(yaml, "\"localhost\""));
    assert_non_null(strstr(yaml, "\"ports\""));
    assert_non_null(strstr(yaml, "- 80"));
    assert_non_null(strstr(yaml, "- 443"));
    free(yaml);
    bc_hrbl_reader_close(reader);
    bc_hrbl_writer_free_buffer(memory, buffer);
    bc_allocators_context_destroy(memory);
}

static void test_ini_simple(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();
    void* buffer = NULL;
    size_t size = 0u;
    build_simple(memory, &buffer, &size);

    bc_hrbl_reader_t* reader = NULL;
    assert_true(bc_hrbl_reader_open_buffer(memory, buffer, size, &reader));
    char* ini = capture(reader, bc_hrbl_export_ini);
    assert_non_null(strstr(ini, "name = \"demo\""));
    assert_non_null(strstr(ini, "port = 8080"));
    assert_non_null(strstr(ini, "tls = true"));
    free(ini);
    bc_hrbl_reader_close(reader);
    bc_hrbl_writer_free_buffer(memory, buffer);
    bc_allocators_context_destroy(memory);
}

static void test_ini_nested(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();
    void* buffer = NULL;
    size_t size = 0u;
    build_nested(memory, &buffer, &size);

    bc_hrbl_reader_t* reader = NULL;
    assert_true(bc_hrbl_reader_open_buffer(memory, buffer, size, &reader));
    char* ini = capture(reader, bc_hrbl_export_ini);
    assert_non_null(strstr(ini, "name = \"app\""));
    assert_non_null(strstr(ini, "ports[]=80"));
    assert_non_null(strstr(ini, "ports[]=443"));
    assert_non_null(strstr(ini, "[server]"));
    assert_non_null(strstr(ini, "host = \"localhost\""));
    assert_non_null(strstr(ini, "port = 8080"));
    free(ini);
    bc_hrbl_reader_close(reader);
    bc_hrbl_writer_free_buffer(memory, buffer);
    bc_allocators_context_destroy(memory);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_yaml_simple),
        cmocka_unit_test(test_yaml_nested),
        cmocka_unit_test(test_ini_simple),
        cmocka_unit_test(test_ini_nested),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
