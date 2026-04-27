// SPDX-License-Identifier: MIT

#include "bc_hrbl_convert.h"
#include "bc_hrbl_writer.h"

#include "bc_allocators.h"
#include "bc_core_memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <json-c/json.h>
#include <json-c/json_object.h>
#include <json-c/json_object_iterator.h>
#include <json-c/json_tokener.h>

static void bc_hrbl_convert_error_set(bc_hrbl_convert_error_t* sink, const char* message, size_t byte_offset)
{
    if (sink == NULL) {
        return;
    }
    sink->message = message;
    sink->byte_offset = byte_offset;
    sink->line = 0u;
    sink->column = 0u;
}

static bool bc_hrbl_convert_emit_value_keyed(bc_hrbl_writer_t* writer, const char* key, size_t key_length,
                                             struct json_object* value, bc_hrbl_convert_error_t* sink);
static bool bc_hrbl_convert_emit_value_indexed(bc_hrbl_writer_t* writer, struct json_object* value,
                                               bc_hrbl_convert_error_t* sink);

static bool bc_hrbl_convert_emit_object_body(bc_hrbl_writer_t* writer, struct json_object* object,
                                             bc_hrbl_convert_error_t* sink)
{
    struct json_object_iterator it = json_object_iter_begin(object);
    struct json_object_iterator end = json_object_iter_end(object);
    while (!json_object_iter_equal(&it, &end)) {
        const char* key = json_object_iter_peek_name(&it);
        struct json_object* value = json_object_iter_peek_value(&it);
        size_t key_length = 0u;
        if (key != NULL) {
            (void)bc_core_length(key, '\0', &key_length);
        }
        if (!bc_hrbl_convert_emit_value_keyed(writer, key, key_length, value, sink)) {
            return false;
        }
        json_object_iter_next(&it);
    }
    return true;
}

static bool bc_hrbl_convert_emit_array_body(bc_hrbl_writer_t* writer, struct json_object* array,
                                            bc_hrbl_convert_error_t* sink)
{
    size_t length = json_object_array_length(array);
    for (size_t i = 0u; i < length; i += 1u) {
        struct json_object* element = json_object_array_get_idx(array, i);
        if (!bc_hrbl_convert_emit_value_indexed(writer, element, sink)) {
            return false;
        }
    }
    return true;
}

static bool bc_hrbl_convert_emit_number_keyed(bc_hrbl_writer_t* writer, const char* key, size_t key_length,
                                              struct json_object* value, bc_hrbl_convert_error_t* sink)
{
    enum json_type type = json_object_get_type(value);
    if (type == json_type_int) {
        int64_t signed_value = json_object_get_int64(value);
        if (signed_value == INT64_MAX) {
            uint64_t unsigned_value = json_object_get_uint64(value);
            if (unsigned_value > (uint64_t)INT64_MAX) {
                return bc_hrbl_writer_set_uint64(writer, key, key_length, unsigned_value);
            }
        }
        return bc_hrbl_writer_set_int64(writer, key, key_length, signed_value);
    }
    if (type == json_type_double) {
        double d = json_object_get_double(value);
        return bc_hrbl_writer_set_float64(writer, key, key_length, d);
    }
    bc_hrbl_convert_error_set(sink, "unsupported numeric type", 0u);
    return false;
}

static bool bc_hrbl_convert_emit_number_indexed(bc_hrbl_writer_t* writer, struct json_object* value,
                                                bc_hrbl_convert_error_t* sink)
{
    enum json_type type = json_object_get_type(value);
    if (type == json_type_int) {
        int64_t signed_value = json_object_get_int64(value);
        if (signed_value == INT64_MAX) {
            uint64_t unsigned_value = json_object_get_uint64(value);
            if (unsigned_value > (uint64_t)INT64_MAX) {
                return bc_hrbl_writer_append_uint64(writer, unsigned_value);
            }
        }
        return bc_hrbl_writer_append_int64(writer, signed_value);
    }
    if (type == json_type_double) {
        double d = json_object_get_double(value);
        return bc_hrbl_writer_append_float64(writer, d);
    }
    bc_hrbl_convert_error_set(sink, "unsupported numeric type", 0u);
    return false;
}

static bool bc_hrbl_convert_emit_value_keyed(bc_hrbl_writer_t* writer, const char* key, size_t key_length,
                                             struct json_object* value, bc_hrbl_convert_error_t* sink)
{
    if (value == NULL) {
        return bc_hrbl_writer_set_null(writer, key, key_length);
    }
    enum json_type type = json_object_get_type(value);
    switch (type) {
    case json_type_null:
        return bc_hrbl_writer_set_null(writer, key, key_length);
    case json_type_boolean:
        return bc_hrbl_writer_set_bool(writer, key, key_length, json_object_get_boolean(value) != 0);
    case json_type_int:
    case json_type_double:
        return bc_hrbl_convert_emit_number_keyed(writer, key, key_length, value, sink);
    case json_type_string: {
        const char* data = json_object_get_string(value);
        int length = json_object_get_string_len(value);
        if (length < 0) {
            bc_hrbl_convert_error_set(sink, "invalid string length", 0u);
            return false;
        }
        return bc_hrbl_writer_set_string(writer, key, key_length, data, (size_t)length);
    }
    case json_type_array:
        if (!bc_hrbl_writer_begin_array(writer, key, key_length)) {
            bc_hrbl_convert_error_set(sink, "writer begin_array failed", 0u);
            return false;
        }
        if (!bc_hrbl_convert_emit_array_body(writer, value, sink)) {
            return false;
        }
        if (!bc_hrbl_writer_end_array(writer)) {
            bc_hrbl_convert_error_set(sink, "writer end_array failed", 0u);
            return false;
        }
        return true;
    case json_type_object:
        if (!bc_hrbl_writer_begin_block(writer, key, key_length)) {
            bc_hrbl_convert_error_set(sink, "writer begin_block failed", 0u);
            return false;
        }
        if (!bc_hrbl_convert_emit_object_body(writer, value, sink)) {
            return false;
        }
        if (!bc_hrbl_writer_end_block(writer)) {
            bc_hrbl_convert_error_set(sink, "writer end_block failed", 0u);
            return false;
        }
        return true;
    }
    bc_hrbl_convert_error_set(sink, "unknown JSON type", 0u);
    return false;
}

