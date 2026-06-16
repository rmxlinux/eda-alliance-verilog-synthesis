#include "vlog_emit.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct StrBuf {
  char *data;
  unsigned int len;
  unsigned int cap;
} StrBuf;

static void sb_init(StrBuf *sb)
{
  sb->data = NULL;
  sb->len = 0;
  sb->cap = 0;
}

static void sb_free(StrBuf *sb)
{
  free(sb->data);
  sb->data = NULL;
  sb->len = 0;
  sb->cap = 0;
}

static int sb_reserve(StrBuf *sb, unsigned int extra)
{
  unsigned int need;
  unsigned int cap;
  char *next;

  need = sb->len + extra + 1;
  if (need <= sb->cap) {
    return 1;
  }

  cap = sb->cap == 0 ? 64 : sb->cap;
  while (cap < need) {
    cap *= 2;
  }

  next = (char *)realloc(sb->data, cap);
  if (next == NULL) {
    return 0;
  }
  sb->data = next;
  sb->cap = cap;
  return 1;
}

static int sb_append(StrBuf *sb, const char *text)
{
  unsigned int length;

  length = (unsigned int)strlen(text);
  if (!sb_reserve(sb, length)) {
    return 0;
  }
  memcpy(sb->data + sb->len, text, length + 1);
  sb->len += length;
  return 1;
}

static int sb_appendf(StrBuf *sb, const char *fmt, ...)
{
  va_list args;
  char small[128];
  int needed;

  va_start(args, fmt);
  needed = vsnprintf(small, sizeof(small), fmt, args);
  va_end(args);

  if (needed < 0) {
    return 0;
  }
  if ((unsigned int)needed < sizeof(small)) {
    return sb_append(sb, small);
  }

  if (!sb_reserve(sb, (unsigned int)needed)) {
    return 0;
  }
  va_start(args, fmt);
  vsnprintf(sb->data + sb->len, sb->cap - sb->len, fmt, args);
  va_end(args);
  sb->len += (unsigned int)needed;
  return 1;
}

static char *sb_take(StrBuf *sb)
{
  char *data;

  if (sb->data == NULL) {
    return vlog_strdup("");
  }
  data = sb->data;
  sb->data = NULL;
  sb->len = 0;
  sb->cap = 0;
  return data;
}

static void set_error(char *error, unsigned int error_size, const char *fmt, ...)
{
  va_list args;

  if (error_size == 0) {
    return;
  }
  va_start(args, fmt);
  vsnprintf(error, error_size, fmt, args);
  va_end(args);
}

static const VlogSignal *find_signal_const(const VlogModule *module, const char *name)
{
  const VlogSignal *signal;

  for (signal = module->signals; signal != NULL; signal = signal->next) {
    if (strcmp(signal->name, name) == 0) {
      return signal;
    }
  }
  return NULL;
}

static const char *dir_to_vbe(VlogDir dir)
{
  if (dir == VLOG_DIR_INPUT) return "in";
  if (dir == VLOG_DIR_OUTPUT) return "out";
  if (dir == VLOG_DIR_INOUT) return "inout";
  return "in";
}

static int emit_type(FILE *out, VlogRange range)
{
  if (range.has_range) {
    return fprintf(out, "BIT_VECTOR(%d downto %d)", range.msb, range.lsb) > 0;
  }
  return fprintf(out, "BIT") > 0;
}

static int hex_value(char ch, int *value)
{
  if (ch >= '0' && ch <= '9') {
    *value = ch - '0';
    return 1;
  }
  if (ch >= 'a' && ch <= 'f') {
    *value = ch - 'a' + 10;
    return 1;
  }
  if (ch >= 'A' && ch <= 'F') {
    *value = ch - 'A' + 10;
    return 1;
  }
  return 0;
}

static int append_binary_digit(StrBuf *bits, char ch)
{
  if (ch == '_') {
    return 1;
  }
  if (ch == '0' || ch == '1') {
    char text[2];
    text[0] = ch;
    text[1] = '\0';
    return sb_append(bits, text);
  }
  return 0;
}

static int append_hex_digit(StrBuf *bits, char ch)
{
  int value;
  char temp[5];

  if (ch == '_') {
    return 1;
  }
  if (!hex_value(ch, &value)) {
    return 0;
  }
  temp[0] = (value & 8) ? '1' : '0';
  temp[1] = (value & 4) ? '1' : '0';
  temp[2] = (value & 2) ? '1' : '0';
  temp[3] = (value & 1) ? '1' : '0';
  temp[4] = '\0';
  return sb_append(bits, temp);
}

