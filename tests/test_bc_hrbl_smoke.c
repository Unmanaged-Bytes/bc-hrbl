// SPDX-License-Identifier: MIT

#include "bc_hrbl.h"

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

static void test_bc_hrbl_magic_constant(void** state)
{
    (void)state;
    assert_int_equal((int)BC_HRBL_MAGIC, (int)0x4C425248);
}

static void test_bc_hrbl_version_constants(void** state)
{
    (void)state;
    assert_int_equal((int)BC_HRBL_VERSION_MAJOR, 1);
    assert_int_equal((int)BC_HRBL_VERSION_MINOR, 0);
}

static void test_bc_hrbl_kind_enum_values(void** state)
{
    (void)state;
    assert_int_equal((int)BC_HRBL_KIND_NULL,    0x00);
    assert_int_equal((int)BC_HRBL_KIND_FALSE,   0x01);
    assert_int_equal((int)BC_HRBL_KIND_TRUE,    0x02);
    assert_int_equal((int)BC_HRBL_KIND_INT64,   0x03);
    assert_int_equal((int)BC_HRBL_KIND_UINT64,  0x04);
    assert_int_equal((int)BC_HRBL_KIND_FLOAT64, 0x05);
    assert_int_equal((int)BC_HRBL_KIND_STRING,  0x06);
    assert_int_equal((int)BC_HRBL_KIND_BLOCK,   0x10);
    assert_int_equal((int)BC_HRBL_KIND_ARRAY,   0x11);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_bc_hrbl_magic_constant),
        cmocka_unit_test(test_bc_hrbl_version_constants),
        cmocka_unit_test(test_bc_hrbl_kind_enum_values),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
