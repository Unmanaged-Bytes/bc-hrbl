// SPDX-License-Identifier: MIT

#include "bc_hrbl_writer.h"
#include "bc_hrbl_writer_internal.h"
#include "bc_hrbl_format_internal.h"
#include "bc_hrbl_hash.h"

#include "bc_allocators.h"
#include "bc_allocators_pool.h"
#include "bc_core.h"
#include "bc_core_memory.h"

#include <pthread.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>

static inline bool bc_hrbl_encoder_fail(bc_hrbl_writer_error_t* out_error, bc_hrbl_writer_error_t code)
{
    if (out_error != NULL) {
        *out_error = code;
    }
    return false;
}

typedef struct bc_hrbl_encoder_buffer {
    uint8_t* data;
    size_t size;
    size_t capacity;
    bc_allocators_context_t* memory_context;
} bc_hrbl_encoder_buffer_t;

static bool bc_hrbl_encoder_buffer_init(bc_hrbl_encoder_buffer_t* buffer, bc_allocators_context_t* memory_context, size_t initial_capacity)
{
    buffer->memory_context = memory_context;
    buffer->size = 0u;
    buffer->capacity = 0u;
    buffer->data = NULL;
    if (initial_capacity == 0u) {
        return true;
    }
    void* pointer = NULL;
    if (!bc_allocators_pool_allocate(memory_context, initial_capacity, &pointer)) {
        return false;
    }
    buffer->data = (uint8_t*)pointer;
    buffer->capacity = initial_capacity;
    return true;
}

static void bc_hrbl_encoder_buffer_destroy(bc_hrbl_encoder_buffer_t* buffer)
{
    if (buffer->data != NULL) {
        bc_allocators_pool_free(buffer->memory_context, buffer->data);
        buffer->data = NULL;
    }
    buffer->size = 0u;
    buffer->capacity = 0u;
}

static bool bc_hrbl_encoder_buffer_reserve(bc_hrbl_encoder_buffer_t* buffer, size_t required)
{
    if (required <= buffer->capacity) {
        return true;
    }
    size_t new_capacity = buffer->capacity == 0u ? 4096u : buffer->capacity;
    while (new_capacity < required) {
        new_capacity *= 2u;
    }
    void* pointer = NULL;
    if (!bc_allocators_pool_reallocate(buffer->memory_context, buffer->data, new_capacity, &pointer)) {
        return false;
    }
    buffer->data = (uint8_t*)pointer;
    buffer->capacity = new_capacity;
    return true;
}

static bool bc_hrbl_encoder_buffer_append(bc_hrbl_encoder_buffer_t* buffer, const void* data, size_t length)
{
    if (!bc_hrbl_encoder_buffer_reserve(buffer, buffer->size + length)) {
        return false;
    }
    if (length != 0u) {
        (void)bc_core_copy(&buffer->data[buffer->size], data, length);
    }
    buffer->size += length;
    return true;
}

static bool bc_hrbl_encoder_buffer_append_zero(bc_hrbl_encoder_buffer_t* buffer, size_t length)
{
    if (!bc_hrbl_encoder_buffer_reserve(buffer, buffer->size + length)) {
        return false;
    }
    if (length != 0u) {
        (void)bc_core_zero(&buffer->data[buffer->size], length);
    }
    buffer->size += length;
    return true;
}

static bool bc_hrbl_encoder_buffer_align_to(bc_hrbl_encoder_buffer_t* buffer, size_t alignment)
{
    size_t aligned = bc_hrbl_align_up(buffer->size, alignment);
    if (aligned == buffer->size) {
        return true;
    }
    return bc_hrbl_encoder_buffer_append_zero(buffer, aligned - buffer->size);
}

typedef struct bc_hrbl_encoder_pool_entry {
    uint32_t pool_offset;
    uint32_t length;
} bc_hrbl_encoder_pool_entry_t;

#define BC_HRBL_ENCODER_POOL_SLOT_EMPTY UINT64_C(0)
#define BC_HRBL_ENCODER_POOL_HASH_NONZERO UINT64_C(0x8000000000000000)

typedef struct bc_hrbl_encoder_pool_slot {
    uint64_t hash;
    uint32_t entry_index;
    uint32_t padding;
} bc_hrbl_encoder_pool_slot_t;

typedef struct bc_hrbl_encoder_pool {
    bc_allocators_context_t* memory_context;
    bc_hrbl_encoder_buffer_t buffer;
    bc_hrbl_encoder_pool_entry_t* entries;
    size_t entries_count;
    size_t entries_capacity;
    bc_hrbl_encoder_pool_slot_t* slots;
    size_t slots_capacity;
    size_t slots_mask;
    size_t slots_used;
    uint64_t* fixup_offsets;
    size_t fixup_count;
    size_t fixup_capacity;
    size_t worker_count;
} bc_hrbl_encoder_pool_t;

static bool bc_hrbl_encoder_pool_record_fixup(bc_hrbl_encoder_pool_t* pool, uint64_t node_offset, bc_hrbl_writer_error_t* out_error)
{
    if (pool->fixup_count == pool->fixup_capacity) {
        size_t new_capacity = pool->fixup_capacity == 0u ? 1024u : pool->fixup_capacity * 2u;
        void* pointer = NULL;
        if (!bc_allocators_pool_reallocate(pool->memory_context, pool->fixup_offsets, new_capacity * sizeof(uint64_t), &pointer)) {
            return bc_hrbl_encoder_fail(out_error, BC_HRBL_WRITER_ERROR_OOM);
        }
        pool->fixup_offsets = (uint64_t*)pointer;
        pool->fixup_capacity = new_capacity;
    }
    pool->fixup_offsets[pool->fixup_count] = node_offset;
    pool->fixup_count += 1u;
    return true;
}

static uint64_t bc_hrbl_encoder_pool_mark(uint64_t raw_hash)
{
    return raw_hash == 0u ? BC_HRBL_ENCODER_POOL_HASH_NONZERO : raw_hash;
}

