// SPDX-License-Identifier: MIT

#include "bc_hrbl_convert.h"
#include "bc_hrbl_writer.h"

#include "bc_allocators.h"
#include "bc_allocators_pool.h"

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct bc_hrbl_json_reader {
    const char*              data;
    size_t                   length;
    size_t                   cursor;
    uint32_t                 line;
    uint32_t                 column;
    bc_hrbl_convert_error_t* error_sink;
    bool                     error_flagged;
} bc_hrbl_json_reader_t;

static void bc_hrbl_json_set_error(bc_hrbl_json_reader_t* reader, const char* message)
{
    if (reader->error_flagged) {
        return;
    }
    reader->error_flagged = true;
    if (reader->error_sink != NULL) {
        reader->error_sink->message = message;
        reader->error_sink->byte_offset = reader->cursor;
        reader->error_sink->line = reader->line;
        reader->error_sink->column = reader->column;
    }
}

static bool bc_hrbl_json_advance(bc_hrbl_json_reader_t* reader)
{
    if (reader->cursor >= reader->length) {
        return false;
    }
    char byte = reader->data[reader->cursor];
    reader->cursor += 1u;
    if (byte == '\n') {
        reader->line += 1u;
        reader->column = 1u;
    } else {
        reader->column += 1u;
    }
    return true;
}

static int bc_hrbl_json_peek(const bc_hrbl_json_reader_t* reader)
{
    if (reader->cursor >= reader->length) {
        return -1;
    }
    return (unsigned char)reader->data[reader->cursor];
}

static void bc_hrbl_json_skip_whitespace(bc_hrbl_json_reader_t* reader)
{
    while (reader->cursor < reader->length) {
        char c = reader->data[reader->cursor];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            (void)bc_hrbl_json_advance(reader);
        } else {
            break;
        }
    }
}

static bool bc_hrbl_json_match_literal(bc_hrbl_json_reader_t* reader, const char* literal)
{
    size_t literal_length = strlen(literal);
    if (reader->cursor + literal_length > reader->length) {
        return false;
    }
    if (memcmp(&reader->data[reader->cursor], literal, literal_length) != 0) {
        return false;
    }
    for (size_t i = 0u; i < literal_length; i += 1u) {
        (void)bc_hrbl_json_advance(reader);
    }
    return true;
}

typedef struct bc_hrbl_json_string {
    char*  data;
    size_t length;
    size_t capacity;
} bc_hrbl_json_string_t;

static bool bc_hrbl_json_string_reserve(bc_hrbl_json_string_t* s, size_t required)
{
    if (required <= s->capacity) {
        return true;
    }
    size_t new_capacity = s->capacity == 0u ? 64u : s->capacity;
    while (new_capacity < required) {
        new_capacity *= 2u;
    }
    char* new_data = (char*)realloc(s->data, new_capacity);
    if (new_data == NULL) {
        return false;
    }
    s->data = new_data;
    s->capacity = new_capacity;
    return true;
}

static bool bc_hrbl_json_string_push(bc_hrbl_json_string_t* s, char byte)
{
    if (!bc_hrbl_json_string_reserve(s, s->length + 1u)) {
        return false;
    }
    s->data[s->length] = byte;
    s->length += 1u;
    return true;
}

static bool bc_hrbl_json_string_push_codepoint(bc_hrbl_json_string_t* s, uint32_t codepoint)
{
    if (codepoint < 0x80u) {
        return bc_hrbl_json_string_push(s, (char)codepoint);
    }
    if (codepoint < 0x800u) {
        return bc_hrbl_json_string_push(s, (char)(0xC0u | (codepoint >> 6)))
               && bc_hrbl_json_string_push(s, (char)(0x80u | (codepoint & 0x3Fu)));
    }
    if (codepoint < 0x10000u) {
        return bc_hrbl_json_string_push(s, (char)(0xE0u | (codepoint >> 12)))
               && bc_hrbl_json_string_push(s, (char)(0x80u | ((codepoint >> 6) & 0x3Fu)))
               && bc_hrbl_json_string_push(s, (char)(0x80u | (codepoint & 0x3Fu)));
    }
    return bc_hrbl_json_string_push(s, (char)(0xF0u | (codepoint >> 18)))
           && bc_hrbl_json_string_push(s, (char)(0x80u | ((codepoint >> 12) & 0x3Fu)))
           && bc_hrbl_json_string_push(s, (char)(0x80u | ((codepoint >> 6) & 0x3Fu)))
           && bc_hrbl_json_string_push(s, (char)(0x80u | (codepoint & 0x3Fu)));
}

