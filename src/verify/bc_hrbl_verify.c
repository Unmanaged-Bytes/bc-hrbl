// SPDX-License-Identifier: MIT

#include "bc_hrbl_verify.h"
#include "bc_hrbl_format_internal.h"

bc_hrbl_verify_status_t bc_hrbl_verify_buffer(const void* data, size_t size)
{
    if (data == NULL) {
        return BC_HRBL_VERIFY_ERR_TOO_SMALL;
    }
    if (size < BC_HRBL_HEADER_SIZE + BC_HRBL_FOOTER_SIZE) {
        return BC_HRBL_VERIFY_ERR_TOO_SMALL;
    }
    return BC_HRBL_VERIFY_ERR_BAD_MAGIC;
}

bc_hrbl_verify_status_t bc_hrbl_verify_file(const char* path)
{
    (void)path;
    return BC_HRBL_VERIFY_ERR_IO;
}

const char* bc_hrbl_verify_status_name(bc_hrbl_verify_status_t status)
{
    switch (status) {
    case BC_HRBL_VERIFY_OK:                 return "ok";
    case BC_HRBL_VERIFY_ERR_TOO_SMALL:      return "too_small";
    case BC_HRBL_VERIFY_ERR_BAD_MAGIC:      return "bad_magic";
    case BC_HRBL_VERIFY_ERR_BAD_VERSION:    return "bad_version";
    case BC_HRBL_VERIFY_ERR_BAD_FLAGS:      return "bad_flags";
    case BC_HRBL_VERIFY_ERR_BAD_FILE_SIZE:  return "bad_file_size";
    case BC_HRBL_VERIFY_ERR_BAD_FOOTER:     return "bad_footer";
    case BC_HRBL_VERIFY_ERR_BAD_CHECKSUM:   return "bad_checksum";
    case BC_HRBL_VERIFY_ERR_BAD_LAYOUT:     return "bad_layout";
    case BC_HRBL_VERIFY_ERR_BAD_ROOT_INDEX: return "bad_root_index";
    case BC_HRBL_VERIFY_ERR_BAD_NODE:       return "bad_node";
    case BC_HRBL_VERIFY_ERR_BAD_STRING:     return "bad_string";
    case BC_HRBL_VERIFY_ERR_BAD_UTF8:       return "bad_utf8";
    case BC_HRBL_VERIFY_ERR_DUPLICATE_KEY:  return "duplicate_key";
    case BC_HRBL_VERIFY_ERR_IO:             return "io_error";
    }
    return "unknown";
}
