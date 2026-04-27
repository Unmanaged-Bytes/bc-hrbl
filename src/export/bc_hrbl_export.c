// SPDX-License-Identifier: MIT

#include "bc_hrbl_export.h"
#include "bc_hrbl_reader.h"
#include "bc_hrbl_reader_internal.h"
#include "bc_hrbl_format_internal.h"

#include "bc_allocators.h"
#include "bc_allocators_pool.h"
#include "bc_core.h"
#include "bc_core_memory.h"

#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static void* bc_hrbl_export_scratch_alloc(bc_allocators_context_t* memory_context, size_t bytes)
{
    void* pointer = NULL;
    if (!bc_allocators_pool_allocate(memory_context, bytes, &pointer)) {
        return NULL;
    }
    return pointer;
}

static void bc_hrbl_export_scratch_free(bc_allocators_context_t* memory_context, void* pointer)
{
    if (pointer == NULL) {
        return;
    }
    bc_allocators_pool_free(memory_context, pointer);
}
typedef struct bc_hrbl_export_state {
    FILE*                           stream;
    const bc_hrbl_reader_t*         reader;
    const bc_hrbl_export_options_t* options;
    bool                            write_failed;
} bc_hrbl_export_state_t;

static bool bc_hrbl_export_write_all(bc_hrbl_export_state_t* state, const char* data, size_t length)
{
    if (state->write_failed) {
        return false;
    }
    if (length == 0u) {
        return true;
    }
    if (fwrite(data, 1u, length, state->stream) != length) {
        state->write_failed = true;
        return false;
    }
    return true;
}

static bool bc_hrbl_export_write_literal(bc_hrbl_export_state_t* state, const char* literal)
{
    size_t length = 0u;
    (void)bc_core_length(literal, '\0', &length);
    return bc_hrbl_export_write_all(state, literal, length);
}

static bool bc_hrbl_export_write_indent(bc_hrbl_export_state_t* state, unsigned int depth)
{
    unsigned int spaces = state->options != NULL ? state->options->indent_spaces : 2u;
    if (spaces == 0u) {
        return true;
    }
    char buffer[64];
    (void)bc_core_fill(buffer, sizeof(buffer), (unsigned char)' ');
    size_t total = (size_t)spaces * (size_t)depth;
    while (total > 0u) {
        size_t chunk = total > sizeof(buffer) ? sizeof(buffer) : total;
        if (!bc_hrbl_export_write_all(state, buffer, chunk)) {
            return false;
        }
        total -= chunk;
    }
    return true;
}

static bool bc_hrbl_export_write_newline(bc_hrbl_export_state_t* state)
{
    unsigned int spaces = state->options != NULL ? state->options->indent_spaces : 2u;
    if (spaces == 0u) {
        return true;
    }
    return bc_hrbl_export_write_all(state, "\n", 1u);
}

static bool bc_hrbl_export_hex_escape(bc_hrbl_export_state_t* state, uint32_t codepoint)
{
    char buffer[8];
    int written = snprintf(buffer, sizeof(buffer), "\\u%04X", (unsigned int)codepoint);
    if (written < 0 || (size_t)written >= sizeof(buffer)) {
        state->write_failed = true;
        return false;
    }
    return bc_hrbl_export_write_all(state, buffer, (size_t)written);
}

static bool bc_hrbl_export_utf8_decode(const unsigned char* data, size_t length, size_t index, uint32_t* out_codepoint,
                                       size_t* out_consumed)
{
    unsigned char byte = data[index];
    if (byte < 0x80u) {
        *out_codepoint = (uint32_t)byte;
        *out_consumed = 1u;
        return true;
    }
    size_t sequence_length;
    uint32_t codepoint;
    if ((byte & 0xE0u) == 0xC0u) {
        sequence_length = 2u;
        codepoint = (uint32_t)(byte & 0x1Fu);
    } else if ((byte & 0xF0u) == 0xE0u) {
        sequence_length = 3u;
        codepoint = (uint32_t)(byte & 0x0Fu);
    } else if ((byte & 0xF8u) == 0xF0u) {
        sequence_length = 4u;
        codepoint = (uint32_t)(byte & 0x07u);
    } else {
        return false;
    }
    if (index + sequence_length > length) {
        return false;
    }
    for (size_t k = 1u; k < sequence_length; k += 1u) {
        unsigned char continuation = data[index + k];
        if ((continuation & 0xC0u) != 0x80u) {
            return false;
        }
        codepoint = (codepoint << 6) | (uint32_t)(continuation & 0x3Fu);
    }
    *out_codepoint = codepoint;
    *out_consumed = sequence_length;
    return true;
}