static bool bc_hrbl_convert_emit_value_indexed(bc_hrbl_writer_t* writer, struct json_object* value,
                                               bc_hrbl_convert_error_t* sink)
{
    if (value == NULL) {
        return bc_hrbl_writer_append_null(writer);
    }
    enum json_type type = json_object_get_type(value);
    switch (type) {
    case json_type_null:
        return bc_hrbl_writer_append_null(writer);
    case json_type_boolean:
        return bc_hrbl_writer_append_bool(writer, json_object_get_boolean(value) != 0);
    case json_type_int:
    case json_type_double:
        return bc_hrbl_convert_emit_number_indexed(writer, value, sink);
    case json_type_string: {
        const char* data = json_object_get_string(value);
        int length = json_object_get_string_len(value);
        if (length < 0) {
            bc_hrbl_convert_error_set(sink, "invalid string length", 0u);
            return false;
        }
        return bc_hrbl_writer_append_string(writer, data, (size_t)length);
    }
    case json_type_array:
        if (!bc_hrbl_writer_begin_array(writer, NULL, 0u)) {
            bc_hrbl_convert_error_set(sink, "writer begin_array failed", 0u);
            return false;
        }
        if (!bc_hrbl_convert_emit_array_body(writer, value, sink)) {
            return false;
        }
        if (!bc_hrbl_writer_end_array(writer)) {
            bc_hrbl_convert_error_set(sink, "writer end_array failed", 0u);
            return false;
        }
        return true;
    case json_type_object:
        if (!bc_hrbl_writer_begin_block(writer, NULL, 0u)) {
            bc_hrbl_convert_error_set(sink, "writer begin_block failed", 0u);
            return false;
        }
        if (!bc_hrbl_convert_emit_object_body(writer, value, sink)) {
            return false;
        }
        if (!bc_hrbl_writer_end_block(writer)) {
            bc_hrbl_convert_error_set(sink, "writer end_block failed", 0u);
            return false;
        }
        return true;
    }
    bc_hrbl_convert_error_set(sink, "unknown JSON type", 0u);
    return false;
}

bool bc_hrbl_convert_json_to_writer(bc_hrbl_writer_t* writer, const char* json_text, size_t text_length,
                                    bc_hrbl_convert_error_t* out_error)
{
    if (out_error != NULL) {
        out_error->message = NULL;
        out_error->byte_offset = 0u;
        out_error->line = 0u;
        out_error->column = 0u;
    }
    if (writer == NULL || json_text == NULL) {
        return false;
    }
    if (text_length > (size_t)INT32_MAX) {
        bc_hrbl_convert_error_set(out_error, "input larger than json-c INT32_MAX limit", text_length);
        return false;
    }

    struct json_tokener* tokener = json_tokener_new();
    if (tokener == NULL) {
        bc_hrbl_convert_error_set(out_error, "json_tokener_new failed", 0u);
        return false;
    }
    json_tokener_set_flags(tokener, 0);
    struct json_object* root = json_tokener_parse_ex(tokener, json_text, (int)text_length);
    enum json_tokener_error err = json_tokener_get_error(tokener);
    if (err != json_tokener_success) {
        size_t byte_offset = (size_t)json_tokener_get_parse_end(tokener);
        bc_hrbl_convert_error_set(out_error, json_tokener_error_desc(err), byte_offset);
        if (root != NULL) {
            json_object_put(root);
        }
        json_tokener_free(tokener);
        return false;
    }
    size_t parse_end = (size_t)json_tokener_get_parse_end(tokener);
    json_tokener_free(tokener);

    if (root == NULL) {
        bc_hrbl_convert_error_set(out_error, "empty JSON input", 0u);
        return false;
    }
    if (parse_end < text_length) {
        size_t remaining = text_length - parse_end;
        for (size_t i = 0u; i < remaining; i += 1u) {
            char c = json_text[parse_end + i];
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
                bc_hrbl_convert_error_set(out_error, "trailing content after root value", parse_end + i);
                json_object_put(root);
                return false;
            }
        }
    }
    if (json_object_get_type(root) != json_type_object) {
        bc_hrbl_convert_error_set(out_error, "root value must be an object", 0u);
        json_object_put(root);
        return false;
    }

    bool ok = bc_hrbl_convert_emit_object_body(writer, root, out_error);
    json_object_put(root);
    return ok;
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
