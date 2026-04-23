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

test_bc_hrbl_verify_edge_cases = executable('test_bc_hrbl_verify_edge_cases',
    'test_bc_hrbl_verify_edge_cases.c',
    dependencies : [cmocka_dep, bc_hrbl_dep, xxhash_dep],
)
test('bc_hrbl_verify_edge_cases', test_bc_hrbl_verify_edge_cases)

test_bc_hrbl_reader_basic = executable('test_bc_hrbl_reader_basic',
    'test_bc_hrbl_reader_basic.c',
    dependencies : [cmocka_dep, bc_hrbl_dep, xxhash_dep],
)
test('bc_hrbl_reader_basic', test_bc_hrbl_reader_basic)

test_bc_hrbl_export_json = executable('test_bc_hrbl_export_json',
    'test_bc_hrbl_export_json.c',
    dependencies : [cmocka_dep, bc_hrbl_dep, xxhash_dep],
)
test('bc_hrbl_export_json', test_bc_hrbl_export_json)

test_bc_hrbl_writer_roundtrip = executable('test_bc_hrbl_writer_roundtrip',
    'test_bc_hrbl_writer_roundtrip.c',
    dependencies : [cmocka_dep, bc_hrbl_dep],
)
test('bc_hrbl_writer_roundtrip', test_bc_hrbl_writer_roundtrip)

test_bc_hrbl_convert_json = executable('test_bc_hrbl_convert_json',
    'test_bc_hrbl_convert_json.c',
    dependencies : [cmocka_dep, bc_hrbl_dep],
)
test('bc_hrbl_convert_json', test_bc_hrbl_convert_json)

test_bc_hrbl_export_yaml_ini = executable('test_bc_hrbl_export_yaml_ini',
    'test_bc_hrbl_export_yaml_ini.c',
    dependencies : [cmocka_dep, bc_hrbl_dep],
)
test('bc_hrbl_export_yaml_ini', test_bc_hrbl_export_yaml_ini)