static bool bc_hrbl_encoder_pool_slots_grow(bc_hrbl_encoder_pool_t* pool, size_t new_capacity, bc_hrbl_writer_error_t* out_error);

static bool bc_hrbl_encoder_pool_init(bc_hrbl_encoder_pool_t* pool, bc_allocators_context_t* memory_context, size_t initial_capacity,
                                      bc_hrbl_writer_error_t* out_error)
{
    pool->memory_context = memory_context;
    pool->entries = NULL;
    pool->entries_count = 0u;
    pool->entries_capacity = 0u;
    pool->slots = NULL;
    pool->slots_capacity = 0u;
    pool->slots_mask = 0u;
    pool->slots_used = 0u;
    pool->fixup_offsets = NULL;
    pool->fixup_count = 0u;
    pool->fixup_capacity = 0u;
    pool->worker_count = 0u;
    if (initial_capacity < 4096u) {
        initial_capacity = 4096u;
    }
    if (!bc_hrbl_encoder_buffer_init(&pool->buffer, memory_context, initial_capacity)) {
        return bc_hrbl_encoder_fail(out_error, BC_HRBL_WRITER_ERROR_OOM);
    }
    if (!bc_hrbl_encoder_pool_slots_grow(pool, 512u, out_error)) {
        bc_hrbl_encoder_buffer_destroy(&pool->buffer);
        return false;
    }
    return true;
}

static void bc_hrbl_encoder_pool_destroy(bc_hrbl_encoder_pool_t* pool)
{
    if (pool->slots != NULL) {
        bc_allocators_pool_free(pool->memory_context, pool->slots);
        pool->slots = NULL;
    }
    bc_hrbl_encoder_buffer_destroy(&pool->buffer);
    if (pool->entries != NULL) {
        bc_allocators_pool_free(pool->memory_context, pool->entries);
        pool->entries = NULL;
    }
    if (pool->fixup_offsets != NULL) {
        bc_allocators_pool_free(pool->memory_context, pool->fixup_offsets);
        pool->fixup_offsets = NULL;
    }
    pool->entries_count = 0u;
    pool->entries_capacity = 0u;
    pool->slots_capacity = 0u;
    pool->slots_used = 0u;
    pool->fixup_count = 0u;
    pool->fixup_capacity = 0u;
}

static bool bc_hrbl_encoder_pool_slots_grow(bc_hrbl_encoder_pool_t* pool, size_t new_capacity, bc_hrbl_writer_error_t* out_error)
{
    void* pointer = NULL;
    size_t bytes = new_capacity * sizeof(bc_hrbl_encoder_pool_slot_t);
    if (!bc_allocators_pool_allocate(pool->memory_context, bytes, &pointer)) {
        return bc_hrbl_encoder_fail(out_error, BC_HRBL_WRITER_ERROR_OOM);
    }
    bc_hrbl_encoder_pool_slot_t* new_slots = (bc_hrbl_encoder_pool_slot_t*)pointer;
    (void)bc_core_zero(new_slots, bytes);

    bc_hrbl_encoder_pool_slot_t* old_slots = pool->slots;
    size_t old_capacity = pool->slots_capacity;

    pool->slots = new_slots;
    pool->slots_capacity = new_capacity;
    pool->slots_mask = new_capacity - 1u;

    if (old_slots != NULL) {
        for (size_t i = 0u; i < old_capacity; i += 1u) {
            if (old_slots[i].hash == BC_HRBL_ENCODER_POOL_SLOT_EMPTY) {
                continue;
            }
            size_t pos = (size_t)old_slots[i].hash & pool->slots_mask;
            while (pool->slots[pos].hash != BC_HRBL_ENCODER_POOL_SLOT_EMPTY) {
                pos = (pos + 1u) & pool->slots_mask;
            }
            pool->slots[pos] = old_slots[i];
        }
        bc_allocators_pool_free(pool->memory_context, old_slots);
    }
    return true;
}

static bool bc_hrbl_encoder_pool_entries_reserve(bc_hrbl_encoder_pool_t* pool, size_t required, bc_hrbl_writer_error_t* out_error)
{
    if (required <= pool->entries_capacity) {
        return true;
    }
    size_t new_capacity = pool->entries_capacity == 0u ? 64u : pool->entries_capacity;
    while (new_capacity < required) {
        new_capacity *= 2u;
    }
    void* pointer = NULL;
    if (!bc_allocators_pool_reallocate(pool->memory_context, pool->entries, new_capacity * sizeof(bc_hrbl_encoder_pool_entry_t),
                                       &pointer)) {
        return bc_hrbl_encoder_fail(out_error, BC_HRBL_WRITER_ERROR_OOM);
    }
    pool->entries = (bc_hrbl_encoder_pool_entry_t*)pointer;
    pool->entries_capacity = new_capacity;
    return true;
}

