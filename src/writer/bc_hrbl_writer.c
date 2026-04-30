// SPDX-License-Identifier: MIT

#include "bc_hrbl_writer.h"
#include "bc_hrbl_writer_internal.h"
#include "bc_hrbl_format_internal.h"
#include "bc_hrbl_hash.h"

#include "bc_allocators.h"
#include "bc_allocators_arena.h"
#include "bc_allocators_pool.h"
#include "bc_core.h"
#include "bc_core_memory.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stddef.h>
#include <sys/stat.h>
#include <unistd.h>

static bool bc_hrbl_writer_arena_clone(bc_hrbl_writer_t* writer, const void* data, size_t length, void** out)
{
    if (length == 0u) {
        *out = NULL;
        return true;
    }
    void* pointer = NULL;
    if (!bc_allocators_arena_allocate(writer->arena, length, 1u, &pointer)) {
        return false;
    }
    (void)bc_core_copy(pointer, data, length);
    *out = pointer;
    return true;
}

static bc_hrbl_writer_node_t* bc_hrbl_writer_alloc_node(bc_hrbl_writer_t* writer)
{
    void* pointer = NULL;
    if (!bc_allocators_arena_allocate(writer->arena, sizeof(bc_hrbl_writer_node_t), 8u, &pointer)) {
        writer->error_flag = true;
        return NULL;
    }
    bc_hrbl_writer_node_t* node = (bc_hrbl_writer_node_t*)pointer;
    (void)bc_core_zero(node, sizeof(*node));
    node->cached_key_pool_offset = BC_HRBL_WRITER_POOL_OFFSET_NONE;
    node->cached_string_pool_offset = BC_HRBL_WRITER_STRING_OFFSET_NONE;
    return node;
}

static bool bc_hrbl_writer_set_key(bc_hrbl_writer_t* writer, bc_hrbl_writer_node_t* node, const char* key, size_t key_length)
{
    if (key == NULL && key_length != 0u) {
        return false;
    }
    if (key_length > UINT32_MAX) {
        return false;
    }
    void* clone = NULL;
    if (!bc_hrbl_writer_arena_clone(writer, key, key_length, &clone)) {
        return false;
    }
    node->key_data = (const char*)clone;
    node->key_length = (uint32_t)key_length;
    node->key_hash64 = bc_hrbl_hash64(key, key_length);
    return true;
}

static bool bc_hrbl_writer_append_to_scope(bc_hrbl_writer_t* writer, bc_hrbl_writer_node_t* node)
{
    if (writer->current_scope == NULL) {
        node->parent = NULL;
        if (writer->root_first == NULL) {
            writer->root_first = node;
        } else {
            writer->root_last->next_sibling = node;
        }
        writer->root_last = node;
        writer->root_count += 1u;
        return true;
    }
    bc_hrbl_writer_node_t* scope = writer->current_scope;
    node->parent = scope;
    if (scope->first_child == NULL) {
        scope->first_child = node;
    } else {
        scope->last_child->next_sibling = node;
    }
    scope->last_child = node;
    scope->child_count += 1u;
    return true;
}

static bool bc_hrbl_writer_expect_keyed_scope(const bc_hrbl_writer_t* writer)
{
    if (writer->current_scope == NULL) {
        return true;
    }
    return writer->current_scope->kind == BC_HRBL_KIND_BLOCK;
}

static bool bc_hrbl_writer_expect_indexed_scope(const bc_hrbl_writer_t* writer)
{
    if (writer->current_scope == NULL) {
        return false;
    }
    return writer->current_scope->kind == BC_HRBL_KIND_ARRAY;
}

bool bc_hrbl_writer_create(bc_allocators_context_t* memory_context, const bc_hrbl_writer_options_t* options, bc_hrbl_writer_t** out_writer)
{
    if (memory_context == NULL || out_writer == NULL) {
        if (out_writer != NULL) {
            *out_writer = NULL;
        }
        return false;
    }
    *out_writer = NULL;
    void* pointer = NULL;
    if (!bc_allocators_pool_allocate(memory_context, sizeof(bc_hrbl_writer_t), &pointer)) {
        return false;
    }
    bc_hrbl_writer_t* writer = (bc_hrbl_writer_t*)pointer;
    (void)bc_core_zero(writer, sizeof(*writer));
    writer->memory_context = memory_context;
    writer->last_error = BC_HRBL_WRITER_OK;
    if (!bc_allocators_arena_create_growable(memory_context, (size_t)256 * 1024, 0u, &writer->arena)) {
        bc_allocators_pool_free(memory_context, writer);
        return false;
    }
    if (options != NULL) {
        writer->options = *options;
    } else {
        writer->options.worker_count = 0u;
        writer->options.deduplicate_strings = true;
    }
    *out_writer = writer;
    return true;
}

