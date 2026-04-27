// SPDX-License-Identifier: MIT

#ifndef BC_HRBL_EXPORT_H
#define BC_HRBL_EXPORT_H

#include "bc_hrbl_types.h"

#include <bc/bc_core_io.h>

#include <stdbool.h>
#include <stddef.h>

typedef struct bc_hrbl_export_options {
    unsigned int indent_spaces;
    bool sort_keys;
    bool ascii_only;
} bc_hrbl_export_options_t;

bool bc_hrbl_export_json(const bc_hrbl_reader_t* reader, bc_core_writer_t* writer);

bool bc_hrbl_export_json_ex(const bc_hrbl_reader_t* reader, bc_core_writer_t* writer, const bc_hrbl_export_options_t* options);

bool bc_hrbl_export_yaml(const bc_hrbl_reader_t* reader, bc_core_writer_t* writer);

bool bc_hrbl_export_yaml_ex(const bc_hrbl_reader_t* reader, bc_core_writer_t* writer, const bc_hrbl_export_options_t* options);

bool bc_hrbl_export_ini(const bc_hrbl_reader_t* reader, bc_core_writer_t* writer);

bool bc_hrbl_export_ini_ex(const bc_hrbl_reader_t* reader, bc_core_writer_t* writer, const bc_hrbl_export_options_t* options);

#endif