static bool bc_hrbl_encoder_pool_intern_with_hash(bc_hrbl_encoder_pool_t* pool, const char* data, size_t length, uint64_t raw_hash,
                                                  uint32_t* out_offset, bc_hrbl_writer_error_t* out_error)
{
    if (length > UINT32_MAX) {
        return bc_hrbl_encoder_fail(out_error, BC_HRBL_WRITER_ERROR_STRING_LENGTH_OVERFLOW_4GB);
    }
    uint64_t hash = bc_hrbl_encoder_pool_mark(raw_hash);
    size_t pos = (size_t)hash & pool->slots_mask;
    for (;;) {
        const bc_hrbl_encoder_pool_slot_t* slot = &pool->slots[pos];
        if (slot->hash == BC_HRBL_ENCODER_POOL_SLOT_EMPTY) {
            break;
        }
        if (slot->hash == hash) {
            const bc_hrbl_encoder_pool_entry_t* candidate = &pool->entries[slot->entry_index];
            if (candidate->length == (uint32_t)length) {
                const char* existing = (const char*)&pool->buffer.data[(size_t)candidate->pool_offset + sizeof(uint32_t)];
                bool equal = false;
                if (length == 0u) {
                    equal = true;
                } else {
                    (void)bc_core_equal(existing, data, length, &equal);
                }
                if (equal) {
                    *out_offset = candidate->pool_offset;
                    return true;
                }
            }
        }
        pos = (pos + 1u) & pool->slots_mask;
    }
    size_t intra_offset = pool->buffer.size;
    if (intra_offset > UINT32_MAX) {
        return bc_hrbl_encoder_fail(out_error, BC_HRBL_WRITER_ERROR_STRING_POOL_OVERFLOW_4GB);
    }
    uint32_t length_u32 = (uint32_t)length;
    if (!bc_hrbl_encoder_buffer_append(&pool->buffer, &length_u32, sizeof(length_u32))) {
        return bc_hrbl_encoder_fail(out_error, BC_HRBL_WRITER_ERROR_OOM);
    }
    if (length != 0u && !bc_hrbl_encoder_buffer_append(&pool->buffer, data, length)) {
        return bc_hrbl_encoder_fail(out_error, BC_HRBL_WRITER_ERROR_OOM);
    }
    if (!bc_hrbl_encoder_buffer_align_to(&pool->buffer, 4u)) {
        return bc_hrbl_encoder_fail(out_error, BC_HRBL_WRITER_ERROR_OOM);
    }
    if (!bc_hrbl_encoder_pool_entries_reserve(pool, pool->entries_count + 1u, out_error)) {
        return false;
    }
    pool->entries[pool->entries_count].pool_offset = (uint32_t)intra_offset;
    pool->entries[pool->entries_count].length = length_u32;
    size_t entry_index = pool->entries_count;
    pool->entries_count += 1u;
    if ((pool->slots_used + 1u) * 4u >= pool->slots_capacity * 3u) {
        if (!bc_hrbl_encoder_pool_slots_grow(pool, pool->slots_capacity * 2u, out_error)) {
            pool->entries_count -= 1u;
            return false;
        }
        pos = (size_t)hash & pool->slots_mask;
        while (pool->slots[pos].hash != BC_HRBL_ENCODER_POOL_SLOT_EMPTY) {
            pos = (pos + 1u) & pool->slots_mask;
        }
    }
    pool->slots[pos].hash = hash;
    pool->slots[pos].entry_index = (uint32_t)entry_index;
    pool->slots_used += 1u;
    *out_offset = (uint32_t)intra_offset;
    return true;
}

static bool bc_hrbl_encoder_pool_intern(bc_hrbl_encoder_pool_t* pool, const char* data, size_t length, uint32_t* out_offset,
                                        bc_hrbl_writer_error_t* out_error)
{
    uint64_t raw_hash = bc_hrbl_hash64(data, length);
    return bc_hrbl_encoder_pool_intern_with_hash(pool, data, length, raw_hash, out_offset, out_error);
}

static bool bc_hrbl_encoder_collect_strings(bc_hrbl_encoder_pool_t* pool, bc_hrbl_writer_node_t* node, bc_hrbl_writer_error_t* out_error)
{
    if (node == NULL) {
        return true;
    }
    if (node->key_data != NULL) {
        uint32_t offset = 0u;
        if (!bc_hrbl_encoder_pool_intern_with_hash(pool, node->key_data, (size_t)node->key_length, node->key_hash64, &offset, out_error)) {
            return false;
        }
        node->cached_key_pool_offset = offset;
    }
    if (node->kind == BC_HRBL_KIND_STRING) {
        const char* data = node->as.string_value.data;
        uint32_t length = node->as.string_value.length;
        if (data == NULL) {
            data = "";
            length = 0u;
        }
        uint32_t offset = 0u;
        if (!bc_hrbl_encoder_pool_intern(pool, data, (size_t)length, &offset, out_error)) {
            return false;
        }
        node->cached_string_pool_offset = offset;
    }
    for (bc_hrbl_writer_node_t* child = node->first_child; child != NULL; child = child->next_sibling) {
        if (!bc_hrbl_encoder_collect_strings(pool, child, out_error)) {
            return false;
        }
    }
    return true;
}

typedef struct bc_hrbl_encoder_pending_entry {
    uint64_t key_hash64;
    uint32_t key_pool_offset;
    uint32_t key_length;
    uint64_t value_offset;
} bc_hrbl_encoder_pending_entry_t;

static bool bc_hrbl_encoder_pending_radix_sort_serial(bc_allocators_context_t* memory_context, bc_hrbl_encoder_pending_entry_t* entries,
                                                      size_t count, bc_hrbl_writer_error_t* out_error)
{
    void* pointer = NULL;
    if (!bc_allocators_pool_allocate(memory_context, count * sizeof(bc_hrbl_encoder_pending_entry_t), &pointer)) {
        return bc_hrbl_encoder_fail(out_error, BC_HRBL_WRITER_ERROR_OOM);
    }
    bc_hrbl_encoder_pending_entry_t* scratch = (bc_hrbl_encoder_pending_entry_t*)pointer;
    size_t counts[256];
    size_t prefix[256];
    bc_hrbl_encoder_pending_entry_t* src = entries;
    bc_hrbl_encoder_pending_entry_t* dst = scratch;
    for (unsigned int pass = 0u; pass < 8u; pass += 1u) {
        (void)bc_core_zero(counts, sizeof(counts));
        unsigned int shift = pass * 8u;
        for (size_t i = 0u; i < count; i += 1u) {
            unsigned int byte = (unsigned int)((src[i].key_hash64 >> shift) & 0xFFu);
            counts[byte] += 1u;
        }
        prefix[0] = 0u;
        for (unsigned int i = 1u; i < 256u; i += 1u) {
            prefix[i] = prefix[i - 1u] + counts[i - 1u];
        }
        for (size_t i = 0u; i < count; i += 1u) {
            unsigned int byte = (unsigned int)((src[i].key_hash64 >> shift) & 0xFFu);
            dst[prefix[byte]] = src[i];
            prefix[byte] += 1u;
        }
        bc_hrbl_encoder_pending_entry_t* swap = src;
        src = dst;
        dst = swap;
    }
    bc_allocators_pool_free(memory_context, pointer);
    return true;
}

