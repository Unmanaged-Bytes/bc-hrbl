// SPDX-License-Identifier: MIT

#include "bc_hrbl_verify.h"
#include "bc_hrbl_format_internal.h"

#include "bc_allocators.h"
#include "bc_core.h"
#include "bc_core_memory.h"
#include "bc_io_mmap.h"

#include <string.h>

#include <xxhash.h>

static bc_hrbl_verify_status_t bc_hrbl_verify_utf8_length(const uint8_t* data, size_t length)
{
    size_t index = 0u;
    while (index < length) {
        uint8_t byte = data[index];
        if (byte < 0x80u) {
            index += 1u;
            continue;
        }
        size_t sequence_length;
        uint32_t codepoint;
        if ((byte & 0xE0u) == 0xC0u) {
            sequence_length = 2u;
            codepoint = (uint32_t)(byte & 0x1Fu);
            if (codepoint < 2u) {
                return BC_HRBL_VERIFY_ERR_BAD_UTF8;
            }
        } else if ((byte & 0xF0u) == 0xE0u) {
            sequence_length = 3u;
            codepoint = (uint32_t)(byte & 0x0Fu);
        } else if ((byte & 0xF8u) == 0xF0u) {
            sequence_length = 4u;
            codepoint = (uint32_t)(byte & 0x07u);
        } else {
            return BC_HRBL_VERIFY_ERR_BAD_UTF8;
        }
        if (index + sequence_length > length) {
            return BC_HRBL_VERIFY_ERR_BAD_UTF8;
        }
        for (size_t k = 1u; k < sequence_length; k += 1u) {
            uint8_t continuation = data[index + k];
            if ((continuation & 0xC0u) != 0x80u) {
                return BC_HRBL_VERIFY_ERR_BAD_UTF8;
            }
            codepoint = (codepoint << 6) | (uint32_t)(continuation & 0x3Fu);
        }
        if (sequence_length == 3u && codepoint < 0x800u) {
            return BC_HRBL_VERIFY_ERR_BAD_UTF8;
        }
        if (sequence_length == 4u && (codepoint < 0x10000u || codepoint > 0x10FFFFu)) {
            return BC_HRBL_VERIFY_ERR_BAD_UTF8;
        }
        if (codepoint >= 0xD800u && codepoint <= 0xDFFFu) {
            return BC_HRBL_VERIFY_ERR_BAD_UTF8;
        }
        index += sequence_length;
    }
    return BC_HRBL_VERIFY_OK;
}

static bc_hrbl_verify_status_t bc_hrbl_verify_header_layout(const bc_hrbl_header_t* header, size_t size)
{
    if (header->magic != BC_HRBL_MAGIC) {
        return BC_HRBL_VERIFY_ERR_BAD_MAGIC;
    }
    if (header->version_major != BC_HRBL_VERSION_MAJOR) {
        return BC_HRBL_VERIFY_ERR_BAD_VERSION;
    }
    if (header->version_minor > BC_HRBL_VERSION_MINOR) {
        return BC_HRBL_VERIFY_ERR_BAD_VERSION;
    }
    if ((header->flags & BC_HRBL_FLAGS_V1_REQUIRED) != BC_HRBL_FLAGS_V1_REQUIRED) {
        return BC_HRBL_VERIFY_ERR_BAD_FLAGS;
    }
    if ((header->flags & BC_HRBL_FLAGS_V1_RESERVED_MASK) != 0u) {
        return BC_HRBL_VERIFY_ERR_BAD_FLAGS;
    }
    if (header->file_size != (uint64_t)size) {
        return BC_HRBL_VERIFY_ERR_BAD_FILE_SIZE;
    }
    for (size_t i = 0u; i < sizeof(header->reserved); i += 1u) {
        if (header->reserved[i] != 0u) {
            return BC_HRBL_VERIFY_ERR_BAD_FLAGS;
        }
    }
    if (header->root_index_offset != BC_HRBL_ROOT_INDEX_OFFSET) {
        return BC_HRBL_VERIFY_ERR_BAD_LAYOUT;
    }
    if (header->root_count > BC_HRBL_ROOT_COUNT_MAX) {
        return BC_HRBL_VERIFY_ERR_BAD_LAYOUT;
    }
    if (header->root_index_size != header->root_count * BC_HRBL_ROOT_ENTRY_SIZE) {
        return BC_HRBL_VERIFY_ERR_BAD_LAYOUT;
    }
    if (header->nodes_offset != header->root_index_offset + header->root_index_size) {
        return BC_HRBL_VERIFY_ERR_BAD_LAYOUT;
    }
    if (header->strings_offset != header->nodes_offset + header->nodes_size) {
        return BC_HRBL_VERIFY_ERR_BAD_LAYOUT;
    }
    if (header->footer_offset != header->strings_offset + header->strings_size) {
        return BC_HRBL_VERIFY_ERR_BAD_LAYOUT;
    }
    if (header->footer_offset + BC_HRBL_FOOTER_SIZE != header->file_size) {
        return BC_HRBL_VERIFY_ERR_BAD_LAYOUT;
    }
    return BC_HRBL_VERIFY_OK;
}