void bc_hrbl_writer_destroy(bc_hrbl_writer_t* writer)
{
    if (writer->arena != NULL) {
        bc_allocators_arena_destroy(writer->arena);
        writer->arena = NULL;
    }
    bc_allocators_pool_free(writer->memory_context, writer);
}

static bool bc_hrbl_writer_add_leaf(bc_hrbl_writer_t* writer, const char* key, size_t key_length, bc_hrbl_kind_t kind, bool require_key)
{
    if (writer == NULL || writer->error_flag) {
        if (writer != NULL) {
            writer->error_flag = true;
        }
        return false;
    }
    if (require_key) {
        if (!bc_hrbl_writer_expect_keyed_scope(writer)) {
            writer->error_flag = true;
            return false;
        }
    } else {
        if (!bc_hrbl_writer_expect_indexed_scope(writer)) {
            writer->error_flag = true;
            return false;
        }
    }
    bc_hrbl_writer_node_t* node = bc_hrbl_writer_alloc_node(writer);
    if (node == NULL) {
        return false;
    }
    node->kind = kind;
    if (require_key) {
        if (!bc_hrbl_writer_set_key(writer, node, key, key_length)) {
            writer->error_flag = true;
            return false;
        }
    }
    return bc_hrbl_writer_append_to_scope(writer, node);
}

bool bc_hrbl_writer_set_null(bc_hrbl_writer_t* writer, const char* key, size_t key_length)
{
    return bc_hrbl_writer_add_leaf(writer, key, key_length, BC_HRBL_KIND_NULL, true);
}

bool bc_hrbl_writer_set_bool(bc_hrbl_writer_t* writer, const char* key, size_t key_length, bool value)
{
    if (!bc_hrbl_writer_add_leaf(writer, key, key_length, value ? BC_HRBL_KIND_TRUE : BC_HRBL_KIND_FALSE, true)) {
        return false;
    }
    return true;
}

bool bc_hrbl_writer_set_int64(bc_hrbl_writer_t* writer, const char* key, size_t key_length, int64_t value)
{
    if (!bc_hrbl_writer_add_leaf(writer, key, key_length, BC_HRBL_KIND_INT64, true)) {
        return false;
    }
    writer->current_scope == NULL ? (writer->root_last->as.int64_value = value)
                                  : (writer->current_scope->last_child->as.int64_value = value);
    return true;
}

bool bc_hrbl_writer_set_uint64(bc_hrbl_writer_t* writer, const char* key, size_t key_length, uint64_t value)
{
    if (!bc_hrbl_writer_add_leaf(writer, key, key_length, BC_HRBL_KIND_UINT64, true)) {
        return false;
    }
    writer->current_scope == NULL ? (writer->root_last->as.uint64_value = value)
                                  : (writer->current_scope->last_child->as.uint64_value = value);
    return true;
}

bool bc_hrbl_writer_set_float64(bc_hrbl_writer_t* writer, const char* key, size_t key_length, double value)
{
    if (!bc_hrbl_writer_add_leaf(writer, key, key_length, BC_HRBL_KIND_FLOAT64, true)) {
        return false;
    }
    writer->current_scope == NULL ? (writer->root_last->as.float64_value = value)
                                  : (writer->current_scope->last_child->as.float64_value = value);
    return true;
}