typedef struct bc_hrbl_encoder_radix_worker_context {
    bc_hrbl_encoder_pending_entry_t* source;
    bc_hrbl_encoder_pending_entry_t* destination;
    size_t range_start;
    size_t range_end;
    unsigned int shift;
    size_t local_counts[256];
    size_t local_write_positions[256];
} bc_hrbl_encoder_radix_worker_context_t;

static void* bc_hrbl_encoder_radix_count_worker(void* argument)
{
    bc_hrbl_encoder_radix_worker_context_t* context = (bc_hrbl_encoder_radix_worker_context_t*)argument;
    (void)bc_core_zero(context->local_counts, sizeof(context->local_counts));
    for (size_t i = context->range_start; i < context->range_end; i += 1u) {
        unsigned int byte = (unsigned int)((context->source[i].key_hash64 >> context->shift) & 0xFFu);
        context->local_counts[byte] += 1u;
    }
    return NULL;
}

static void* bc_hrbl_encoder_radix_scatter_worker(void* argument)
{
    bc_hrbl_encoder_radix_worker_context_t* context = (bc_hrbl_encoder_radix_worker_context_t*)argument;
    for (size_t i = context->range_start; i < context->range_end; i += 1u) {
        unsigned int byte = (unsigned int)((context->source[i].key_hash64 >> context->shift) & 0xFFu);
        context->destination[context->local_write_positions[byte]] = context->source[i];
        context->local_write_positions[byte] += 1u;
    }
    return NULL;
}

static bool bc_hrbl_encoder_pending_radix_sort_parallel(bc_allocators_context_t* memory_context, bc_hrbl_encoder_pending_entry_t* entries,
                                                        size_t count, size_t worker_count, bc_hrbl_writer_error_t* out_error)
{
    void* scratch_pointer = NULL;
    if (!bc_allocators_pool_allocate(memory_context, count * sizeof(bc_hrbl_encoder_pending_entry_t), &scratch_pointer)) {
        return bc_hrbl_encoder_fail(out_error, BC_HRBL_WRITER_ERROR_OOM);
    }
    void* contexts_pointer = NULL;
    if (!bc_allocators_pool_allocate(memory_context, worker_count * sizeof(bc_hrbl_encoder_radix_worker_context_t), &contexts_pointer)) {
        bc_allocators_pool_free(memory_context, scratch_pointer);
        return bc_hrbl_encoder_fail(out_error, BC_HRBL_WRITER_ERROR_OOM);
    }
    void* threads_pointer = NULL;
    if (!bc_allocators_pool_allocate(memory_context, worker_count * sizeof(pthread_t), &threads_pointer)) {
        bc_allocators_pool_free(memory_context, scratch_pointer);
        bc_allocators_pool_free(memory_context, contexts_pointer);
        return bc_hrbl_encoder_fail(out_error, BC_HRBL_WRITER_ERROR_OOM);
    }

    bc_hrbl_encoder_pending_entry_t* scratch = (bc_hrbl_encoder_pending_entry_t*)scratch_pointer;
    bc_hrbl_encoder_radix_worker_context_t* contexts = (bc_hrbl_encoder_radix_worker_context_t*)contexts_pointer;
    pthread_t* threads = (pthread_t*)threads_pointer;

    bc_hrbl_encoder_pending_entry_t* source = entries;
    bc_hrbl_encoder_pending_entry_t* destination = scratch;
    size_t chunk_size = count / worker_count;

    for (unsigned int pass = 0u; pass < 8u; pass += 1u) {
        unsigned int shift = pass * 8u;
        for (size_t worker_index = 0u; worker_index < worker_count; worker_index += 1u) {
            contexts[worker_index].source = source;
            contexts[worker_index].destination = destination;
            contexts[worker_index].range_start = worker_index * chunk_size;
            contexts[worker_index].range_end = (worker_index + 1u == worker_count) ? count : (worker_index + 1u) * chunk_size;
            contexts[worker_index].shift = shift;
            if (pthread_create(&threads[worker_index], NULL, bc_hrbl_encoder_radix_count_worker, &contexts[worker_index]) != 0) {
                for (size_t joined = 0u; joined < worker_index; joined += 1u) {
                    pthread_join(threads[joined], NULL);
                }
                bc_allocators_pool_free(memory_context, scratch_pointer);
                bc_allocators_pool_free(memory_context, contexts_pointer);
                bc_allocators_pool_free(memory_context, threads_pointer);
                return bc_hrbl_encoder_fail(out_error, BC_HRBL_WRITER_ERROR_INTERNAL);
            }
        }
        for (size_t worker_index = 0u; worker_index < worker_count; worker_index += 1u) {
            pthread_join(threads[worker_index], NULL);
        }
        size_t running_total = 0u;
        for (unsigned int bucket = 0u; bucket < 256u; bucket += 1u) {
            size_t write_position = running_total;
            for (size_t worker_index = 0u; worker_index < worker_count; worker_index += 1u) {
                contexts[worker_index].local_write_positions[bucket] = write_position;
                write_position += contexts[worker_index].local_counts[bucket];
            }
            running_total = write_position;
        }
        for (size_t worker_index = 0u; worker_index < worker_count; worker_index += 1u) {
            if (pthread_create(&threads[worker_index], NULL, bc_hrbl_encoder_radix_scatter_worker, &contexts[worker_index]) != 0) {
                for (size_t joined = 0u; joined < worker_index; joined += 1u) {
                    pthread_join(threads[joined], NULL);
                }
                bc_allocators_pool_free(memory_context, scratch_pointer);
                bc_allocators_pool_free(memory_context, contexts_pointer);
                bc_allocators_pool_free(memory_context, threads_pointer);
                return bc_hrbl_encoder_fail(out_error, BC_HRBL_WRITER_ERROR_INTERNAL);
            }
        }
        for (size_t worker_index = 0u; worker_index < worker_count; worker_index += 1u) {
            pthread_join(threads[worker_index], NULL);
        }
        bc_hrbl_encoder_pending_entry_t* swap = source;
        source = destination;
        destination = swap;
    }

    bc_allocators_pool_free(memory_context, scratch_pointer);
    bc_allocators_pool_free(memory_context, contexts_pointer);
    bc_allocators_pool_free(memory_context, threads_pointer);
    return true;
}