static bool bc_hrbl_export_write_json_string(bc_hrbl_export_state_t* state, const char* data, size_t length)
{
    if (!bc_hrbl_export_write_all(state, "\"", 1u)) {
        return false;
    }
    bool ascii_only = state->options != NULL ? state->options->ascii_only : false;
    const unsigned char* bytes = (const unsigned char*)data;
    size_t index = 0u;
    while (index < length) {
        unsigned char byte = bytes[index];
        const char* replacement = NULL;
        switch (byte) {
        case '\"': replacement = "\\\""; break;
        case '\\': replacement = "\\\\"; break;
        case '\b': replacement = "\\b";  break;
        case '\f': replacement = "\\f";  break;
        case '\n': replacement = "\\n";  break;
        case '\r': replacement = "\\r";  break;
        case '\t': replacement = "\\t";  break;
        default:   replacement = NULL;   break;
        }
        if (replacement != NULL) {
            if (!bc_hrbl_export_write_literal(state, replacement)) {
                return false;
            }
            index += 1u;
            continue;
        }
        if (byte < 0x20u) {
            if (!bc_hrbl_export_hex_escape(state, (uint32_t)byte)) {
                return false;
            }
            index += 1u;
            continue;
        }
        if (byte < 0x80u) {
            if (!bc_hrbl_export_write_all(state, (const char*)&byte, 1u)) {
                return false;
            }
            index += 1u;
            continue;
        }
        uint32_t codepoint = 0u;
        size_t consumed = 0u;
        if (!bc_hrbl_export_utf8_decode(bytes, length, index, &codepoint, &consumed)) {
            state->write_failed = true;
            return false;
        }
        if (!ascii_only) {
            if (!bc_hrbl_export_write_all(state, (const char*)&bytes[index], consumed)) {
                return false;
            }
            index += consumed;
            continue;
        }
        if (codepoint <= 0xFFFFu) {
            if (!bc_hrbl_export_hex_escape(state, codepoint)) {
                return false;
            }
        } else {
            uint32_t adjusted = codepoint - 0x10000u;
            uint32_t high = 0xD800u + (adjusted >> 10);
            uint32_t low = 0xDC00u + (adjusted & 0x3FFu);
            if (!bc_hrbl_export_hex_escape(state, high)) {
                return false;
            }
            if (!bc_hrbl_export_hex_escape(state, low)) {
                return false;
            }
        }
        index += consumed;
    }
    return bc_hrbl_export_write_all(state, "\"", 1u);
}

static bool bc_hrbl_export_write_number_int64(bc_hrbl_export_state_t* state, int64_t value)
{
    char buffer[32];
    int written = snprintf(buffer, sizeof(buffer), "%" PRId64, value);
    if (written < 0 || (size_t)written >= sizeof(buffer)) {
        state->write_failed = true;
        return false;
    }
    return bc_hrbl_export_write_all(state, buffer, (size_t)written);
}

static bool bc_hrbl_export_write_number_uint64(bc_hrbl_export_state_t* state, uint64_t value)
{
    char buffer[32];
    int written = snprintf(buffer, sizeof(buffer), "%" PRIu64, value);
    if (written < 0 || (size_t)written >= sizeof(buffer)) {
        state->write_failed = true;
        return false;
    }
    return bc_hrbl_export_write_all(state, buffer, (size_t)written);
}

static bool bc_hrbl_export_write_number_float64(bc_hrbl_export_state_t* state, double value)
{
    if (isnan(value) || isinf(value)) {
        return bc_hrbl_export_write_literal(state, "null");
    }
    char buffer[64];
    int written = snprintf(buffer, sizeof(buffer), "%.17g", value);
    if (written < 0 || (size_t)written >= sizeof(buffer)) {
        state->write_failed = true;
        return false;
    }
    bool has_dot_or_exp = false;
    for (int i = 0; i < written; i += 1) {
        char c = buffer[i];
        if (c == '.' || c == 'e' || c == 'E') {
            has_dot_or_exp = true;
            break;
        }
    }
    if (!bc_hrbl_export_write_all(state, buffer, (size_t)written)) {
        return false;
    }
    if (!has_dot_or_exp) {
        if (!bc_hrbl_export_write_all(state, ".0", 2u)) {
            return false;
        }
    }
    return true;
}

typedef struct bc_hrbl_export_sort_entry {
    uint64_t entry_offset;
    uint64_t key_pool_offset;
    uint32_t key_length;
    uint64_t value_offset;
} bc_hrbl_export_sort_entry_t;

