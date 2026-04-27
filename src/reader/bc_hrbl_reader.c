// SPDX-License-Identifier: MIT

#include "bc_hrbl_reader.h"
#include "bc_hrbl_reader_internal.h"
#include "bc_hrbl_format_internal.h"

#include "bc_allocators.h"
#include "bc_allocators_pool.h"
#include "bc_core.h"
#include "bc_core_memory.h"
#include "bc_io_mmap.h"

#include <stdlib.h>
#include <string.h>

#include <xxhash.h>

static bool bc_hrbl_reader_header_layout_valid(const bc_hrbl_header_t* header, size_t size)
{
    if (header->magic != BC_HRBL_MAGIC) {
        return false;
    }
    if (header->version_major != BC_HRBL_VERSION_MAJOR) {
        return false;
    }
    if (header->version_minor > BC_HRBL_VERSION_MINOR) {
        return false;
    }
    if ((header->flags & BC_HRBL_FLAGS_V1_REQUIRED) != BC_HRBL_FLAGS_V1_REQUIRED) {
        return false;
    }
    if ((header->flags & BC_HRBL_FLAGS_V1_RESERVED_MASK) != 0u) {
        return false;
    }
    if (header->file_size != (uint64_t)size) {
        return false;
    }
    if (header->root_index_offset != BC_HRBL_ROOT_INDEX_OFFSET) {
        return false;
    }
    if (header->root_count > BC_HRBL_ROOT_COUNT_MAX) {
        return false;
    }
    if (header->root_index_size != header->root_count * BC_HRBL_ROOT_ENTRY_SIZE) {
        return false;
    }
    if (header->nodes_offset != header->root_index_offset + header->root_index_size) {
        return false;
    }
    if (header->strings_offset != header->nodes_offset + header->nodes_size) {
        return false;
    }
    if (header->footer_offset != header->strings_offset + header->strings_size) {
        return false;
    }
    if (header->footer_offset + BC_HRBL_FOOTER_SIZE != header->file_size) {
        return false;
    }
    for (size_t i = 0u; i < sizeof(header->reserved); i += 1u) {
        if (header->reserved[i] != 0u) {
            return false;
        }
    }
    return true;
}

static bool bc_hrbl_reader_footer_matches(const uint8_t* base, const bc_hrbl_header_t* header)
{
    bc_hrbl_footer_t footer;
    bc_hrbl_load_footer(&footer, base + header->footer_offset);
    if (footer.magic_end != BC_HRBL_MAGIC) {
        return false;
    }
    if (footer.file_size != header->file_size) {
        return false;
    }
    if (footer.checksum_xxh3_64 != header->checksum_xxh3_64) {
        return false;
    }
    return true;
}

static bool bc_hrbl_reader_attach_buffer(bc_allocators_context_t* memory_context, const void* data, size_t size, bc_io_mmap_t* mmap_handle,
                                         bc_hrbl_reader_t** out_reader)
{
    if (size < BC_HRBL_HEADER_SIZE + BC_HRBL_FOOTER_SIZE) {
        return false;
    }
    const bc_hrbl_header_t* header = (const bc_hrbl_header_t*)data;
    if (!bc_hrbl_reader_header_layout_valid(header, size)) {
        return false;
    }
    if (!bc_hrbl_reader_footer_matches((const uint8_t*)data, header)) {
        return false;
    }

    void* reader_ptr = NULL;
    if (!bc_allocators_pool_allocate(memory_context, sizeof(bc_hrbl_reader_t), &reader_ptr)) {
        return false;
    }
    bc_hrbl_reader_t* reader = (bc_hrbl_reader_t*)reader_ptr;
    reader->memory_context = memory_context;
    reader->mmap_handle = mmap_handle;
    reader->base = (const uint8_t*)data;
    reader->size = size;
    reader->header = header;
    *out_reader = reader;
    return true;
}

