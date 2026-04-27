// SPDX-License-Identifier: MIT

#ifndef BC_HRBL_READER_INTERNAL_H
#define BC_HRBL_READER_INTERNAL_H

#include "bc_hrbl_format_internal.h"
#include "bc_hrbl_types.h"

#include "bc_allocators.h"
#include "bc_io_mmap.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct bc_hrbl_reader {
    bc_allocators_context_t* memory_context;
    bc_io_mmap_t* mmap_handle;
    const uint8_t* base;
    size_t size;
    const bc_hrbl_header_t* header;
};

bool bc_hrbl_reader_root_at_offset(const bc_hrbl_reader_t* reader, uint64_t index, const char** out_key, uint32_t* out_key_length,
                                   uint64_t* out_value_offset);

bool bc_hrbl_reader_find_root_offset(const bc_hrbl_reader_t* reader, const char* key, size_t key_length, uint64_t* out_value_offset);

bool bc_hrbl_reader_kind_at(const bc_hrbl_reader_t* reader, uint64_t value_offset, bc_hrbl_kind_t* out_kind);

bool bc_hrbl_reader_block_child_count_at(const bc_hrbl_reader_t* reader, uint64_t block_offset, uint32_t* out_count);
bool bc_hrbl_reader_block_entry_at_offset(const bc_hrbl_reader_t* reader, uint64_t block_offset, uint32_t index, const char** out_key,
                                          uint32_t* out_key_length, uint64_t* out_value_offset);
bool bc_hrbl_reader_block_find_offset(const bc_hrbl_reader_t* reader, uint64_t block_offset, const char* key, size_t key_length,
                                      uint64_t* out_value_offset);

bool bc_hrbl_reader_array_length_at(const bc_hrbl_reader_t* reader, uint64_t array_offset, uint32_t* out_length);
bool bc_hrbl_reader_array_at_offset(const bc_hrbl_reader_t* reader, uint64_t array_offset, uint32_t index, uint64_t* out_value_offset);

bool bc_hrbl_reader_scalar_int64_at(const bc_hrbl_reader_t* reader, uint64_t value_offset, int64_t* out_value);
bool bc_hrbl_reader_scalar_uint64_at(const bc_hrbl_reader_t* reader, uint64_t value_offset, uint64_t* out_value);
bool bc_hrbl_reader_scalar_float64_at(const bc_hrbl_reader_t* reader, uint64_t value_offset, double* out_value);
bool bc_hrbl_reader_scalar_bool_at(const bc_hrbl_reader_t* reader, uint64_t value_offset, bool* out_value);
bool bc_hrbl_reader_scalar_string_at(const bc_hrbl_reader_t* reader, uint64_t value_offset, const char** out_data, size_t* out_length);

bool bc_hrbl_reader_resolve_path(const bc_hrbl_reader_t* reader, const char* path, size_t path_length, uint64_t* out_value_offset);

bool bc_hrbl_reader_block_body_offsets(const bc_hrbl_reader_t* reader, uint64_t block_offset, bc_hrbl_block_header_t* out_header,
                                       uint64_t* out_entries_offset);

bool bc_hrbl_reader_array_body_offsets(const bc_hrbl_reader_t* reader, uint64_t array_offset, bc_hrbl_array_header_t* out_header,
                                       uint64_t* out_elements_offset);

#endif
