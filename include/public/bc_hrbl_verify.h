// SPDX-License-Identifier: MIT

#ifndef BC_HRBL_VERIFY_H
#define BC_HRBL_VERIFY_H

#include <stdbool.h>
#include <stddef.h>

typedef enum bc_hrbl_verify_status {
    BC_HRBL_VERIFY_OK = 0,
    BC_HRBL_VERIFY_ERR_TOO_SMALL = 1,
    BC_HRBL_VERIFY_ERR_BAD_MAGIC = 2,
    BC_HRBL_VERIFY_ERR_BAD_VERSION = 3,
    BC_HRBL_VERIFY_ERR_BAD_FLAGS = 4,
    BC_HRBL_VERIFY_ERR_BAD_FILE_SIZE = 5,
    BC_HRBL_VERIFY_ERR_BAD_FOOTER = 6,
    BC_HRBL_VERIFY_ERR_BAD_CHECKSUM = 7,
    BC_HRBL_VERIFY_ERR_BAD_LAYOUT = 8,
    BC_HRBL_VERIFY_ERR_BAD_ROOT_INDEX = 9,
    BC_HRBL_VERIFY_ERR_BAD_NODE = 10,
    BC_HRBL_VERIFY_ERR_BAD_STRING = 11,
    BC_HRBL_VERIFY_ERR_BAD_UTF8 = 12,
    BC_HRBL_VERIFY_ERR_DUPLICATE_KEY = 13,
    BC_HRBL_VERIFY_ERR_IO = 14
} bc_hrbl_verify_status_t;

bc_hrbl_verify_status_t bc_hrbl_verify_buffer(const void* data, size_t size);

bc_hrbl_verify_status_t bc_hrbl_verify_file(const char* path);

const char* bc_hrbl_verify_status_name(bc_hrbl_verify_status_t status);

#endif