bool bc_hrbl_reader_open(bc_allocators_context_t* memory_context, const char* path, bc_hrbl_reader_t** out_reader)
{
    if (out_reader == NULL) {
        return false;
    }
    *out_reader = NULL;

    bc_io_mmap_options_t options;
    bc_core_zero(&options, sizeof(options));
    options.read_only = true;
    options.madvise_hint = BC_IO_MADVISE_RANDOM;

    bc_io_mmap_t* handle = NULL;
    if (!bc_io_mmap_file(memory_context, path, &options, &handle)) {
        return false;
    }

    const void* data = NULL;
    size_t size = 0u;
    if (!bc_io_mmap_get_data(handle, &data, &size)) {
        bc_io_mmap_destroy(handle);
        return false;
    }

    if (!bc_hrbl_reader_attach_buffer(memory_context, data, size, handle, out_reader)) {
        bc_io_mmap_destroy(handle);
        return false;
    }
    return true;
}

bool bc_hrbl_reader_open_buffer(bc_allocators_context_t* memory_context, const void* data, size_t size, bc_hrbl_reader_t** out_reader)
{
    if (out_reader == NULL) {
        return false;
    }
    *out_reader = NULL;
    if (data == NULL) {
        return false;
    }
    return bc_hrbl_reader_attach_buffer(memory_context, data, size, NULL, out_reader);
}

void bc_hrbl_reader_destroy(bc_hrbl_reader_t* reader)
{
    if (reader->mmap_handle != NULL) {
        bc_io_mmap_destroy(reader->mmap_handle);
    }
    bc_allocators_pool_free(reader->memory_context, reader);
}

bool bc_hrbl_reader_root_count(const bc_hrbl_reader_t* reader, uint64_t* out_count)
{
    if (reader == NULL || out_count == NULL) {
        if (out_count != NULL) {
            *out_count = 0u;
        }
        return false;
    }
    *out_count = reader->header->root_count;
    return true;
}

static bool bc_hrbl_reader_read_entry(const bc_hrbl_reader_t* reader, uint64_t entry_offset, bc_hrbl_entry_t* out_entry)
{
    if (entry_offset + BC_HRBL_ROOT_ENTRY_SIZE > reader->size) {
        return false;
    }
    bc_hrbl_load_entry(out_entry, &reader->base[entry_offset]);
    return true;
}

static bool bc_hrbl_reader_key_equals(const bc_hrbl_reader_t* reader, const bc_hrbl_entry_t* entry, const char* key_data, size_t key_length)
{
    if ((size_t)entry->key_length != key_length) {
        return false;
    }
    if (key_length == 0u) {
        return true;
    }
    size_t pool_offset = (size_t)entry->key_pool_offset + sizeof(uint32_t);
    if (pool_offset + key_length > reader->size) {
        return false;
    }
    return __builtin_memcmp(&reader->base[pool_offset], key_data, key_length) == 0;
}

static bool bc_hrbl_reader_linear_probe_range(const bc_hrbl_reader_t* reader, uint64_t entries_start, uint64_t entry_count, uint64_t index,
                                              uint64_t target_hash, const char* key_data, size_t key_length, bc_hrbl_entry_t* out_entry)
{
    int64_t left = (int64_t)index;
    while (left >= 0) {
        uint64_t off = entries_start + (uint64_t)left * BC_HRBL_ROOT_ENTRY_SIZE;
        bc_hrbl_entry_t candidate;
        if (!bc_hrbl_reader_read_entry(reader, off, &candidate)) {
            return false;
        }
        if (candidate.key_hash64 != target_hash) {
            break;
        }
        if (bc_hrbl_reader_key_equals(reader, &candidate, key_data, key_length)) {
            *out_entry = candidate;
            return true;
        }
        left -= 1;
    }
    uint64_t right = index + 1u;
    while (right < entry_count) {
        uint64_t off = entries_start + right * BC_HRBL_ROOT_ENTRY_SIZE;
        bc_hrbl_entry_t candidate;
        if (!bc_hrbl_reader_read_entry(reader, off, &candidate)) {
            return false;
        }
        if (candidate.key_hash64 != target_hash) {
            break;
        }
        if (bc_hrbl_reader_key_equals(reader, &candidate, key_data, key_length)) {
            *out_entry = candidate;
            return true;
        }
        right += 1u;
    }
    return false;
}