static int bc_hrbl_export_sort_compare(const void* left, const void* right)
{
    const bc_hrbl_export_sort_entry_t* a = (const bc_hrbl_export_sort_entry_t*)left;
    const bc_hrbl_export_sort_entry_t* b = (const bc_hrbl_export_sort_entry_t*)right;
    if (a->entry_offset < b->entry_offset) {
        return -1;
    }
    if (a->entry_offset > b->entry_offset) {
        return 1;
    }
    return 0;
}

/* cppcheck-suppress [constParameterCallback] */
static int bc_hrbl_export_sort_by_key(const void* left, const void* right, void* cookie)
{
    const bc_hrbl_export_sort_entry_t* a = (const bc_hrbl_export_sort_entry_t*)left;
    const bc_hrbl_export_sort_entry_t* b = (const bc_hrbl_export_sort_entry_t*)right;
    const bc_hrbl_reader_t* reader = (const bc_hrbl_reader_t*)cookie;
    const char* a_data = (const char*)&reader->base[(size_t)a->key_pool_offset + sizeof(uint32_t)];
    const char* b_data = (const char*)&reader->base[(size_t)b->key_pool_offset + sizeof(uint32_t)];
    size_t a_length = (size_t)a->key_length;
    size_t b_length = (size_t)b->key_length;
    size_t min_length = a_length < b_length ? a_length : b_length;
    int cmp = __builtin_memcmp(a_data, b_data, min_length);
    if (cmp != 0) {
        return cmp;
    }
    if (a_length < b_length) {
        return -1;
    }
    if (a_length > b_length) {
        return 1;
    }
    return 0;
}

static bool bc_hrbl_export_value(bc_hrbl_export_state_t* state, uint64_t value_offset, unsigned int depth);

static bool bc_hrbl_export_block(bc_hrbl_export_state_t* state, uint64_t block_offset, unsigned int depth)
{
    bc_hrbl_block_header_t header;
    uint64_t entries_offset = 0u;
    if (!bc_hrbl_reader_block_body_offsets(state->reader, block_offset, &header, &entries_offset)) {
        state->write_failed = true;
        return false;
    }
    if (header.child_count == 0u) {
        return bc_hrbl_export_write_literal(state, "{}");
    }
    if (!bc_hrbl_export_write_literal(state, "{")) {
        return false;
    }
    if (!bc_hrbl_export_write_newline(state)) {
        return false;
    }

    bool sort_keys = state->options != NULL ? state->options->sort_keys : true;
    size_t byte_size = (size_t)header.child_count * sizeof(bc_hrbl_export_sort_entry_t);
    bc_hrbl_export_sort_entry_t* entries = (bc_hrbl_export_sort_entry_t*)bc_hrbl_export_scratch_alloc(state->reader->memory_context, byte_size);
    if (entries == NULL) {
        state->write_failed = true;
        return false;
    }
    for (uint32_t i = 0u; i < header.child_count; i += 1u) {
        bc_hrbl_entry_t raw;
        (void)bc_core_copy(&raw, &state->reader->base[entries_offset + (uint64_t)i * BC_HRBL_BLOCK_ENTRY_SIZE], sizeof(raw));
        entries[i].entry_offset = entries_offset + (uint64_t)i * BC_HRBL_BLOCK_ENTRY_SIZE;
        entries[i].key_pool_offset = raw.key_pool_offset;
        entries[i].key_length = raw.key_length;
        entries[i].value_offset = raw.value_offset;
    }
    if (sort_keys) {
        qsort_r(entries, header.child_count, sizeof(*entries), bc_hrbl_export_sort_by_key, (void*)state->reader);
    } else {
        qsort(entries, header.child_count, sizeof(*entries), bc_hrbl_export_sort_compare);
    }

    for (uint32_t i = 0u; i < header.child_count; i += 1u) {
        if (i != 0u) {
            if (!bc_hrbl_export_write_all(state, ",", 1u)) {
                bc_hrbl_export_scratch_free(state->reader->memory_context, entries);
                return false;
            }
            if (!bc_hrbl_export_write_newline(state)) {
                bc_hrbl_export_scratch_free(state->reader->memory_context, entries);
                return false;
            }
        }
        if (!bc_hrbl_export_write_indent(state, depth + 1u)) {
            bc_hrbl_export_scratch_free(state->reader->memory_context, entries);
            return false;
        }
        const char* key_data = (const char*)&state->reader->base[(size_t)entries[i].key_pool_offset + sizeof(uint32_t)];
        if (!bc_hrbl_export_write_json_string(state, key_data, (size_t)entries[i].key_length)) {
            bc_hrbl_export_scratch_free(state->reader->memory_context, entries);
            return false;
        }
        unsigned int spaces = state->options != NULL ? state->options->indent_spaces : 2u;
        if (spaces == 0u) {
            if (!bc_hrbl_export_write_all(state, ":", 1u)) {
                bc_hrbl_export_scratch_free(state->reader->memory_context, entries);
                return false;
            }
        } else {
            if (!bc_hrbl_export_write_all(state, ": ", 2u)) {
                bc_hrbl_export_scratch_free(state->reader->memory_context, entries);
                return false;
            }
        }
        if (!bc_hrbl_export_value(state, entries[i].value_offset, depth + 1u)) {
            bc_hrbl_export_scratch_free(state->reader->memory_context, entries);
            return false;
        }
    }
    bc_hrbl_export_scratch_free(state->reader->memory_context, entries);
    if (!bc_hrbl_export_write_newline(state)) {
        return false;
    }
    if (!bc_hrbl_export_write_indent(state, depth)) {
        return false;
    }
    return bc_hrbl_export_write_all(state, "}", 1u);
}