static int append_octal_digit(StrBuf *bits, char ch)
{
  int value;
  char temp[4];

  if (ch == '_') {
    return 1;
  }
  if (ch < '0' || ch > '7') {
    return 0;
  }
  value = ch - '0';
  temp[0] = (value & 4) ? '1' : '0';
  temp[1] = (value & 2) ? '1' : '0';
  temp[2] = (value & 1) ? '1' : '0';
  temp[3] = '\0';
  return sb_append(bits, temp);
}

static char *ulong_to_bits(unsigned long value, int width)
{
  StrBuf sb;
  int bit;
  int started;

  sb_init(&sb);
  if (width > 0) {
    for (bit = width - 1; bit >= 0; bit--) {
      sb_append(&sb, (value & (1UL << bit)) ? "1" : "0");
    }
    return sb_take(&sb);
  }

  started = 0;
  for (bit = (int)(sizeof(unsigned long) * 8) - 1; bit >= 0; bit--) {
    if (value & (1UL << bit)) {
      started = 1;
    }
    if (started) {
      sb_append(&sb, (value & (1UL << bit)) ? "1" : "0");
    }
  }
  if (!started) {
    sb_append(&sb, "0");
  }
  return sb_take(&sb);
}

static char *fit_width(char *bits, int width)
{
  unsigned int len;
  StrBuf sb;

  if (width <= 0) {
    return bits;
  }

  len = (unsigned int)strlen(bits);
  if (len == (unsigned int)width) {
    return bits;
  }
  if (len > (unsigned int)width) {
    char *trimmed;
    trimmed = vlog_strdup(bits + len - width);
    free(bits);
    return trimmed;
  }

  sb_init(&sb);
  while (sb.len + len < (unsigned int)width) {
    sb_append(&sb, "0");
  }
  sb_append(&sb, bits);
  free(bits);
  return sb_take(&sb);
}

static char *normalize_number(const char *text, char *error, unsigned int error_size)
{
  const char *quote;
  int width;
  char base;
  const char *digits;
  StrBuf bits;
  char *endptr;
  unsigned long value;

  quote = strchr(text, '\'');
  if (quote == NULL) {
    value = strtoul(text, &endptr, 10);
    if (*endptr != '\0') {
      set_error(error, error_size, "unsupported numeric constant '%s'", text);
      return NULL;
    }
    return ulong_to_bits(value, 0);
  }

  width = 0;
  if (quote != text) {
    char *width_text;
    width_text = vlog_strndup(text, (unsigned int)(quote - text));
    width = (int)strtol(width_text, &endptr, 10);
    if (*endptr != '\0' || width < 0) {
      free(width_text);
      set_error(error, error_size, "invalid constant width in '%s'", text);
      return NULL;
    }
    free(width_text);
  }

  digits = quote + 1;
  if (*digits == 's' || *digits == 'S') {
    digits++;
  }
  base = (char)tolower((unsigned char)*digits);
  if (base == '\0') {
    set_error(error, error_size, "missing base in constant '%s'", text);
    return NULL;
  }
  digits++;

  if (base == 'd') {
    char *clean;
    unsigned int i;
    unsigned int j;

    clean = (char *)malloc(strlen(digits) + 1);
    if (clean == NULL) return NULL;
    for (i = 0, j = 0; digits[i] != '\0'; i++) {
      if (digits[i] != '_') {
        clean[j++] = digits[i];
      }
    }
    clean[j] = '\0';
    value = strtoul(clean, &endptr, 10);
    if (*endptr != '\0') {
      free(clean);
      set_error(error, error_size, "unsupported decimal constant '%s'", text);
      return NULL;
    }
    free(clean);
    return ulong_to_bits(value, width);
  }

  sb_init(&bits);
  while (*digits != '\0') {
    char ch;

    ch = *digits++;
    if (ch == 'x' || ch == 'X' || ch == 'z' || ch == 'Z' || ch == '?') {
      sb_free(&bits);
      set_error(error,
                error_size,
                "constant '%s' contains x/z; first version only supports 0/1",
                text);
      return NULL;
    }
    if (base == 'b') {
      if (!append_binary_digit(&bits, ch)) {
        sb_free(&bits);
        set_error(error, error_size, "invalid binary constant '%s'", text);
        return NULL;
      }
    } else if (base == 'h') {
      if (!append_hex_digit(&bits, ch)) {
        sb_free(&bits);
        set_error(error, error_size, "invalid hex constant '%s'", text);
        return NULL;
      }
    } else if (base == 'o') {
      if (!append_octal_digit(&bits, ch)) {
        sb_free(&bits);
        set_error(error, error_size, "invalid octal constant '%s'", text);
        return NULL;
      }
    } else {
      sb_free(&bits);
      set_error(error, error_size, "unsupported constant base in '%s'", text);
      return NULL;
    }
  }

  if (bits.len == 0) {
    sb_append(&bits, "0");
  }
  return fit_width(sb_take(&bits), width);
}