static bool bc_hrbl_reader_binary_search_entries(const bc_hrbl_reader_t* reader, uint64_t entries_start, uint64_t entry_count,
                                                 uint64_t target_hash, const char* key_data, size_t key_length, bc_hrbl_entry_t* out_entry)
{
    if (entry_count == 0u) {
        return false;
    }
    uint64_t low = 0u;
    uint64_t high = entry_count;
    while (low < high) {
        uint64_t mid = low + (high - low) / 2u;
        uint64_t off = entries_start + mid * BC_HRBL_ROOT_ENTRY_SIZE;
        bc_hrbl_entry_t candidate;
        if (!bc_hrbl_reader_read_entry(reader, off, &candidate)) {
            return false;
        }
        if (candidate.key_hash64 < target_hash) {
            low = mid + 1u;
        } else if (candidate.key_hash64 > target_hash) {
            high = mid;
        } else {
            if (bc_hrbl_reader_key_equals(reader, &candidate, key_data, key_length)) {
                *out_entry = candidate;
                return true;
            }
            return bc_hrbl_reader_linear_probe_range(reader, entries_start, entry_count, mid, target_hash, key_data, key_length, out_entry);
        }
    }
    return false;
}

bool bc_hrbl_reader_root_at_offset(const bc_hrbl_reader_t* reader, uint64_t index, const char** out_key, uint32_t* out_key_length,
                                   uint64_t* out_value_offset)
{
    if (index >= reader->header->root_count) {
        return false;
    }
    uint64_t offset = reader->header->root_index_offset + index * BC_HRBL_ROOT_ENTRY_SIZE;
    bc_hrbl_entry_t entry;
    if (!bc_hrbl_reader_read_entry(reader, offset, &entry)) {
        return false;
    }
    if (out_key != NULL) {
        if ((size_t)entry.key_pool_offset + sizeof(uint32_t) + (size_t)entry.key_length > reader->size) {
            return false;
        }
        *out_key = (const char*)&reader->base[(size_t)entry.key_pool_offset + sizeof(uint32_t)];
    }
    if (out_key_length != NULL) {
        *out_key_length = entry.key_length;
    }
    if (out_value_offset != NULL) {
        *out_value_offset = entry.value_offset;
    }
    return true;
}

bool bc_hrbl_reader_find_root_offset(const bc_hrbl_reader_t* reader, const char* key, size_t key_length, uint64_t* out_value_offset)
{
    uint64_t hash = (uint64_t)XXH3_64bits(key, key_length);
    bc_hrbl_entry_t entry;
    if (!bc_hrbl_reader_binary_search_entries(reader, reader->header->root_index_offset, reader->header->root_count, hash, key, key_length,
                                              &entry)) {
        return false;
    }
    *out_value_offset = entry.value_offset;
    return true;
}

bool bc_hrbl_reader_kind_at(const bc_hrbl_reader_t* reader, uint64_t value_offset, bc_hrbl_kind_t* out_kind)
{
    if (value_offset < reader->header->nodes_offset || value_offset >= reader->header->nodes_offset + reader->header->nodes_size) {
        return false;
    }
    uint8_t byte = reader->base[value_offset];
    switch (byte) {
    case BC_HRBL_KIND_NULL:
    case BC_HRBL_KIND_FALSE:
    case BC_HRBL_KIND_TRUE:
    case BC_HRBL_KIND_INT64:
    case BC_HRBL_KIND_UINT64:
    case BC_HRBL_KIND_FLOAT64:
    case BC_HRBL_KIND_STRING:
    case BC_HRBL_KIND_BLOCK:
    case BC_HRBL_KIND_ARRAY:
        break;
    default:
        return false;
    }
    *out_kind = (bc_hrbl_kind_t)byte;
    return true;
}

