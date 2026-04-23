bc_hrbl_sources = [
    'reader/bc_hrbl_reader.c',
    'writer/bc_hrbl_writer.c',
    'writer/bc_hrbl_writer_encoder.c',
    'verify/bc_hrbl_verify.c',
    'export/bc_hrbl_export.c',
    'export/bc_hrbl_export_yaml.c',
    'export/bc_hrbl_export_ini.c',
    'convert/bc_hrbl_convert_json.c',
]

bc_hrbl_lib = static_library('bc-hrbl',
    bc_hrbl_sources,
    include_directories : [bc_hrbl_public_inc, bc_hrbl_internal_inc],
    dependencies : [
        bc_core_dep,
        bc_allocators_dep,
        bc_io_dep,
        bc_concurrency_dep,
        xxhash_dep,
        json_c_dep,
        yaml_dep,
        thread_dep,
    ],
    c_args : lto_c_args,
    link_args : lto_link_args,
    install : true,
)

bc_hrbl_dep = declare_dependency(
    link_with : bc_hrbl_lib,
    include_directories : bc_hrbl_public_inc,
    dependencies : [
        bc_core_dep,
        bc_allocators_dep,
        bc_io_dep,
        bc_concurrency_dep,
        xxhash_dep,
        json_c_dep,
        yaml_dep,
        thread_dep,
    ],
)
