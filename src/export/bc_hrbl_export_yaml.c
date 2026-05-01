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

#include <yaml.h>

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
typedef struct bc_hrbl_yaml_state {
    bc_core_writer_t* writer;
    const bc_hrbl_reader_t* reader;
    const bc_hrbl_export_options_t* options;
    yaml_emitter_t emitter;
    bool failed;
} bc_hrbl_yaml_state_t;

/* cppcheck-suppress [constParameterCallback,constParameterPointer] */
static int bc_hrbl_yaml_write_handler(void* user_data, unsigned char* buffer, size_t size)
{
    bc_hrbl_yaml_state_t* state = (bc_hrbl_yaml_state_t*)user_data;
    if (size == 0u) {
        return 1;
    }
    if (!bc_core_writer_write_bytes(state->writer, buffer, size)) {
        state->failed = true;
        return 0;
    }
    return 1;
}

static bool bc_hrbl_yaml_emit_scalar_str(bc_hrbl_yaml_state_t* state, const char* data, size_t length)
{
    yaml_event_t event;
    yaml_scalar_event_initialize(&event, NULL, NULL, (yaml_char_t*)(void*)data, (int)length, 1, 1, YAML_PLAIN_SCALAR_STYLE);
    if (yaml_emitter_emit(&state->emitter, &event) == 0) {
        state->failed = true;
        return false;
    }
    return true;
}

static bool bc_hrbl_yaml_emit_scalar_double_quoted(bc_hrbl_yaml_state_t* state, const char* data, size_t length)
{
    yaml_event_t event;
    yaml_scalar_event_initialize(&event, NULL, NULL, (yaml_char_t*)(void*)data, (int)length, 0, 1, YAML_DOUBLE_QUOTED_SCALAR_STYLE);
    if (yaml_emitter_emit(&state->emitter, &event) == 0) {
        state->failed = true;
        return false;
    }
    return true;
}

static bool bc_hrbl_yaml_emit_mapping_start(bc_hrbl_yaml_state_t* state)
{
    yaml_event_t event;
    yaml_mapping_start_event_initialize(&event, NULL, NULL, 1, YAML_BLOCK_MAPPING_STYLE);
    if (yaml_emitter_emit(&state->emitter, &event) == 0) {
        state->failed = true;
        return false;
    }
    return true;
}

static bool bc_hrbl_yaml_emit_mapping_end(bc_hrbl_yaml_state_t* state)
{
    yaml_event_t event;
    yaml_mapping_end_event_initialize(&event);
    if (yaml_emitter_emit(&state->emitter, &event) == 0) {
        state->failed = true;
        return false;
    }
    return true;
}

static bool bc_hrbl_yaml_emit_sequence_start(bc_hrbl_yaml_state_t* state)
{
    yaml_event_t event;
    yaml_sequence_start_event_initialize(&event, NULL, NULL, 1, YAML_BLOCK_SEQUENCE_STYLE);
    if (yaml_emitter_emit(&state->emitter, &event) == 0) {
        state->failed = true;
        return false;
    }
    return true;
}

static bool bc_hrbl_yaml_emit_sequence_end(bc_hrbl_yaml_state_t* state)
{
    yaml_event_t event;
    yaml_sequence_end_event_initialize(&event);
    if (yaml_emitter_emit(&state->emitter, &event) == 0) {
        state->failed = true;
        return false;
    }
    return true;
}

typedef struct bc_hrbl_yaml_sort_entry {
    uint64_t key_pool_offset;
    uint32_t key_length;
    uint64_t value_offset;
} bc_hrbl_yaml_sort_entry_t;