bool bc_hrbl_reader_block_body_offsets(const bc_hrbl_reader_t* reader, uint64_t block_offset, bc_hrbl_block_header_t* out_header,
                                       uint64_t* out_entries_offset)
{
    if (block_offset >= reader->size) {
        return false;
    }
    if (reader->base[block_offset] != (uint8_t)BC_HRBL_KIND_BLOCK) {
        return false;
    }
    uint64_t body_offset = (uint64_t)bc_hrbl_align_up((size_t)block_offset + 1u, 8u);
    if (body_offset + sizeof(bc_hrbl_block_header_t) > reader->size) {
        return false;
    }
    __builtin_memcpy(out_header, &reader->base[body_offset], sizeof(*out_header));
    *out_entries_offset = body_offset + sizeof(bc_hrbl_block_header_t);
    if (*out_entries_offset + (uint64_t)out_header->child_count * BC_HRBL_BLOCK_ENTRY_SIZE > reader->size) {
        return false;
    }
    return true;
}

bool bc_hrbl_reader_block_child_count_at(const bc_hrbl_reader_t* reader, uint64_t block_offset, uint32_t* out_count)
{
    bc_hrbl_block_header_t header;
    uint64_t entries_offset = 0u;
    if (!bc_hrbl_reader_block_body_offsets(reader, block_offset, &header, &entries_offset)) {
        return false;
    }
    *out_count = header.child_count;
    return true;
}

bool bc_hrbl_reader_block_entry_at_offset(const bc_hrbl_reader_t* reader, uint64_t block_offset, uint32_t index, const char** out_key,
                                          uint32_t* out_key_length, uint64_t* out_value_offset)
{
    bc_hrbl_block_header_t header;
    uint64_t entries_offset = 0u;
    if (!bc_hrbl_reader_block_body_offsets(reader, block_offset, &header, &entries_offset)) {
        return false;
    }
    if (index >= header.child_count) {
        return false;
    }
    bc_hrbl_entry_t entry;
    if (!bc_hrbl_reader_read_entry(reader, entries_offset + (uint64_t)index * BC_HRBL_BLOCK_ENTRY_SIZE, &entry)) {
        return false;
    }
    if (out_key != NULL) {
        if ((size_t)entry.key_pool_offset + sizeof(uint32_t) + (size_t)entry.key_length > reader->size) {
            return false;
        }
        *out_key = (const char*)&reader->base[(size_t)entry.key_pool_offset + sizeof(uint32_t)];
    }
    if (out_key_length != NULL) {
        *out_key_length = entry.key_length;
    }
    if (out_value_offset != NULL) {
        *out_value_offset = entry.value_offset;
    }
    return true;
}

bool bc_hrbl_reader_block_find_offset(const bc_hrbl_reader_t* reader, uint64_t block_offset, const char* key, size_t key_length,
                                      uint64_t* out_value_offset)
{
    bc_hrbl_block_header_t header;
    uint64_t entries_offset = 0u;
    if (!bc_hrbl_reader_block_body_offsets(reader, block_offset, &header, &entries_offset)) {
        return false;
    }
    uint64_t hash = (uint64_t)XXH3_64bits(key, key_length);
    bc_hrbl_entry_t entry;
    if (!bc_hrbl_reader_binary_search_entries(reader, entries_offset, header.child_count, hash, key, key_length, &entry)) {
        return false;
    }
    *out_value_offset = entry.value_offset;
    return true;
}

