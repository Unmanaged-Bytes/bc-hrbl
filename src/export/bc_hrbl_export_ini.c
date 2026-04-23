// SPDX-License-Identifier: MIT

#include "bc_hrbl_export.h"
#include "bc_hrbl_reader.h"
#include "bc_hrbl_reader_internal.h"
#include "bc_hrbl_format_internal.h"

#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct bc_hrbl_ini_state {
    FILE*                           stream;
    const bc_hrbl_reader_t*         reader;
    const bc_hrbl_export_options_t* options;
    bool                            failed;
} bc_hrbl_ini_state_t;

static bool bc_hrbl_ini_write(bc_hrbl_ini_state_t* state, const char* data, size_t length)
{
    if (state->failed) {
        return false;
    }
    if (length == 0u) {
        return true;
    }
    if (fwrite(data, 1u, length, state->stream) != length) {
        state->failed = true;
        return false;
    }
    return true;
}

static bool bc_hrbl_ini_write_literal(bc_hrbl_ini_state_t* state, const char* literal)
{
    return bc_hrbl_ini_write(state, literal, strlen(literal));
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
        case '\\': chunk = "\\\\"; chunk_length = 2u; break;
        case '"':  chunk = "\\\""; chunk_length = 2u; break;
        case '\n': chunk = "\\n";  chunk_length = 2u; break;
        case '\r': chunk = "\\r";  chunk_length = 2u; break;
        case '\t': chunk = "\\t";  chunk_length = 2u; break;
        default:   break;
        }
        if (chunk != NULL) {
            if (!bc_hrbl_ini_write(state, chunk, chunk_length)) {
                return false;
            }
            continue;
        }
        if (c < 0x20u) {
            char buf[8];
            int n = snprintf(buf, sizeof(buf), "\\x%02X", (unsigned int)c);
            if (n < 0) {
                state->failed = true;
                return false;
            }
            if (!bc_hrbl_ini_write(state, buf, (size_t)n)) {
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
        int n = snprintf(buffer, sizeof(buffer), "%" PRId64, v);
        return n >= 0 && bc_hrbl_ini_write(state, buffer, (size_t)n);
    }
    case BC_HRBL_KIND_UINT64: {
        uint64_t v = 0u;
        if (!bc_hrbl_reader_scalar_uint64_at(state->reader, value_offset, &v)) {
            state->failed = true;
            return false;
        }
        int n = snprintf(buffer, sizeof(buffer), "%" PRIu64, v);
        return n >= 0 && bc_hrbl_ini_write(state, buffer, (size_t)n);
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
        int n = snprintf(buffer, sizeof(buffer), "%.17g", v);
        return n >= 0 && bc_hrbl_ini_write(state, buffer, (size_t)n);
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
static int bc_hrbl_ini_sort_by_key(const void* left, const void* right, void* cookie)
{
    const bc_hrbl_ini_sort_entry_t* a = (const bc_hrbl_ini_sort_entry_t*)left;
    const bc_hrbl_ini_sort_entry_t* b = (const bc_hrbl_ini_sort_entry_t*)right;
    const bc_hrbl_reader_t* reader = (const bc_hrbl_reader_t*)cookie;
    const char* a_data = (const char*)&reader->base[(size_t)a->key_pool_offset + sizeof(uint32_t)];
    const char* b_data = (const char*)&reader->base[(size_t)b->key_pool_offset + sizeof(uint32_t)];
    size_t a_length = (size_t)a->key_length;
    size_t b_length = (size_t)b->key_length;
    size_t min_length = a_length < b_length ? a_length : b_length;
    int cmp = memcmp(a_data, b_data, min_length);
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
        char* new_section = (char*)malloc(new_section_length + 1u);
        if (new_section == NULL) {
            state->failed = true;
            return false;
        }
        if (section_length != 0u) {
            memcpy(new_section, section, section_length);
            new_section[section_length] = '.';
            memcpy(new_section + section_length + 1u, key, key_length);
        } else {
            memcpy(new_section, key, key_length);
        }
        new_section[new_section_length] = '\0';
        bool ok = bc_hrbl_ini_emit_block(state, new_section, new_section_length, value_offset, false);
        free(new_section);
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
            memcpy(&element_offset, &state->reader->base[elements_offset + (uint64_t)i * BC_HRBL_ARRAY_ELEMENT_SIZE],
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
        entries = (bc_hrbl_ini_sort_entry_t*)malloc((size_t)header.child_count * sizeof(*entries));
        if (entries == NULL) {
            state->failed = true;
            return false;
        }
        for (uint32_t i = 0u; i < header.child_count; i += 1u) {
            bc_hrbl_entry_t raw;
            memcpy(&raw, &state->reader->base[entries_offset + (uint64_t)i * BC_HRBL_BLOCK_ENTRY_SIZE], sizeof(raw));
            entries[i].key_pool_offset = raw.key_pool_offset;
            entries[i].key_length = raw.key_length;
            entries[i].value_offset = raw.value_offset;
        }
        if (sort_keys) {
            qsort_r(entries, header.child_count, sizeof(*entries), bc_hrbl_ini_sort_by_key, (void*)state->reader);
        }
    }

    if (!is_root && section_length != 0u) {
        if (!bc_hrbl_ini_write_literal(state, "[")) {
            free(entries);
            return false;
        }
        if (!bc_hrbl_ini_write(state, section, section_length)) {
            free(entries);
            return false;
        }
        if (!bc_hrbl_ini_write_literal(state, "]\n")) {
            free(entries);
            return false;
        }
    }

    bc_hrbl_ini_sort_entry_t* blocks_to_emit = NULL;
    size_t blocks_count = 0u;
    for (uint32_t i = 0u; i < header.child_count; i += 1u) {
        bc_hrbl_kind_t kind;
        if (!bc_hrbl_reader_kind_at(state->reader, entries[i].value_offset, &kind)) {
            free(entries);
            state->failed = true;
            return false;
        }
        const char* key_data = (const char*)&state->reader->base[(size_t)entries[i].key_pool_offset + sizeof(uint32_t)];
        if (kind == BC_HRBL_KIND_BLOCK) {
            if (blocks_to_emit == NULL) {
                blocks_to_emit = (bc_hrbl_ini_sort_entry_t*)malloc((size_t)header.child_count * sizeof(*blocks_to_emit));
                if (blocks_to_emit == NULL) {
                    free(entries);
                    state->failed = true;
                    return false;
                }
            }
            blocks_to_emit[blocks_count] = entries[i];
            blocks_count += 1u;
            continue;
        }
        if (!bc_hrbl_ini_emit_entry(state, section, section_length, key_data, (size_t)entries[i].key_length,
                                    entries[i].value_offset)) {
            free(entries);
            free(blocks_to_emit);
            return false;
        }
    }
    free(entries);
    if (blocks_count != 0u) {
        if (!bc_hrbl_ini_write_literal(state, "\n")) {
            free(blocks_to_emit);
            return false;
        }
        for (size_t i = 0u; i < blocks_count; i += 1u) {
            const char* key_data = (const char*)&state->reader->base[(size_t)blocks_to_emit[i].key_pool_offset + sizeof(uint32_t)];
            if (!bc_hrbl_ini_emit_entry(state, section, section_length, key_data, (size_t)blocks_to_emit[i].key_length,
                                        blocks_to_emit[i].value_offset)) {
                free(blocks_to_emit);
                return false;
            }
        }
    }
    free(blocks_to_emit);
    return true;
}

bool bc_hrbl_export_ini(const bc_hrbl_reader_t* reader, FILE* stream)
{
    return bc_hrbl_export_ini_ex(reader, stream, NULL);
}

bool bc_hrbl_export_ini_ex(const bc_hrbl_reader_t* reader, FILE* stream, const bc_hrbl_export_options_t* options)
{
    if (reader == NULL || stream == NULL) {
        return false;
    }
    bc_hrbl_ini_state_t state;
    state.stream = stream;
    state.reader = reader;
    state.options = options;
    state.failed = false;

    uint64_t root_count = reader->header->root_count;
    if (root_count == 0u) {
        return true;
    }

    bool sort_keys = options != NULL ? options->sort_keys : true;
    bc_hrbl_ini_sort_entry_t* entries = (bc_hrbl_ini_sort_entry_t*)malloc((size_t)root_count * sizeof(*entries));
    if (entries == NULL) {
        return false;
    }
    for (uint64_t i = 0u; i < root_count; i += 1u) {
        bc_hrbl_entry_t raw;
        memcpy(&raw, &reader->base[reader->header->root_index_offset + i * BC_HRBL_ROOT_ENTRY_SIZE], sizeof(raw));
        entries[i].key_pool_offset = raw.key_pool_offset;
        entries[i].key_length = raw.key_length;
        entries[i].value_offset = raw.value_offset;
    }
    if (sort_keys) {
        qsort_r(entries, (size_t)root_count, sizeof(*entries), bc_hrbl_ini_sort_by_key, (void*)reader);
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
                blocks_to_emit = (bc_hrbl_ini_sort_entry_t*)malloc((size_t)root_count * sizeof(*blocks_to_emit));
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
    free(entries);
    for (size_t i = 0u; i < blocks_count && !state.failed; i += 1u) {
        const char* key_data = (const char*)&reader->base[(size_t)blocks_to_emit[i].key_pool_offset + sizeof(uint32_t)];
        if (!bc_hrbl_ini_emit_entry(&state, "", 0u, key_data, (size_t)blocks_to_emit[i].key_length,
                                    blocks_to_emit[i].value_offset)) {
            break;
        }
    }
    free(blocks_to_emit);
    return !state.failed;
}
