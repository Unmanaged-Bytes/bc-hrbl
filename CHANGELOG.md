# Changelog

All notable changes to bc-hrbl are documented here.

## [1.0.0]

Initial public release of the Hash-Routed Binary Layout library,
rebased from the binary format originally shipped in the experimental
bc-hrtl tool.

### Format

- Little-endian, 8-byte aligned binary layout.
- Magic `HRBL` (0x4C425248), extension `.hrbl`.
- 128-byte header with xxh3_64 checksum, 32-byte footer with duplicate
  checksum + file size for truncation detection.
- Root index and block entries sorted by `xxh3_64`, unique keys per scope.
- Typed scalars: null, false, true, int64, uint64, float64, string
  (length-prefixed, null bytes allowed, UTF-8 enforced). Containers:
  block (key → value map), array (ordered values).
- SemVer: `version_major` bumps break compatibility; readers reject
  unknown majors.

### Library

- `bc_hrbl_reader_*` — zero-copy mmap reader, path-style lookup
  (`a.b[0].c`), typed getters, block/array iterators.
- `bc_hrbl_writer_*` — flat append-only builder with `begin_block` /
  `end_block` / `begin_array` / `end_array` scopes; parallel finalize
  (configurable worker count).
- `bc_hrbl_verify_{buffer,file}` — integrity check (magic, version,
  header/footer consistency, root index ordering, UTF-8, checksums).
- `bc_hrbl_export_json` — one-way pretty JSON export.

### Quality

- Sanitizers (asan / tsan / ubsan / memcheck) clean.
- cppcheck clean.
- Fuzz harnesses on `verify`, reader query, writer finalise, JSON convert.

[1.0.0]: https://github.com/Unmanaged-Bytes/bc-hrbl/releases/tag/v1.0.0
