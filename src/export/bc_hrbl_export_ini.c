// SPDX-License-Identifier: MIT

#include "bc_hrbl_export.h"
#include "bc_hrbl_reader.h"
#include "bc_hrbl_reader_internal.h"
#include "bc_hrbl_format_internal.h"

#include "bc_allocators.h"
#include "bc_allocators_pool.h"
#include "bc_core.h"
#include "bc_core_format.h"
#include "bc_core_io.h"
#include "bc_core_memory.h"
#include "bc_core_sort.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

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
typedef struct bc_hrbl_ini_state {
    bc_core_writer_t* writer;
    const bc_hrbl_reader_t* reader;
    const bc_hrbl_export_options_t* options;
    bool failed;
} bc_hrbl_ini_state_t;

static bool bc_hrbl_ini_write(bc_hrbl_ini_state_t* state, const char* data, size_t length)
{
    if (state->failed) {
        return false;
    }
    if (length == 0u) {
        return true;
    }
    if (!bc_core_writer_write_bytes(state->writer, data, length)) {
        state->failed = true;
        return false;
    }
    return true;
}

static bool bc_hrbl_ini_write_literal(bc_hrbl_ini_state_t* state, const char* literal)
{
    size_t length = 0u;
    (void)bc_core_length(literal, '\0', &length);
    return bc_hrbl_ini_write(state, literal, length);
}

static bool bc_hrbl_ini_write_escaped(bc_hrbl_ini_state_t* state, const char* data, size_t length)
{
    if (!bc_hrbl_ini_write_literal(state, "\"")) {
        return false;
    }
    for (size_t i = 0u; i < length; i += 1u) {
        unsigned char c = (unsigned char)data[i];
        char escape[3] = {'\\', 0, 0};
        const char* chunk = NULL;
        size_t chunk_length = 0u;
        switch (c) {
        case '\\':
            chunk = "\\\\";
            chunk_length = 2u;
            break;
        case '"':
            chunk = "\\\"";
            chunk_length = 2u;
            break;
        case '\n':
            chunk = "\\n";
            chunk_length = 2u;
            break;
        case '\r':
            chunk = "\\r";
            chunk_length = 2u;
            break;
        case '\t':
            chunk = "\\t";
            chunk_length = 2u;
            break;
        default:
            break;
        }
        if (chunk != NULL) {
            if (!bc_hrbl_ini_write(state, chunk, chunk_length)) {
                return false;
            }
            continue;
        }
        if (c < 0x20u) {
            char hex_buffer[2];
            size_t hex_length = 0u;
            if (!bc_core_format_unsigned_integer_64_hexadecimal_padded(hex_buffer, sizeof(hex_buffer), (uint64_t)c, 2u, &hex_length)) {
                state->failed = true;
                return false;
            }
            /* bc_core_format hex output is lowercase; legacy "\\x%02X" was uppercase. */
            for (size_t k = 0u; k < hex_length; k += 1u) {
                char ch = hex_buffer[k];
                if (ch >= 'a' && ch <= 'f') {
                    hex_buffer[k] = (char)(ch - ('a' - 'A'));
                }
            }
            if (!bc_hrbl_ini_write_literal(state, "\\x")) {
                return false;
            }
            if (!bc_hrbl_ini_write(state, hex_buffer, hex_length)) {
                return false;
            }
            continue;
        }
        escape[0] = (char)c;
        if (!bc_hrbl_ini_write(state, escape, 1u)) {
            return false;
        }
    }
    return bc_hrbl_ini_write_literal(state, "\"");
}