static bool bc_hrbl_encoder_pending_radix_sort(bc_allocators_context_t* memory_context, bc_hrbl_encoder_pending_entry_t* entries,
                                               size_t count, size_t worker_count, bc_hrbl_writer_error_t* out_error)
{
    if (worker_count >= 2u && count >= 10000u) {
        return bc_hrbl_encoder_pending_radix_sort_parallel(memory_context, entries, count, worker_count, out_error);
    }
    return bc_hrbl_encoder_pending_radix_sort_serial(memory_context, entries, count, out_error);
}

static bool bc_hrbl_encoder_pending_sort(bc_allocators_context_t* memory_context, bc_hrbl_encoder_pending_entry_t* entries, size_t count,
                                         size_t worker_count, bc_hrbl_writer_error_t* out_error)
{
    if (count < 2u) {
        return true;
    }
    if (count <= 32u) {
        for (size_t i = 1u; i < count; i += 1u) {
            bc_hrbl_encoder_pending_entry_t pivot = entries[i];
            size_t j = i;
            while (j > 0u && entries[j - 1u].key_hash64 > pivot.key_hash64) {
                entries[j] = entries[j - 1u];
                j -= 1u;
            }
            entries[j] = pivot;
        }
        return true;
    }
    return bc_hrbl_encoder_pending_radix_sort(memory_context, entries, count, worker_count, out_error);
}

static bool bc_hrbl_encoder_emit_value(bc_hrbl_encoder_buffer_t* nodes, bc_hrbl_encoder_pool_t* pool, uint64_t nodes_base_offset,
                                       const bc_hrbl_writer_node_t* node, uint64_t* out_value_offset, bc_hrbl_writer_error_t* out_error);

static bool bc_hrbl_encoder_emit_scalar(bc_hrbl_encoder_buffer_t* nodes, bc_hrbl_encoder_pool_t* pool, uint64_t nodes_base_offset,
                                        const bc_hrbl_writer_node_t* node, uint64_t* out_value_offset, bc_hrbl_writer_error_t* out_error)
{
    bc_hrbl_kind_t kind = node->kind;
    size_t body_align = bc_hrbl_kind_body_align(kind);
    size_t kind_offset = bc_hrbl_align_up(nodes->size + 1u, body_align) - 1u;
    if (kind_offset > nodes->size) {
        if (!bc_hrbl_encoder_buffer_append_zero(nodes, kind_offset - nodes->size)) {
            return bc_hrbl_encoder_fail(out_error, BC_HRBL_WRITER_ERROR_OOM);
        }
    }
    uint8_t kind_byte = (uint8_t)kind;
    if (!bc_hrbl_encoder_buffer_append(nodes, &kind_byte, 1u)) {
        return bc_hrbl_encoder_fail(out_error, BC_HRBL_WRITER_ERROR_OOM);
    }
    switch (kind) {
    case BC_HRBL_KIND_NULL:
    case BC_HRBL_KIND_FALSE:
    case BC_HRBL_KIND_TRUE:
        break;
    case BC_HRBL_KIND_INT64: {
        int64_t value = node->as.int64_value;
        if (!bc_hrbl_encoder_buffer_append(nodes, &value, sizeof(value))) {
            return bc_hrbl_encoder_fail(out_error, BC_HRBL_WRITER_ERROR_OOM);
        }
        break;
    }
    case BC_HRBL_KIND_UINT64: {
        uint64_t value = node->as.uint64_value;
        if (!bc_hrbl_encoder_buffer_append(nodes, &value, sizeof(value))) {
            return bc_hrbl_encoder_fail(out_error, BC_HRBL_WRITER_ERROR_OOM);
        }
        break;
    }
    case BC_HRBL_KIND_FLOAT64: {
        double value = node->as.float64_value;
        if (!bc_hrbl_encoder_buffer_append(nodes, &value, sizeof(value))) {
            return bc_hrbl_encoder_fail(out_error, BC_HRBL_WRITER_ERROR_OOM);
        }
        break;
    }
    case BC_HRBL_KIND_STRING: {
        const char* data = node->as.string_value.data;
        size_t length = (size_t)node->as.string_value.length;
        if (data == NULL) {
            data = "";
            length = 0u;
        }
        uint32_t pool_offset_intra = node->cached_string_pool_offset;
        if (pool_offset_intra == BC_HRBL_WRITER_STRING_OFFSET_NONE) {
            if (!bc_hrbl_encoder_pool_intern(pool, data, length, &pool_offset_intra, out_error)) {
                return false;
            }
        }
        bc_hrbl_string_ref_t ref;
        ref.pool_offset = pool_offset_intra;
        ref.length = (uint32_t)length;
        uint64_t string_ref_offset = (uint64_t)nodes->size;
        if (!bc_hrbl_encoder_buffer_append(nodes, &ref, sizeof(ref))) {
            return bc_hrbl_encoder_fail(out_error, BC_HRBL_WRITER_ERROR_OOM);
        }
        if (!bc_hrbl_encoder_pool_record_fixup(pool, string_ref_offset, out_error)) {
            return false;
        }
        break;
    }
    case BC_HRBL_KIND_BLOCK:
    case BC_HRBL_KIND_ARRAY:
        return bc_hrbl_encoder_fail(out_error, BC_HRBL_WRITER_ERROR_INTERNAL);
    }
    *out_value_offset = nodes_base_offset + kind_offset;
    return true;
}

