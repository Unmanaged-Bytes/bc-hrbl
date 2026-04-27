// SPDX-License-Identifier: MIT

#include "bc_hrbl.h"
#include "bc_allocators.h"

#include <bc/bc_core_io.h>

#include <stdint.h>
#include <stdio.h>
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
        static char sink[1u << 20];
        bc_core_writer_t writer;
        if (bc_core_writer_init_buffer_only(&writer, sink, sizeof(sink))) {
            (void)bc_hrbl_export_json(reader, &writer);
            bc_core_writer_destroy(&writer);
        }
        if (bc_core_writer_init_buffer_only(&writer, sink, sizeof(sink))) {
            (void)bc_hrbl_export_yaml(reader, &writer);
            bc_core_writer_destroy(&writer);
        }
        if (bc_core_writer_init_buffer_only(&writer, sink, sizeof(sink))) {
            (void)bc_hrbl_export_ini(reader, &writer);
            bc_core_writer_destroy(&writer);
        }
        bc_hrbl_reader_destroy(reader);
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