static bool bc_hrbl_json_parse_hex4(bc_hrbl_json_reader_t* reader, uint32_t* out_value)
{
    if (reader->cursor + 4u > reader->length) {
        bc_hrbl_json_set_error(reader, "truncated \\u escape");
        return false;
    }
    uint32_t value = 0u;
    for (size_t i = 0u; i < 4u; i += 1u) {
        char c = reader->data[reader->cursor];
        uint32_t digit;
        if (c >= '0' && c <= '9') {
            digit = (uint32_t)(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            digit = (uint32_t)(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            digit = (uint32_t)(c - 'A' + 10);
        } else {
            bc_hrbl_json_set_error(reader, "invalid hex digit in \\u escape");
            return false;
        }
        value = (value << 4) | digit;
        (void)bc_hrbl_json_advance(reader);
    }
    *out_value = value;
    return true;
}

static bool bc_hrbl_json_parse_string(bc_hrbl_json_reader_t* reader, bc_hrbl_json_string_t* out_string)
{
    out_string->data = NULL;
    out_string->length = 0u;
    out_string->capacity = 0u;
    if (bc_hrbl_json_peek(reader) != '"') {
        bc_hrbl_json_set_error(reader, "expected '\"'");
        return false;
    }
    (void)bc_hrbl_json_advance(reader);
    while (reader->cursor < reader->length) {
        unsigned char c = (unsigned char)reader->data[reader->cursor];
        if (c == '"') {
            (void)bc_hrbl_json_advance(reader);
            return true;
        }
        if (c < 0x20u) {
            bc_hrbl_json_set_error(reader, "unescaped control byte in string");
            free(out_string->data);
            out_string->data = NULL;
            out_string->length = 0u;
            return false;
        }
        if (c == '\\') {
            (void)bc_hrbl_json_advance(reader);
            if (reader->cursor >= reader->length) {
                bc_hrbl_json_set_error(reader, "truncated escape sequence");
                free(out_string->data);
                out_string->data = NULL;
                return false;
            }
            char escape = reader->data[reader->cursor];
            (void)bc_hrbl_json_advance(reader);
            switch (escape) {
            case '"':  (void)bc_hrbl_json_string_push(out_string, '"'); break;
            case '\\': (void)bc_hrbl_json_string_push(out_string, '\\'); break;
            case '/':  (void)bc_hrbl_json_string_push(out_string, '/'); break;
            case 'b':  (void)bc_hrbl_json_string_push(out_string, '\b'); break;
            case 'f':  (void)bc_hrbl_json_string_push(out_string, '\f'); break;
            case 'n':  (void)bc_hrbl_json_string_push(out_string, '\n'); break;
            case 'r':  (void)bc_hrbl_json_string_push(out_string, '\r'); break;
            case 't':  (void)bc_hrbl_json_string_push(out_string, '\t'); break;
            case 'u': {
                uint32_t codepoint = 0u;
                if (!bc_hrbl_json_parse_hex4(reader, &codepoint)) {
                    free(out_string->data);
                    out_string->data = NULL;
                    return false;
                }
                if (codepoint >= 0xD800u && codepoint <= 0xDBFFu) {
                    if (reader->cursor + 2u > reader->length || reader->data[reader->cursor] != '\\'
                        || reader->data[reader->cursor + 1u] != 'u') {
                        bc_hrbl_json_set_error(reader, "lone high surrogate");
                        free(out_string->data);
                        out_string->data = NULL;
                        return false;
                    }
                    (void)bc_hrbl_json_advance(reader);
                    (void)bc_hrbl_json_advance(reader);
                    uint32_t low = 0u;
                    if (!bc_hrbl_json_parse_hex4(reader, &low)) {
                        free(out_string->data);
                        out_string->data = NULL;
                        return false;
                    }
                    if (low < 0xDC00u || low > 0xDFFFu) {
                        bc_hrbl_json_set_error(reader, "invalid low surrogate");
                        free(out_string->data);
                        out_string->data = NULL;
                        return false;
                    }
                    codepoint = 0x10000u + ((codepoint - 0xD800u) << 10) + (low - 0xDC00u);
                } else if (codepoint >= 0xDC00u && codepoint <= 0xDFFFu) {
                    bc_hrbl_json_set_error(reader, "unexpected low surrogate");
                    free(out_string->data);
                    out_string->data = NULL;
                    return false;
                }
                (void)bc_hrbl_json_string_push_codepoint(out_string, codepoint);
                break;
            }
            default:
                bc_hrbl_json_set_error(reader, "invalid escape character");
                free(out_string->data);
                out_string->data = NULL;
                return false;
            }
            continue;
        }
        (void)bc_hrbl_json_string_push(out_string, (char)c);
        (void)bc_hrbl_json_advance(reader);
    }
    bc_hrbl_json_set_error(reader, "unterminated string");
    free(out_string->data);
    out_string->data = NULL;
    return false;
}

typedef enum bc_hrbl_json_number_kind {
    BC_HRBL_JSON_NUMBER_INT64,
    BC_HRBL_JSON_NUMBER_UINT64,
    BC_HRBL_JSON_NUMBER_FLOAT64
} bc_hrbl_json_number_kind_t;

typedef struct bc_hrbl_json_number {
    bc_hrbl_json_number_kind_t kind;
    union {
        int64_t  int64_value;
        uint64_t uint64_value;
        double   float64_value;
    } as;
} bc_hrbl_json_number_t;

static bool bc_hrbl_json_parse_number(bc_hrbl_json_reader_t* reader, bc_hrbl_json_number_t* out_number)
{
    size_t start = reader->cursor;
    bool is_float = false;
    if (reader->cursor < reader->length && reader->data[reader->cursor] == '-') {
        (void)bc_hrbl_json_advance(reader);
    }
    bool has_digit = false;
    while (reader->cursor < reader->length) {
        char c = reader->data[reader->cursor];
        if (c < '0' || c > '9') {
            break;
        }
        has_digit = true;
        (void)bc_hrbl_json_advance(reader);
    }
    if (!has_digit) {
        bc_hrbl_json_set_error(reader, "expected digit");
        return false;
    }
    if (reader->cursor < reader->length && reader->data[reader->cursor] == '.') {
        is_float = true;
        (void)bc_hrbl_json_advance(reader);
        has_digit = false;
        while (reader->cursor < reader->length) {
            char c = reader->data[reader->cursor];
            if (c < '0' || c > '9') {
                break;
            }
            has_digit = true;
            (void)bc_hrbl_json_advance(reader);
        }
        if (!has_digit) {
            bc_hrbl_json_set_error(reader, "expected fraction digits");
            return false;
        }
    }
    if (reader->cursor < reader->length && (reader->data[reader->cursor] == 'e' || reader->data[reader->cursor] == 'E')) {
        is_float = true;
        (void)bc_hrbl_json_advance(reader);
        if (reader->cursor < reader->length && (reader->data[reader->cursor] == '+' || reader->data[reader->cursor] == '-')) {
            (void)bc_hrbl_json_advance(reader);
        }
        has_digit = false;
        while (reader->cursor < reader->length) {
            char c = reader->data[reader->cursor];
            if (c < '0' || c > '9') {
                break;
            }
            has_digit = true;
            (void)bc_hrbl_json_advance(reader);
        }
        if (!has_digit) {
            bc_hrbl_json_set_error(reader, "expected exponent digits");
            return false;
        }
    }

    size_t end = reader->cursor;
    size_t literal_length = end - start;
    char local_buffer[64];
    char* literal_buffer = local_buffer;
    if (literal_length >= sizeof(local_buffer)) {
        literal_buffer = (char*)malloc(literal_length + 1u);
        if (literal_buffer == NULL) {
            bc_hrbl_json_set_error(reader, "allocation failure");
            return false;
        }
    }
    memcpy(literal_buffer, &reader->data[start], literal_length);
    literal_buffer[literal_length] = '\0';

    bool result = true;
    if (is_float) {
        char* end_ptr = NULL;
        double value = strtod(literal_buffer, &end_ptr);
        if (end_ptr == literal_buffer) {
            bc_hrbl_json_set_error(reader, "invalid float literal");
            result = false;
        } else {
            out_number->kind = BC_HRBL_JSON_NUMBER_FLOAT64;
            out_number->as.float64_value = value;
        }
    } else if (literal_buffer[0] == '-') {
        char* end_ptr = NULL;
        errno = 0;
        long long value = strtoll(literal_buffer, &end_ptr, 10);
        if (end_ptr == literal_buffer || errno != 0) {
            bc_hrbl_json_set_error(reader, "invalid int literal");
            result = false;
        } else {
            out_number->kind = BC_HRBL_JSON_NUMBER_INT64;
            out_number->as.int64_value = (int64_t)value;
        }
    } else {
        char* end_ptr = NULL;
        errno = 0;
        unsigned long long value = strtoull(literal_buffer, &end_ptr, 10);
        if (end_ptr == literal_buffer || errno != 0) {
            bc_hrbl_json_set_error(reader, "invalid uint literal");
            result = false;
        } else if (value <= (unsigned long long)INT64_MAX) {
            out_number->kind = BC_HRBL_JSON_NUMBER_INT64;
            out_number->as.int64_value = (int64_t)value;
        } else {
            out_number->kind = BC_HRBL_JSON_NUMBER_UINT64;
            out_number->as.uint64_value = (uint64_t)value;
        }
    }
    if (literal_buffer != local_buffer) {
        free(literal_buffer);
    }
    return result;
}

static bool bc_hrbl_json_parse_value_root(bc_hrbl_json_reader_t* reader, bc_hrbl_writer_t* writer);
static bool bc_hrbl_json_parse_value_keyed(bc_hrbl_json_reader_t* reader, bc_hrbl_writer_t* writer, const char* key,
                                           size_t key_length);
static bool bc_hrbl_json_parse_value_indexed(bc_hrbl_json_reader_t* reader, bc_hrbl_writer_t* writer);

static bool bc_hrbl_json_parse_object_body(bc_hrbl_json_reader_t* reader, bc_hrbl_writer_t* writer)
{
    bc_hrbl_json_skip_whitespace(reader);
    if (bc_hrbl_json_peek(reader) == '}') {
        (void)bc_hrbl_json_advance(reader);
        return true;
    }
    for (;;) {
        bc_hrbl_json_skip_whitespace(reader);
        bc_hrbl_json_string_t key_string;
        if (!bc_hrbl_json_parse_string(reader, &key_string)) {
            return false;
        }
        bc_hrbl_json_skip_whitespace(reader);
        if (bc_hrbl_json_peek(reader) != ':') {
            free(key_string.data);
            bc_hrbl_json_set_error(reader, "expected ':'");
            return false;
        }
        (void)bc_hrbl_json_advance(reader);
        bc_hrbl_json_skip_whitespace(reader);
        bool ok = bc_hrbl_json_parse_value_keyed(reader, writer, key_string.data, key_string.length);
        free(key_string.data);
        if (!ok) {
            return false;
        }
        bc_hrbl_json_skip_whitespace(reader);
        int next = bc_hrbl_json_peek(reader);
        if (next == ',') {
            (void)bc_hrbl_json_advance(reader);
            continue;
        }
        if (next == '}') {
            (void)bc_hrbl_json_advance(reader);
            return true;
        }
        bc_hrbl_json_set_error(reader, "expected ',' or '}'");
        return false;
    }
}

static bool bc_hrbl_json_parse_array_body(bc_hrbl_json_reader_t* reader, bc_hrbl_writer_t* writer)
{
    bc_hrbl_json_skip_whitespace(reader);
    if (bc_hrbl_json_peek(reader) == ']') {
        (void)bc_hrbl_json_advance(reader);
        return true;
    }
    for (;;) {
        bc_hrbl_json_skip_whitespace(reader);
        if (!bc_hrbl_json_parse_value_indexed(reader, writer)) {
            return false;
        }
        bc_hrbl_json_skip_whitespace(reader);
        int next = bc_hrbl_json_peek(reader);
        if (next == ',') {
            (void)bc_hrbl_json_advance(reader);
            continue;
        }
        if (next == ']') {
            (void)bc_hrbl_json_advance(reader);
            return true;
        }
        bc_hrbl_json_set_error(reader, "expected ',' or ']'");
        return false;
    }
}

static bool bc_hrbl_json_parse_value_keyed(bc_hrbl_json_reader_t* reader, bc_hrbl_writer_t* writer, const char* key,
                                           size_t key_length)
{
    bc_hrbl_json_skip_whitespace(reader);
    int next = bc_hrbl_json_peek(reader);
    if (next == '{') {
        (void)bc_hrbl_json_advance(reader);
        if (!bc_hrbl_writer_begin_block(writer, key, key_length)) {
            bc_hrbl_json_set_error(reader, "writer begin_block failed");
            return false;
        }
        if (!bc_hrbl_json_parse_object_body(reader, writer)) {
            return false;
        }
        if (!bc_hrbl_writer_end_block(writer)) {
            bc_hrbl_json_set_error(reader, "writer end_block failed");
            return false;
        }
        return true;
    }
    if (next == '[') {
        (void)bc_hrbl_json_advance(reader);
        if (!bc_hrbl_writer_begin_array(writer, key, key_length)) {
            bc_hrbl_json_set_error(reader, "writer begin_array failed");
            return false;
        }
        if (!bc_hrbl_json_parse_array_body(reader, writer)) {
            return false;
        }
        if (!bc_hrbl_writer_end_array(writer)) {
            bc_hrbl_json_set_error(reader, "writer end_array failed");
            return false;
        }
        return true;
    }
    if (next == 'n') {
        if (!bc_hrbl_json_match_literal(reader, "null")) {
            bc_hrbl_json_set_error(reader, "expected null");
            return false;
        }
        return bc_hrbl_writer_set_null(writer, key, key_length);
    }
    if (next == 't') {
        if (!bc_hrbl_json_match_literal(reader, "true")) {
            bc_hrbl_json_set_error(reader, "expected true");
            return false;
        }
        return bc_hrbl_writer_set_bool(writer, key, key_length, true);
    }
    if (next == 'f') {
        if (!bc_hrbl_json_match_literal(reader, "false")) {
            bc_hrbl_json_set_error(reader, "expected false");
            return false;
        }
        return bc_hrbl_writer_set_bool(writer, key, key_length, false);
    }
    if (next == '"') {
        bc_hrbl_json_string_t value_string;
        if (!bc_hrbl_json_parse_string(reader, &value_string)) {
            return false;
        }
        bool ok = bc_hrbl_writer_set_string(writer, key, key_length, value_string.data, value_string.length);
        free(value_string.data);
        return ok;
    }
    if ((next >= '0' && next <= '9') || next == '-') {
        bc_hrbl_json_number_t number;
        if (!bc_hrbl_json_parse_number(reader, &number)) {
            return false;
        }
        switch (number.kind) {
        case BC_HRBL_JSON_NUMBER_INT64:   return bc_hrbl_writer_set_int64(writer, key, key_length, number.as.int64_value);
        case BC_HRBL_JSON_NUMBER_UINT64:  return bc_hrbl_writer_set_uint64(writer, key, key_length, number.as.uint64_value);
        case BC_HRBL_JSON_NUMBER_FLOAT64: return bc_hrbl_writer_set_float64(writer, key, key_length, number.as.float64_value);
        }
    }
    bc_hrbl_json_set_error(reader, "unexpected character");
    return false;
}

static bool bc_hrbl_json_parse_value_indexed(bc_hrbl_json_reader_t* reader, bc_hrbl_writer_t* writer)
{
    bc_hrbl_json_skip_whitespace(reader);
    int next = bc_hrbl_json_peek(reader);
    if (next == '{') {
        (void)bc_hrbl_json_advance(reader);
        if (!bc_hrbl_writer_begin_block(writer, NULL, 0u)) {
            bc_hrbl_json_set_error(reader, "writer begin_block failed");
            return false;
        }
        if (!bc_hrbl_json_parse_object_body(reader, writer)) {
            return false;
        }
        if (!bc_hrbl_writer_end_block(writer)) {
            bc_hrbl_json_set_error(reader, "writer end_block failed");
            return false;
        }
        return true;
    }
    if (next == '[') {
        (void)bc_hrbl_json_advance(reader);
        if (!bc_hrbl_writer_begin_array(writer, NULL, 0u)) {
            bc_hrbl_json_set_error(reader, "writer begin_array failed");
            return false;
        }
        if (!bc_hrbl_json_parse_array_body(reader, writer)) {
            return false;
        }
        if (!bc_hrbl_writer_end_array(writer)) {
            bc_hrbl_json_set_error(reader, "writer end_array failed");
            return false;
        }
        return true;
    }
    if (next == 'n') {
        if (!bc_hrbl_json_match_literal(reader, "null")) {
            bc_hrbl_json_set_error(reader, "expected null");
            return false;
        }
        return bc_hrbl_writer_append_null(writer);
    }
    if (next == 't') {
        if (!bc_hrbl_json_match_literal(reader, "true")) {
            bc_hrbl_json_set_error(reader, "expected true");
            return false;
        }
        return bc_hrbl_writer_append_bool(writer, true);
    }
    if (next == 'f') {
        if (!bc_hrbl_json_match_literal(reader, "false")) {
            bc_hrbl_json_set_error(reader, "expected false");
            return false;
        }
        return bc_hrbl_writer_append_bool(writer, false);
    }
    if (next == '"') {
        bc_hrbl_json_string_t value_string;
        if (!bc_hrbl_json_parse_string(reader, &value_string)) {
            return false;
        }
        bool ok = bc_hrbl_writer_append_string(writer, value_string.data, value_string.length);
        free(value_string.data);
        return ok;
    }
    if ((next >= '0' && next <= '9') || next == '-') {
        bc_hrbl_json_number_t number;
        if (!bc_hrbl_json_parse_number(reader, &number)) {
            return false;
        }
        switch (number.kind) {
        case BC_HRBL_JSON_NUMBER_INT64:   return bc_hrbl_writer_append_int64(writer, number.as.int64_value);
        case BC_HRBL_JSON_NUMBER_UINT64:  return bc_hrbl_writer_append_uint64(writer, number.as.uint64_value);
        case BC_HRBL_JSON_NUMBER_FLOAT64: return bc_hrbl_writer_append_float64(writer, number.as.float64_value);
        }
    }
    bc_hrbl_json_set_error(reader, "unexpected character");
    return false;
}

static bool bc_hrbl_json_parse_value_root(bc_hrbl_json_reader_t* reader, bc_hrbl_writer_t* writer)
{
    bc_hrbl_json_skip_whitespace(reader);
    if (bc_hrbl_json_peek(reader) != '{') {
        bc_hrbl_json_set_error(reader, "root value must be an object");
        return false;
    }
    (void)bc_hrbl_json_advance(reader);
    return bc_hrbl_json_parse_object_body(reader, writer);
}

bool bc_hrbl_convert_json_to_writer(bc_hrbl_writer_t* writer, const char* json_text, size_t text_length,
                                    bc_hrbl_convert_error_t* out_error)
{
    if (writer == NULL || json_text == NULL) {
        return false;
    }
    bc_hrbl_json_reader_t reader;
    reader.data = json_text;
    reader.length = text_length;
    reader.cursor = 0u;
    reader.line = 1u;
    reader.column = 1u;
    reader.error_sink = out_error;
    reader.error_flagged = false;
    if (out_error != NULL) {
        out_error->message = NULL;
        out_error->byte_offset = 0u;
        out_error->line = 0u;
        out_error->column = 0u;
    }
    if (!bc_hrbl_json_parse_value_root(&reader, writer)) {
        return false;
    }
    bc_hrbl_json_skip_whitespace(&reader);
    if (reader.cursor != reader.length) {
        bc_hrbl_json_set_error(&reader, "trailing content after root object");
        return false;
    }
    return !reader.error_flagged;
}

bool bc_hrbl_convert_json_buffer_to_hrbl(bc_allocators_context_t* memory_context, const char* json_text, size_t text_length,
                                         void** out_hrbl_buffer, size_t* out_hrbl_size, bc_hrbl_convert_error_t* out_error)
{
    if (out_hrbl_buffer != NULL) {
        *out_hrbl_buffer = NULL;
    }
    if (out_hrbl_size != NULL) {
        *out_hrbl_size = 0u;
    }
    if (memory_context == NULL || json_text == NULL || out_hrbl_buffer == NULL || out_hrbl_size == NULL) {
        return false;
    }
    bc_hrbl_writer_t* writer = NULL;
    if (!bc_hrbl_writer_create(memory_context, &writer)) {
        return false;
    }
    bool ok = bc_hrbl_convert_json_to_writer(writer, json_text, text_length, out_error);
    if (!ok) {
        bc_hrbl_writer_destroy(writer);
        return false;
    }
    void* buffer = NULL;
    size_t size = 0u;
    ok = bc_hrbl_writer_finalize_to_buffer(writer, &buffer, &size);
    bc_hrbl_writer_destroy(writer);
    if (!ok) {
        return false;
    }
    *out_hrbl_buffer = buffer;
    *out_hrbl_size = size;
    return true;
}
