// SPDX-License-Identifier: MIT

#include "bc_hrbl.h"

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <string.h>
#include <cmocka.h>

#include <xxhash.h>

#define HRBL_HEADER_SIZE 128u
#define HRBL_FOOTER_SIZE 32u
#define HRBL_MIN_SIZE (HRBL_HEADER_SIZE + HRBL_FOOTER_SIZE)

typedef struct hrbl_empty_doc {
    uint8_t bytes[HRBL_MIN_SIZE];
} hrbl_empty_doc_t;

static void hrbl_write_u16(uint8_t* buffer, size_t offset, uint16_t value)
{
    memcpy(buffer + offset, &value, sizeof(value));
}

static void hrbl_write_u32(uint8_t* buffer, size_t offset, uint32_t value)
{
    memcpy(buffer + offset, &value, sizeof(value));
}

static void hrbl_write_u64(uint8_t* buffer, size_t offset, uint64_t value)
{
    memcpy(buffer + offset, &value, sizeof(value));
}

static void hrbl_make_empty_doc(hrbl_empty_doc_t* out)
{
    memset(out->bytes, 0, sizeof(out->bytes));
    uint8_t* b = out->bytes;
    hrbl_write_u32(b, 0u, 0x4C425248u);
    hrbl_write_u16(b, 4u, 1u);
    hrbl_write_u16(b, 6u, 0u);
    hrbl_write_u64(b, 8u, HRBL_MIN_SIZE);
    hrbl_write_u64(b, 16u, (uint64_t)(0x1u | 0x2u | 0x4u | 0x8u));
    hrbl_write_u64(b, 24u, 0u);
    hrbl_write_u64(b, 32u, 128u);
    hrbl_write_u64(b, 40u, 0u);
    hrbl_write_u64(b, 48u, 128u);
    hrbl_write_u64(b, 56u, 0u);
    hrbl_write_u64(b, 64u, 128u);
    hrbl_write_u64(b, 72u, 0u);
    hrbl_write_u64(b, 80u, 0u);
    hrbl_write_u64(b, 88u, 128u);

    uint64_t checksum = (uint64_t)XXH3_64bits(b + 128u, 0u);
    hrbl_write_u64(b, 96u, checksum);

    hrbl_write_u64(b, 128u, checksum);
    hrbl_write_u64(b, 136u, HRBL_MIN_SIZE);
    hrbl_write_u32(b, 144u, 0x4C425248u);
}

static void test_verify_accepts_empty_document(void** state)
{
    (void)state;
    hrbl_empty_doc_t doc;
    hrbl_make_empty_doc(&doc);
    assert_int_equal((int)bc_hrbl_verify_buffer(doc.bytes, sizeof(doc.bytes)), (int)BC_HRBL_VERIFY_OK);
}

static void test_verify_null_buffer_too_small(void** state)
{
    (void)state;
    assert_int_equal((int)bc_hrbl_verify_buffer(NULL, 0u), (int)BC_HRBL_VERIFY_ERR_TOO_SMALL);
}

static void test_verify_truncated_buffer_too_small(void** state)
{
    (void)state;
    uint8_t shorty[64];
    memset(shorty, 0, sizeof(shorty));
    assert_int_equal((int)bc_hrbl_verify_buffer(shorty, sizeof(shorty)), (int)BC_HRBL_VERIFY_ERR_TOO_SMALL);
}

static void test_verify_bad_magic(void** state)
{
    (void)state;
    hrbl_empty_doc_t doc;
    hrbl_make_empty_doc(&doc);
    hrbl_write_u32(doc.bytes, 0u, 0u);
    assert_int_equal((int)bc_hrbl_verify_buffer(doc.bytes, sizeof(doc.bytes)), (int)BC_HRBL_VERIFY_ERR_BAD_MAGIC);
}

static void test_verify_bad_version_major(void** state)
{
    (void)state;
    hrbl_empty_doc_t doc;
    hrbl_make_empty_doc(&doc);
    hrbl_write_u16(doc.bytes, 4u, 99u);
    assert_int_equal((int)bc_hrbl_verify_buffer(doc.bytes, sizeof(doc.bytes)), (int)BC_HRBL_VERIFY_ERR_BAD_VERSION);
}

static void test_verify_bad_version_minor_future(void** state)
{
    (void)state;
    hrbl_empty_doc_t doc;
    hrbl_make_empty_doc(&doc);
    hrbl_write_u16(doc.bytes, 6u, 99u);
    assert_int_equal((int)bc_hrbl_verify_buffer(doc.bytes, sizeof(doc.bytes)), (int)BC_HRBL_VERIFY_ERR_BAD_VERSION);
}

static void test_verify_bad_flags_missing_required(void** state)
{
    (void)state;
    hrbl_empty_doc_t doc;
    hrbl_make_empty_doc(&doc);
    hrbl_write_u64(doc.bytes, 16u, 0u);
    assert_int_equal((int)bc_hrbl_verify_buffer(doc.bytes, sizeof(doc.bytes)), (int)BC_HRBL_VERIFY_ERR_BAD_FLAGS);
}