bool bc_hrbl_reader_array_body_offsets(const bc_hrbl_reader_t* reader, uint64_t array_offset, bc_hrbl_array_header_t* out_header,
                                       uint64_t* out_elements_offset)
{
    if (array_offset >= reader->size) {
        return false;
    }
    if (reader->base[array_offset] != (uint8_t)BC_HRBL_KIND_ARRAY) {
        return false;
    }
    uint64_t body_offset = (uint64_t)bc_hrbl_align_up((size_t)array_offset + 1u, 8u);
    if (body_offset + sizeof(bc_hrbl_array_header_t) > reader->size) {
        return false;
    }
    __builtin_memcpy(out_header, &reader->base[body_offset], sizeof(*out_header));
    *out_elements_offset = body_offset + sizeof(bc_hrbl_array_header_t);
    if (*out_elements_offset + (uint64_t)out_header->element_count * BC_HRBL_ARRAY_ELEMENT_SIZE > reader->size) {
        return false;
    }
    return true;
}

bool bc_hrbl_reader_array_length_at(const bc_hrbl_reader_t* reader, uint64_t array_offset, uint32_t* out_length)
{
    bc_hrbl_array_header_t header;
    uint64_t elements_offset = 0u;
    if (!bc_hrbl_reader_array_body_offsets(reader, array_offset, &header, &elements_offset)) {
        return false;
    }
    *out_length = header.element_count;
    return true;
}

bool bc_hrbl_reader_array_at_offset(const bc_hrbl_reader_t* reader, uint64_t array_offset, uint32_t index, uint64_t* out_value_offset)
{
    bc_hrbl_array_header_t header;
    uint64_t elements_offset = 0u;
    if (!bc_hrbl_reader_array_body_offsets(reader, array_offset, &header, &elements_offset)) {
        return false;
    }
    if (index >= header.element_count) {
        return false;
    }
    uint64_t location = elements_offset + (uint64_t)index * BC_HRBL_ARRAY_ELEMENT_SIZE;
    uint64_t value_offset = 0u;
    bc_hrbl_load_u64(&value_offset, &reader->base[location]);
    *out_value_offset = value_offset;
    return true;
}

static bool bc_hrbl_reader_scalar_body(const bc_hrbl_reader_t* reader, uint64_t value_offset, bc_hrbl_kind_t expected_kind,
                                       uint64_t* out_body_offset)
{
    bc_hrbl_kind_t kind;
    if (!bc_hrbl_reader_kind_at(reader, value_offset, &kind)) {
        return false;
    }
    if (kind != expected_kind) {
        return false;
    }
    uint8_t body_align = bc_hrbl_kind_body_align(expected_kind);
    uint64_t body_offset = (uint64_t)bc_hrbl_align_up((size_t)value_offset + 1u, body_align);
    uint8_t body_size = bc_hrbl_kind_body_size(expected_kind);
    if (body_offset + body_size > reader->size) {
        return false;
    }
    *out_body_offset = body_offset;
    return true;
}

bool bc_hrbl_reader_scalar_int64_at(const bc_hrbl_reader_t* reader, uint64_t value_offset, int64_t* out_value)
{
    uint64_t body_offset = 0u;
    if (!bc_hrbl_reader_scalar_body(reader, value_offset, BC_HRBL_KIND_INT64, &body_offset)) {
        return false;
    }
    __builtin_memcpy(out_value, &reader->base[body_offset], sizeof(*out_value));
    return true;
}

bool bc_hrbl_reader_scalar_uint64_at(const bc_hrbl_reader_t* reader, uint64_t value_offset, uint64_t* out_value)
{
    uint64_t body_offset = 0u;
    if (!bc_hrbl_reader_scalar_body(reader, value_offset, BC_HRBL_KIND_UINT64, &body_offset)) {
        return false;
    }
    __builtin_memcpy(out_value, &reader->base[body_offset], sizeof(*out_value));
    return true;
}

bool bc_hrbl_reader_scalar_float64_at(const bc_hrbl_reader_t* reader, uint64_t value_offset, double* out_value)
{
    uint64_t body_offset = 0u;
    if (!bc_hrbl_reader_scalar_body(reader, value_offset, BC_HRBL_KIND_FLOAT64, &body_offset)) {
        return false;
    }
    __builtin_memcpy(out_value, &reader->base[body_offset], sizeof(*out_value));
    return true;
}

