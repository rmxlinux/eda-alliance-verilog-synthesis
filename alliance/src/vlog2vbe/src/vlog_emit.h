#ifndef VLOG_EMIT_H
#define VLOG_EMIT_H

#include "vlog_ast.h"

int vlog_emit_vbe_file(const VlogModule *module,
                       const char *path,
                       char *error,
                       unsigned int error_size);

#endif