static bool bc_hrbl_encoder_emit_block(bc_hrbl_encoder_buffer_t* nodes, bc_hrbl_encoder_pool_t* pool, uint64_t nodes_base_offset,
                                       const bc_hrbl_writer_node_t* node, uint64_t* out_value_offset, bc_hrbl_writer_error_t* out_error)
{
    size_t kind_offset = bc_hrbl_align_up(nodes->size + 1u, 8u) - 1u;
    if (kind_offset > nodes->size) {
        if (!bc_hrbl_encoder_buffer_append_zero(nodes, kind_offset - nodes->size)) {
            return bc_hrbl_encoder_fail(out_error, BC_HRBL_WRITER_ERROR_OOM);
        }
    }
    uint8_t kind_byte = (uint8_t)BC_HRBL_KIND_BLOCK;
    if (!bc_hrbl_encoder_buffer_append(nodes, &kind_byte, 1u)) {
        return bc_hrbl_encoder_fail(out_error, BC_HRBL_WRITER_ERROR_OOM);
    }
    if (!bc_hrbl_encoder_buffer_align_to(nodes, 8u)) {
        return bc_hrbl_encoder_fail(out_error, BC_HRBL_WRITER_ERROR_OOM);
    }

    uint32_t child_count = node->child_count;
    bc_hrbl_block_header_t block_header;
    block_header.child_count = child_count;
    block_header.entries_size_bytes = child_count * BC_HRBL_BLOCK_ENTRY_SIZE;
    if (!bc_hrbl_encoder_buffer_append(nodes, &block_header, sizeof(block_header))) {
        return bc_hrbl_encoder_fail(out_error, BC_HRBL_WRITER_ERROR_OOM);
    }

    size_t entries_buffer_offset = nodes->size;
    size_t entries_total_size = (size_t)child_count * BC_HRBL_BLOCK_ENTRY_SIZE;
    if (!bc_hrbl_encoder_buffer_append_zero(nodes, entries_total_size)) {
        return bc_hrbl_encoder_fail(out_error, BC_HRBL_WRITER_ERROR_OOM);
    }

    bc_hrbl_encoder_pending_entry_t* pending = NULL;
    if (child_count != 0u) {
        void* pending_ptr = NULL;
        if (!bc_allocators_pool_allocate(pool->memory_context, (size_t)child_count * sizeof(bc_hrbl_encoder_pending_entry_t),
                                         &pending_ptr)) {
            return bc_hrbl_encoder_fail(out_error, BC_HRBL_WRITER_ERROR_OOM);
        }
        pending = (bc_hrbl_encoder_pending_entry_t*)pending_ptr;
    }

    uint32_t index = 0u;
    for (const bc_hrbl_writer_node_t* child = node->first_child; child != NULL; child = child->next_sibling) {
        const char* key_data = child->key_data;
        size_t key_length = (size_t)child->key_length;
        if (key_data == NULL) {
            key_data = "";
            key_length = 0u;
        }
        uint64_t key_hash = child->key_hash64;
        if (key_length == 0u && child->key_data == NULL) {
            key_hash = bc_hrbl_hash64(key_data, key_length);
        }
        uint32_t key_pool_offset = child->cached_key_pool_offset;
        if (key_pool_offset == BC_HRBL_WRITER_POOL_OFFSET_NONE) {
            if (!bc_hrbl_encoder_pool_intern_with_hash(pool, key_data, key_length, key_hash, &key_pool_offset, out_error)) {
                if (pending != NULL) {
                    bc_allocators_pool_free(pool->memory_context, pending);
                }
                return false;
            }
        }
        uint64_t child_value_offset = 0u;
        if (!bc_hrbl_encoder_emit_value(nodes, pool, nodes_base_offset, child, &child_value_offset, out_error)) {
            if (pending != NULL) {
                bc_allocators_pool_free(pool->memory_context, pending);
            }
            return false;
        }
        pending[index].key_hash64 = key_hash;
        pending[index].key_pool_offset = key_pool_offset;
        pending[index].key_length = (uint32_t)key_length;
        pending[index].value_offset = child_value_offset;
        index += 1u;
    }

    if (!bc_hrbl_encoder_pending_sort(pool->memory_context, pending, child_count, pool->worker_count, out_error)) {
        if (pending != NULL) {
            bc_allocators_pool_free(pool->memory_context, pending);
        }
        return false;
    }

    uint8_t* entries_write = &nodes->data[entries_buffer_offset];
    for (uint32_t i = 0u; i < child_count; i += 1u) {
        bc_hrbl_entry_t entry;
        entry.key_hash64 = pending[i].key_hash64;
        entry.key_pool_offset = pending[i].key_pool_offset;
        entry.key_length = pending[i].key_length;
        entry.value_offset = pending[i].value_offset;
        bc_hrbl_store_entry(entries_write + (size_t)i * BC_HRBL_BLOCK_ENTRY_SIZE, &entry);
        uint64_t entry_buffer_offset = (uint64_t)entries_buffer_offset + (uint64_t)i * BC_HRBL_BLOCK_ENTRY_SIZE;
        if (!bc_hrbl_encoder_pool_record_fixup(pool, entry_buffer_offset + (uint64_t)offsetof(bc_hrbl_entry_t, key_pool_offset),
                                               out_error)) {
            bc_allocators_pool_free(pool->memory_context, pending);
            return false;
        }
    }
    if (pending != NULL) {
        bc_allocators_pool_free(pool->memory_context, pending);
    }
    *out_value_offset = nodes_base_offset + kind_offset;
    return true;
}