bool bc_hrbl_reader_scalar_bool_at(const bc_hrbl_reader_t* reader, uint64_t value_offset, bool* out_value)
{
    bc_hrbl_kind_t kind;
    if (!bc_hrbl_reader_kind_at(reader, value_offset, &kind)) {
        return false;
    }
    if (kind == BC_HRBL_KIND_TRUE) {
        *out_value = true;
        return true;
    }
    if (kind == BC_HRBL_KIND_FALSE) {
        *out_value = false;
        return true;
    }
    return false;
}

bool bc_hrbl_reader_scalar_string_at(const bc_hrbl_reader_t* reader, uint64_t value_offset, const char** out_data, size_t* out_length)
{
    uint64_t body_offset = 0u;
    if (!bc_hrbl_reader_scalar_body(reader, value_offset, BC_HRBL_KIND_STRING, &body_offset)) {
        return false;
    }
    bc_hrbl_string_ref_t ref;
    bc_hrbl_load_string_ref(&ref, &reader->base[body_offset]);
    size_t absolute = (size_t)ref.pool_offset + sizeof(uint32_t);
    if (absolute + (size_t)ref.length > reader->size) {
        return false;
    }
    *out_data = (const char*)&reader->base[absolute];
    *out_length = (size_t)ref.length;
    return true;
}

bool bc_hrbl_reader_resolve_path(const bc_hrbl_reader_t* reader, const char* path, size_t path_length, uint64_t* out_value_offset)
{
    if (reader == NULL || path == NULL || out_value_offset == NULL) {
        return false;
    }
    if (path_length == 0u) {
        return false;
    }

    uint64_t current_offset = 0u;
    size_t cursor = 0u;
    bool resolved_root = false;

    while (cursor < path_length) {
        if (path[cursor] == '.') {
            cursor += 1u;
            continue;
        }
        if (path[cursor] == '[') {
            if (!resolved_root) {
                return false;
            }
            size_t close = cursor + 1u;
            while (close < path_length && path[close] != ']') {
                close += 1u;
            }
            if (close >= path_length) {
                return false;
            }
            size_t number_length = close - (cursor + 1u);
            if (number_length == 0u || number_length > 10u) {
                return false;
            }
            uint64_t index = 0u;
            for (size_t digit_position = 0u; digit_position < number_length; digit_position += 1u) {
                char digit_char = path[cursor + 1u + digit_position];
                if (digit_char < '0' || digit_char > '9') {
                    return false;
                }
                index = index * 10u + (uint64_t)(digit_char - '0');
            }
            if (index > UINT32_MAX) {
                return false;
            }
            uint64_t next_offset = 0u;
            if (!bc_hrbl_reader_array_at_offset(reader, current_offset, (uint32_t)index, &next_offset)) {
                return false;
            }
            current_offset = next_offset;
            cursor = close + 1u;
            continue;
        }
        size_t segment_start;
        size_t segment_length;
        if (path[cursor] == '\'' || path[cursor] == '"') {
            char quote = path[cursor];
            segment_start = cursor + 1u;
            size_t close = segment_start;
            while (close < path_length && path[close] != quote) {
                close += 1u;
            }
            if (close >= path_length) {
                return false;
            }
            segment_length = close - segment_start;
            cursor = close + 1u;
        } else {
            segment_start = cursor;
            while (cursor < path_length && path[cursor] != '.' && path[cursor] != '[') {
                cursor += 1u;
            }
            segment_length = cursor - segment_start;
        }
        if (segment_length == 0u) {
            return false;
        }
        if (!resolved_root) {
            if (!bc_hrbl_reader_find_root_offset(reader, &path[segment_start], segment_length, &current_offset)) {
                return false;
            }
            resolved_root = true;
        } else {
            uint64_t next_offset = 0u;
            if (!bc_hrbl_reader_block_find_offset(reader, current_offset, &path[segment_start], segment_length, &next_offset)) {
                return false;
            }
            current_offset = next_offset;
        }
    }

    if (!resolved_root) {
        return false;
    }
    *out_value_offset = current_offset;
    return true;
}

