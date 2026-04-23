// SPDX-License-Identifier: MIT

#include "bc_hrbl.h"

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <string.h>
#include <cmocka.h>

static void test_bc_hrbl_verify_buffer_null_is_too_small(void** state)
{
    (void)state;
    assert_int_equal((int)bc_hrbl_verify_buffer(NULL, 0), (int)BC_HRBL_VERIFY_ERR_TOO_SMALL);
}

static void test_bc_hrbl_verify_buffer_truncated_is_too_small(void** state)
{
    (void)state;
    uint8_t buffer[16] = {0};
    assert_int_equal((int)bc_hrbl_verify_buffer(buffer, sizeof(buffer)), (int)BC_HRBL_VERIFY_ERR_TOO_SMALL);
}

static void test_bc_hrbl_verify_status_name_known(void** state)
{
    (void)state;
    assert_string_equal(bc_hrbl_verify_status_name(BC_HRBL_VERIFY_OK), "ok");
    assert_string_equal(bc_hrbl_verify_status_name(BC_HRBL_VERIFY_ERR_BAD_MAGIC), "bad_magic");
    assert_string_equal(bc_hrbl_verify_status_name(BC_HRBL_VERIFY_ERR_DUPLICATE_KEY), "duplicate_key");
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_bc_hrbl_verify_buffer_null_is_too_small),
        cmocka_unit_test(test_bc_hrbl_verify_buffer_truncated_is_too_small),
        cmocka_unit_test(test_bc_hrbl_verify_status_name_known),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
