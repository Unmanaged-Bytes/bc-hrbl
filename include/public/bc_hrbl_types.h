// SPDX-License-Identifier: MIT

#ifndef BC_HRBL_TYPES_H
#define BC_HRBL_TYPES_H

#include <stdint.h>

#define BC_HRBL_MAGIC UINT32_C(0x4C425248)
#define BC_HRBL_VERSION_MAJOR UINT16_C(1)
#define BC_HRBL_VERSION_MINOR UINT16_C(0)

typedef enum bc_hrbl_kind {
    BC_HRBL_KIND_NULL = 0x00,
    BC_HRBL_KIND_FALSE = 0x01,
    BC_HRBL_KIND_TRUE = 0x02,
    BC_HRBL_KIND_INT64 = 0x03,
    BC_HRBL_KIND_UINT64 = 0x04,
    BC_HRBL_KIND_FLOAT64 = 0x05,
    BC_HRBL_KIND_STRING = 0x06,
    BC_HRBL_KIND_BLOCK = 0x10,
    BC_HRBL_KIND_ARRAY = 0x11
} bc_hrbl_kind_t;

typedef struct bc_hrbl_reader bc_hrbl_reader_t;
typedef struct bc_hrbl_writer bc_hrbl_writer_t;

typedef struct bc_hrbl_value_ref {
    const bc_hrbl_reader_t* reader;
    uint64_t node_offset;
    bc_hrbl_kind_t kind;
    uint32_t reserved;
} bc_hrbl_value_ref_t;

typedef struct bc_hrbl_iter {
    const bc_hrbl_reader_t* reader;
    uint64_t cursor_offset;
    uint64_t end_offset;
    uint32_t remaining;
    uint32_t is_block;
} bc_hrbl_iter_t;

#endif