bool bc_hrbl_writer_set_string(bc_hrbl_writer_t* writer, const char* key, size_t key_length, const char* value, size_t value_length)
{
    if (value_length > UINT32_MAX) {
        if (writer != NULL) {
            writer->error_flag = true;
        }
        return false;
    }
    if (!bc_hrbl_writer_add_leaf(writer, key, key_length, BC_HRBL_KIND_STRING, true)) {
        return false;
    }
    bc_hrbl_writer_node_t* node = writer->current_scope == NULL ? writer->root_last : writer->current_scope->last_child;
    void* clone = NULL;
    if (!bc_hrbl_writer_arena_clone(writer, value, value_length, &clone)) {
        writer->error_flag = true;
        return false;
    }
    node->as.string_value.data = (const char*)clone;
    node->as.string_value.length = (uint32_t)value_length;
    return true;
}

bool bc_hrbl_writer_append_null(bc_hrbl_writer_t* writer)
{
    return bc_hrbl_writer_add_leaf(writer, NULL, 0u, BC_HRBL_KIND_NULL, false);
}

bool bc_hrbl_writer_append_bool(bc_hrbl_writer_t* writer, bool value)
{
    return bc_hrbl_writer_add_leaf(writer, NULL, 0u, value ? BC_HRBL_KIND_TRUE : BC_HRBL_KIND_FALSE, false);
}

bool bc_hrbl_writer_append_int64(bc_hrbl_writer_t* writer, int64_t value)
{
    if (!bc_hrbl_writer_add_leaf(writer, NULL, 0u, BC_HRBL_KIND_INT64, false)) {
        return false;
    }
    writer->current_scope->last_child->as.int64_value = value;
    return true;
}

bool bc_hrbl_writer_append_uint64(bc_hrbl_writer_t* writer, uint64_t value)
{
    if (!bc_hrbl_writer_add_leaf(writer, NULL, 0u, BC_HRBL_KIND_UINT64, false)) {
        return false;
    }
    writer->current_scope->last_child->as.uint64_value = value;
    return true;
}

bool bc_hrbl_writer_append_float64(bc_hrbl_writer_t* writer, double value)
{
    if (!bc_hrbl_writer_add_leaf(writer, NULL, 0u, BC_HRBL_KIND_FLOAT64, false)) {
        return false;
    }
    writer->current_scope->last_child->as.float64_value = value;
    return true;
}

bool bc_hrbl_writer_append_string(bc_hrbl_writer_t* writer, const char* value, size_t value_length)
{
    if (value_length > UINT32_MAX) {
        if (writer != NULL) {
            writer->error_flag = true;
        }
        return false;
    }
    if (!bc_hrbl_writer_add_leaf(writer, NULL, 0u, BC_HRBL_KIND_STRING, false)) {
        return false;
    }
    bc_hrbl_writer_node_t* node = writer->current_scope->last_child;
    void* clone = NULL;
    if (!bc_hrbl_writer_arena_clone(writer, value, value_length, &clone)) {
        writer->error_flag = true;
        return false;
    }
    node->as.string_value.data = (const char*)clone;
    node->as.string_value.length = (uint32_t)value_length;
    return true;
}

static bool bc_hrbl_writer_begin_container(bc_hrbl_writer_t* writer, const char* key, size_t key_length, bc_hrbl_kind_t kind)
{
    if (writer == NULL || writer->error_flag) {
        if (writer != NULL) {
            writer->error_flag = true;
        }
        return false;
    }
    bool at_root = writer->current_scope == NULL;
    bool in_block = !at_root && writer->current_scope->kind == BC_HRBL_KIND_BLOCK;
    bool in_array = !at_root && writer->current_scope->kind == BC_HRBL_KIND_ARRAY;
    if (in_array) {
        if (key != NULL || key_length != 0u) {
            writer->error_flag = true;
            return false;
        }
    } else if (at_root || in_block) {
        if (key == NULL && key_length != 0u) {
            writer->error_flag = true;
            return false;
        }
    }
    bc_hrbl_writer_node_t* node = bc_hrbl_writer_alloc_node(writer);
    if (node == NULL) {
        return false;
    }
    node->kind = kind;
    if ((at_root || in_block) && key != NULL) {
        if (!bc_hrbl_writer_set_key(writer, node, key, key_length)) {
            writer->error_flag = true;
            return false;
        }
    }
    if (!bc_hrbl_writer_append_to_scope(writer, node)) {
        return false;
    }
    writer->current_scope = node;
    return true;
}

