// SPDX-License-Identifier: MIT

#ifndef BC_HRBL_CONVERT_H
#define BC_HRBL_CONVERT_H

#include "bc_allocators.h"
#include "bc_hrbl_writer.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct bc_hrbl_convert_error {
    const char* message;
    size_t      byte_offset;
    uint32_t    line;
    uint32_t    column;
} bc_hrbl_convert_error_t;

bool bc_hrbl_convert_json_to_writer(bc_hrbl_writer_t* writer, const char* json_text, size_t text_length,
                                    bc_hrbl_convert_error_t* out_error);

bool bc_hrbl_convert_json_buffer_to_hrbl(bc_allocators_context_t* memory_context, const char* json_text, size_t text_length,
                                         void** out_hrbl_buffer, size_t* out_hrbl_size, bc_hrbl_convert_error_t* out_error);

#endif
