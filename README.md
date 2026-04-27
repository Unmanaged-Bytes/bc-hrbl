# bc-hrbl

[![CI](https://github.com/Unmanaged-Bytes/bc-hrbl/actions/workflows/ci.yml/badge.svg)](https://github.com/Unmanaged-Bytes/bc-hrbl/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
![Language: C11](https://img.shields.io/badge/language-C11-informational)
![Platform: Linux](https://img.shields.io/badge/platform-Linux-lightgrey)

> Hash-Routed Binary Layout — zero-copy mmap reader, writer, verify,
> one-way exports (JSON / YAML / INI) and a JSON bootstrap convert.
> Part of the `bc-*` ecosystem.

## Scope

Binary configuration / manifest format:

- Little-endian, 8-byte aligned, magic `HRBL`, extension `.hrbl`.
- Root index and block entries sorted strictly by `xxh3_64`;
  key uniqueness per scope.
- mmap read-only reader, `verify()` mandatory before any use.
- Typed scalars (null, bool, int64, uint64, float64, string) plus
  blocks (key → value maps) and arrays.
- Writer builds in memory (arena-backed) and finalises to an
  immutable `.hrbl`.
- Export API:
  - `bc_hrbl_export_json()` — pretty JSON (default indent=2).
  - `bc_hrbl_export_yaml()` — block-style YAML via libyaml.
  - `bc_hrbl_export_ini()` — flat + `[section]` dotted INI.
- Import: `bc_hrbl_convert_json_to_writer()` — JSON → .hrbl via
  json-c. The binary format remains the source of truth; no
  text → .hrbl inverse parser outside JSON.

## Quick start

```c
#include "bc_hrbl.h"

bc_allocators_context_t* memory = /* ... */;
bc_hrbl_reader_t* reader = NULL;
if (!bc_hrbl_verify_file("manifest.hrbl")) { /* abort */ }
if (!bc_hrbl_reader_open(memory, "manifest.hrbl", &reader)) { /* abort */ }

bc_hrbl_value_ref_t value;
if (bc_hrbl_reader_find(reader, "server.port", 11, &value)) {
    int64_t port = 0;
    bc_hrbl_reader_get_int64(&value, &port);
}
bc_hrbl_reader_destroy(reader);
```

## Path grammar

`bc_hrbl_reader_find` accepts a dotted path with optional array indices:

- `foo.bar.baz` — nested block lookup
- `items[3]` — array indexing (0-based, positive integers only)
- `'literal.key'` or `"literal.key"` — quoted segment for keys that
  contain `.`, `[` or other syntax characters. Single and double quotes
  are interchangeable (pick whichever doesn't appear in the key). No
  escape sequences; the segment ends at the next matching quote.

Example: `files.'src/main.c'.digest` looks up the key `src/main.c`
inside the `files` block.

## License

MIT — see [LICENSE](LICENSE).
