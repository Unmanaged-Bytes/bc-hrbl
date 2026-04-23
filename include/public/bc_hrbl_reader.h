// SPDX-License-Identifier: MIT

#ifndef BC_HRBL_READER_H
#define BC_HRBL_READER_H

#include "bc_allocators.h"
#include "bc_hrbl_types.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool bc_hrbl_reader_open(bc_allocators_context_t* memory_context, const char* path, bc_hrbl_reader_t** out_reader);

bool bc_hrbl_reader_open_buffer(bc_allocators_context_t* memory_context, const void* data, size_t size,
                                bc_hrbl_reader_t** out_reader);

void bc_hrbl_reader_destroy(bc_hrbl_reader_t* reader);

bool bc_hrbl_reader_root_count(const bc_hrbl_reader_t* reader, uint64_t* out_count);

bool bc_hrbl_reader_find(const bc_hrbl_reader_t* reader, const char* path, size_t path_length,
                         bc_hrbl_value_ref_t* out_value);

bool bc_hrbl_reader_value_kind(const bc_hrbl_value_ref_t* value, bc_hrbl_kind_t* out_kind);

bool bc_hrbl_reader_get_bool(const bc_hrbl_value_ref_t* value, bool* out_value);
bool bc_hrbl_reader_get_int64(const bc_hrbl_value_ref_t* value, int64_t* out_value);
bool bc_hrbl_reader_get_uint64(const bc_hrbl_value_ref_t* value, uint64_t* out_value);
bool bc_hrbl_reader_get_float64(const bc_hrbl_value_ref_t* value, double* out_value);
bool bc_hrbl_reader_get_string(const bc_hrbl_value_ref_t* value, const char** out_data, size_t* out_length);

bool bc_hrbl_reader_iter_block(const bc_hrbl_value_ref_t* block, bc_hrbl_iter_t* out_iter);
bool bc_hrbl_reader_iter_array(const bc_hrbl_value_ref_t* array, bc_hrbl_iter_t* out_iter);

bool bc_hrbl_iter_next(bc_hrbl_iter_t* iter, bc_hrbl_value_ref_t* out_value, const char** out_key, size_t* out_key_length);

#endif