static void bc_hrbl_value_ref_fill(bc_hrbl_value_ref_t* ref, const bc_hrbl_reader_t* reader, uint64_t value_offset, bc_hrbl_kind_t kind)
{
    ref->reader = reader;
    ref->node_offset = value_offset;
    ref->kind = kind;
    ref->reserved = 0u;
}

bool bc_hrbl_reader_find(const bc_hrbl_reader_t* reader, const char* path, size_t path_length, bc_hrbl_value_ref_t* out_value)
{
    if (out_value != NULL) {
        (void)bc_core_zero(out_value, sizeof(*out_value));
    }
    if (reader == NULL || path == NULL || out_value == NULL) {
        return false;
    }
    uint64_t value_offset = 0u;
    if (!bc_hrbl_reader_resolve_path(reader, path, path_length, &value_offset)) {
        return false;
    }
    bc_hrbl_kind_t kind;
    if (!bc_hrbl_reader_kind_at(reader, value_offset, &kind)) {
        return false;
    }
    bc_hrbl_value_ref_fill(out_value, reader, value_offset, kind);
    return true;
}

bool bc_hrbl_reader_value_kind(const bc_hrbl_value_ref_t* value, bc_hrbl_kind_t* out_kind)
{
    if (value == NULL || value->reader == NULL || out_kind == NULL) {
        if (out_kind != NULL) {
            *out_kind = BC_HRBL_KIND_NULL;
        }
        return false;
    }
    *out_kind = value->kind;
    return true;
}

bool bc_hrbl_reader_get_bool(const bc_hrbl_value_ref_t* value, bool* out_value)
{
    if (value == NULL || value->reader == NULL || out_value == NULL) {
        if (out_value != NULL) {
            *out_value = false;
        }
        return false;
    }
    return bc_hrbl_reader_scalar_bool_at(value->reader, value->node_offset, out_value);
}

bool bc_hrbl_reader_get_int64(const bc_hrbl_value_ref_t* value, int64_t* out_value)
{
    if (value == NULL || value->reader == NULL || out_value == NULL) {
        if (out_value != NULL) {
            *out_value = 0;
        }
        return false;
    }
    return bc_hrbl_reader_scalar_int64_at(value->reader, value->node_offset, out_value);
}

bool bc_hrbl_reader_get_uint64(const bc_hrbl_value_ref_t* value, uint64_t* out_value)
{
    if (value == NULL || value->reader == NULL || out_value == NULL) {
        if (out_value != NULL) {
            *out_value = 0u;
        }
        return false;
    }
    return bc_hrbl_reader_scalar_uint64_at(value->reader, value->node_offset, out_value);
}

bool bc_hrbl_reader_get_float64(const bc_hrbl_value_ref_t* value, double* out_value)
{
    if (value == NULL || value->reader == NULL || out_value == NULL) {
        if (out_value != NULL) {
            *out_value = 0.0;
        }
        return false;
    }
    return bc_hrbl_reader_scalar_float64_at(value->reader, value->node_offset, out_value);
}

bool bc_hrbl_reader_get_string(const bc_hrbl_value_ref_t* value, const char** out_data, size_t* out_length)
{
    if (value == NULL || value->reader == NULL) {
        if (out_data != NULL) {
            *out_data = NULL;
        }
        if (out_length != NULL) {
            *out_length = 0u;
        }
        return false;
    }
    return bc_hrbl_reader_scalar_string_at(value->reader, value->node_offset, out_data, out_length);
}

