// SPDX-License-Identifier: MIT

#include "bc_hrbl_hash.h"

#include <xxhash.h>

uint64_t bc_hrbl_hash64(const void* data, size_t length)
{
    return (uint64_t)XXH3_64bits(data, length);
}