/* cppcheck-suppress [constParameterCallback] */
static bool bc_hrbl_yaml_sort_less_by_key(const void* left, const void* right, void* user_data)
{
    const bc_hrbl_yaml_sort_entry_t* a = (const bc_hrbl_yaml_sort_entry_t*)left;
    const bc_hrbl_yaml_sort_entry_t* b = (const bc_hrbl_yaml_sort_entry_t*)right;
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

static bool bc_hrbl_yaml_emit_value(bc_hrbl_yaml_state_t* state, uint64_t value_offset);

static bool bc_hrbl_yaml_emit_block(bc_hrbl_yaml_state_t* state, uint64_t block_offset)
{
    bc_hrbl_block_header_t header;
    uint64_t entries_offset = 0u;
    if (!bc_hrbl_reader_block_body_offsets(state->reader, block_offset, &header, &entries_offset)) {
        state->failed = true;
        return false;
    }
    if (!bc_hrbl_yaml_emit_mapping_start(state)) {
        return false;
    }
    bool sort_keys = state->options != NULL ? state->options->sort_keys : true;
    bc_hrbl_yaml_sort_entry_t* entries = NULL;
    if (header.child_count != 0u) {
        entries = (bc_hrbl_yaml_sort_entry_t*)bc_hrbl_export_scratch_alloc(state->reader->memory_context,
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
            (void)bc_core_sort_with_compare(entries, header.child_count, sizeof(*entries), bc_hrbl_yaml_sort_less_by_key,
                                            (void*)state->reader);
        }
    }
    for (uint32_t i = 0u; i < header.child_count; i += 1u) {
        const char* key_data = (const char*)&state->reader->base[(size_t)entries[i].key_pool_offset + sizeof(uint32_t)];
        if (!bc_hrbl_yaml_emit_scalar_double_quoted(state, key_data, (size_t)entries[i].key_length)) {
            bc_hrbl_export_scratch_free(state->reader->memory_context, entries);
            return false;
        }
        if (!bc_hrbl_yaml_emit_value(state, entries[i].value_offset)) {
            bc_hrbl_export_scratch_free(state->reader->memory_context, entries);
            return false;
        }
    }
    bc_hrbl_export_scratch_free(state->reader->memory_context, entries);
    return bc_hrbl_yaml_emit_mapping_end(state);
}

static bool bc_hrbl_yaml_emit_array(bc_hrbl_yaml_state_t* state, uint64_t array_offset)
{
    bc_hrbl_array_header_t header;
    uint64_t elements_offset = 0u;
    if (!bc_hrbl_reader_array_body_offsets(state->reader, array_offset, &header, &elements_offset)) {
        state->failed = true;
        return false;
    }
    if (!bc_hrbl_yaml_emit_sequence_start(state)) {
        return false;
    }
    for (uint32_t i = 0u; i < header.element_count; i += 1u) {
        uint64_t value_offset = 0u;
        (void)bc_core_copy(&value_offset, &state->reader->base[elements_offset + (uint64_t)i * BC_HRBL_ARRAY_ELEMENT_SIZE],
                           sizeof(value_offset));
        if (!bc_hrbl_yaml_emit_value(state, value_offset)) {
            return false;
        }
    }
    return bc_hrbl_yaml_emit_sequence_end(state);
}

static bool bc_hrbl_yaml_emit_value(bc_hrbl_yaml_state_t* state, uint64_t value_offset)
{
    bc_hrbl_kind_t kind;
    if (!bc_hrbl_reader_kind_at(state->reader, value_offset, &kind)) {
        state->failed = true;
        return false;
    }
    char buffer[64];
    switch (kind) {
    case BC_HRBL_KIND_NULL:
        return bc_hrbl_yaml_emit_scalar_str(state, "null", 4u);
    case BC_HRBL_KIND_FALSE:
        return bc_hrbl_yaml_emit_scalar_str(state, "false", 5u);
    case BC_HRBL_KIND_TRUE:
        return bc_hrbl_yaml_emit_scalar_str(state, "true", 4u);
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
        return bc_hrbl_yaml_emit_scalar_str(state, buffer, formatted);
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
        return bc_hrbl_yaml_emit_scalar_str(state, buffer, formatted);
    }
    case BC_HRBL_KIND_FLOAT64: {
        double v = 0.0;
        if (!bc_hrbl_reader_scalar_float64_at(state->reader, value_offset, &v)) {
            state->failed = true;
            return false;
        }
        if (isnan(v)) {
            return bc_hrbl_yaml_emit_scalar_str(state, ".nan", 4u);
        }
        if (isinf(v)) {
            return bc_hrbl_yaml_emit_scalar_str(state, v < 0.0 ? "-.inf" : ".inf", v < 0.0 ? 5u : 4u);
        }
        size_t n = 0;
        if (!bc_core_format_double_shortest_round_trip(buffer, sizeof(buffer), v, &n)) {
            state->failed = true;
            return false;
        }
        return bc_hrbl_yaml_emit_scalar_str(state, buffer, n);
    }
    case BC_HRBL_KIND_STRING: {
        const char* data = NULL;
        size_t length = 0u;
        if (!bc_hrbl_reader_scalar_string_at(state->reader, value_offset, &data, &length)) {
            state->failed = true;
            return false;
        }
        return bc_hrbl_yaml_emit_scalar_double_quoted(state, data, length);
    }
    case BC_HRBL_KIND_BLOCK:
        return bc_hrbl_yaml_emit_block(state, value_offset);
    case BC_HRBL_KIND_ARRAY:
        return bc_hrbl_yaml_emit_array(state, value_offset);
    }
    state->failed = true;
    return false;
}

