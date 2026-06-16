#ifndef VLOG_PARSE_H
#define VLOG_PARSE_H

#include "vlog_ast.h"

int vlog_parse_file(const char *path,
                    VlogModule *module,
                    char *error,
                    unsigned int error_size);

int vlog_parse_design_file(const char *path,
                           VlogDesign *design,
                           char *error,
                           unsigned int error_size);

#endif
