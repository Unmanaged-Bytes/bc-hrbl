// SPDX-License-Identifier: MIT

#ifndef BC_HRBL_WRITER_INTERNAL_H
#define BC_HRBL_WRITER_INTERNAL_H

#include "bc_hrbl_format_internal.h"
#include "bc_hrbl_types.h"

#include "bc_allocators.h"
#include "bc_allocators_arena.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct bc_hrbl_writer_node bc_hrbl_writer_node_t;

#define BC_HRBL_WRITER_POOL_OFFSET_NONE UINT32_C(0xFFFFFFFF)
#define BC_HRBL_WRITER_STRING_OFFSET_NONE UINT32_C(0xFFFFFFFF)

struct bc_hrbl_writer_node {
    bc_hrbl_kind_t               kind;
    uint32_t                     key_length;
    const char*                  key_data;
    uint64_t                     key_hash64;
    uint32_t                     cached_key_pool_offset;
    uint32_t                     cached_string_pool_offset;
    uint32_t                     child_count;
    uint32_t                     reserved;
    bc_hrbl_writer_node_t*       parent;
    bc_hrbl_writer_node_t*       first_child;
    bc_hrbl_writer_node_t*       last_child;
    bc_hrbl_writer_node_t*       next_sibling;
    union {
        int64_t                  int64_value;
        uint64_t                 uint64_value;
        double                   float64_value;
        struct {
            const char*          data;
            uint32_t             length;
        } string_value;
    } as;
};

struct bc_hrbl_writer {
    bc_allocators_context_t*      memory_context;
    bc_allocators_arena_t*        arena;
    bc_hrbl_writer_options_t      options;
    bc_hrbl_writer_node_t*        root_first;
    bc_hrbl_writer_node_t*        root_last;
    uint32_t                      root_count;
    bc_hrbl_writer_node_t*        current_scope;
    bool                          error_flag;
};

bool bc_hrbl_writer_serialize_to_buffer(struct bc_hrbl_writer* writer, uint8_t** out_buffer, size_t* out_size);

#endif