static bool bc_hrbl_encoder_emit_array(bc_hrbl_encoder_buffer_t* nodes, bc_hrbl_encoder_pool_t* pool, uint64_t nodes_base_offset,
                                       const bc_hrbl_writer_node_t* node, uint64_t* out_value_offset, bc_hrbl_writer_error_t* out_error)
{
    size_t kind_offset = bc_hrbl_align_up(nodes->size + 1u, 8u) - 1u;
    if (kind_offset > nodes->size) {
        if (!bc_hrbl_encoder_buffer_append_zero(nodes, kind_offset - nodes->size)) {
            return bc_hrbl_encoder_fail(out_error, BC_HRBL_WRITER_ERROR_OOM);
        }
    }
    uint8_t kind_byte = (uint8_t)BC_HRBL_KIND_ARRAY;
    if (!bc_hrbl_encoder_buffer_append(nodes, &kind_byte, 1u)) {
        return bc_hrbl_encoder_fail(out_error, BC_HRBL_WRITER_ERROR_OOM);
    }
    if (!bc_hrbl_encoder_buffer_align_to(nodes, 8u)) {
        return bc_hrbl_encoder_fail(out_error, BC_HRBL_WRITER_ERROR_OOM);
    }

    uint32_t element_count = node->child_count;
    bc_hrbl_array_header_t array_header;
    array_header.element_count = element_count;
    array_header.body_size_bytes = element_count * (uint32_t)BC_HRBL_ARRAY_ELEMENT_SIZE;
    if (!bc_hrbl_encoder_buffer_append(nodes, &array_header, sizeof(array_header))) {
        return bc_hrbl_encoder_fail(out_error, BC_HRBL_WRITER_ERROR_OOM);
    }

    size_t offsets_buffer_offset = nodes->size;
    size_t offsets_total_size = (size_t)element_count * BC_HRBL_ARRAY_ELEMENT_SIZE;
    if (!bc_hrbl_encoder_buffer_append_zero(nodes, offsets_total_size)) {
        return bc_hrbl_encoder_fail(out_error, BC_HRBL_WRITER_ERROR_OOM);
    }

    uint32_t index = 0u;
    for (const bc_hrbl_writer_node_t* element = node->first_child; element != NULL; element = element->next_sibling) {
        uint64_t element_offset = 0u;
        if (!bc_hrbl_encoder_emit_value(nodes, pool, nodes_base_offset, element, &element_offset, out_error)) {
            return false;
        }
        uint8_t* base = &nodes->data[offsets_buffer_offset + (size_t)index * BC_HRBL_ARRAY_ELEMENT_SIZE];
        bc_hrbl_store_u64(base, element_offset);
        index += 1u;
    }
    *out_value_offset = nodes_base_offset + kind_offset;
    return true;
}

static bool bc_hrbl_encoder_emit_value(bc_hrbl_encoder_buffer_t* nodes, bc_hrbl_encoder_pool_t* pool, uint64_t nodes_base_offset,
                                       const bc_hrbl_writer_node_t* node, uint64_t* out_value_offset, bc_hrbl_writer_error_t* out_error)
{
    switch (node->kind) {
    case BC_HRBL_KIND_NULL:
    case BC_HRBL_KIND_FALSE:
    case BC_HRBL_KIND_TRUE:
    case BC_HRBL_KIND_INT64:
    case BC_HRBL_KIND_UINT64:
    case BC_HRBL_KIND_FLOAT64:
    case BC_HRBL_KIND_STRING:
        return bc_hrbl_encoder_emit_scalar(nodes, pool, nodes_base_offset, node, out_value_offset, out_error);
    case BC_HRBL_KIND_BLOCK:
        return bc_hrbl_encoder_emit_block(nodes, pool, nodes_base_offset, node, out_value_offset, out_error);
    case BC_HRBL_KIND_ARRAY:
        return bc_hrbl_encoder_emit_array(nodes, pool, nodes_base_offset, node, out_value_offset, out_error);
    }
    return bc_hrbl_encoder_fail(out_error, BC_HRBL_WRITER_ERROR_INTERNAL);
}

static void bc_hrbl_encoder_apply_fixups(bc_hrbl_encoder_buffer_t* nodes, const bc_hrbl_encoder_pool_t* pool, uint64_t strings_offset)
{
    uint32_t delta = (uint32_t)strings_offset;
    for (size_t i = 0u; i < pool->fixup_count; i += 1u) {
        uint8_t* pointer = &nodes->data[pool->fixup_offsets[i]];
        uint32_t value;
        bc_core_memcpy(&value, pointer, sizeof(value));
        value += delta;
        bc_core_memcpy(pointer, &value, sizeof(value));
    }
}

