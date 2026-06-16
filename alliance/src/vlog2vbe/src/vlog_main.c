#include "vlog_ast.h"
#include "vlog_elab.h"
#include "vlog_emit.h"
#include "vlog_parse.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VLOG2VBE_VERSION "0.1"

static void usage(FILE *out)
{
  fprintf(out, "Usage: vlog2vbe [-o output.vbe] [-top module] file.v [module]\n");
  fprintf(out, "       vlog2vbe -V\n");
}

static char *default_output_name(const char *module_name)
{
  char *name;
  unsigned int len;

  len = (unsigned int)strlen(module_name) + 5;
  name = (char *)malloc(len);
  if (name == NULL) {
    return NULL;
  }
  snprintf(name, len, "%s.vbe", module_name);
  return name;
}

int main(int argc, char **argv)
{
  const char *input_path;
  const char *output_path;
  const char *top_name;
  char *owned_output;
  char error[512];
  VlogDesign design;
  VlogModule module;
  int i;

  input_path = NULL;
  output_path = NULL;
  top_name = NULL;
  owned_output = NULL;
  error[0] = '\0';

  for (i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
      usage(stdout);
      return 0;
    } else if (strcmp(argv[i], "-V") == 0 || strcmp(argv[i], "--version") == 0) {
      printf("vlog2vbe %s\n", VLOG2VBE_VERSION);
      return 0;
    } else if (strcmp(argv[i], "-o") == 0) {
      if (++i >= argc) {
        usage(stderr);
        return 1;
      }
      output_path = argv[i];
    } else if (strcmp(argv[i], "-top") == 0 || strcmp(argv[i], "--top") == 0) {
      if (++i >= argc) {
        usage(stderr);
        return 1;
      }
      top_name = argv[i];
    } else if (argv[i][0] == '-') {
      fprintf(stderr, "vlog2vbe: unknown option '%s'\n", argv[i]);
      usage(stderr);
      return 1;
    } else if (input_path == NULL) {
      input_path = argv[i];
    } else if (top_name == NULL) {
      top_name = argv[i];
    } else {
      fprintf(stderr, "vlog2vbe: unexpected argument '%s'\n", argv[i]);
      usage(stderr);
      return 1;
    }
  }

  if (input_path == NULL) {
    usage(stderr);
    return 1;
  }

  vlog_design_init(&design);
  vlog_module_init(&module);
  if (!vlog_parse_design_file(input_path, &design, error, sizeof(error))) {
    fprintf(stderr, "vlog2vbe: parse error in %s: %s\n", input_path, error);
    vlog_design_free(&design);
    return 1;
  }

  if (!vlog_elaborate_design(&design, top_name, &module, error, sizeof(error))) {
    fprintf(stderr, "vlog2vbe: elaboration error: %s\n", error);
    vlog_design_free(&design);
    vlog_module_free(&module);
    return 1;
  }

  if (output_path == NULL) {
    owned_output = default_output_name(module.name);
    if (owned_output == NULL) {
      fprintf(stderr, "vlog2vbe: out of memory\n");
      vlog_design_free(&design);
      vlog_module_free(&module);
      return 2;
    }
    output_path = owned_output;
  }

  if (!vlog_emit_vbe_file(&module, output_path, error, sizeof(error))) {
    fprintf(stderr, "vlog2vbe: emit error: %s\n", error);
    free(owned_output);
    vlog_design_free(&design);
    vlog_module_free(&module);
    return 1;
  }

  printf("vlog2vbe: wrote %s\n", output_path);
  free(owned_output);
  vlog_design_free(&design);
  vlog_module_free(&module);
  return 0;
}