static bc_hrbl_verify_status_t bc_hrbl_verify_strings_pool(const uint8_t* data, const bc_hrbl_header_t* header)
{
    if (header->strings_size == 0u) {
        if (header->strings_count != 0u) {
            return BC_HRBL_VERIFY_ERR_BAD_STRING;
        }
        return BC_HRBL_VERIFY_OK;
    }
    uint64_t offset = 0u;
    uint64_t counted = 0u;
    const uint8_t* pool = data + header->strings_offset;
    while (offset < header->strings_size) {
        if (offset + sizeof(uint32_t) > header->strings_size) {
            return BC_HRBL_VERIFY_ERR_BAD_STRING;
        }
        uint32_t length = 0u;
        bc_hrbl_load_u32(&length, pool + offset);
        uint64_t total = (uint64_t)sizeof(uint32_t) + (uint64_t)length;
        if (offset + total > header->strings_size) {
            return BC_HRBL_VERIFY_ERR_BAD_STRING;
        }
        bc_hrbl_verify_status_t utf8_status = bc_hrbl_verify_utf8_length(pool + offset + sizeof(uint32_t), (size_t)length);
        if (utf8_status != BC_HRBL_VERIFY_OK) {
            return utf8_status;
        }
        uint64_t aligned_total = (uint64_t)bc_hrbl_align_up((size_t)total, 4u);
        if (aligned_total > header->strings_size - offset) {
            return BC_HRBL_VERIFY_ERR_BAD_STRING;
        }
        for (uint64_t pad = total; pad < aligned_total; pad += 1u) {
            if (pool[offset + pad] != 0u) {
                return BC_HRBL_VERIFY_ERR_BAD_STRING;
            }
        }
        offset += aligned_total;
        counted += 1u;
    }
    if (offset != header->strings_size) {
        return BC_HRBL_VERIFY_ERR_BAD_STRING;
    }
    if (counted != header->strings_count) {
        return BC_HRBL_VERIFY_ERR_BAD_STRING;
    }
    return BC_HRBL_VERIFY_OK;
}

static bc_hrbl_verify_status_t bc_hrbl_verify_root_index(const uint8_t* data, const bc_hrbl_header_t* header)
{
    uint64_t previous_hash = 0u;
    for (uint64_t i = 0u; i < header->root_count; i += 1u) {
        bc_hrbl_entry_t entry;
        bc_hrbl_load_entry(&entry, data + header->root_index_offset + i * BC_HRBL_ROOT_ENTRY_SIZE);
        if (i != 0u) {
            if (entry.key_hash64 < previous_hash) {
                return BC_HRBL_VERIFY_ERR_BAD_ROOT_INDEX;
            }
            if (entry.key_hash64 == previous_hash) {
                return BC_HRBL_VERIFY_ERR_DUPLICATE_KEY;
            }
        }
        previous_hash = entry.key_hash64;
        if (entry.value_offset < header->nodes_offset || entry.value_offset >= header->strings_offset) {
            return BC_HRBL_VERIFY_ERR_BAD_ROOT_INDEX;
        }
        if ((uint64_t)entry.key_pool_offset < header->strings_offset ||
            (uint64_t)entry.key_pool_offset + sizeof(uint32_t) + entry.key_length > header->strings_offset + header->strings_size) {
            return BC_HRBL_VERIFY_ERR_BAD_ROOT_INDEX;
        }
    }
    return BC_HRBL_VERIFY_OK;
}

