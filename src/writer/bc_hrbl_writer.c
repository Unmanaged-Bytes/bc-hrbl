// SPDX-License-Identifier: MIT

#include "bc_hrbl_writer.h"

#include <stdlib.h>

bool bc_hrbl_writer_create(bc_allocators_context_t* memory_context, bc_hrbl_writer_t** out_writer)
{
    (void)memory_context;
    if (out_writer == NULL) {
        return false;
    }
    *out_writer = NULL;
    return false;
}

bool bc_hrbl_writer_create_ex(bc_allocators_context_t* memory_context, const bc_hrbl_writer_options_t* options,
                              bc_hrbl_writer_t** out_writer)
{
    (void)memory_context;
    (void)options;
    if (out_writer == NULL) {
        return false;
    }
    *out_writer = NULL;
    return false;
}

void bc_hrbl_writer_destroy(bc_hrbl_writer_t* writer)
{
    (void)writer;
}

bool bc_hrbl_writer_set_null(bc_hrbl_writer_t* writer, const char* key, size_t key_length)
{
    (void)writer;
    (void)key;
    (void)key_length;
    return false;
}

bool bc_hrbl_writer_set_bool(bc_hrbl_writer_t* writer, const char* key, size_t key_length, bool value)
{
    (void)writer;
    (void)key;
    (void)key_length;
    (void)value;
    return false;
}

bool bc_hrbl_writer_set_int64(bc_hrbl_writer_t* writer, const char* key, size_t key_length, int64_t value)
{
    (void)writer;
    (void)key;
    (void)key_length;
    (void)value;
    return false;
}

bool bc_hrbl_writer_set_uint64(bc_hrbl_writer_t* writer, const char* key, size_t key_length, uint64_t value)
{
    (void)writer;
    (void)key;
    (void)key_length;
    (void)value;
    return false;
}

bool bc_hrbl_writer_set_float64(bc_hrbl_writer_t* writer, const char* key, size_t key_length, double value)
{
    (void)writer;
    (void)key;
    (void)key_length;
    (void)value;
    return false;
}

bool bc_hrbl_writer_set_string(bc_hrbl_writer_t* writer, const char* key, size_t key_length,
                               const char* value, size_t value_length)
{
    (void)writer;
    (void)key;
    (void)key_length;
    (void)value;
    (void)value_length;
    return false;
}

bool bc_hrbl_writer_begin_block(bc_hrbl_writer_t* writer, const char* key, size_t key_length)
{
    (void)writer;
    (void)key;
    (void)key_length;
    return false;
}

bool bc_hrbl_writer_end_block(bc_hrbl_writer_t* writer)
{
    (void)writer;
    return false;
}

bool bc_hrbl_writer_begin_array(bc_hrbl_writer_t* writer, const char* key, size_t key_length)
{
    (void)writer;
    (void)key;
    (void)key_length;
    return false;
}

bool bc_hrbl_writer_end_array(bc_hrbl_writer_t* writer)
{
    (void)writer;
    return false;
}

bool bc_hrbl_writer_append_null(bc_hrbl_writer_t* writer)
{
    (void)writer;
    return false;
}

bool bc_hrbl_writer_append_bool(bc_hrbl_writer_t* writer, bool value)
{
    (void)writer;
    (void)value;
    return false;
}

bool bc_hrbl_writer_append_int64(bc_hrbl_writer_t* writer, int64_t value)
{
    (void)writer;
    (void)value;
    return false;
}

bool bc_hrbl_writer_append_uint64(bc_hrbl_writer_t* writer, uint64_t value)
{
    (void)writer;
    (void)value;
    return false;
}

bool bc_hrbl_writer_append_float64(bc_hrbl_writer_t* writer, double value)
{
    (void)writer;
    (void)value;
    return false;
}

bool bc_hrbl_writer_append_string(bc_hrbl_writer_t* writer, const char* value, size_t value_length)
{
    (void)writer;
    (void)value;
    (void)value_length;
    return false;
}

bool bc_hrbl_writer_finalize_to_file(bc_hrbl_writer_t* writer, const char* output_path)
{
    (void)writer;
    (void)output_path;
    return false;
}

bool bc_hrbl_writer_finalize_to_buffer(bc_hrbl_writer_t* writer, void** out_buffer, size_t* out_size)
{
    (void)writer;
    if (out_buffer != NULL) {
        *out_buffer = NULL;
    }
    if (out_size != NULL) {
        *out_size = 0;
    }
    return false;
}
