// SPDX-License-Identifier: MIT

#include "bc_hrbl_reader.h"
#include "bc_hrbl_format_internal.h"

#include <stdlib.h>
#include <string.h>

bool bc_hrbl_reader_open(bc_allocators_context_t* memory_context, const char* path, bc_hrbl_reader_t** out_reader)
{
    (void)memory_context;
    (void)path;
    if (out_reader == NULL) {
        return false;
    }
    *out_reader = NULL;
    return false;
}

bool bc_hrbl_reader_open_buffer(bc_allocators_context_t* memory_context, const void* data, size_t size,
                                bc_hrbl_reader_t** out_reader)
{
    (void)memory_context;
    (void)data;
    (void)size;
    if (out_reader == NULL) {
        return false;
    }
    *out_reader = NULL;
    return false;
}

void bc_hrbl_reader_destroy(bc_hrbl_reader_t* reader)
{
    (void)reader;
}

bool bc_hrbl_reader_root_count(const bc_hrbl_reader_t* reader, uint64_t* out_count)
{
    (void)reader;
    if (out_count != NULL) {
        *out_count = 0;
    }
    return false;
}

bool bc_hrbl_reader_find(const bc_hrbl_reader_t* reader, const char* path, size_t path_length,
                         bc_hrbl_value_ref_t* out_value)
{
    (void)reader;
    (void)path;
    (void)path_length;
    if (out_value != NULL) {
        memset(out_value, 0, sizeof(*out_value));
    }
    return false;
}

bool bc_hrbl_reader_value_kind(const bc_hrbl_value_ref_t* value, bc_hrbl_kind_t* out_kind)
{
    (void)value;
    if (out_kind != NULL) {
        *out_kind = BC_HRBL_KIND_NULL;
    }
    return false;
}

bool bc_hrbl_reader_get_bool(const bc_hrbl_value_ref_t* value, bool* out_value)
{
    (void)value;
    if (out_value != NULL) {
        *out_value = false;
    }
    return false;
}

bool bc_hrbl_reader_get_int64(const bc_hrbl_value_ref_t* value, int64_t* out_value)
{
    (void)value;
    if (out_value != NULL) {
        *out_value = 0;
    }
    return false;
}

bool bc_hrbl_reader_get_uint64(const bc_hrbl_value_ref_t* value, uint64_t* out_value)
{
    (void)value;
    if (out_value != NULL) {
        *out_value = 0;
    }
    return false;
}

bool bc_hrbl_reader_get_float64(const bc_hrbl_value_ref_t* value, double* out_value)
{
    (void)value;
    if (out_value != NULL) {
        *out_value = 0.0;
    }
    return false;
}

bool bc_hrbl_reader_get_string(const bc_hrbl_value_ref_t* value, const char** out_data, size_t* out_length)
{
    (void)value;
    if (out_data != NULL) {
        *out_data = NULL;
    }
    if (out_length != NULL) {
        *out_length = 0;
    }
    return false;
}

bool bc_hrbl_reader_iter_block(const bc_hrbl_value_ref_t* block, bc_hrbl_iter_t* out_iter)
{
    (void)block;
    if (out_iter != NULL) {
        memset(out_iter, 0, sizeof(*out_iter));
    }
    return false;
}

bool bc_hrbl_reader_iter_array(const bc_hrbl_value_ref_t* array, bc_hrbl_iter_t* out_iter)
{
    (void)array;
    if (out_iter != NULL) {
        memset(out_iter, 0, sizeof(*out_iter));
    }
    return false;
}

bool bc_hrbl_iter_next(bc_hrbl_iter_t* iter, bc_hrbl_value_ref_t* out_value, const char** out_key, size_t* out_key_length)
{
    (void)iter;
    if (out_value != NULL) {
        memset(out_value, 0, sizeof(*out_value));
    }
    if (out_key != NULL) {
        *out_key = NULL;
    }
    if (out_key_length != NULL) {
        *out_key_length = 0;
    }
    return false;
}
