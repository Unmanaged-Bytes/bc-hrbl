cmocka_dep = dependency('cmocka')

test_bc_hrbl_smoke = executable('test_bc_hrbl_smoke',
    'test_bc_hrbl_smoke.c',
    dependencies : [cmocka_dep, bc_hrbl_dep],
)
test('bc_hrbl_smoke', test_bc_hrbl_smoke)

test_bc_hrbl_verify_status = executable('test_bc_hrbl_verify_status',
    'test_bc_hrbl_verify_status.c',
    dependencies : [cmocka_dep, bc_hrbl_dep],
)
test('bc_hrbl_verify_status', test_bc_hrbl_verify_status)
