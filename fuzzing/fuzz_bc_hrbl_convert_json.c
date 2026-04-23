// SPDX-License-Identifier: MIT

#include "bc_hrbl_convert.h"
#include "bc_allocators.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef BC_FUZZ_LIBFUZZER
int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    bc_allocators_context_config_t config;
    memset(&config, 0, sizeof(config));
    bc_allocators_context_t* memory = NULL;
    if (!bc_allocators_context_create(&config, &memory)) {
        return 0;
    }
    void* buffer = NULL;
    size_t out_size = 0u;
    bc_hrbl_convert_error_t error;
    (void)bc_hrbl_convert_json_buffer_to_hrbl(memory, (const char*)data, size, &buffer, &out_size, &error);
    free(buffer);
    bc_allocators_context_destroy(memory);
    return 0;
}
#else
int main(void)
{
    return 0;
}
#endif