static bool bc_hrbl_writer_end_container(bc_hrbl_writer_t* writer, bc_hrbl_kind_t expected_kind)
{
    if (writer == NULL || writer->error_flag) {
        if (writer != NULL) {
            writer->error_flag = true;
        }
        return false;
    }
    if (writer->current_scope == NULL) {
        writer->error_flag = true;
        return false;
    }
    if (writer->current_scope->kind != expected_kind) {
        writer->error_flag = true;
        return false;
    }
    writer->current_scope = writer->current_scope->parent;
    return true;
}

bool bc_hrbl_writer_begin_block(bc_hrbl_writer_t* writer, const char* key, size_t key_length)
{
    return bc_hrbl_writer_begin_container(writer, key, key_length, BC_HRBL_KIND_BLOCK);
}

bool bc_hrbl_writer_end_block(bc_hrbl_writer_t* writer)
{
    return bc_hrbl_writer_end_container(writer, BC_HRBL_KIND_BLOCK);
}

bool bc_hrbl_writer_begin_array(bc_hrbl_writer_t* writer, const char* key, size_t key_length)
{
    return bc_hrbl_writer_begin_container(writer, key, key_length, BC_HRBL_KIND_ARRAY);
}

bool bc_hrbl_writer_end_array(bc_hrbl_writer_t* writer)
{
    return bc_hrbl_writer_end_container(writer, BC_HRBL_KIND_ARRAY);
}

bool bc_hrbl_writer_finalize_to_buffer(bc_hrbl_writer_t* writer, void** out_buffer, size_t* out_size)
{
    if (out_buffer != NULL) {
        *out_buffer = NULL;
    }
    if (out_size != NULL) {
        *out_size = 0u;
    }
    if (writer == NULL || out_buffer == NULL || out_size == NULL) {
        if (writer != NULL) {
            writer->last_error = BC_HRBL_WRITER_ERROR_INVALID_ARGUMENT;
        }
        return false;
    }
    if (writer->error_flag) {
        writer->last_error = BC_HRBL_WRITER_ERROR_CONSTRUCTION;
        return false;
    }
    if (writer->current_scope != NULL) {
        writer->last_error = BC_HRBL_WRITER_ERROR_UNCLOSED_SCOPE;
        return false;
    }
    uint8_t* buffer = NULL;
    size_t size = 0u;
    bc_hrbl_writer_error_t serialize_error = BC_HRBL_WRITER_OK;
    if (!bc_hrbl_writer_serialize_to_buffer(writer, &buffer, &size, &serialize_error)) {
        writer->last_error = (serialize_error != BC_HRBL_WRITER_OK) ? serialize_error : BC_HRBL_WRITER_ERROR_INTERNAL;
        return false;
    }
    *out_buffer = buffer;
    *out_size = size;
    return true;
}

bc_hrbl_writer_error_t bc_hrbl_writer_last_error(const bc_hrbl_writer_t* writer)
{
    if (writer == NULL) {
        return BC_HRBL_WRITER_ERROR_INVALID_ARGUMENT;
    }
    return writer->last_error;
}

bool bc_hrbl_writer_finalize_to_file(bc_hrbl_writer_t* writer, const char* output_path)
{
    if (writer == NULL || output_path == NULL) {
        return false;
    }
    void* buffer = NULL;
    size_t size = 0u;
    if (!bc_hrbl_writer_finalize_to_buffer(writer, &buffer, &size)) {
        return false;
    }
    int file_descriptor = open(output_path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (file_descriptor < 0) {
        bc_allocators_pool_free(writer->memory_context, buffer);
        return false;
    }
    size_t written_total = 0u;
    while (written_total < size) {
        ssize_t n = write(file_descriptor, (const uint8_t*)buffer + written_total, size - written_total);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            close(file_descriptor);
            bc_allocators_pool_free(writer->memory_context, buffer);
            return false;
        }
        written_total += (size_t)n;
    }
    int close_result = close(file_descriptor);
    bc_allocators_pool_free(writer->memory_context, buffer);
    return close_result == 0;
}

void bc_hrbl_writer_free_buffer(bc_allocators_context_t* memory_context, void* buffer)
{
    bc_allocators_pool_free(memory_context, buffer);
}