bool bc_hrbl_reader_iter_block(const bc_hrbl_value_ref_t* block, bc_hrbl_iter_t* out_iter)
{
    if (out_iter != NULL) {
        (void)bc_core_zero(out_iter, sizeof(*out_iter));
    }
    if (block == NULL || block->reader == NULL || out_iter == NULL) {
        return false;
    }
    if (block->kind != BC_HRBL_KIND_BLOCK) {
        return false;
    }
    bc_hrbl_block_header_t header;
    uint64_t entries_offset = 0u;
    if (!bc_hrbl_reader_block_body_offsets(block->reader, block->node_offset, &header, &entries_offset)) {
        return false;
    }
    out_iter->reader = block->reader;
    out_iter->cursor_offset = entries_offset;
    out_iter->end_offset = entries_offset + (uint64_t)header.child_count * BC_HRBL_BLOCK_ENTRY_SIZE;
    out_iter->remaining = header.child_count;
    out_iter->is_block = 1u;
    return true;
}

bool bc_hrbl_reader_iter_array(const bc_hrbl_value_ref_t* array, bc_hrbl_iter_t* out_iter)
{
    if (out_iter != NULL) {
        (void)bc_core_zero(out_iter, sizeof(*out_iter));
    }
    if (array == NULL || array->reader == NULL || out_iter == NULL) {
        return false;
    }
    if (array->kind != BC_HRBL_KIND_ARRAY) {
        return false;
    }
    bc_hrbl_array_header_t header;
    uint64_t elements_offset = 0u;
    if (!bc_hrbl_reader_array_body_offsets(array->reader, array->node_offset, &header, &elements_offset)) {
        return false;
    }
    out_iter->reader = array->reader;
    out_iter->cursor_offset = elements_offset;
    out_iter->end_offset = elements_offset + (uint64_t)header.element_count * BC_HRBL_ARRAY_ELEMENT_SIZE;
    out_iter->remaining = header.element_count;
    out_iter->is_block = 0u;
    return true;
}

bool bc_hrbl_iter_next(bc_hrbl_iter_t* iter, bc_hrbl_value_ref_t* out_value, const char** out_key, size_t* out_key_length)
{
    if (out_value != NULL) {
        (void)bc_core_zero(out_value, sizeof(*out_value));
    }
    if (out_key != NULL) {
        *out_key = NULL;
    }
    if (out_key_length != NULL) {
        *out_key_length = 0u;
    }
    if (iter == NULL || iter->reader == NULL || out_value == NULL) {
        return false;
    }
    if (iter->remaining == 0u) {
        return false;
    }

    if (iter->is_block != 0u) {
        bc_hrbl_entry_t entry;
        if (!bc_hrbl_reader_read_entry(iter->reader, iter->cursor_offset, &entry)) {
            return false;
        }
        if ((size_t)entry.key_pool_offset + sizeof(uint32_t) + (size_t)entry.key_length > iter->reader->size) {
            return false;
        }
        bc_hrbl_kind_t kind;
        if (!bc_hrbl_reader_kind_at(iter->reader, entry.value_offset, &kind)) {
            return false;
        }
        bc_hrbl_value_ref_fill(out_value, iter->reader, entry.value_offset, kind);
        if (out_key != NULL) {
            *out_key = (const char*)&iter->reader->base[(size_t)entry.key_pool_offset + sizeof(uint32_t)];
        }
        if (out_key_length != NULL) {
            *out_key_length = (size_t)entry.key_length;
        }
        iter->cursor_offset += BC_HRBL_BLOCK_ENTRY_SIZE;
    } else {
        uint64_t value_offset = 0u;
        if (iter->cursor_offset + sizeof(value_offset) > iter->reader->size) {
            return false;
        }
        bc_hrbl_load_u64(&value_offset, &iter->reader->base[iter->cursor_offset]);
        bc_hrbl_kind_t kind;
        if (!bc_hrbl_reader_kind_at(iter->reader, value_offset, &kind)) {
            return false;
        }
        bc_hrbl_value_ref_fill(out_value, iter->reader, value_offset, kind);
        iter->cursor_offset += BC_HRBL_ARRAY_ELEMENT_SIZE;
    }
    iter->remaining -= 1u;
    return true;
}
