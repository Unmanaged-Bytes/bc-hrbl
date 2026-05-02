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

test_bc_hrbl_writer_errors = executable('test_bc_hrbl_writer_errors',
    'test_bc_hrbl_writer_errors.c',
    dependencies : [cmocka_dep, bc_hrbl_dep],
)
test('bc_hrbl_writer_errors', test_bc_hrbl_writer_errors)

test_bc_hrbl_writer_validation = executable('test_bc_hrbl_writer_validation',
    'test_bc_hrbl_writer_validation.c',
    dependencies : [cmocka_dep, bc_hrbl_dep],
)
test('bc_hrbl_writer_validation', test_bc_hrbl_writer_validation)

test_bc_hrbl_writer_kinds = executable('test_bc_hrbl_writer_kinds',
    'test_bc_hrbl_writer_kinds.c',
    dependencies : [cmocka_dep, bc_hrbl_dep],
)
test('bc_hrbl_writer_kinds', test_bc_hrbl_writer_kinds)

test_bc_hrbl_writer_encoding_paths = executable('test_bc_hrbl_writer_encoding_paths',
    'test_bc_hrbl_writer_encoding_paths.c',
    dependencies : [cmocka_dep, bc_hrbl_dep],
)
test('bc_hrbl_writer_encoding_paths', test_bc_hrbl_writer_encoding_paths)

test_bc_hrbl_writer_finalize = executable('test_bc_hrbl_writer_finalize',
    'test_bc_hrbl_writer_finalize.c',
    dependencies : [cmocka_dep, bc_hrbl_dep],
)
test('bc_hrbl_writer_finalize', test_bc_hrbl_writer_finalize)

test_bc_hrbl_writer_oom = executable('test_bc_hrbl_writer_oom',
    'test_bc_hrbl_writer_oom.c',
    dependencies : [cmocka_dep, bc_hrbl_dep],
    link_args : [
        '-Wl,--wrap=bc_allocators_pool_allocate',
        '-Wl,--wrap=bc_allocators_pool_reallocate',
        '-Wl,--wrap=bc_allocators_arena_allocate',
    ],
)
test('bc_hrbl_writer_oom', test_bc_hrbl_writer_oom)