static bool bc_hrbl_ini_write_scalar_value(bc_hrbl_ini_state_t* state, uint64_t value_offset)
{
    bc_hrbl_kind_t kind;
    if (!bc_hrbl_reader_kind_at(state->reader, value_offset, &kind)) {
        state->failed = true;
        return false;
    }
    char buffer[64];
    switch (kind) {
    case BC_HRBL_KIND_NULL:
        return bc_hrbl_ini_write_literal(state, "");
    case BC_HRBL_KIND_FALSE:
        return bc_hrbl_ini_write_literal(state, "false");
    case BC_HRBL_KIND_TRUE:
        return bc_hrbl_ini_write_literal(state, "true");
    case BC_HRBL_KIND_INT64: {
        int64_t v = 0;
        if (!bc_hrbl_reader_scalar_int64_at(state->reader, value_offset, &v)) {
            state->failed = true;
            return false;
        }
        size_t formatted = 0u;
        if (!bc_core_format_signed_integer_64(buffer, sizeof(buffer), v, &formatted)) {
            state->failed = true;
            return false;
        }
        return bc_hrbl_ini_write(state, buffer, formatted);
    }
    case BC_HRBL_KIND_UINT64: {
        uint64_t v = 0u;
        if (!bc_hrbl_reader_scalar_uint64_at(state->reader, value_offset, &v)) {
            state->failed = true;
            return false;
        }
        size_t formatted = 0u;
        if (!bc_core_format_unsigned_integer_64_decimal(buffer, sizeof(buffer), v, &formatted)) {
            state->failed = true;
            return false;
        }
        return bc_hrbl_ini_write(state, buffer, formatted);
    }
    case BC_HRBL_KIND_FLOAT64: {
        double v = 0.0;
        if (!bc_hrbl_reader_scalar_float64_at(state->reader, value_offset, &v)) {
            state->failed = true;
            return false;
        }
        if (isnan(v) || isinf(v)) {
            return bc_hrbl_ini_write_literal(state, "");
        }
        size_t n = 0;
        return bc_core_format_double_shortest_round_trip(buffer, sizeof(buffer), v, &n) && bc_hrbl_ini_write(state, buffer, n);
    }
    case BC_HRBL_KIND_STRING: {
        const char* data = NULL;
        size_t length = 0u;
        if (!bc_hrbl_reader_scalar_string_at(state->reader, value_offset, &data, &length)) {
            state->failed = true;
            return false;
        }
        return bc_hrbl_ini_write_escaped(state, data, length);
    }
    case BC_HRBL_KIND_BLOCK:
    case BC_HRBL_KIND_ARRAY:
        return false;
    }
    return false;
}

typedef struct bc_hrbl_ini_sort_entry {
    uint64_t key_pool_offset;
    uint32_t key_length;
    uint64_t value_offset;
} bc_hrbl_ini_sort_entry_t;

/* cppcheck-suppress [constParameterCallback] */
static bool bc_hrbl_ini_sort_less_by_key(const void* left, const void* right, void* user_data)
{
    const bc_hrbl_ini_sort_entry_t* a = (const bc_hrbl_ini_sort_entry_t*)left;
    const bc_hrbl_ini_sort_entry_t* b = (const bc_hrbl_ini_sort_entry_t*)right;
    const bc_hrbl_reader_t* reader = (const bc_hrbl_reader_t*)user_data;
    const char* a_data = (const char*)&reader->base[(size_t)a->key_pool_offset + sizeof(uint32_t)];
    const char* b_data = (const char*)&reader->base[(size_t)b->key_pool_offset + sizeof(uint32_t)];
    size_t a_length = (size_t)a->key_length;
    size_t b_length = (size_t)b->key_length;
    size_t min_length = a_length < b_length ? a_length : b_length;
    int cmp = bc_core_memcmp(a_data, b_data, min_length);
    if (cmp != 0) {
        return cmp < 0;
    }
    return a_length < b_length;
}

static bool bc_hrbl_ini_emit_block(bc_hrbl_ini_state_t* state, const char* section, size_t section_length, uint64_t block_offset,
                                   bool is_root);

static bool bc_hrbl_ini_emit_entry(bc_hrbl_ini_state_t* state, const char* section, size_t section_length, const char* key,
                                   size_t key_length, uint64_t value_offset)
{
    bc_hrbl_kind_t kind;
    if (!bc_hrbl_reader_kind_at(state->reader, value_offset, &kind)) {
        state->failed = true;
        return false;
    }
    if (kind == BC_HRBL_KIND_BLOCK) {
        size_t new_section_length = section_length + key_length;
        if (section_length != 0u) {
            new_section_length += 1u;
        }
        char* new_section = (char*)bc_hrbl_export_scratch_alloc(state->reader->memory_context, new_section_length + 1u);
        if (new_section == NULL) {
            state->failed = true;
            return false;
        }
        if (section_length != 0u) {
            (void)bc_core_copy(new_section, section, section_length);
            new_section[section_length] = '.';
            (void)bc_core_copy(new_section + section_length + 1u, key, key_length);
        } else {
            (void)bc_core_copy(new_section, key, key_length);
        }
        new_section[new_section_length] = '\0';
        bool ok = bc_hrbl_ini_emit_block(state, new_section, new_section_length, value_offset, false);
        bc_hrbl_export_scratch_free(state->reader->memory_context, new_section);
        return ok;
    }
    if (kind == BC_HRBL_KIND_ARRAY) {
        bc_hrbl_array_header_t header;
        uint64_t elements_offset = 0u;
        if (!bc_hrbl_reader_array_body_offsets(state->reader, value_offset, &header, &elements_offset)) {
            state->failed = true;
            return false;
        }
        for (uint32_t i = 0u; i < header.element_count; i += 1u) {
            uint64_t element_offset = 0u;
            (void)bc_core_copy(&element_offset, &state->reader->base[elements_offset + (uint64_t)i * BC_HRBL_ARRAY_ELEMENT_SIZE],
                               sizeof(element_offset));
            bc_hrbl_kind_t element_kind;
            if (!bc_hrbl_reader_kind_at(state->reader, element_offset, &element_kind)) {
                state->failed = true;
                return false;
            }
            if (element_kind == BC_HRBL_KIND_BLOCK || element_kind == BC_HRBL_KIND_ARRAY) {
                state->failed = true;
                return false;
            }
            if (!bc_hrbl_ini_write(state, key, key_length)) {
                return false;
            }
            if (!bc_hrbl_ini_write_literal(state, "[]=")) {
                return false;
            }
            if (!bc_hrbl_ini_write_scalar_value(state, element_offset)) {
                return false;
            }
            if (!bc_hrbl_ini_write_literal(state, "\n")) {
                return false;
            }
        }
        return true;
    }
    if (!bc_hrbl_ini_write(state, key, key_length)) {
        return false;
    }
    if (!bc_hrbl_ini_write_literal(state, " = ")) {
        return false;
    }
    if (!bc_hrbl_ini_write_scalar_value(state, value_offset)) {
        return false;
    }
    return bc_hrbl_ini_write_literal(state, "\n");
}