static char *emit_ref(const VlogRef *ref)
{
  StrBuf sb;

  sb_init(&sb);
  sb_append(&sb, ref->name);
  if (ref->has_select) {
    if (ref->select_msb == ref->select_lsb) {
      sb_appendf(&sb, "(%d)", ref->select_msb);
    } else {
      sb_appendf(&sb, "(%d downto %d)", ref->select_msb, ref->select_lsb);
    }
  }
  return sb_take(&sb);
}

static const char *binary_op_text(VlogOp op)
{
  if (op == VLOG_OP_AND || op == VLOG_OP_LOGIC_AND) return "and";
  if (op == VLOG_OP_OR || op == VLOG_OP_LOGIC_OR) return "or";
  if (op == VLOG_OP_XOR) return "xor";
  return "";
}

static char *emit_expr(const VlogExpr *expr, char *error, unsigned int error_size)
{
  char *left;
  char *right;
  char *third;
  char *result;
  StrBuf sb;

  if (expr == NULL) {
    return vlog_strdup("");
  }

  if (expr->kind == VLOG_EXPR_REF) {
    return emit_ref(&expr->ref);
  }

  if (expr->kind == VLOG_EXPR_CONST) {
    char *bits;
    bits = normalize_number(expr->text, error, error_size);
    if (bits == NULL) return NULL;
    sb_init(&sb);
    sb_appendf(&sb, "B\"%s\"", bits);
    free(bits);
    return sb_take(&sb);
  }

  if (expr->kind == VLOG_EXPR_UNARY) {
    left = emit_expr(expr->left, error, error_size);
    if (left == NULL) return NULL;
    sb_init(&sb);
    sb_appendf(&sb, "(not %s)", left);
    free(left);
    return sb_take(&sb);
  }

  if (expr->kind == VLOG_EXPR_BINARY) {
    left = emit_expr(expr->left, error, error_size);
    if (left == NULL) return NULL;
    right = emit_expr(expr->right, error, error_size);
    if (right == NULL) {
      free(left);
      return NULL;
    }

    sb_init(&sb);
    if (expr->op == VLOG_OP_EQ) {
      sb_appendf(&sb, "(not (%s xor %s))", left, right);
    } else if (expr->op == VLOG_OP_NE) {
      sb_appendf(&sb, "(%s xor %s)", left, right);
    } else {
      sb_appendf(&sb, "(%s %s %s)", left, binary_op_text(expr->op), right);
    }
    free(left);
    free(right);
    return sb_take(&sb);
  }

  if (expr->kind == VLOG_EXPR_TERNARY) {
    left = emit_expr(expr->left, error, error_size);
    if (left == NULL) return NULL;
    right = emit_expr(expr->right, error, error_size);
    if (right == NULL) {
      free(left);
      return NULL;
    }
    third = emit_expr(expr->third, error, error_size);
    if (third == NULL) {
      free(left);
      free(right);
      return NULL;
    }
    sb_init(&sb);
    sb_appendf(&sb,
               "((%s and %s) or ((not %s) and %s))",
               left,
               right,
               left,
               third);
    free(left);
    free(right);
    free(third);
    return sb_take(&sb);
  }

  if (expr->kind == VLOG_EXPR_CONCAT) {
    VlogExprList *item;
    int first;

    sb_init(&sb);
    sb_append(&sb, "(");
    first = 1;
    for (item = expr->items; item != NULL; item = item->next) {
      char *part;

      part = emit_expr(item->expr, error, error_size);
      if (part == NULL) {
        sb_free(&sb);
        return NULL;
      }
      if (!first) {
        sb_append(&sb, " & ");
      }
      sb_append(&sb, part);
      free(part);
      first = 0;
    }
    sb_append(&sb, ")");
    return sb_take(&sb);
  }

  result = vlog_strdup("");
  return result;
}

