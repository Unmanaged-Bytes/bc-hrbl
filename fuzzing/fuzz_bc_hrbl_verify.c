// SPDX-License-Identifier: MIT

#include "bc_hrbl_verify.h"

#include <stddef.h>
#include <stdint.h>

#ifdef BC_FUZZ_LIBFUZZER
int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    (void)bc_hrbl_verify_buffer(data, size);
    return 0;
}
#else
int main(void)
{
    return 0;
}
#endif
