// SPDX-License-Identifier: MIT

#ifndef BC_HRBL_FORMAT_INTERNAL_H
#define BC_HRBL_FORMAT_INTERNAL_H

#include "bc_hrbl_types.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#define BC_HRBL_HEADER_SIZE         128u
#define BC_HRBL_FOOTER_SIZE         32u
#define BC_HRBL_ROOT_INDEX_OFFSET   128u
#define BC_HRBL_ROOT_ENTRY_SIZE     24u
#define BC_HRBL_BLOCK_ENTRY_SIZE    24u
#define BC_HRBL_ARRAY_ELEMENT_SIZE  8u

#define BC_HRBL_FLAG_STRINGS_DEDUP       (UINT64_C(1) << 0)
#define BC_HRBL_FLAG_KEYS_SORTED_HASH    (UINT64_C(1) << 1)
#define BC_HRBL_FLAG_ROOTS_SORTED_HASH   (UINT64_C(1) << 2)
#define BC_HRBL_FLAG_HAS_CHECKSUM        (UINT64_C(1) << 3)
#define BC_HRBL_FLAGS_V1_REQUIRED        (BC_HRBL_FLAG_STRINGS_DEDUP \
                                          | BC_HRBL_FLAG_KEYS_SORTED_HASH \
                                          | BC_HRBL_FLAG_ROOTS_SORTED_HASH \
                                          | BC_HRBL_FLAG_HAS_CHECKSUM)
#define BC_HRBL_FLAGS_V1_RESERVED_MASK   (~BC_HRBL_FLAGS_V1_REQUIRED)

typedef struct bc_hrbl_header {
    uint32_t magic;
    uint16_t version_major;
    uint16_t version_minor;
    uint64_t file_size;
    uint64_t flags;
    uint64_t root_count;
    uint64_t root_index_offset;
    uint64_t root_index_size;
    uint64_t nodes_offset;
    uint64_t nodes_size;
    uint64_t strings_offset;
    uint64_t strings_size;
    uint64_t strings_count;
    uint64_t footer_offset;
    uint64_t checksum_xxh3_64;
    uint8_t  reserved[24];
} bc_hrbl_header_t;

static_assert(sizeof(bc_hrbl_header_t) == BC_HRBL_HEADER_SIZE, "hrbl header must be 128 B");
static_assert(offsetof(bc_hrbl_header_t, magic)             ==   0, "magic offset");
static_assert(offsetof(bc_hrbl_header_t, version_major)     ==   4, "version_major offset");
static_assert(offsetof(bc_hrbl_header_t, version_minor)     ==   6, "version_minor offset");
static_assert(offsetof(bc_hrbl_header_t, file_size)         ==   8, "file_size offset");
static_assert(offsetof(bc_hrbl_header_t, flags)             ==  16, "flags offset");
static_assert(offsetof(bc_hrbl_header_t, root_count)        ==  24, "root_count offset");
static_assert(offsetof(bc_hrbl_header_t, root_index_offset) ==  32, "root_index_offset offset");
static_assert(offsetof(bc_hrbl_header_t, root_index_size)   ==  40, "root_index_size offset");
static_assert(offsetof(bc_hrbl_header_t, nodes_offset)      ==  48, "nodes_offset offset");
static_assert(offsetof(bc_hrbl_header_t, nodes_size)        ==  56, "nodes_size offset");
static_assert(offsetof(bc_hrbl_header_t, strings_offset)    ==  64, "strings_offset offset");
static_assert(offsetof(bc_hrbl_header_t, strings_size)      ==  72, "strings_size offset");
static_assert(offsetof(bc_hrbl_header_t, strings_count)     ==  80, "strings_count offset");
static_assert(offsetof(bc_hrbl_header_t, footer_offset)     ==  88, "footer_offset offset");
static_assert(offsetof(bc_hrbl_header_t, checksum_xxh3_64)  ==  96, "checksum offset");
static_assert(offsetof(bc_hrbl_header_t, reserved)          == 104, "reserved offset");

