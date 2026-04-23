# bc-hrbl

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

## Requirements

- Debian 13 (trixie) or any Linux distro with glibc ≥ 2.38
- `meson >= 1.0`, `ninja-build`, `pkg-config`
- `libbc-core-dev (>= 1.3.1)`, `libbc-allocators-dev (>= 1.2.0)`,
  `libbc-io-dev (>= 1.1.1)`, `libbc-concurrency-dev (>= 1.1.1)`
- `libxxhash-dev (>= 0.8.0)`
- `libjson-c-dev (>= 0.15)`, `libyaml-dev (>= 0.2.0)`
- `libcmocka-dev` (tests, optional for end users)

## Build

```
meson setup build --buildtype=release
meson compile -C build
```

Run the test suite:

```
meson setup build/debug --buildtype=debug -Dtests=true
meson test -C build/debug
```

## Install

```
sudo meson install -C build
pkg-config --cflags --libs bc-hrbl
```

The package installs:
- Headers under `/usr/local/include/bc/` (`bc_hrbl.h`, `bc_hrbl_types.h`,
  `bc_hrbl_reader.h`, `bc_hrbl_writer.h`, `bc_hrbl_verify.h`,
  `bc_hrbl_export.h`, `bc_hrbl_convert.h`)
- Static library at `/usr/local/lib/<multiarch>/libbc-hrbl.a`
- pkg-config descriptor at `/usr/local/lib/<multiarch>/pkgconfig/bc-hrbl.pc`

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

## License

MIT — see [LICENSE](LICENSE).