bool bc_hrbl_export_yaml(const bc_hrbl_reader_t* reader, bc_core_writer_t* writer)
{
    return bc_hrbl_export_yaml_ex(reader, writer, NULL);
}

bool bc_hrbl_export_yaml_ex(const bc_hrbl_reader_t* reader, bc_core_writer_t* writer, const bc_hrbl_export_options_t* options)
{
    bc_hrbl_yaml_state_t state;
    state.writer = writer;
    state.reader = reader;
    state.options = options;
    state.failed = false;

    if (yaml_emitter_initialize(&state.emitter) == 0) {
        return false;
    }
    yaml_emitter_set_output(&state.emitter, bc_hrbl_yaml_write_handler, &state);
    yaml_emitter_set_width(&state.emitter, -1);
    yaml_emitter_set_unicode(&state.emitter, options != NULL ? options->ascii_only == false : 1);

    yaml_event_t event;
    yaml_stream_start_event_initialize(&event, YAML_UTF8_ENCODING);
    if (yaml_emitter_emit(&state.emitter, &event) == 0) {
        yaml_emitter_delete(&state.emitter);
        return false;
    }
    yaml_document_start_event_initialize(&event, NULL, NULL, NULL, 1);
    if (yaml_emitter_emit(&state.emitter, &event) == 0) {
        yaml_emitter_delete(&state.emitter);
        return false;
    }

    uint64_t root_count = reader->header->root_count;
    bool ok = true;
    if (root_count == 0u) {
        yaml_event_t empty;
        yaml_mapping_start_event_initialize(&empty, NULL, NULL, 1, YAML_BLOCK_MAPPING_STYLE);
        if (yaml_emitter_emit(&state.emitter, &empty) == 0) {
            yaml_emitter_delete(&state.emitter);
            return false;
        }
        yaml_mapping_end_event_initialize(&empty);
        if (yaml_emitter_emit(&state.emitter, &empty) == 0) {
            yaml_emitter_delete(&state.emitter);
            return false;
        }
    } else {
        bool sort_keys = options != NULL ? options->sort_keys : true;
        bc_hrbl_yaml_sort_entry_t* entries =
            (bc_hrbl_yaml_sort_entry_t*)bc_hrbl_export_scratch_alloc(reader->memory_context, (size_t)root_count * sizeof(*entries));
        if (entries == NULL) {
            yaml_emitter_delete(&state.emitter);
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
            (void)bc_core_sort_with_compare(entries, (size_t)root_count, sizeof(*entries), bc_hrbl_yaml_sort_less_by_key, (void*)reader);
        }
        if (!bc_hrbl_yaml_emit_mapping_start(&state)) {
            bc_hrbl_export_scratch_free(reader->memory_context, entries);
            yaml_emitter_delete(&state.emitter);
            return false;
        }
        for (uint64_t i = 0u; i < root_count && ok; i += 1u) {
            const char* key_data = (const char*)&reader->base[(size_t)entries[i].key_pool_offset + sizeof(uint32_t)];
            if (!bc_hrbl_yaml_emit_scalar_double_quoted(&state, key_data, (size_t)entries[i].key_length)) {
                ok = false;
                break;
            }
            if (!bc_hrbl_yaml_emit_value(&state, entries[i].value_offset)) {
                ok = false;
                break;
            }
        }
        bc_hrbl_export_scratch_free(reader->memory_context, entries);
        if (!bc_hrbl_yaml_emit_mapping_end(&state)) {
            ok = false;
        }
    }

    yaml_document_end_event_initialize(&event, 1);
    if (yaml_emitter_emit(&state.emitter, &event) == 0) {
        ok = false;
    }
    yaml_stream_end_event_initialize(&event);
    if (yaml_emitter_emit(&state.emitter, &event) == 0) {
        ok = false;
    }
    yaml_emitter_flush(&state.emitter);
    yaml_emitter_delete(&state.emitter);
    return ok && !state.failed;
}
