// SPDX-License-Identifier: MIT

#include "bc_hrbl_reader.h"
#include "bc_hrbl_verify.h"
#include "bc_allocators.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef BC_FUZZ_LIBFUZZER
int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (bc_hrbl_verify_buffer(data, size) != BC_HRBL_VERIFY_OK) {
        return 0;
    }
    bc_allocators_context_config_t config;
    memset(&config, 0, sizeof(config));
    bc_allocators_context_t* memory = NULL;
    if (!bc_allocators_context_create(&config, &memory)) {
        return 0;
    }
    bc_hrbl_reader_t* reader = NULL;
    if (bc_hrbl_reader_open_buffer(memory, data, size, &reader)) {
        uint64_t root_count = 0u;
        (void)bc_hrbl_reader_root_count(reader, &root_count);
        bc_hrbl_value_ref_t value;
        (void)bc_hrbl_reader_find(reader, "nonexistent", 11u, &value);
        (void)bc_hrbl_reader_find(reader, "a.b[0].c", 8u, &value);
        bc_hrbl_reader_close(reader);
    }
    bc_allocators_context_destroy(memory);
    return 0;
}
#else
int main(void)
{
    return 0;
}
#endif
