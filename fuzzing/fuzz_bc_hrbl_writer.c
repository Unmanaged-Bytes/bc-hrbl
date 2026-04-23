// SPDX-License-Identifier: MIT

#include "bc_hrbl.h"
#include "bc_allocators.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define OP_SET_NULL   0x00
#define OP_SET_BOOL   0x01
#define OP_SET_INT    0x02
#define OP_SET_UINT   0x03
#define OP_SET_FLOAT  0x04
#define OP_SET_STRING 0x05
#define OP_BEGIN_BLK  0x06
#define OP_END_BLK    0x07
#define OP_BEGIN_ARR  0x08
#define OP_END_ARR    0x09
#define OP_APP_NULL   0x0A
#define OP_APP_BOOL   0x0B
#define OP_APP_INT    0x0C
#define OP_APP_UINT   0x0D
#define OP_APP_FLOAT  0x0E
#define OP_APP_STRING 0x0F
#define OP_COUNT      0x10

#ifdef BC_FUZZ_LIBFUZZER
int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size == 0u) {
        return 0;
    }
    bc_allocators_context_config_t config;
    memset(&config, 0, sizeof(config));
    bc_allocators_context_t* memory = NULL;
    if (!bc_allocators_context_create(&config, &memory)) {
        return 0;
    }
    bc_hrbl_writer_t* writer = NULL;
    if (!bc_hrbl_writer_create(memory, &writer)) {
        bc_allocators_context_destroy(memory);
        return 0;
    }

    size_t cursor = 0u;
    size_t depth = 0u;
    size_t scope_is_block_bits = 0xFFFFFFFFFFFFFFFFul;
    size_t operations = 0u;
    while (cursor < size && operations < 256u) {
        uint8_t op = data[cursor++] % OP_COUNT;
        bool in_block_scope = depth == 0u || ((scope_is_block_bits >> (depth - 1u)) & 1u);

        if (!in_block_scope && (op >= OP_SET_NULL && op <= OP_BEGIN_ARR && op != OP_END_BLK && op != OP_END_ARR)) {
            op = (uint8_t)(OP_APP_NULL + (op - OP_SET_NULL) % (OP_APP_STRING - OP_APP_NULL + 1));
        }

        char key_buf[16];
        size_t key_length = 0u;
        if (in_block_scope) {
            if (cursor < size) {
                uint8_t klen = (uint8_t)(data[cursor++] % 8u + 1u);
                for (uint8_t i = 0u; i < klen && cursor < size; i += 1u) {
                    key_buf[i] = (char)('a' + (data[cursor++] % 26u));
                }
                key_length = (size_t)klen;
                if (cursor >= size) {
                    break;
                }
            }
        }

        switch (op) {
        case 0x00: bc_hrbl_writer_set_null(writer, key_buf, key_length); break;
        case 0x01: bc_hrbl_writer_set_bool(writer, key_buf, key_length, cursor < size ? data[cursor++] & 1u : false); break;
        case 0x02: {
            int64_t v = 0;
            if (cursor + 8u <= size) {
                memcpy(&v, data + cursor, 8u);
                cursor += 8u;
            }
            bc_hrbl_writer_set_int64(writer, key_buf, key_length, v);
            break;
        }
        case 0x03: {
            uint64_t v = 0u;
            if (cursor + 8u <= size) {
                memcpy(&v, data + cursor, 8u);
                cursor += 8u;
            }
            bc_hrbl_writer_set_uint64(writer, key_buf, key_length, v);
            break;
        }
        case 0x04: {
            double v = 0.0;
            if (cursor + 8u <= size) {
                memcpy(&v, data + cursor, 8u);
                cursor += 8u;
            }
            bc_hrbl_writer_set_float64(writer, key_buf, key_length, v);
            break;
        }
        case 0x05: {
            uint8_t vlen = cursor < size ? data[cursor++] % 8u : 0u;
            const char* vp = cursor < size ? (const char*)(data + cursor) : "";
            size_t avail = cursor <= size ? size - cursor : 0u;
            if (vlen > avail) {
                vlen = (uint8_t)avail;
            }
            bc_hrbl_writer_set_string(writer, key_buf, key_length, vp, vlen);
            cursor += vlen;
            break;
        }
        case 0x06:
            if (depth < 32u && bc_hrbl_writer_begin_block(writer, key_buf, key_length)) {
                scope_is_block_bits |= (size_t)1u << depth;
                depth += 1u;
            }
            break;
        case 0x07:
            if (depth > 0u && ((scope_is_block_bits >> (depth - 1u)) & 1u)) {
                bc_hrbl_writer_end_block(writer);
                depth -= 1u;
            }
            break;
        case 0x08:
            if (depth < 32u && bc_hrbl_writer_begin_array(writer, key_buf, key_length)) {
                scope_is_block_bits &= ~((size_t)1u << depth);
                depth += 1u;
            }
            break;
        case 0x09:
            if (depth > 0u && (((scope_is_block_bits >> (depth - 1u)) & 1u) == 0u)) {
                bc_hrbl_writer_end_array(writer);
                depth -= 1u;
            }
            break;
        case 0x0A: bc_hrbl_writer_append_null(writer); break;
        case 0x0B: bc_hrbl_writer_append_bool(writer, cursor < size ? data[cursor++] & 1u : false); break;
        case 0x0C: {
            int64_t v = 0;
            if (cursor + 8u <= size) {
                memcpy(&v, data + cursor, 8u);
                cursor += 8u;
            }
            bc_hrbl_writer_append_int64(writer, v);
            break;
        }
        case 0x0D: {
            uint64_t v = 0u;
            if (cursor + 8u <= size) {
                memcpy(&v, data + cursor, 8u);
                cursor += 8u;
            }
            bc_hrbl_writer_append_uint64(writer, v);
            break;
        }
        case 0x0E: {
            double v = 0.0;
            if (cursor + 8u <= size) {
                memcpy(&v, data + cursor, 8u);
                cursor += 8u;
            }
            bc_hrbl_writer_append_float64(writer, v);
            break;
        }
        case 0x0F: {
            uint8_t vlen = cursor < size ? data[cursor++] % 8u : 0u;
            const char* vp = cursor < size ? (const char*)(data + cursor) : "";
            size_t avail = cursor <= size ? size - cursor : 0u;
            if (vlen > avail) {
                vlen = (uint8_t)avail;
            }
            bc_hrbl_writer_append_string(writer, vp, vlen);
            cursor += vlen;
            break;
        }
        default:
            break;
        }
        operations += 1u;
    }

    while (depth > 0u) {
        if ((scope_is_block_bits >> (depth - 1u)) & 1u) {
            bc_hrbl_writer_end_block(writer);
        } else {
            bc_hrbl_writer_end_array(writer);
        }
        depth -= 1u;
    }

    void* buffer = NULL;
    size_t buffer_size = 0u;
    if (bc_hrbl_writer_finalize_to_buffer(writer, &buffer, &buffer_size)) {
        (void)bc_hrbl_verify_buffer(buffer, buffer_size);
        free(buffer);
    }
    bc_hrbl_writer_destroy(writer);
    bc_allocators_context_destroy(memory);
    return 0;
}
#else
int main(void)
{
    return 0;
}
#endif