bc_hrbl_verify_status_t bc_hrbl_verify_buffer(const void* data, size_t size)
{
    if (data == NULL) {
        return BC_HRBL_VERIFY_ERR_TOO_SMALL;
    }
    if (size < BC_HRBL_HEADER_SIZE + BC_HRBL_FOOTER_SIZE) {
        return BC_HRBL_VERIFY_ERR_TOO_SMALL;
    }

    bc_hrbl_header_t header;
    bc_hrbl_load_header(&header, data);

    bc_hrbl_verify_status_t header_status = bc_hrbl_verify_header_layout(&header, size);
    if (header_status != BC_HRBL_VERIFY_OK) {
        return header_status;
    }

    bc_hrbl_footer_t footer;
    bc_hrbl_load_footer(&footer, (const uint8_t*)data + header.footer_offset);
    if (footer.magic_end != BC_HRBL_MAGIC) {
        return BC_HRBL_VERIFY_ERR_BAD_FOOTER;
    }
    if (footer.file_size != header.file_size) {
        return BC_HRBL_VERIFY_ERR_BAD_FOOTER;
    }
    if (footer.checksum_xxh3_64 != header.checksum_xxh3_64) {
        return BC_HRBL_VERIFY_ERR_BAD_FOOTER;
    }

    size_t payload_length = (size_t)(header.footer_offset - header.root_index_offset);
    uint64_t checksum = (uint64_t)XXH3_64bits((const uint8_t*)data + header.root_index_offset, payload_length);
    if (checksum != header.checksum_xxh3_64) {
        return BC_HRBL_VERIFY_ERR_BAD_CHECKSUM;
    }

    bc_hrbl_verify_status_t strings_status = bc_hrbl_verify_strings_pool((const uint8_t*)data, &header);
    if (strings_status != BC_HRBL_VERIFY_OK) {
        return strings_status;
    }

    return bc_hrbl_verify_root_index((const uint8_t*)data, &header);
}

bc_hrbl_verify_status_t bc_hrbl_verify_file(const char* path)
{
    if (path == NULL) {
        return BC_HRBL_VERIFY_ERR_IO;
    }

    bc_allocators_context_config_t memory_config;
    bc_core_zero(&memory_config, sizeof(memory_config));
    memory_config.max_pool_memory = 0u;
    memory_config.tracking_enabled = false;
    bc_allocators_context_t* memory_context = NULL;
    if (!bc_allocators_context_create(&memory_config, &memory_context)) {
        return BC_HRBL_VERIFY_ERR_IO;
    }

    bc_io_mmap_options_t options;
    bc_core_zero(&options, sizeof(options));
    options.read_only = true;
    options.madvise_hint = BC_IO_MADVISE_SEQUENTIAL;

    bc_io_mmap_t* handle = NULL;
    if (!bc_io_mmap_file(memory_context, path, &options, &handle)) {
        bc_allocators_context_destroy(memory_context);
        return BC_HRBL_VERIFY_ERR_IO;
    }

    const void* data = NULL;
    size_t size = 0u;
    if (!bc_io_mmap_get_data(handle, &data, &size)) {
        bc_io_mmap_destroy(handle);
        bc_allocators_context_destroy(memory_context);
        return BC_HRBL_VERIFY_ERR_IO;
    }

    bc_hrbl_verify_status_t status = bc_hrbl_verify_buffer(data, size);
    bc_io_mmap_destroy(handle);
    bc_allocators_context_destroy(memory_context);
    return status;
}

const char* bc_hrbl_verify_status_name(bc_hrbl_verify_status_t status)
{
    switch (status) {
    case BC_HRBL_VERIFY_OK:
        return "ok";
    case BC_HRBL_VERIFY_ERR_TOO_SMALL:
        return "too_small";
    case BC_HRBL_VERIFY_ERR_BAD_MAGIC:
        return "bad_magic";
    case BC_HRBL_VERIFY_ERR_BAD_VERSION:
        return "bad_version";
    case BC_HRBL_VERIFY_ERR_BAD_FLAGS:
        return "bad_flags";
    case BC_HRBL_VERIFY_ERR_BAD_FILE_SIZE:
        return "bad_file_size";
    case BC_HRBL_VERIFY_ERR_BAD_FOOTER:
        return "bad_footer";
    case BC_HRBL_VERIFY_ERR_BAD_CHECKSUM:
        return "bad_checksum";
    case BC_HRBL_VERIFY_ERR_BAD_LAYOUT:
        return "bad_layout";
    case BC_HRBL_VERIFY_ERR_BAD_ROOT_INDEX:
        return "bad_root_index";
    case BC_HRBL_VERIFY_ERR_BAD_NODE:
        return "bad_node";
    case BC_HRBL_VERIFY_ERR_BAD_STRING:
        return "bad_string";
    case BC_HRBL_VERIFY_ERR_BAD_UTF8:
        return "bad_utf8";
    case BC_HRBL_VERIFY_ERR_DUPLICATE_KEY:
        return "duplicate_key";
    case BC_HRBL_VERIFY_ERR_IO:
        return "io_error";
    }
    return "unknown";
}
