// SPDX-License-Identifier: MIT

#include "bc_hrbl.h"

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

static char* export_to_string(const bc_hrbl_reader_t* reader)
{
    char* buffer = NULL;
    size_t size = 0u;
    FILE* stream = open_memstream(&buffer, &size);
    assert_non_null(stream);
    bc_hrbl_export_options_t options;
    options.indent_spaces = 2u;
    options.sort_keys = true;
    options.ascii_only = false;
    assert_true(bc_hrbl_export_json_ex(reader, stream, &options));
    fflush(stream);
    fclose(stream);
    return buffer;
}

static void test_convert_empty_object(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();
    const char* json = "{}";

    void* buffer = NULL;
    size_t size = 0u;
    bc_hrbl_convert_error_t error;
    assert_true(bc_hrbl_convert_json_buffer_to_hrbl(memory, json, strlen(json), &buffer, &size, &error));
    assert_int_equal((int)bc_hrbl_verify_buffer(buffer, size), (int)BC_HRBL_VERIFY_OK);
    assert_int_equal((int)size, 160);
    free(buffer);
    bc_allocators_context_destroy(memory);
}

static void test_convert_simple_object(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();
    const char* json = "{\"name\":\"bc-hrbl\",\"active\":true,\"count\":-7,\"pi\":3.14,\"missing\":null}";

    void* buffer = NULL;
    size_t size = 0u;
    bc_hrbl_convert_error_t error;
    assert_true(bc_hrbl_convert_json_buffer_to_hrbl(memory, json, strlen(json), &buffer, &size, &error));
    assert_int_equal((int)bc_hrbl_verify_buffer(buffer, size), (int)BC_HRBL_VERIFY_OK);

    bc_hrbl_reader_t* reader = NULL;
    assert_true(bc_hrbl_reader_open_buffer(memory, buffer, size, &reader));
    bc_hrbl_value_ref_t value;
    assert_true(bc_hrbl_reader_find(reader, "count", 5u, &value));
    int64_t count = 0;
    assert_true(bc_hrbl_reader_get_int64(&value, &count));
    assert_true(count == -7);

    assert_true(bc_hrbl_reader_find(reader, "active", 6u, &value));
    bool active = false;
    assert_true(bc_hrbl_reader_get_bool(&value, &active));
    assert_true(active);

    assert_true(bc_hrbl_reader_find(reader, "name", 4u, &value));
    const char* str = NULL;
    size_t str_len = 0u;
    assert_true(bc_hrbl_reader_get_string(&value, &str, &str_len));
    assert_int_equal((int)str_len, 7);
    assert_memory_equal(str, "bc-hrbl", 7u);

    assert_true(bc_hrbl_reader_find(reader, "pi", 2u, &value));
    double pi = 0.0;
    assert_true(bc_hrbl_reader_get_float64(&value, &pi));
    assert_true(pi > 3.13 && pi < 3.15);

    bc_hrbl_reader_destroy(reader);
    free(buffer);
    bc_allocators_context_destroy(memory);
}

static void test_convert_nested_structure(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();
    const char* json = "{\"server\":{\"host\":\"localhost\",\"port\":8080},\"ports\":[80,443,8080]}";

    void* buffer = NULL;
    size_t size = 0u;
    bc_hrbl_convert_error_t error;
    assert_true(bc_hrbl_convert_json_buffer_to_hrbl(memory, json, strlen(json), &buffer, &size, &error));
    assert_int_equal((int)bc_hrbl_verify_buffer(buffer, size), (int)BC_HRBL_VERIFY_OK);

    bc_hrbl_reader_t* reader = NULL;
    assert_true(bc_hrbl_reader_open_buffer(memory, buffer, size, &reader));

    char* exported = export_to_string(reader);
    const char* expected =
        "{\n"
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
    assert_string_equal(exported, expected);

    free(exported);
    bc_hrbl_reader_destroy(reader);
    free(buffer);
    bc_allocators_context_destroy(memory);
}

static void test_convert_unicode_escapes(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();
    const char* json = "{\"greeting\":\"hello \\u00e9 world\",\"emoji\":\"\\uD83D\\uDE00\"}";

    void* buffer = NULL;
    size_t size = 0u;
    bc_hrbl_convert_error_t error;
    assert_true(bc_hrbl_convert_json_buffer_to_hrbl(memory, json, strlen(json), &buffer, &size, &error));
    assert_int_equal((int)bc_hrbl_verify_buffer(buffer, size), (int)BC_HRBL_VERIFY_OK);

    bc_hrbl_reader_t* reader = NULL;
    assert_true(bc_hrbl_reader_open_buffer(memory, buffer, size, &reader));

    bc_hrbl_value_ref_t value;
    const char* str = NULL;
    size_t str_len = 0u;
    assert_true(bc_hrbl_reader_find(reader, "greeting", 8u, &value));
    assert_true(bc_hrbl_reader_get_string(&value, &str, &str_len));
    const char* expected_greeting = "hello \xc3\xa9 world";
    assert_int_equal((int)str_len, (int)strlen(expected_greeting));
    assert_memory_equal(str, expected_greeting, strlen(expected_greeting));

    assert_true(bc_hrbl_reader_find(reader, "emoji", 5u, &value));
    assert_true(bc_hrbl_reader_get_string(&value, &str, &str_len));
    const char* expected_emoji = "\xf0\x9f\x98\x80";
    assert_int_equal((int)str_len, (int)strlen(expected_emoji));
    assert_memory_equal(str, expected_emoji, strlen(expected_emoji));

    bc_hrbl_reader_destroy(reader);
    free(buffer);
    bc_allocators_context_destroy(memory);
}

static void test_convert_invalid_rejected(void** state)
{
    (void)state;
    bc_allocators_context_t* memory = make_memory();

    const char* cases[] = {
        "",
        "[]",
        "{",
        "{\"key\":",
        "{\"key\":null,}",
        "{\"a\":1 extra}",
        "{\"bad\":\"\\q\"}",
        "{\"badnum\":1.",
        "{\"ctrl\":\"\x01\"}",
    };
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); i += 1u) {
        void* buffer = NULL;
        size_t size = 0u;
        bc_hrbl_convert_error_t error;
        bool ok = bc_hrbl_convert_json_buffer_to_hrbl(memory, cases[i], strlen(cases[i]), &buffer, &size, &error);
        assert_false(ok);
        assert_null(buffer);
    }
    bc_allocators_context_destroy(memory);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_convert_empty_object),
        cmocka_unit_test(test_convert_simple_object),
        cmocka_unit_test(test_convert_nested_structure),
        cmocka_unit_test(test_convert_unicode_escapes),
        cmocka_unit_test(test_convert_invalid_rejected),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