static bool bc_hrbl_export_array(bc_hrbl_export_state_t* state, uint64_t array_offset, unsigned int depth)
{
    bc_hrbl_array_header_t header;
    uint64_t elements_offset = 0u;
    if (!bc_hrbl_reader_array_body_offsets(state->reader, array_offset, &header, &elements_offset)) {
        state->write_failed = true;
        return false;
    }
    if (header.element_count == 0u) {
        return bc_hrbl_export_write_literal(state, "[]");
    }
    if (!bc_hrbl_export_write_literal(state, "[")) {
        return false;
    }
    if (!bc_hrbl_export_write_newline(state)) {
        return false;
    }
    for (uint32_t i = 0u; i < header.element_count; i += 1u) {
        if (i != 0u) {
            if (!bc_hrbl_export_write_all(state, ",", 1u)) {
                return false;
            }
            if (!bc_hrbl_export_write_newline(state)) {
                return false;
            }
        }
        if (!bc_hrbl_export_write_indent(state, depth + 1u)) {
            return false;
        }
        uint64_t value_offset = 0u;
        (void)bc_core_copy(&value_offset, &state->reader->base[elements_offset + (uint64_t)i * BC_HRBL_ARRAY_ELEMENT_SIZE],
               sizeof(value_offset));
        if (!bc_hrbl_export_value(state, value_offset, depth + 1u)) {
            return false;
        }
    }
    if (!bc_hrbl_export_write_newline(state)) {
        return false;
    }
    if (!bc_hrbl_export_write_indent(state, depth)) {
        return false;
    }
    return bc_hrbl_export_write_all(state, "]", 1u);
}

static bool bc_hrbl_export_value(bc_hrbl_export_state_t* state, uint64_t value_offset, unsigned int depth)
{
    bc_hrbl_kind_t kind;
    if (!bc_hrbl_reader_kind_at(state->reader, value_offset, &kind)) {
        state->write_failed = true;
        return false;
    }
    switch (kind) {
    case BC_HRBL_KIND_NULL:
        return bc_hrbl_export_write_literal(state, "null");
    case BC_HRBL_KIND_FALSE:
        return bc_hrbl_export_write_literal(state, "false");
    case BC_HRBL_KIND_TRUE:
        return bc_hrbl_export_write_literal(state, "true");
    case BC_HRBL_KIND_INT64: {
        int64_t value = 0;
        if (!bc_hrbl_reader_scalar_int64_at(state->reader, value_offset, &value)) {
            state->write_failed = true;
            return false;
        }
        return bc_hrbl_export_write_number_int64(state, value);
    }
    case BC_HRBL_KIND_UINT64: {
        uint64_t value = 0u;
        if (!bc_hrbl_reader_scalar_uint64_at(state->reader, value_offset, &value)) {
            state->write_failed = true;
            return false;
        }
        return bc_hrbl_export_write_number_uint64(state, value);
    }
    case BC_HRBL_KIND_FLOAT64: {
        double value = 0.0;
        if (!bc_hrbl_reader_scalar_float64_at(state->reader, value_offset, &value)) {
            state->write_failed = true;
            return false;
        }
        return bc_hrbl_export_write_number_float64(state, value);
    }
    case BC_HRBL_KIND_STRING: {
        const char* data = NULL;
        size_t length = 0u;
        if (!bc_hrbl_reader_scalar_string_at(state->reader, value_offset, &data, &length)) {
            state->write_failed = true;
            return false;
        }
        return bc_hrbl_export_write_json_string(state, data, length);
    }
    case BC_HRBL_KIND_BLOCK:
        return bc_hrbl_export_block(state, value_offset, depth);
    case BC_HRBL_KIND_ARRAY:
        return bc_hrbl_export_array(state, value_offset, depth);
    }
    state->write_failed = true;
    return false;
}