bool bc_hrbl_writer_serialize_to_buffer(bc_hrbl_writer_t* writer, uint8_t** out_buffer, size_t* out_size, bc_hrbl_writer_error_t* out_error)
{
    bc_allocators_context_t* memory_context = writer->memory_context;

    bc_hrbl_encoder_pool_t pool;
    if (!bc_hrbl_encoder_pool_init(&pool, memory_context, 4096u, out_error)) {
        return false;
    }
    pool.worker_count = writer->options.worker_count;
    for (bc_hrbl_writer_node_t* root = writer->root_first; root != NULL; root = root->next_sibling) {
        if (!bc_hrbl_encoder_collect_strings(&pool, root, out_error)) {
            bc_hrbl_encoder_pool_destroy(&pool);
            return false;
        }
    }

    bc_hrbl_encoder_buffer_t nodes_buffer;
    if (!bc_hrbl_encoder_buffer_init(&nodes_buffer, memory_context, 4096u)) {
        bc_hrbl_encoder_pool_destroy(&pool);
        return bc_hrbl_encoder_fail(out_error, BC_HRBL_WRITER_ERROR_OOM);
    }

    uint64_t root_count = writer->root_count;
    uint64_t root_index_size = root_count * BC_HRBL_ROOT_ENTRY_SIZE;
    uint64_t root_index_offset = BC_HRBL_ROOT_INDEX_OFFSET;
    uint64_t nodes_offset = root_index_offset + root_index_size;

    bc_hrbl_encoder_pending_entry_t* root_entries = NULL;
    if (root_count != 0u) {
        void* pointer = NULL;
        if (!bc_allocators_pool_allocate(memory_context, (size_t)root_count * sizeof(bc_hrbl_encoder_pending_entry_t), &pointer)) {
            bc_hrbl_encoder_buffer_destroy(&nodes_buffer);
            bc_hrbl_encoder_pool_destroy(&pool);
            return bc_hrbl_encoder_fail(out_error, BC_HRBL_WRITER_ERROR_OOM);
        }
        root_entries = (bc_hrbl_encoder_pending_entry_t*)pointer;
    }

    size_t root_index = 0u;
    for (const bc_hrbl_writer_node_t* root = writer->root_first; root != NULL; root = root->next_sibling) {
        const char* key_data = root->key_data;
        size_t key_length = (size_t)root->key_length;
        if (key_data == NULL) {
            key_data = "";
            key_length = 0u;
        }
        uint64_t key_hash = root->key_hash64;
        if (key_length == 0u && root->key_data == NULL) {
            key_hash = bc_hrbl_hash64(key_data, key_length);
        }
        uint32_t key_pool_offset = root->cached_key_pool_offset;
        if (key_pool_offset == BC_HRBL_WRITER_POOL_OFFSET_NONE) {
            if (!bc_hrbl_encoder_pool_intern_with_hash(&pool, key_data, key_length, key_hash, &key_pool_offset, out_error)) {
                if (root_entries != NULL) {
                    bc_allocators_pool_free(memory_context, root_entries);
                }
                bc_hrbl_encoder_buffer_destroy(&nodes_buffer);
                bc_hrbl_encoder_pool_destroy(&pool);
                return false;
            }
        }
        uint64_t root_value_offset = 0u;
        if (!bc_hrbl_encoder_emit_value(&nodes_buffer, &pool, nodes_offset, root, &root_value_offset, out_error)) {
            if (root_entries != NULL) {
                bc_allocators_pool_free(memory_context, root_entries);
            }
            bc_hrbl_encoder_buffer_destroy(&nodes_buffer);
            bc_hrbl_encoder_pool_destroy(&pool);
            return false;
        }
        root_entries[root_index].key_hash64 = key_hash;
        root_entries[root_index].key_pool_offset = key_pool_offset;
        root_entries[root_index].key_length = (uint32_t)key_length;
        root_entries[root_index].value_offset = root_value_offset;
        root_index += 1u;
    }

    if (!bc_hrbl_encoder_pending_sort(memory_context, root_entries, root_count, pool.worker_count, out_error)) {
        if (root_entries != NULL) {
            bc_allocators_pool_free(memory_context, root_entries);
        }
        bc_hrbl_encoder_buffer_destroy(&nodes_buffer);
        bc_hrbl_encoder_pool_destroy(&pool);
        return false;
    }

    uint64_t nodes_size = nodes_buffer.size;
    uint64_t strings_offset = nodes_offset + nodes_size;
    uint64_t strings_size = pool.buffer.size;
    uint64_t footer_offset = strings_offset + strings_size;
    uint64_t file_size = footer_offset + BC_HRBL_FOOTER_SIZE;

    for (uint64_t i = 0u; i < root_count; i += 1u) {
        uint32_t relative = root_entries[i].key_pool_offset;
        root_entries[i].key_pool_offset = (uint32_t)(strings_offset + relative);
    }

    bc_hrbl_encoder_apply_fixups(&nodes_buffer, &pool, strings_offset);

    void* output_pointer = NULL;
    if (!bc_allocators_pool_allocate(memory_context, (size_t)file_size, &output_pointer)) {
        bc_allocators_pool_free(memory_context, root_entries);
        bc_hrbl_encoder_buffer_destroy(&nodes_buffer);
        bc_hrbl_encoder_pool_destroy(&pool);
        return bc_hrbl_encoder_fail(out_error, BC_HRBL_WRITER_ERROR_OOM);
    }
    uint8_t* output_data = (uint8_t*)output_pointer;
    (void)bc_core_zero(output_data, (size_t)file_size);

    bc_hrbl_header_t header;
    (void)bc_core_zero(&header, sizeof(header));
    header.magic = BC_HRBL_MAGIC;
    header.version_major = BC_HRBL_VERSION_MAJOR;
    header.version_minor = BC_HRBL_VERSION_MINOR;
    header.file_size = file_size;
    header.flags = BC_HRBL_FLAGS_V1_REQUIRED;
    header.root_count = root_count;
    header.root_index_offset = root_index_offset;
    header.root_index_size = root_index_size;
    header.nodes_offset = nodes_offset;
    header.nodes_size = nodes_size;
    header.strings_offset = strings_offset;
    header.strings_size = strings_size;
    header.strings_count = (uint64_t)pool.entries_count;
    header.footer_offset = footer_offset;
    header.checksum_xxh3_64 = 0u;
    bc_core_memcpy(&output_data[0], &header, sizeof(header));

    for (uint64_t i = 0u; i < root_count; i += 1u) {
        bc_hrbl_entry_t entry;
        entry.key_hash64 = root_entries[i].key_hash64;
        entry.key_pool_offset = root_entries[i].key_pool_offset;
        entry.key_length = root_entries[i].key_length;
        entry.value_offset = root_entries[i].value_offset;
        bc_hrbl_store_entry(&output_data[root_index_offset + i * BC_HRBL_ROOT_ENTRY_SIZE], &entry);
    }

    if (nodes_buffer.size != 0u) {
        (void)bc_core_copy(&output_data[nodes_offset], nodes_buffer.data, nodes_buffer.size);
    }
    if (pool.buffer.size != 0u) {
        (void)bc_core_copy(&output_data[strings_offset], pool.buffer.data, pool.buffer.size);
    }

    size_t payload_length = (size_t)(footer_offset - root_index_offset);
    uint64_t checksum = bc_hrbl_hash64(&output_data[root_index_offset], payload_length);

    bc_hrbl_store_u64(&output_data[offsetof(bc_hrbl_header_t, checksum_xxh3_64)], checksum);

    bc_hrbl_footer_t footer;
    (void)bc_core_zero(&footer, sizeof(footer));
    footer.checksum_xxh3_64 = checksum;
    footer.file_size = file_size;
    footer.magic_end = BC_HRBL_MAGIC;
    bc_core_memcpy(&output_data[footer_offset], &footer, sizeof(footer));

    bc_allocators_pool_free(memory_context, root_entries);
    bc_hrbl_encoder_buffer_destroy(&nodes_buffer);
    bc_hrbl_encoder_pool_destroy(&pool);

    *out_buffer = output_data;
    *out_size = (size_t)file_size;
    return true;
}