static void test_verify_bad_flags_reserved_bit(void** state)
{
    (void)state;
    hrbl_empty_doc_t doc;
    hrbl_make_empty_doc(&doc);
    hrbl_write_u64(doc.bytes, 16u, (uint64_t)(0x1u | 0x2u | 0x4u | 0x8u | 0x10u));
    assert_int_equal((int)bc_hrbl_verify_buffer(doc.bytes, sizeof(doc.bytes)), (int)BC_HRBL_VERIFY_ERR_BAD_FLAGS);
}

static void test_verify_bad_file_size(void** state)
{
    (void)state;
    hrbl_empty_doc_t doc;
    hrbl_make_empty_doc(&doc);
    hrbl_write_u64(doc.bytes, 8u, (uint64_t)(HRBL_MIN_SIZE + 1u));
    assert_int_equal((int)bc_hrbl_verify_buffer(doc.bytes, sizeof(doc.bytes)), (int)BC_HRBL_VERIFY_ERR_BAD_FILE_SIZE);
}

static void test_verify_bad_footer_magic(void** state)
{
    (void)state;
    hrbl_empty_doc_t doc;
    hrbl_make_empty_doc(&doc);
    hrbl_write_u32(doc.bytes, 144u, 0u);
    assert_int_equal((int)bc_hrbl_verify_buffer(doc.bytes, sizeof(doc.bytes)), (int)BC_HRBL_VERIFY_ERR_BAD_FOOTER);
}

static void test_verify_footer_file_size_mismatch(void** state)
{
    (void)state;
    hrbl_empty_doc_t doc;
    hrbl_make_empty_doc(&doc);
    hrbl_write_u64(doc.bytes, 136u, (uint64_t)(HRBL_MIN_SIZE - 1u));
    assert_int_equal((int)bc_hrbl_verify_buffer(doc.bytes, sizeof(doc.bytes)), (int)BC_HRBL_VERIFY_ERR_BAD_FOOTER);
}

static void test_verify_footer_checksum_mismatch(void** state)
{
    (void)state;
    hrbl_empty_doc_t doc;
    hrbl_make_empty_doc(&doc);
    uint64_t original = 0u;
    memcpy(&original, doc.bytes + 128u, sizeof(original));
    hrbl_write_u64(doc.bytes, 128u, original ^ 0xFFu);
    assert_int_equal((int)bc_hrbl_verify_buffer(doc.bytes, sizeof(doc.bytes)), (int)BC_HRBL_VERIFY_ERR_BAD_FOOTER);
}

static void test_verify_header_reserved_byte_set(void** state)
{
    (void)state;
    hrbl_empty_doc_t doc;
    hrbl_make_empty_doc(&doc);
    doc.bytes[104u] = 1u;
    assert_int_equal((int)bc_hrbl_verify_buffer(doc.bytes, sizeof(doc.bytes)), (int)BC_HRBL_VERIFY_ERR_BAD_FLAGS);
}

static void test_verify_bad_root_index_offset(void** state)
{
    (void)state;
    hrbl_empty_doc_t doc;
    hrbl_make_empty_doc(&doc);
    hrbl_write_u64(doc.bytes, 32u, 200u);
    assert_int_equal((int)bc_hrbl_verify_buffer(doc.bytes, sizeof(doc.bytes)), (int)BC_HRBL_VERIFY_ERR_BAD_LAYOUT);
}

static void test_verify_root_count_overflow_rejected(void** state)
{
    (void)state;
    hrbl_empty_doc_t doc;
    hrbl_make_empty_doc(&doc);

    const uint64_t attacker_root_count = (uint64_t)1u << 61;
    hrbl_write_u64(doc.bytes, 24u, attacker_root_count);

    assert_int_equal((int)bc_hrbl_verify_buffer(doc.bytes, sizeof(doc.bytes)), (int)BC_HRBL_VERIFY_ERR_BAD_LAYOUT);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_verify_accepts_empty_document),
        cmocka_unit_test(test_verify_null_buffer_too_small),
        cmocka_unit_test(test_verify_truncated_buffer_too_small),
        cmocka_unit_test(test_verify_bad_magic),
        cmocka_unit_test(test_verify_bad_version_major),
        cmocka_unit_test(test_verify_bad_version_minor_future),
        cmocka_unit_test(test_verify_bad_flags_missing_required),
        cmocka_unit_test(test_verify_bad_flags_reserved_bit),
        cmocka_unit_test(test_verify_bad_file_size),
        cmocka_unit_test(test_verify_bad_footer_magic),
        cmocka_unit_test(test_verify_footer_file_size_mismatch),
        cmocka_unit_test(test_verify_footer_checksum_mismatch),
        cmocka_unit_test(test_verify_header_reserved_byte_set),
        cmocka_unit_test(test_verify_bad_root_index_offset),
        cmocka_unit_test(test_verify_root_count_overflow_rejected),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