typedef struct bc_hrbl_footer {
    uint64_t checksum_xxh3_64;
    uint64_t file_size;
    uint32_t magic_end;
    uint8_t  reserved[12];
} bc_hrbl_footer_t;

static_assert(sizeof(bc_hrbl_footer_t) == BC_HRBL_FOOTER_SIZE, "hrbl footer must be 32 B");
static_assert(offsetof(bc_hrbl_footer_t, checksum_xxh3_64) ==  0, "footer checksum offset");
static_assert(offsetof(bc_hrbl_footer_t, file_size)        ==  8, "footer file_size offset");
static_assert(offsetof(bc_hrbl_footer_t, magic_end)        == 16, "footer magic_end offset");
static_assert(offsetof(bc_hrbl_footer_t, reserved)         == 20, "footer reserved offset");

typedef struct bc_hrbl_entry {
    uint64_t key_hash64;
    uint32_t key_pool_offset;
    uint32_t key_length;
    uint64_t value_offset;
} bc_hrbl_entry_t;

static_assert(sizeof(bc_hrbl_entry_t) == BC_HRBL_ROOT_ENTRY_SIZE, "hrbl entry must be 24 B");
static_assert(offsetof(bc_hrbl_entry_t, key_hash64)      ==  0, "entry hash offset");
static_assert(offsetof(bc_hrbl_entry_t, key_pool_offset) ==  8, "entry key_pool_offset offset");
static_assert(offsetof(bc_hrbl_entry_t, key_length)      == 12, "entry key_length offset");
static_assert(offsetof(bc_hrbl_entry_t, value_offset)    == 16, "entry value_offset offset");

typedef struct bc_hrbl_block_header {
    uint32_t child_count;
    uint32_t entries_size_bytes;
} bc_hrbl_block_header_t;

static_assert(sizeof(bc_hrbl_block_header_t) == 8, "hrbl block header must be 8 B");

typedef struct bc_hrbl_array_header {
    uint32_t element_count;
    uint32_t body_size_bytes;
} bc_hrbl_array_header_t;

static_assert(sizeof(bc_hrbl_array_header_t) == 8, "hrbl array header must be 8 B");

typedef struct bc_hrbl_string_ref {
    uint32_t pool_offset;
    uint32_t length;
} bc_hrbl_string_ref_t;

static_assert(sizeof(bc_hrbl_string_ref_t) == 8, "hrbl string ref must be 8 B");

static inline uint8_t bc_hrbl_kind_body_align(bc_hrbl_kind_t kind)
{
    switch (kind) {
    case BC_HRBL_KIND_NULL:
    case BC_HRBL_KIND_FALSE:
    case BC_HRBL_KIND_TRUE:
        return 1;
    case BC_HRBL_KIND_STRING:
        return 4;
    case BC_HRBL_KIND_INT64:
    case BC_HRBL_KIND_UINT64:
    case BC_HRBL_KIND_FLOAT64:
    case BC_HRBL_KIND_BLOCK:
    case BC_HRBL_KIND_ARRAY:
        return 8;
    }
    return 0;
}

static inline uint8_t bc_hrbl_kind_body_size(bc_hrbl_kind_t kind)
{
    switch (kind) {
    case BC_HRBL_KIND_NULL:
    case BC_HRBL_KIND_FALSE:
    case BC_HRBL_KIND_TRUE:
        return 0;
    case BC_HRBL_KIND_STRING:
        return 8;
    case BC_HRBL_KIND_INT64:
    case BC_HRBL_KIND_UINT64:
    case BC_HRBL_KIND_FLOAT64:
        return 8;
    case BC_HRBL_KIND_BLOCK:
    case BC_HRBL_KIND_ARRAY:
        return 0;
    }
    return 0;
}

static inline size_t bc_hrbl_align_up(size_t value, size_t alignment)
{
    return (value + alignment - 1u) & ~(alignment - 1u);
}

static inline size_t bc_hrbl_body_offset_from_kind(size_t kind_offset, bc_hrbl_kind_t kind)
{
    return bc_hrbl_align_up(kind_offset + 1u, bc_hrbl_kind_body_align(kind));
}

#endif
