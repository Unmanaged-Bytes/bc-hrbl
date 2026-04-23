// SPDX-License-Identifier: MIT

#ifndef BC_HRBL_WRITER_H
#define BC_HRBL_WRITER_H

#include "bc_allocators.h"
#include "bc_hrbl_types.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct bc_hrbl_writer_options {
    uint32_t worker_count;
    bool     deduplicate_strings;
} bc_hrbl_writer_options_t;

bool bc_hrbl_writer_create(bc_allocators_context_t* memory_context, bc_hrbl_writer_t** out_writer);

bool bc_hrbl_writer_create_ex(bc_allocators_context_t* memory_context, const bc_hrbl_writer_options_t* options,
                              bc_hrbl_writer_t** out_writer);

void bc_hrbl_writer_destroy(bc_hrbl_writer_t* writer);

bool bc_hrbl_writer_set_null(bc_hrbl_writer_t* writer, const char* key, size_t key_length);
bool bc_hrbl_writer_set_bool(bc_hrbl_writer_t* writer, const char* key, size_t key_length, bool value);
bool bc_hrbl_writer_set_int64(bc_hrbl_writer_t* writer, const char* key, size_t key_length, int64_t value);
bool bc_hrbl_writer_set_uint64(bc_hrbl_writer_t* writer, const char* key, size_t key_length, uint64_t value);
bool bc_hrbl_writer_set_float64(bc_hrbl_writer_t* writer, const char* key, size_t key_length, double value);
bool bc_hrbl_writer_set_string(bc_hrbl_writer_t* writer, const char* key, size_t key_length,
                               const char* value, size_t value_length);

bool bc_hrbl_writer_begin_block(bc_hrbl_writer_t* writer, const char* key, size_t key_length);
bool bc_hrbl_writer_end_block(bc_hrbl_writer_t* writer);

bool bc_hrbl_writer_begin_array(bc_hrbl_writer_t* writer, const char* key, size_t key_length);
bool bc_hrbl_writer_end_array(bc_hrbl_writer_t* writer);

bool bc_hrbl_writer_append_null(bc_hrbl_writer_t* writer);
bool bc_hrbl_writer_append_bool(bc_hrbl_writer_t* writer, bool value);
bool bc_hrbl_writer_append_int64(bc_hrbl_writer_t* writer, int64_t value);
bool bc_hrbl_writer_append_uint64(bc_hrbl_writer_t* writer, uint64_t value);
bool bc_hrbl_writer_append_float64(bc_hrbl_writer_t* writer, double value);
bool bc_hrbl_writer_append_string(bc_hrbl_writer_t* writer, const char* value, size_t value_length);

bool bc_hrbl_writer_finalize_to_file(bc_hrbl_writer_t* writer, const char* output_path);
bool bc_hrbl_writer_finalize_to_buffer(bc_hrbl_writer_t* writer, void** out_buffer, size_t* out_size);

#endif