static bool bc_hrbl_ini_emit_block(bc_hrbl_ini_state_t* state, const char* section, size_t section_length, uint64_t block_offset,
                                   bool is_root)
{
    bc_hrbl_block_header_t header;
    uint64_t entries_offset = 0u;
    if (!bc_hrbl_reader_block_body_offsets(state->reader, block_offset, &header, &entries_offset)) {
        state->failed = true;
        return false;
    }

    bool sort_keys = state->options != NULL ? state->options->sort_keys : true;
    bc_hrbl_ini_sort_entry_t* entries = NULL;
    if (header.child_count != 0u) {
        entries = (bc_hrbl_ini_sort_entry_t*)bc_hrbl_export_scratch_alloc(state->reader->memory_context,
                                                                          (size_t)header.child_count * sizeof(*entries));
        if (entries == NULL) {
            state->failed = true;
            return false;
        }
        for (uint32_t i = 0u; i < header.child_count; i += 1u) {
            bc_hrbl_entry_t raw;
            (void)bc_core_copy(&raw, &state->reader->base[entries_offset + (uint64_t)i * BC_HRBL_BLOCK_ENTRY_SIZE], sizeof(raw));
            entries[i].key_pool_offset = raw.key_pool_offset;
            entries[i].key_length = raw.key_length;
            entries[i].value_offset = raw.value_offset;
        }
        if (sort_keys) {
            (void)bc_core_sort_with_compare(entries, header.child_count, sizeof(*entries), bc_hrbl_ini_sort_less_by_key,
                                            (void*)state->reader);
        }
    }

    if (!is_root && section_length != 0u) {
        if (!bc_hrbl_ini_write_literal(state, "[")) {
            bc_hrbl_export_scratch_free(state->reader->memory_context, entries);
            return false;
        }
        if (!bc_hrbl_ini_write(state, section, section_length)) {
            bc_hrbl_export_scratch_free(state->reader->memory_context, entries);
            return false;
        }
        if (!bc_hrbl_ini_write_literal(state, "]\n")) {
            bc_hrbl_export_scratch_free(state->reader->memory_context, entries);
            return false;
        }
    }

    bc_hrbl_ini_sort_entry_t* blocks_to_emit = NULL;
    size_t blocks_count = 0u;
    for (uint32_t i = 0u; i < header.child_count; i += 1u) {
        bc_hrbl_kind_t kind;
        if (!bc_hrbl_reader_kind_at(state->reader, entries[i].value_offset, &kind)) {
            bc_hrbl_export_scratch_free(state->reader->memory_context, entries);
            state->failed = true;
            return false;
        }
        const char* key_data = (const char*)&state->reader->base[(size_t)entries[i].key_pool_offset + sizeof(uint32_t)];
        if (kind == BC_HRBL_KIND_BLOCK) {
            if (blocks_to_emit == NULL) {
                blocks_to_emit = (bc_hrbl_ini_sort_entry_t*)bc_hrbl_export_scratch_alloc(
                    state->reader->memory_context, (size_t)header.child_count * sizeof(*blocks_to_emit));
                if (blocks_to_emit == NULL) {
                    bc_hrbl_export_scratch_free(state->reader->memory_context, entries);
                    state->failed = true;
                    return false;
                }
            }
            blocks_to_emit[blocks_count] = entries[i];
            blocks_count += 1u;
            continue;
        }
        if (!bc_hrbl_ini_emit_entry(state, section, section_length, key_data, (size_t)entries[i].key_length, entries[i].value_offset)) {
            bc_hrbl_export_scratch_free(state->reader->memory_context, entries);
            bc_hrbl_export_scratch_free(state->reader->memory_context, blocks_to_emit);
            return false;
        }
    }
    bc_hrbl_export_scratch_free(state->reader->memory_context, entries);
    if (blocks_count != 0u) {
        if (!bc_hrbl_ini_write_literal(state, "\n")) {
            bc_hrbl_export_scratch_free(state->reader->memory_context, blocks_to_emit);
            return false;
        }
        for (size_t i = 0u; i < blocks_count; i += 1u) {
            const char* key_data = (const char*)&state->reader->base[(size_t)blocks_to_emit[i].key_pool_offset + sizeof(uint32_t)];
            if (!bc_hrbl_ini_emit_entry(state, section, section_length, key_data, (size_t)blocks_to_emit[i].key_length,
                                        blocks_to_emit[i].value_offset)) {
                bc_hrbl_export_scratch_free(state->reader->memory_context, blocks_to_emit);
                return false;
            }
        }
    }
    bc_hrbl_export_scratch_free(state->reader->memory_context, blocks_to_emit);
    return true;
}

