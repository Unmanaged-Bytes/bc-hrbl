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
- `bc_hrbl_writer_*` — arena-backed append-only builder with
  `begin_block` / `end_block` / `begin_array` / `end_array` scopes.
  Serialisation sorts block entries by xxh3_64, de-duplicates strings
  via an open-addressing pool, patches pool offsets in a single DFS
  pass, writes a self-contained 128 B header + 32 B footer.
- `bc_hrbl_verify_{buffer,file}` — integrity check (magic, version,
  header/footer consistency, root index ordering, UTF-8, checksums).
  Returns a precise `bc_hrbl_verify_status_t` enum with stable names.
- `bc_hrbl_export_json` / `_ex` — one-way pretty JSON export.
- `bc_hrbl_export_yaml` / `_ex` — block-style YAML via libyaml.
- `bc_hrbl_export_ini` / `_ex` — `[section]` + `key = value` / array
  `key[]=v` entries with string-escape support.
- `bc_hrbl_convert_json_to_writer` / `_buffer_to_hrbl` — bootstrap
  `.hrbl` from JSON via json-c.

### Performance (Ryzen 7 5700G, 100k-entry flat block)

- query latency median 2-segment path : 12.8 ns (target < 100 ns).
- reader sequential scan : 4.34 GB/s (target > 5 GB/s).
- writer finalise : 196 MB/s (bootstrap path).
- JSON → .hrbl : 22.5 MB/s (json-c tree materialisation cost).
- .hrbl → JSON : 55 MB/s.

### Quality

- 8 cmocka test binaries, 27 individual test cases, covering verify
  edge-cases, reader round-trip, JSON/YAML/INI export, JSON convert.
- Sanitizers (asan / tsan / ubsan / memcheck) clean.
- cppcheck clean with narrow inline suppressions on qsort_r callbacks
  and the libyaml write-handler signature.
- Fuzz harnesses on `bc_hrbl_verify_buffer` (30 min, 2.8 B exec,
  0 crash) and JSON convert (30 min, 10.6 M exec, 0 crash). Corpora
  checked in under `fuzzing/corpus/`.

[1.0.0]: https://github.com/Unmanaged-Bytes/bc-hrbl-lib/releases/tag/v1.0.0