bool bc_hrbl_export_json(const bc_hrbl_reader_t* reader, FILE* stream)
{
    bc_hrbl_export_options_t options;
    options.indent_spaces = 2u;
    options.sort_keys = true;
    options.ascii_only = false;
    return bc_hrbl_export_json_ex(reader, stream, &options);
}

bool bc_hrbl_export_json_ex(const bc_hrbl_reader_t* reader, FILE* stream, const bc_hrbl_export_options_t* options)
{
    if (reader == NULL || stream == NULL) {
        return false;
    }

    bc_hrbl_export_state_t state;
    state.stream = stream;
    state.reader = reader;
    state.options = options;
    state.write_failed = false;

    uint64_t root_count = reader->header->root_count;
    if (root_count == 0u) {
        if (!bc_hrbl_export_write_literal(&state, "{}")) {
            return false;
        }
        if (!bc_hrbl_export_write_newline(&state)) {
            return false;
        }
        return !state.write_failed;
    }

    if (!bc_hrbl_export_write_literal(&state, "{")) {
        return false;
    }
    if (!bc_hrbl_export_write_newline(&state)) {
        return false;
    }

    bool sort_keys = options != NULL ? options->sort_keys : true;
    bc_hrbl_export_sort_entry_t* entries = (bc_hrbl_export_sort_entry_t*)bc_hrbl_export_scratch_alloc(reader->memory_context, (size_t)root_count * sizeof(*entries));
    if (entries == NULL) {
        return false;
    }
    for (uint64_t i = 0u; i < root_count; i += 1u) {
        bc_hrbl_entry_t raw;
        (void)bc_core_copy(&raw, &reader->base[reader->header->root_index_offset + i * BC_HRBL_ROOT_ENTRY_SIZE], sizeof(raw));
        entries[i].entry_offset = reader->header->root_index_offset + i * BC_HRBL_ROOT_ENTRY_SIZE;
        entries[i].key_pool_offset = raw.key_pool_offset;
        entries[i].key_length = raw.key_length;
        entries[i].value_offset = raw.value_offset;
    }
    if (sort_keys) {
        qsort_r(entries, (size_t)root_count, sizeof(*entries), bc_hrbl_export_sort_by_key, (void*)reader);
    } else {
        qsort(entries, (size_t)root_count, sizeof(*entries), bc_hrbl_export_sort_compare);
    }

    bool ok = true;
    for (uint64_t i = 0u; i < root_count; i += 1u) {
        if (i != 0u) {
            if (!bc_hrbl_export_write_all(&state, ",", 1u)) {
                ok = false;
                break;
            }
            if (!bc_hrbl_export_write_newline(&state)) {
                ok = false;
                break;
            }
        }
        if (!bc_hrbl_export_write_indent(&state, 1u)) {
            ok = false;
            break;
        }
        const char* key_data = (const char*)&reader->base[(size_t)entries[i].key_pool_offset + sizeof(uint32_t)];
        if (!bc_hrbl_export_write_json_string(&state, key_data, (size_t)entries[i].key_length)) {
            ok = false;
            break;
        }
        unsigned int spaces = options != NULL ? options->indent_spaces : 2u;
        if (spaces == 0u) {
            if (!bc_hrbl_export_write_all(&state, ":", 1u)) {
                ok = false;
                break;
            }
        } else {
            if (!bc_hrbl_export_write_all(&state, ": ", 2u)) {
                ok = false;
                break;
            }
        }
        if (!bc_hrbl_export_value(&state, entries[i].value_offset, 1u)) {
            ok = false;
            break;
        }
    }
    bc_hrbl_export_scratch_free(reader->memory_context, entries);
    if (!ok) {
        return false;
    }
    if (!bc_hrbl_export_write_newline(&state)) {
        return false;
    }
    if (!bc_hrbl_export_write_all(&state, "}", 1u)) {
        return false;
    }
    if (!bc_hrbl_export_write_newline(&state)) {
        return false;
    }
    return !state.write_failed;
}