bool bc_hrbl_export_ini(const bc_hrbl_reader_t* reader, bc_core_writer_t* writer)
{
    return bc_hrbl_export_ini_ex(reader, writer, NULL);
}

bool bc_hrbl_export_ini_ex(const bc_hrbl_reader_t* reader, bc_core_writer_t* writer, const bc_hrbl_export_options_t* options)
{
    bc_hrbl_ini_state_t state;
    state.writer = writer;
    state.reader = reader;
    state.options = options;
    state.failed = false;

    uint64_t root_count = reader->header->root_count;
    if (root_count == 0u) {
        return true;
    }

    bool sort_keys = options != NULL ? options->sort_keys : true;
    bc_hrbl_ini_sort_entry_t* entries =
        (bc_hrbl_ini_sort_entry_t*)bc_hrbl_export_scratch_alloc(reader->memory_context, (size_t)root_count * sizeof(*entries));
    if (entries == NULL) {
        return false;
    }
    for (uint64_t i = 0u; i < root_count; i += 1u) {
        bc_hrbl_entry_t raw;
        (void)bc_core_copy(&raw, &reader->base[reader->header->root_index_offset + i * BC_HRBL_ROOT_ENTRY_SIZE], sizeof(raw));
        entries[i].key_pool_offset = raw.key_pool_offset;
        entries[i].key_length = raw.key_length;
        entries[i].value_offset = raw.value_offset;
    }
    if (sort_keys) {
        (void)bc_core_sort_with_compare(entries, (size_t)root_count, sizeof(*entries), bc_hrbl_ini_sort_less_by_key, (void*)reader);
    }

    bc_hrbl_ini_sort_entry_t* blocks_to_emit = NULL;
    size_t blocks_count = 0u;
    for (uint64_t i = 0u; i < root_count && !state.failed; i += 1u) {
        bc_hrbl_kind_t kind;
        if (!bc_hrbl_reader_kind_at(reader, entries[i].value_offset, &kind)) {
            state.failed = true;
            break;
        }
        const char* key_data = (const char*)&reader->base[(size_t)entries[i].key_pool_offset + sizeof(uint32_t)];
        if (kind == BC_HRBL_KIND_BLOCK) {
            if (blocks_to_emit == NULL) {
                blocks_to_emit = (bc_hrbl_ini_sort_entry_t*)bc_hrbl_export_scratch_alloc(reader->memory_context,
                                                                                         (size_t)root_count * sizeof(*blocks_to_emit));
                if (blocks_to_emit == NULL) {
                    state.failed = true;
                    break;
                }
            }
            blocks_to_emit[blocks_count] = entries[i];
            blocks_count += 1u;
            continue;
        }
        if (!bc_hrbl_ini_emit_entry(&state, "", 0u, key_data, (size_t)entries[i].key_length, entries[i].value_offset)) {
            break;
        }
    }
    bc_hrbl_export_scratch_free(reader->memory_context, entries);
    for (size_t i = 0u; i < blocks_count && !state.failed; i += 1u) {
        const char* key_data = (const char*)&reader->base[(size_t)blocks_to_emit[i].key_pool_offset + sizeof(uint32_t)];
        if (!bc_hrbl_ini_emit_entry(&state, "", 0u, key_data, (size_t)blocks_to_emit[i].key_length, blocks_to_emit[i].value_offset)) {
            break;
        }
    }
    bc_hrbl_export_scratch_free(reader->memory_context, blocks_to_emit);
    return !state.failed;
}
