#ifndef VLOG_ELAB_H
#define VLOG_ELAB_H

#include "vlog_ast.h"

int vlog_elaborate_design(const VlogDesign *design,
                          const char *top_name,
                          VlogModule *flat,
                          char *error,
                          unsigned int error_size);

#endif