static int validate_expr_refs(const VlogModule *module,
                              const VlogExpr *expr,
                              char *error,
                              unsigned int error_size);

static int validate_module(const VlogModule *module, char *error, unsigned int error_size)
{
  const VlogPort *port;
  const VlogAssign *assign;

  if (module->name == NULL) {
    set_error(error, error_size, "empty module");
    return 0;
  }

  for (port = module->ports; port != NULL; port = port->next) {
    const VlogSignal *signal;
    signal = find_signal_const(module, port->name);
    if (signal == NULL || signal->dir == VLOG_DIR_NONE) {
      set_error(error,
                error_size,
                "port '%s' has no input/output/inout declaration",
                port->name);
      return 0;
    }
  }

  for (assign = module->assigns; assign != NULL; assign = assign->next) {
    const VlogSignal *signal;
    signal = find_signal_const(module, assign->target.name);
    if (signal == NULL) {
      set_error(error, error_size, "assignment target '%s' is undeclared", assign->target.name);
      return 0;
    }
    if (signal->dir == VLOG_DIR_INPUT) {
      set_error(error, error_size, "assignment target '%s' is an input", assign->target.name);
      return 0;
    }
    if (!validate_expr_refs(module, assign->expr, error, error_size)) {
      return 0;
    }
  }
  return 1;
}

static int validate_expr_refs(const VlogModule *module,
                              const VlogExpr *expr,
                              char *error,
                              unsigned int error_size)
{
  const VlogExprList *item;

  if (expr == NULL) {
    return 1;
  }

  if (expr->kind == VLOG_EXPR_REF) {
    if (find_signal_const(module, expr->ref.name) == NULL) {
      set_error(error,
                error_size,
                "expression references undeclared signal '%s'",
                expr->ref.name);
      return 0;
    }
    return 1;
  }

  if (!validate_expr_refs(module, expr->left, error, error_size)) return 0;
  if (!validate_expr_refs(module, expr->right, error, error_size)) return 0;
  if (!validate_expr_refs(module, expr->third, error, error_size)) return 0;

  for (item = expr->items; item != NULL; item = item->next) {
    if (!validate_expr_refs(module, item->expr, error, error_size)) {
      return 0;
    }
  }
  return 1;
}

int vlog_emit_vbe_file(const VlogModule *module,
                       const char *path,
                       char *error,
                       unsigned int error_size)
{
  FILE *out;
  const VlogPort *port;
  const VlogSignal *signal;
  const VlogAssign *assign;
  int first;

  if (!validate_module(module, error, error_size)) {
    return 0;
  }

  out = fopen(path, "wb");
  if (out == NULL) {
    set_error(error, error_size, "cannot write '%s'", path);
    return 0;
  }

  fprintf(out, "ENTITY %s IS\n", module->name);
  fprintf(out, "PORT(\n");

  first = 1;
  for (port = module->ports; port != NULL; port = port->next) {
    signal = find_signal_const(module, port->name);
    if (signal == NULL) continue;
    if (!first) {
      fprintf(out, ";\n");
    }
    fprintf(out, "  %s : %s ", signal->name, dir_to_vbe(signal->dir));
    emit_type(out, signal->range);
    first = 0;
  }
  fprintf(out, "\n);\n");
  fprintf(out, "END %s;\n\n", module->name);

  fprintf(out, "ARCHITECTURE behavior_data_flow OF %s IS\n", module->name);
  for (signal = module->signals; signal != NULL; signal = signal->next) {
    if (signal->dir == VLOG_DIR_NONE) {
      fprintf(out, "  SIGNAL %s : ", signal->name);
      emit_type(out, signal->range);
      fprintf(out, ";\n");
    }
  }
  fprintf(out, "BEGIN\n");

  for (assign = module->assigns; assign != NULL; assign = assign->next) {
    char *target;
    char *expr;

    target = emit_ref(&assign->target);
    expr = emit_expr(assign->expr, error, error_size);
    if (expr == NULL) {
      free(target);
      fclose(out);
      return 0;
    }
    fprintf(out, "  %s <= %s;\n", target, expr);
    free(target);
    free(expr);
  }

  fprintf(out, "END behavior_data_flow;\n");
  fclose(out);
  return 1;
}
