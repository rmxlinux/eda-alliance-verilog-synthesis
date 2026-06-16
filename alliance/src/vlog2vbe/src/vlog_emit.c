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

static int signal_has_reg_driver(const VlogModule *module, const char *name)
{
  const VlogRegDriver *driver;

  for (driver = module->reg_drivers; driver != NULL; driver = driver->next) {
    if (strcmp(driver->target.name, name) == 0) {
      return 1;
    }
  }
  return 0;
}

static int signal_is_registered_output(const VlogModule *module, const char *name)
{
  const VlogSignal *signal;

  signal = find_signal_const(module, name);
  return signal != NULL &&
         signal->dir == VLOG_DIR_OUTPUT &&
         signal_has_reg_driver(module, name);
}

static char *reg_storage_name(const VlogModule *module, const char *name)
{
  StrBuf sb;

  if (!signal_is_registered_output(module, name)) {
    return vlog_strdup(name);
  }

  sb_init(&sb);
  sb_appendf(&sb, "%s_reg", name);
  return sb_take(&sb);
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

static char *emit_ref_raw(const VlogRef *ref)
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

static char *emit_ref(const VlogModule *module, const VlogRef *ref)
{
  StrBuf sb;
  char *name;

  name = reg_storage_name(module, ref->name);
  sb_init(&sb);
  sb_append(&sb, name);
  free(name);
  if (ref->has_select) {
    if (ref->select_msb == ref->select_lsb) {
      sb_appendf(&sb, "(%d)", ref->select_msb);
    } else {
      sb_appendf(&sb, "(%d downto %d)", ref->select_msb, ref->select_lsb);
    }
  }
  return sb_take(&sb);
}

static int range_width(VlogRange range)
{
  if (!range.has_range) {
    return 1;
  }
  return range.msb >= range.lsb ?
    range.msb - range.lsb + 1 :
    range.lsb - range.msb + 1;
}

static int ref_width(const VlogModule *module, const VlogRef *ref)
{
  const VlogSignal *signal;

  if (ref->has_select) {
    return ref->select_msb >= ref->select_lsb ?
      ref->select_msb - ref->select_lsb + 1 :
      ref->select_lsb - ref->select_msb + 1;
  }

  signal = find_signal_const(module, ref->name);
  if (signal == NULL) {
    return 1;
  }
  return range_width(signal->range);
}

static int ref_bit_number(const VlogModule *module,
                          const VlogRef *ref,
                          int bit_index,
                          int *bit_number)
{
  const VlogSignal *signal;
  int lsb;
  int msb;
  int width;

  if (ref->has_select) {
    msb = ref->select_msb;
    lsb = ref->select_lsb;
  } else {
    signal = find_signal_const(module, ref->name);
    if (signal == NULL || !signal->range.has_range) {
      if (bit_index != 0) {
        return 0;
      }
      *bit_number = 0;
      return 1;
    }
    msb = signal->range.msb;
    lsb = signal->range.lsb;
  }

  width = msb >= lsb ? msb - lsb + 1 : lsb - msb + 1;
  if (bit_index < 0 || bit_index >= width) {
    return 0;
  }

  *bit_number = msb >= lsb ? lsb + bit_index : lsb - bit_index;
  return 1;
}

static int number_width(const char *text)
{
  const char *quote;
  const char *digits;
  char base;
  int width;
  int count;
  char *endptr;

  quote = strchr(text, '\'');
  if (quote == NULL) {
    return 1;
  }

  width = 0;
  if (quote != text) {
    char *width_text;

    width_text = vlog_strndup(text, (unsigned int)(quote - text));
    width = (int)strtol(width_text, &endptr, 10);
    free(width_text);
    if (*endptr == '\0' && width > 0) {
      return width;
    }
  }

  digits = quote + 1;
  if (*digits == 's' || *digits == 'S') {
    digits++;
  }
  base = (char)tolower((unsigned char)*digits);
  if (base == '\0') {
    return 1;
  }
  digits++;

  count = 0;
  while (*digits != '\0') {
    if (*digits != '_') {
      if (base == 'b') count += 1;
      else if (base == 'o') count += 3;
      else if (base == 'h') count += 4;
      else count += 1;
    }
    digits++;
  }
  return count == 0 ? 1 : count;
}

static int max_int(int left, int right)
{
  return left > right ? left : right;
}

static int expr_width(const VlogModule *module, const VlogExpr *expr)
{
  int width;
  const VlogExprList *item;

  if (expr == NULL) {
    return 1;
  }

  if (expr->kind == VLOG_EXPR_REF) {
    return ref_width(module, &expr->ref);
  }
  if (expr->kind == VLOG_EXPR_CONST) {
    return number_width(expr->text);
  }
  if (expr->kind == VLOG_EXPR_UNARY) {
    return expr_width(module, expr->left);
  }
  if (expr->kind == VLOG_EXPR_BINARY) {
    if (expr->op == VLOG_OP_EQ || expr->op == VLOG_OP_NE) {
      return 1;
    }
    return max_int(expr_width(module, expr->left),
                   expr_width(module, expr->right));
  }
  if (expr->kind == VLOG_EXPR_TERNARY) {
    return max_int(expr_width(module, expr->right),
                   expr_width(module, expr->third));
  }
  if (expr->kind == VLOG_EXPR_CONCAT) {
    width = 0;
    for (item = expr->items; item != NULL; item = item->next) {
      width += expr_width(module, item->expr);
    }
    return width == 0 ? 1 : width;
  }
  return 1;
}

static int expr_contains_ternary(const VlogExpr *expr)
{
  const VlogExprList *item;

  if (expr == NULL) {
    return 0;
  }
  if (expr->kind == VLOG_EXPR_TERNARY) {
    return 1;
  }
  if (expr_contains_ternary(expr->left) ||
      expr_contains_ternary(expr->right) ||
      expr_contains_ternary(expr->third)) {
    return 1;
  }
  for (item = expr->items; item != NULL; item = item->next) {
    if (expr_contains_ternary(item->expr)) {
      return 1;
    }
  }
  return 0;
}

static const char *binary_op_text(VlogOp op)
{
  if (op == VLOG_OP_AND || op == VLOG_OP_LOGIC_AND) return "and";
  if (op == VLOG_OP_OR || op == VLOG_OP_LOGIC_OR) return "or";
  if (op == VLOG_OP_XOR) return "xor";
  return "";
}

static char *emit_expr(const VlogModule *module,
                       const VlogExpr *expr,
                       char *error,
                       unsigned int error_size);

static char *emit_expr_bit(const VlogModule *module,
                           const VlogExpr *expr,
                           int bit_index,
                           char *error,
                           unsigned int error_size);

static char *emit_condition_expr(const VlogModule *module,
                                 const VlogExpr *expr,
                                 char *error,
                                 unsigned int error_size)
{
  int width;
  int i;
  StrBuf sb;

  width = expr_width(module, expr);
  if (width <= 1) {
    return emit_expr(module, expr, error, error_size);
  }

  sb_init(&sb);
  for (i = 0; i < width; i++) {
    char *bit;

    bit = emit_expr_bit(module, expr, i, error, error_size);
    if (bit == NULL) {
      sb_free(&sb);
      return NULL;
    }
    if (i == 0) {
      sb_append(&sb, "(");
      sb_append(&sb, bit);
    } else {
      sb_append(&sb, " or ");
      sb_append(&sb, bit);
    }
    free(bit);
  }
  sb_append(&sb, ")");
  return sb_take(&sb);
}

static char *emit_equality_expr(const VlogModule *module,
                                const VlogExpr *left_expr,
                                const VlogExpr *right_expr,
                                int negate,
                                char *error,
                                unsigned int error_size)
{
  int width;
  int i;
  StrBuf diff;
  StrBuf out;

  width = max_int(expr_width(module, left_expr), expr_width(module, right_expr));
  if (width < 1) {
    width = 1;
  }

  sb_init(&diff);
  for (i = 0; i < width; i++) {
    char *left;
    char *right;

    left = emit_expr_bit(module, left_expr, i, error, error_size);
    if (left == NULL) {
      sb_free(&diff);
      return NULL;
    }
    right = emit_expr_bit(module, right_expr, i, error, error_size);
    if (right == NULL) {
      free(left);
      sb_free(&diff);
      return NULL;
    }

    if (i == 0) {
      sb_append(&diff, "(");
    } else {
      sb_append(&diff, " or ");
    }
    sb_appendf(&diff, "(%s xor %s)", left, right);
    free(left);
    free(right);
  }
  sb_append(&diff, ")");

  if (negate) {
    return sb_take(&diff);
  }

  sb_init(&out);
  sb_appendf(&out, "(not %s)", diff.data);
  sb_free(&diff);
  return sb_take(&out);
}

static char *emit_ref_bit(const VlogModule *module,
                          const VlogRef *ref,
                          int bit_index,
                          char *error,
                          unsigned int error_size)
{
  int width;
  int bit_number;
  StrBuf sb;

  width = ref_width(module, ref);
  if (bit_index >= width) {
    return vlog_strdup("'0'");
  }
  if (!ref_bit_number(module, ref, bit_index, &bit_number)) {
    set_error(error, error_size, "cannot select bit %d of '%s'", bit_index, ref->name);
    return NULL;
  }
  if (width == 1 && !ref->has_select) {
    return emit_ref(module, ref);
  }

  sb_init(&sb);
  {
    char *name;
    name = reg_storage_name(module, ref->name);
    sb_appendf(&sb, "%s(%d)", name, bit_number);
    free(name);
  }
  return sb_take(&sb);
}

static char *emit_const_bit(const VlogExpr *expr,
                            int bit_index,
                            int target_width,
                            char *error,
                            unsigned int error_size)
{
  char *bits;
  unsigned int len;
  char text[4];

  bits = normalize_number(expr->text, error, error_size);
  if (bits == NULL) {
    return NULL;
  }
  bits = fit_width(bits, target_width);
  len = (unsigned int)strlen(bits);
  if (bit_index >= (int)len) {
    free(bits);
    return vlog_strdup("'0'");
  }

  text[0] = '\'';
  text[1] = bits[len - 1 - bit_index];
  text[2] = '\'';
  text[3] = '\0';
  free(bits);
  return vlog_strdup(text);
}

static char *emit_expr_bit(const VlogModule *module,
                           const VlogExpr *expr,
                           int bit_index,
                           char *error,
                           unsigned int error_size)
{
  int width;
  char *left;
  char *right;
  char *third;
  char *cond;
  StrBuf sb;

  width = expr_width(module, expr);
  if (bit_index >= width) {
    return vlog_strdup("'0'");
  }

  if (expr->kind == VLOG_EXPR_REF) {
    return emit_ref_bit(module, &expr->ref, bit_index, error, error_size);
  }
  if (expr->kind == VLOG_EXPR_CONST) {
    return emit_const_bit(expr, bit_index, width, error, error_size);
  }
  if (expr->kind == VLOG_EXPR_UNARY) {
    left = emit_expr_bit(module, expr->left, bit_index, error, error_size);
    if (left == NULL) return NULL;
    sb_init(&sb);
    sb_appendf(&sb, "(not %s)", left);
    free(left);
    return sb_take(&sb);
  }
  if (expr->kind == VLOG_EXPR_BINARY) {
    if (expr->op == VLOG_OP_EQ || expr->op == VLOG_OP_NE) {
      if (bit_index != 0) {
        return vlog_strdup("'0'");
      }
      return emit_equality_expr(module,
                                expr->left,
                                expr->right,
                                expr->op == VLOG_OP_NE,
                                error,
                                error_size);
    }

    left = emit_expr_bit(module, expr->left, bit_index, error, error_size);
    if (left == NULL) return NULL;
    right = emit_expr_bit(module, expr->right, bit_index, error, error_size);
    if (right == NULL) {
      free(left);
      return NULL;
    }
    sb_init(&sb);
    sb_appendf(&sb, "(%s %s %s)", left, binary_op_text(expr->op), right);
    free(left);
    free(right);
    return sb_take(&sb);
  }
  if (expr->kind == VLOG_EXPR_TERNARY) {
    cond = emit_condition_expr(module, expr->left, error, error_size);
    if (cond == NULL) return NULL;
    right = emit_expr_bit(module, expr->right, bit_index, error, error_size);
    if (right == NULL) {
      free(cond);
      return NULL;
    }
    third = emit_expr_bit(module, expr->third, bit_index, error, error_size);
    if (third == NULL) {
      free(cond);
      free(right);
      return NULL;
    }
    sb_init(&sb);
    sb_appendf(&sb,
               "((%s and %s) or ((not %s) and %s))",
               cond,
               right,
               cond,
               third);
    free(cond);
    free(right);
    free(third);
    return sb_take(&sb);
  }
  if (expr->kind == VLOG_EXPR_CONCAT) {
    const VlogExprList *item;
    int total_width;
    int from_msb;

    total_width = expr_width(module, expr);
    from_msb = total_width - 1 - bit_index;
    for (item = expr->items; item != NULL; item = item->next) {
      int item_width;

      item_width = expr_width(module, item->expr);
      if (from_msb < item_width) {
        return emit_expr_bit(module,
                             item->expr,
                             item_width - 1 - from_msb,
                             error,
                             error_size);
      }
      from_msb -= item_width;
    }
  }

  set_error(error, error_size, "cannot emit bit %d of expression", bit_index);
  return NULL;
}

static char *emit_expr(const VlogModule *module,
                       const VlogExpr *expr,
                       char *error,
                       unsigned int error_size)
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
    return emit_ref(module, &expr->ref);
  }

  if (expr->kind == VLOG_EXPR_CONST) {
    char *bits;
    bits = normalize_number(expr->text, error, error_size);
    if (bits == NULL) return NULL;
    sb_init(&sb);
    if (strlen(bits) == 1) {
      sb_appendf(&sb, "'%s'", bits);
    } else {
      sb_appendf(&sb, "B\"%s\"", bits);
    }
    free(bits);
    return sb_take(&sb);
  }

  if (expr->kind == VLOG_EXPR_UNARY) {
    left = emit_expr(module, expr->left, error, error_size);
    if (left == NULL) return NULL;
    sb_init(&sb);
    sb_appendf(&sb, "(not %s)", left);
    free(left);
    return sb_take(&sb);
  }

  if (expr->kind == VLOG_EXPR_BINARY) {
    if (expr->op == VLOG_OP_EQ || expr->op == VLOG_OP_NE) {
      return emit_equality_expr(module,
                                expr->left,
                                expr->right,
                                expr->op == VLOG_OP_NE,
                                error,
                                error_size);
    }

    left = emit_expr(module, expr->left, error, error_size);
    if (left == NULL) return NULL;
    right = emit_expr(module, expr->right, error, error_size);
    if (right == NULL) {
      free(left);
      return NULL;
    }

    sb_init(&sb);
    sb_appendf(&sb, "(%s %s %s)", left, binary_op_text(expr->op), right);
    free(left);
    free(right);
    return sb_take(&sb);
  }

  if (expr->kind == VLOG_EXPR_TERNARY) {
    left = emit_condition_expr(module, expr->left, error, error_size);
    if (left == NULL) return NULL;
    right = emit_expr(module, expr->right, error, error_size);
    if (right == NULL) {
      free(left);
      return NULL;
    }
    third = emit_expr(module, expr->third, error, error_size);
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

      part = emit_expr(module, item->expr, error, error_size);
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

static int emit_reg_type(FILE *out, VlogRange range)
{
  if (range.has_range) {
    return fprintf(out,
                   "REG_VECTOR(%d downto %d) REGISTER",
                   range.msb,
                   range.lsb) > 0;
  }
  return fprintf(out, "REG_BIT REGISTER") > 0;
}

static char *emit_storage_target(const VlogModule *module, const VlogRef *target)
{
  StrBuf sb;
  char *name;

  name = reg_storage_name(module, target->name);
  sb_init(&sb);
  sb_append(&sb, name);
  free(name);
  if (target->has_select) {
    if (target->select_msb == target->select_lsb) {
      sb_appendf(&sb, "(%d)", target->select_msb);
    } else {
      sb_appendf(&sb, "(%d downto %d)", target->select_msb, target->select_lsb);
    }
  }
  return sb_take(&sb);
}

static char *emit_storage_target_bit(const VlogModule *module,
                                     const VlogRef *target,
                                     int bit_index,
                                     char *error,
                                     unsigned int error_size)
{
  int bit_number;
  char *name;
  StrBuf sb;

  if (!ref_bit_number(module, target, bit_index, &bit_number)) {
    set_error(error,
              error_size,
              "cannot select register bit %d of '%s'",
              bit_index,
              target->name);
    return NULL;
  }

  name = reg_storage_name(module, target->name);
  sb_init(&sb);
  sb_appendf(&sb, "%s(%d)", name, bit_number);
  free(name);
  return sb_take(&sb);
}

static char *emit_driver_guard(const VlogModule *module,
                               const VlogRegDriver *driver,
                               char *error,
                               unsigned int error_size)
{
  StrBuf sb;
  char *guard;

  guard = NULL;
  if (driver->guard != NULL) {
    guard = emit_condition_expr(module, driver->guard, error, error_size);
    if (guard == NULL) {
      return NULL;
    }
  }

  sb_init(&sb);
  if (driver->clock == NULL) {
    if (guard == NULL) {
      set_error(error, error_size, "level-sensitive register driver lacks a guard");
      return NULL;
    }
    sb_appendf(&sb, "(%s = '1')", guard);
    free(guard);
    return sb_take(&sb);
  }

  if (driver->clock_posedge) {
    if (guard != NULL) {
      sb_appendf(&sb,
                 "(((%s and not (%s'STABLE)) and %s) = '1')",
                 driver->clock,
                 driver->clock,
                 guard);
    } else {
      sb_appendf(&sb,
                 "((%s and not (%s'STABLE)) = '1')",
                 driver->clock,
                 driver->clock);
    }
  } else {
    if (guard != NULL) {
      sb_appendf(&sb,
                 "((((not %s) and not (%s'STABLE)) and %s) = '1')",
                 driver->clock,
                 driver->clock,
                 guard);
    } else {
      sb_appendf(&sb,
                 "(((not %s) and not (%s'STABLE)) = '1')",
                 driver->clock,
                 driver->clock);
    }
  }
  free(guard);
  return sb_take(&sb);
}

static int emit_reg_block(FILE *out,
                          const VlogModule *module,
                          const VlogRegDriver *driver,
                          const char *target,
                          const char *expr,
                          int label,
                          char *error,
                          unsigned int error_size)
{
  char *guard;

  guard = emit_driver_guard(module, driver, error, error_size);
  if (guard == NULL) {
    return 0;
  }

  fprintf(out, "  label%d : BLOCK %s\n", label, guard);
  fprintf(out, "  BEGIN\n");
  fprintf(out, "    %s <= GUARDED %s;\n", target, expr);
  fprintf(out, "  END BLOCK label%d;\n", label);
  free(guard);
  return 1;
}

static int emit_reg_driver(FILE *out,
                           const VlogModule *module,
                           const VlogRegDriver *driver,
                           int *label,
                           char *error,
                           unsigned int error_size)
{
  const VlogSignal *target_signal;
  int target_width;
  char *target;
  char *expr;

  target_signal = find_signal_const(module, driver->target.name);
  target_width = driver->target.has_select || target_signal == NULL ?
    ref_width(module, &driver->target) :
    range_width(target_signal->range);

  if (!driver->target.has_select &&
      target_width > 1 &&
      expr_contains_ternary(driver->expr)) {
    int i;

    for (i = 0; i < target_width; i++) {
      target = emit_storage_target_bit(module,
                                       &driver->target,
                                       i,
                                       error,
                                       error_size);
      if (target == NULL) {
        return 0;
      }
      expr = emit_expr_bit(module, driver->expr, i, error, error_size);
      if (expr == NULL) {
        free(target);
        return 0;
      }
      if (!emit_reg_block(out,
                          module,
                          driver,
                          target,
                          expr,
                          *label,
                          error,
                          error_size)) {
        set_error(error, error_size, "cannot emit register guard");
        free(target);
        free(expr);
        return 0;
      }
      (*label)++;
      free(target);
      free(expr);
    }
    return 1;
  }

  target = emit_storage_target(module, &driver->target);
  expr = emit_expr(module, driver->expr, error, error_size);
  if (expr == NULL) {
    free(target);
    return 0;
  }
  if (!emit_reg_block(out,
                      module,
                      driver,
                      target,
                      expr,
                      *label,
                      error,
                      error_size)) {
    set_error(error, error_size, "cannot emit register guard");
    free(target);
    free(expr);
    return 0;
  }
  (*label)++;
  free(target);
  free(expr);
  return 1;
}

static int validate_expr_refs(const VlogModule *module,
                              const VlogExpr *expr,
                              char *error,
                              unsigned int error_size);

static int refs_same_target(const VlogRef *left, const VlogRef *right)
{
  if (strcmp(left->name, right->name) != 0) {
    return 0;
  }
  if (left->has_select != right->has_select) {
    return 0;
  }
  if (!left->has_select) {
    return 1;
  }
  return left->select_msb == right->select_msb &&
         left->select_lsb == right->select_lsb;
}

static int validate_module(const VlogModule *module, char *error, unsigned int error_size)
{
  const VlogPort *port;
  const VlogAssign *assign;
  const VlogAssign *other_assign;
  const VlogRegDriver *driver;

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
    if (signal_has_reg_driver(module, assign->target.name)) {
      set_error(error,
                error_size,
                "signal '%s' is driven by both combinational and sequential logic",
                assign->target.name);
      return 0;
    }
    for (other_assign = assign->next;
         other_assign != NULL;
         other_assign = other_assign->next) {
      if (refs_same_target(&assign->target, &other_assign->target)) {
        set_error(error,
                  error_size,
                  "multiple assignments drive '%s'",
                  assign->target.name);
        return 0;
      }
    }
  }

  for (driver = module->reg_drivers; driver != NULL; driver = driver->next) {
    const VlogSignal *signal;

    signal = find_signal_const(module, driver->target.name);
    if (signal == NULL) {
      set_error(error,
                error_size,
                "register target '%s' is undeclared",
                driver->target.name);
      return 0;
    }
    if (signal->dir == VLOG_DIR_INPUT) {
      set_error(error,
                error_size,
                "register target '%s' is an input",
                driver->target.name);
      return 0;
    }
    if (!validate_expr_refs(module, driver->guard, error, error_size) ||
        !validate_expr_refs(module, driver->expr, error, error_size)) {
      return 0;
    }
    if (driver->clock != NULL && find_signal_const(module, driver->clock) == NULL) {
      set_error(error,
                error_size,
                "clock signal '%s' is undeclared",
                driver->clock);
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
  const VlogRegDriver *driver;
  int first;
  int label;

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
    if (signal_has_reg_driver(module, signal->name)) {
      char *name;

      name = reg_storage_name(module, signal->name);
      fprintf(out, "  SIGNAL %s : ", name);
      emit_reg_type(out, signal->range);
      fprintf(out, ";\n");
      free(name);
    }
  }
  for (signal = module->signals; signal != NULL; signal = signal->next) {
    if (signal->dir == VLOG_DIR_NONE && !signal_has_reg_driver(module, signal->name)) {
      fprintf(out, "  SIGNAL %s : ", signal->name);
      emit_type(out, signal->range);
      fprintf(out, ";\n");
    }
  }
  fprintf(out, "BEGIN\n");

  label = 0;
  for (driver = module->reg_drivers; driver != NULL; driver = driver->next) {
    if (!emit_reg_driver(out, module, driver, &label, error, error_size)) {
      fclose(out);
      return 0;
    }
  }

  for (assign = module->assigns; assign != NULL; assign = assign->next) {
    char *target;
    char *expr;
    const VlogSignal *target_signal;
    int target_width;

    target_signal = find_signal_const(module, assign->target.name);
    target_width = assign->target.has_select || target_signal == NULL ?
      ref_width(module, &assign->target) :
      range_width(target_signal->range);

    if (!assign->target.has_select &&
        target_width > 1 &&
        expr_contains_ternary(assign->expr)) {
      int i;

      for (i = 0; i < target_width; i++) {
        int bit_number;

        if (!ref_bit_number(module, &assign->target, i, &bit_number)) {
          set_error(error,
                    error_size,
                    "cannot emit bit assignment for '%s'",
                    assign->target.name);
          fclose(out);
          return 0;
        }
        expr = emit_expr_bit(module, assign->expr, i, error, error_size);
        if (expr == NULL) {
          fclose(out);
          return 0;
        }
        fprintf(out, "  %s(%d) <= %s;\n", assign->target.name, bit_number, expr);
        free(expr);
      }
      continue;
    }

    target = emit_ref_raw(&assign->target);
    expr = emit_expr(module, assign->expr, error, error_size);
    if (expr == NULL) {
      free(target);
      fclose(out);
      return 0;
    }
    fprintf(out, "  %s <= %s;\n", target, expr);
    free(target);
    free(expr);
  }

  for (signal = module->signals; signal != NULL; signal = signal->next) {
    if (signal->dir == VLOG_DIR_OUTPUT && signal_has_reg_driver(module, signal->name)) {
      char *storage;

      storage = reg_storage_name(module, signal->name);
      fprintf(out, "  %s <= %s;\n", signal->name, storage);
      free(storage);
    }
  }

  fprintf(out, "END behavior_data_flow;\n");
  fclose(out);
  return 1;
}
