#include "vlog_elab.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct StrBuf {
  char *data;
  unsigned int len;
  unsigned int cap;
} StrBuf;

typedef struct PortBinding {
  char *port_name;
  VlogExpr *expr;
  int line;
  struct PortBinding *next;
} PortBinding;

typedef struct ParamBinding {
  char *name;
  int value;
  struct ParamBinding *next;
} ParamBinding;

typedef struct ModuleStack {
  const VlogModule *module;
  const struct ModuleStack *next;
} ModuleStack;

typedef struct ElabContext {
  const VlogDesign *design;
  VlogModule *flat;
  const VlogModule *module;
  const char *prefix;
  const PortBinding *bindings;
  const ParamBinding *params;
  char *error;
  unsigned int error_size;
} ElabContext;

static void *xmalloc(unsigned int size)
{
  void *ptr;

  ptr = malloc(size == 0 ? 1 : size);
  if (ptr == NULL) {
    fprintf(stderr, "vlog2vbe: out of memory\n");
    exit(2);
  }
  return ptr;
}

static void sb_init(StrBuf *sb)
{
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

static const VlogPort *find_port_const(const VlogModule *module, const char *name)
{
  const VlogPort *port;

  for (port = module->ports; port != NULL; port = port->next) {
    if (strcmp(port->name, name) == 0) {
      return port;
    }
  }
  return NULL;
}

static const ParamBinding *param_find_const(const ParamBinding *params, const char *name)
{
  while (params != NULL) {
    if (strcmp(params->name, name) == 0) {
      return params;
    }
    params = params->next;
  }
  return NULL;
}

static int param_append(ParamBinding **params,
                        const char *name,
                        int value,
                        char *error,
                        unsigned int error_size)
{
  ParamBinding *node;
  ParamBinding **tail;

  if (param_find_const(*params, name) != NULL) {
    set_error(error, error_size, "duplicate parameter '%s'", name);
    return 0;
  }

  node = (ParamBinding *)xmalloc(sizeof(ParamBinding));
  node->name = vlog_strdup(name);
  node->value = value;
  node->next = NULL;

  tail = params;
  while (*tail != NULL) {
    tail = &(*tail)->next;
  }
  *tail = node;
  return 1;
}

static void param_free(ParamBinding *params)
{
  ParamBinding *next;

  while (params != NULL) {
    next = params->next;
    free(params->name);
    free(params);
    params = next;
  }
}

static int eval_int_expr(const ParamBinding *params,
                         const VlogIntExpr *expr,
                         int *value,
                         char *error,
                         unsigned int error_size)
{
  int left;
  int right;
  const ParamBinding *binding;

  if (expr == NULL) {
    set_error(error, error_size, "missing integer expression");
    return 0;
  }

  if (expr->kind == VLOG_INT_CONST) {
    *value = expr->value;
    return 1;
  }

  if (expr->kind == VLOG_INT_REF) {
    binding = param_find_const(params, expr->name);
    if (binding == NULL) {
      set_error(error,
                error_size,
                "line %d: unknown parameter '%s'",
                expr->line,
                expr->name);
      return 0;
    }
    *value = binding->value;
    return 1;
  }

  if (expr->kind == VLOG_INT_UNARY) {
    if (!eval_int_expr(params, expr->left, &left, error, error_size)) {
      return 0;
    }
    if (expr->op == VLOG_INT_OP_NEG) {
      *value = -left;
      return 1;
    }
  }

  if (expr->kind == VLOG_INT_BINARY) {
    if (!eval_int_expr(params, expr->left, &left, error, error_size) ||
        !eval_int_expr(params, expr->right, &right, error, error_size)) {
      return 0;
    }
    if (expr->op == VLOG_INT_OP_ADD) {
      *value = left + right;
      return 1;
    }
    if (expr->op == VLOG_INT_OP_SUB) {
      *value = left - right;
      return 1;
    }
    if (expr->op == VLOG_INT_OP_MUL) {
      *value = left * right;
      return 1;
    }
    if (expr->op == VLOG_INT_OP_DIV || expr->op == VLOG_INT_OP_MOD) {
      if (right == 0) {
        set_error(error, error_size, "line %d: division by zero in parameter expression", expr->line);
        return 0;
      }
      *value = expr->op == VLOG_INT_OP_DIV ? left / right : left % right;
      return 1;
    }
  }

  set_error(error, error_size, "line %d: unsupported parameter expression", expr->line);
  return 0;
}

static PortBinding *binding_find(PortBinding *bindings, const char *port_name)
{
  while (bindings != NULL) {
    if (strcmp(bindings->port_name, port_name) == 0) {
      return bindings;
    }
    bindings = bindings->next;
  }
  return NULL;
}

static const PortBinding *binding_find_const(const PortBinding *bindings,
                                             const char *port_name)
{
  while (bindings != NULL) {
    if (strcmp(bindings->port_name, port_name) == 0) {
      return bindings;
    }
    bindings = bindings->next;
  }
  return NULL;
}

static int binding_append(PortBinding **bindings,
                          const char *port_name,
                          VlogExpr *expr,
                          int line,
                          char *error,
                          unsigned int error_size)
{
  PortBinding *node;
  PortBinding **tail;

  if (binding_find(*bindings, port_name) != NULL) {
    set_error(error, error_size, "duplicate connection for port '%s'", port_name);
    vlog_expr_free(expr);
    return 0;
  }

  node = (PortBinding *)xmalloc(sizeof(PortBinding));
  node->port_name = vlog_strdup(port_name);
  node->expr = expr;
  node->line = line;
  node->next = NULL;

  tail = bindings;
  while (*tail != NULL) {
    tail = &(*tail)->next;
  }
  *tail = node;
  return 1;
}

static void binding_free(PortBinding *bindings)
{
  PortBinding *next;

  while (bindings != NULL) {
    next = bindings->next;
    free(bindings->port_name);
    vlog_expr_free(bindings->expr);
    free(bindings);
    bindings = next;
  }
}

static char *prefixed_name(const char *prefix, const char *name)
{
  unsigned int prefix_len;
  unsigned int name_len;
  char *out;

  if (prefix == NULL || prefix[0] == '\0') {
    return vlog_strdup(name);
  }

  prefix_len = (unsigned int)strlen(prefix);
  name_len = (unsigned int)strlen(name);
  out = (char *)xmalloc(prefix_len + name_len + 1);
  memcpy(out, prefix, prefix_len);
  memcpy(out + prefix_len, name, name_len + 1);
  return out;
}

static char *instance_prefix(const char *parent_prefix, const char *instance_name)
{
  unsigned int parent_len;
  unsigned int inst_len;
  char *out;

  parent_len = parent_prefix == NULL ? 0 : (unsigned int)strlen(parent_prefix);
  inst_len = (unsigned int)strlen(instance_name);
  out = (char *)xmalloc(parent_len + inst_len + 2);
  if (parent_len > 0) {
    memcpy(out, parent_prefix, parent_len);
  }
  memcpy(out + parent_len, instance_name, inst_len);
  out[parent_len + inst_len] = '_';
  out[parent_len + inst_len + 1] = '\0';
  return out;
}

static VlogRef ref_clone_with_name(const VlogRef *ref, const char *name)
{
  VlogRef out;

  out.name = vlog_strdup(name);
  out.has_select = ref->has_select;
  out.select_msb = ref->select_msb;
  out.select_lsb = ref->select_lsb;
  out.select_msb_expr = NULL;
  out.select_lsb_expr = NULL;
  return out;
}

static int resolve_range(const ElabContext *ctx,
                         VlogRange range,
                         int line,
                         VlogRange *out)
{
  *out = vlog_range_none();
  if (!range.has_range) {
    return 1;
  }

  out->has_range = 1;
  if (range.msb_expr != NULL) {
    if (!eval_int_expr(ctx->params,
                       range.msb_expr,
                       &out->msb,
                       ctx->error,
                       ctx->error_size)) {
      return 0;
    }
  } else {
    out->msb = range.msb;
  }

  if (range.lsb_expr != NULL) {
    if (!eval_int_expr(ctx->params,
                       range.lsb_expr,
                       &out->lsb,
                       ctx->error,
                       ctx->error_size)) {
      return 0;
    }
  } else {
    out->lsb = range.lsb;
  }

  if (out->msb < 0 || out->lsb < 0) {
    set_error(ctx->error, ctx->error_size, "line %d: negative range bound", line);
    return 0;
  }
  return 1;
}

static int resolve_ref(const ElabContext *ctx,
                       const VlogRef *ref,
                       int line,
                       VlogRef *out)
{
  *out = ref_clone_with_name(ref, ref->name);
  if (!ref->has_select) {
    return 1;
  }

  if (ref->select_msb_expr != NULL) {
    if (!eval_int_expr(ctx->params,
                       ref->select_msb_expr,
                       &out->select_msb,
                       ctx->error,
                       ctx->error_size)) {
      vlog_ref_free(out);
      return 0;
    }
  }
  if (ref->select_lsb_expr != NULL) {
    if (!eval_int_expr(ctx->params,
                       ref->select_lsb_expr,
                       &out->select_lsb,
                       ctx->error,
                       ctx->error_size)) {
      vlog_ref_free(out);
      return 0;
    }
  }
  if (out->select_msb < 0 || out->select_lsb < 0) {
    set_error(ctx->error, ctx->error_size, "line %d: negative bit select", line);
    vlog_ref_free(out);
    return 0;
  }
  return 1;
}

static VlogExpr *int_const_expr(int value, int line)
{
  char text[32];

  snprintf(text, sizeof(text), "%d", value);
  return vlog_expr_const(text, line);
}

static int apply_child_select(VlogRef *target,
                              const VlogRef *child_ref,
                              int line,
                              char *error,
                              unsigned int error_size)
{
  if (!child_ref->has_select) {
    return 1;
  }
  if (target->has_select) {
    set_error(error,
              error_size,
              "line %d: cannot combine two levels of part-select on '%s'",
              line,
              child_ref->name);
    return 0;
  }
  target->has_select = 1;
  target->select_msb = child_ref->select_msb;
  target->select_lsb = child_ref->select_lsb;
  return 1;
}

static VlogExpr *rewrite_expr(const ElabContext *ctx, const VlogExpr *expr);

static VlogExpr *rewrite_ref_expr(const ElabContext *ctx, const VlogRef *ref, int line)
{
  const PortBinding *binding;
  const ParamBinding *param;
  char *name;
  VlogRef resolved_ref;
  VlogRef out_ref;
  VlogExpr *out;

  param = param_find_const(ctx->params, ref->name);
  if (param != NULL) {
    if (ref->has_select) {
      set_error(ctx->error,
                ctx->error_size,
                "line %d: parameter '%s' cannot be bit-selected in this version",
                line,
                ref->name);
      return NULL;
    }
    return int_const_expr(param->value, line);
  }

  if (!resolve_ref(ctx, ref, line, &resolved_ref)) {
    return NULL;
  }

  binding = binding_find_const(ctx->bindings, resolved_ref.name);
  if (binding != NULL && binding->expr != NULL) {
    out = vlog_expr_clone(binding->expr);
    if (resolved_ref.has_select) {
      if (out->kind != VLOG_EXPR_REF) {
        set_error(ctx->error,
                  ctx->error_size,
                  "line %d: selected port '%s' is connected to a non-reference expression",
                  line,
                  resolved_ref.name);
        vlog_expr_free(out);
        vlog_ref_free(&resolved_ref);
        return NULL;
      }
      if (!apply_child_select(&out->ref,
                              &resolved_ref,
                              line,
                              ctx->error,
                              ctx->error_size)) {
        vlog_expr_free(out);
        vlog_ref_free(&resolved_ref);
        return NULL;
      }
    }
    vlog_ref_free(&resolved_ref);
    return out;
  }

  name = prefixed_name(ctx->prefix, resolved_ref.name);
  out_ref = ref_clone_with_name(&resolved_ref, name);
  free(name);
  vlog_ref_free(&resolved_ref);
  return vlog_expr_ref(out_ref, line);
}

static VlogExprList *rewrite_expr_list(const ElabContext *ctx,
                                       const VlogExprList *items)
{
  VlogExprList *out;

  out = NULL;
  while (items != NULL) {
    VlogExpr *item;

    item = rewrite_expr(ctx, items->expr);
    if (item == NULL) {
      vlog_expr_list_free(out);
      return NULL;
    }
    out = vlog_expr_list_append(out, item);
    items = items->next;
  }
  return out;
}

static VlogExpr *rewrite_expr(const ElabContext *ctx, const VlogExpr *expr)
{
  VlogExpr *left;
  VlogExpr *right;
  VlogExpr *third;
  VlogExprList *items;

  if (expr == NULL) {
    return NULL;
  }

  if (expr->kind == VLOG_EXPR_REF) {
    return rewrite_ref_expr(ctx, &expr->ref, expr->line);
  }
  if (expr->kind == VLOG_EXPR_CONST) {
    return vlog_expr_clone(expr);
  }
  if (expr->kind == VLOG_EXPR_UNARY) {
    left = rewrite_expr(ctx, expr->left);
    if (left == NULL) return NULL;
    return vlog_expr_unary(expr->op, left, expr->line);
  }
  if (expr->kind == VLOG_EXPR_BINARY) {
    left = rewrite_expr(ctx, expr->left);
    if (left == NULL) return NULL;
    right = rewrite_expr(ctx, expr->right);
    if (right == NULL) {
      vlog_expr_free(left);
      return NULL;
    }
    return vlog_expr_binary(expr->op, left, right, expr->line);
  }
  if (expr->kind == VLOG_EXPR_TERNARY) {
    left = rewrite_expr(ctx, expr->left);
    if (left == NULL) return NULL;
    right = rewrite_expr(ctx, expr->right);
    if (right == NULL) {
      vlog_expr_free(left);
      return NULL;
    }
    third = rewrite_expr(ctx, expr->third);
    if (third == NULL) {
      vlog_expr_free(left);
      vlog_expr_free(right);
      return NULL;
    }
    return vlog_expr_ternary(left, right, third, expr->line);
  }
  if (expr->kind == VLOG_EXPR_CONCAT) {
    items = rewrite_expr_list(ctx, expr->items);
    if (items == NULL) return NULL;
    return vlog_expr_concat(items, expr->line);
  }

  return NULL;
}

static int rewrite_lvalue_ref(const ElabContext *ctx,
                              const VlogRef *ref,
                              int line,
                              VlogRef *out)
{
  const PortBinding *binding;
  VlogRef resolved_ref;
  char *name;

  if (param_find_const(ctx->params, ref->name) != NULL) {
    set_error(ctx->error,
              ctx->error_size,
              "line %d: parameter '%s' cannot be assigned",
              line,
              ref->name);
    return 0;
  }

  if (!resolve_ref(ctx, ref, line, &resolved_ref)) {
    return 0;
  }

  binding = binding_find_const(ctx->bindings, resolved_ref.name);
  if (binding != NULL && binding->expr != NULL) {
    if (binding->expr->kind != VLOG_EXPR_REF) {
      set_error(ctx->error,
                ctx->error_size,
                "line %d: output port '%s' is connected to a non-assignable expression",
                line,
                resolved_ref.name);
      vlog_ref_free(&resolved_ref);
      return 0;
    }
    *out = ref_clone_with_name(&binding->expr->ref, binding->expr->ref.name);
    if (!apply_child_select(out, &resolved_ref, line, ctx->error, ctx->error_size)) {
      vlog_ref_free(out);
      vlog_ref_free(&resolved_ref);
      return 0;
    }
    vlog_ref_free(&resolved_ref);
    return 1;
  }

  name = prefixed_name(ctx->prefix, resolved_ref.name);
  *out = ref_clone_with_name(&resolved_ref, name);
  free(name);
  vlog_ref_free(&resolved_ref);
  return 1;
}

static char *rewrite_clock_name(const ElabContext *ctx,
                                const char *clock,
                                int line)
{
  VlogRef clock_ref;
  VlogExpr *clock_expr;
  char *name;

  if (clock == NULL) {
    return NULL;
  }

  clock_ref = vlog_ref_make(clock);
  clock_expr = rewrite_ref_expr(ctx, &clock_ref, line);
  vlog_ref_free(&clock_ref);
  if (clock_expr == NULL) {
    return NULL;
  }
  if (clock_expr->kind != VLOG_EXPR_REF || clock_expr->ref.has_select) {
    set_error(ctx->error,
              ctx->error_size,
              "line %d: clock '%s' must elaborate to a simple signal",
              line,
              clock);
    vlog_expr_free(clock_expr);
    return NULL;
  }

  name = vlog_strdup(clock_expr->ref.name);
  vlog_expr_free(clock_expr);
  return name;
}

static int copy_top_signals(const ElabContext *ctx)
{
  const VlogPort *port;
  const VlogSignal *signal;

  for (port = ctx->module->ports; port != NULL; port = port->next) {
    signal = find_signal_const(ctx->module, port->name);
    if (signal != NULL) {
      VlogRange range;

      if (!resolve_range(ctx, signal->range, 0, &range)) {
        return 0;
      }
      vlog_module_update_signal(ctx->flat,
                                signal->name,
                                signal->dir,
                                1,
                                signal->is_reg,
                                signal->is_signed,
                                range);
      vlog_range_free(&range);
    }
  }

  for (signal = ctx->module->signals; signal != NULL; signal = signal->next) {
    if (!signal->is_port) {
      VlogRange range;

      if (!resolve_range(ctx, signal->range, 0, &range)) {
        return 0;
      }
      vlog_module_update_signal(ctx->flat,
                                signal->name,
                                VLOG_DIR_NONE,
                                0,
                                signal->is_reg,
                                signal->is_signed,
                                range);
      vlog_range_free(&range);
    }
  }
  return 1;
}

static int add_local_child_signals(const ElabContext *ctx)
{
  const VlogSignal *signal;

  for (signal = ctx->module->signals; signal != NULL; signal = signal->next) {
    const PortBinding *binding;
    char *name;

    binding = binding_find_const(ctx->bindings, signal->name);
    if (signal->is_port && binding != NULL && binding->expr != NULL) {
      continue;
    }
    if (signal->is_port && signal->dir == VLOG_DIR_INPUT) {
      set_error(ctx->error,
                ctx->error_size,
                "input port '%s' of module '%s' is unconnected",
                signal->name,
                ctx->module->name);
      return 0;
    }

    name = prefixed_name(ctx->prefix, signal->name);
    if (vlog_module_find_signal(ctx->flat, name) != NULL) {
      set_error(ctx->error,
                ctx->error_size,
                "flattened signal name '%s' collides with an existing signal",
                name);
      free(name);
      return 0;
    }
    {
      VlogRange range;

      if (!resolve_range(ctx, signal->range, 0, &range)) {
        free(name);
        return 0;
      }
      vlog_module_update_signal(ctx->flat,
                                name,
                                VLOG_DIR_NONE,
                                0,
                                signal->is_reg,
                                signal->is_signed,
                                range);
      vlog_range_free(&range);
    }
    free(name);
  }
  return 1;
}

static int copy_assigns(const ElabContext *ctx)
{
  const VlogAssign *assign;

  for (assign = ctx->module->assigns; assign != NULL; assign = assign->next) {
    VlogRef target;
    VlogExpr *expr;

    if (!rewrite_lvalue_ref(ctx, &assign->target, assign->line, &target)) {
      return 0;
    }
    expr = rewrite_expr(ctx, assign->expr);
    if (expr == NULL) {
      vlog_ref_free(&target);
      return 0;
    }
    vlog_module_add_assign(ctx->flat, target, expr, assign->line);
  }
  return 1;
}

static int copy_reg_drivers(const ElabContext *ctx)
{
  const VlogRegDriver *driver;

  for (driver = ctx->module->reg_drivers; driver != NULL; driver = driver->next) {
    VlogRef target;
    VlogExpr *guard;
    VlogExpr *expr;
    char *clock;

    if (!rewrite_lvalue_ref(ctx, &driver->target, driver->line, &target)) {
      return 0;
    }
    guard = rewrite_expr(ctx, driver->guard);
    if (driver->guard != NULL && guard == NULL) {
      vlog_ref_free(&target);
      return 0;
    }
    expr = rewrite_expr(ctx, driver->expr);
    if (expr == NULL) {
      vlog_ref_free(&target);
      vlog_expr_free(guard);
      return 0;
    }
    clock = rewrite_clock_name(ctx, driver->clock, driver->line);
    if (driver->clock != NULL && clock == NULL) {
      vlog_ref_free(&target);
      vlog_expr_free(guard);
      vlog_expr_free(expr);
      return 0;
    }

    vlog_module_add_reg_driver(ctx->flat,
                               target,
                               clock,
                               driver->clock_posedge,
                               guard,
                               expr,
                               driver->line);
    free(clock);
  }
  return 1;
}

static int module_on_stack(const ModuleStack *stack, const VlogModule *module)
{
  while (stack != NULL) {
    if (stack->module == module) {
      return 1;
    }
    stack = stack->next;
  }
  return 0;
}

static const VlogParamOverride *find_named_param_override(const VlogParamOverride *overrides,
                                                          const char *name)
{
  while (overrides != NULL) {
    if (overrides->is_named &&
        overrides->param_name != NULL &&
        strcmp(overrides->param_name, name) == 0) {
      return overrides;
    }
    overrides = overrides->next;
  }
  return NULL;
}

static const VlogParamOverride *find_positional_param_override(const VlogParamOverride *overrides,
                                                               int index)
{
  int current;

  current = 0;
  while (overrides != NULL) {
    if (!overrides->is_named) {
      if (current == index) {
        return overrides;
      }
      current++;
    }
    overrides = overrides->next;
  }
  return NULL;
}

static int check_param_overrides(const ElabContext *ctx,
                                 const VlogModule *child,
                                 const VlogInstance *instance)
{
  const VlogParamOverride *override;
  int has_named;
  int has_positional;

  has_named = 0;
  has_positional = 0;
  for (override = instance->param_overrides; override != NULL; override = override->next) {
    const VlogParamOverride *other;

    if (override->is_named) {
      const VlogParam *param;
      int found;

      has_named = 1;
      found = 0;
      for (param = child->parameters; param != NULL; param = param->next) {
        if (strcmp(param->name, override->param_name) == 0) {
          if (param->is_local) {
            set_error(ctx->error,
                      ctx->error_size,
                      "instance '%s' cannot override localparam '%s' on module '%s'",
                      instance->name,
                      override->param_name,
                      child->name);
            return 0;
          }
          found = 1;
          break;
        }
      }
      if (!found) {
        set_error(ctx->error,
                  ctx->error_size,
                  "instance '%s' overrides unknown parameter '%s' on module '%s'",
                  instance->name,
                  override->param_name,
                  child->name);
        return 0;
      }
      for (other = override->next; other != NULL; other = other->next) {
        if (other->is_named &&
            other->param_name != NULL &&
            strcmp(other->param_name, override->param_name) == 0) {
          set_error(ctx->error,
                    ctx->error_size,
                    "instance '%s' duplicates parameter override '%s'",
                    instance->name,
                    override->param_name);
          return 0;
        }
      }
    } else {
      has_positional = 1;
    }
  }

  if (has_named && has_positional) {
    set_error(ctx->error,
              ctx->error_size,
              "instance '%s' mixes named and positional parameter overrides",
              instance->name);
    return 0;
  }
  return 1;
}

static int build_param_bindings(const ElabContext *parent_ctx,
                                const VlogModule *child,
                                const VlogInstance *instance,
                                ParamBinding **params)
{
  const VlogParam *param;
  int index;

  *params = NULL;
  if (instance != NULL && !check_param_overrides(parent_ctx, child, instance)) {
    return 0;
  }

  index = 0;
  for (param = child->parameters; param != NULL; param = param->next) {
    const VlogParamOverride *override;
    const VlogIntExpr *expr;
    const ParamBinding *eval_params;
    int value;

    override = NULL;
    if (instance != NULL && !param->is_local) {
      override = find_named_param_override(instance->param_overrides, param->name);
      if (override == NULL) {
        override = find_positional_param_override(instance->param_overrides, index);
      }
    }

    if (override != NULL) {
      expr = override->expr;
      eval_params = parent_ctx->params;
    } else {
      expr = param->expr;
      eval_params = *params;
    }

    if (!eval_int_expr(eval_params,
                       expr,
                       &value,
                       parent_ctx->error,
                       parent_ctx->error_size)) {
      param_free(*params);
      *params = NULL;
      return 0;
    }
    if (!param_append(params,
                      param->name,
                      value,
                      parent_ctx->error,
                      parent_ctx->error_size)) {
      param_free(*params);
      *params = NULL;
      return 0;
    }
    if (!param->is_local) {
      index++;
    }
  }

  if (instance != NULL) {
    const VlogParamOverride *override;
    int positional_count;

    positional_count = 0;
    for (override = instance->param_overrides; override != NULL; override = override->next) {
      if (!override->is_named) {
        positional_count++;
      }
    }
    if (positional_count > index) {
      set_error(parent_ctx->error,
                parent_ctx->error_size,
                "instance '%s' has too many positional parameter overrides",
                instance->name);
      param_free(*params);
      *params = NULL;
      return 0;
    }
  }

  return 1;
}

static int inline_module(const ElabContext *ctx, const ModuleStack *stack);

static int child_port_requires_connection(const VlogSignal *signal)
{
  return signal != NULL &&
         (signal->dir == VLOG_DIR_INPUT || signal->dir == VLOG_DIR_INOUT);
}

static int add_missing_port_bindings(const VlogModule *child,
                                     PortBinding **bindings,
                                     const char *inst_name,
                                     char *error,
                                     unsigned int error_size)
{
  const VlogPort *port;

  for (port = child->ports; port != NULL; port = port->next) {
    const VlogSignal *signal;

    if (binding_find(*bindings, port->name) != NULL) {
      continue;
    }

    signal = find_signal_const(child, port->name);
    if (child_port_requires_connection(signal)) {
      set_error(error,
                error_size,
                "instance '%s' leaves input port '%s' unconnected",
                inst_name,
                port->name);
      return 0;
    }
    if (!binding_append(bindings, port->name, NULL, 0, error, error_size)) {
      return 0;
    }
  }
  return 1;
}

static int build_named_bindings(const ElabContext *ctx,
                                const VlogModule *child,
                                const VlogInstance *instance,
                                PortBinding **bindings)
{
  const VlogConn *conn;

  for (conn = instance->conns; conn != NULL; conn = conn->next) {
    VlogExpr *expr;

    if (conn->port_name == NULL ||
        find_port_const(child, conn->port_name) == NULL) {
      set_error(ctx->error,
                ctx->error_size,
                "instance '%s' connects unknown port '%s' on module '%s'",
                instance->name,
                conn->port_name == NULL ? "" : conn->port_name,
                child->name);
      return 0;
    }
    expr = conn->expr == NULL ? NULL : rewrite_expr(ctx, conn->expr);
    if (conn->expr != NULL && expr == NULL) {
      return 0;
    }
    if (!binding_append(bindings,
                        conn->port_name,
                        expr,
                        conn->line,
                        ctx->error,
                        ctx->error_size)) {
      return 0;
    }
  }

  return add_missing_port_bindings(child,
                                   bindings,
                                   instance->name,
                                   ctx->error,
                                   ctx->error_size);
}

static int build_positional_bindings(const ElabContext *ctx,
                                     const VlogModule *child,
                                     const VlogInstance *instance,
                                     PortBinding **bindings)
{
  const VlogPort *port;
  const VlogConn *conn;

  port = child->ports;
  conn = instance->conns;
  while (port != NULL && conn != NULL) {
    VlogExpr *expr;

    expr = conn->expr == NULL ? NULL : rewrite_expr(ctx, conn->expr);
    if (conn->expr != NULL && expr == NULL) {
      return 0;
    }
    if (!binding_append(bindings,
                        port->name,
                        expr,
                        conn->line,
                        ctx->error,
                        ctx->error_size)) {
      return 0;
    }
    port = port->next;
    conn = conn->next;
  }

  if (conn != NULL) {
    set_error(ctx->error,
              ctx->error_size,
              "instance '%s' has too many positional port connections",
              instance->name);
    return 0;
  }

  return add_missing_port_bindings(child,
                                   bindings,
                                   instance->name,
                                   ctx->error,
                                   ctx->error_size);
}

static int build_instance_bindings(const ElabContext *ctx,
                                   const VlogModule *child,
                                   const VlogInstance *instance,
                                   PortBinding **bindings)
{
  const VlogConn *conn;
  int has_named;
  int has_positional;

  *bindings = NULL;
  has_named = 0;
  has_positional = 0;
  for (conn = instance->conns; conn != NULL; conn = conn->next) {
    if (conn->is_named) {
      has_named = 1;
    } else {
      has_positional = 1;
    }
  }

  if (has_named && has_positional) {
    set_error(ctx->error,
              ctx->error_size,
              "instance '%s' mixes named and positional connections",
              instance->name);
    return 0;
  }

  if (has_named) {
    return build_named_bindings(ctx, child, instance, bindings);
  }
  return build_positional_bindings(ctx, child, instance, bindings);
}

static int inline_instance(const ElabContext *ctx,
                           const VlogInstance *instance,
                           const ModuleStack *stack)
{
  const VlogModule *child;
  PortBinding *bindings;
  ParamBinding *params;
  char *prefix;
  ElabContext child_ctx;
  int ok;

  child = vlog_design_find_module(ctx->design, instance->module_name);
  if (child == NULL) {
    set_error(ctx->error,
              ctx->error_size,
              "instance '%s' refers to unknown module '%s'",
              instance->name,
              instance->module_name);
    return 0;
  }
  if (module_on_stack(stack, child)) {
    set_error(ctx->error,
              ctx->error_size,
              "recursive hierarchy through module '%s' is not supported",
              child->name);
    return 0;
  }

  bindings = NULL;
  params = NULL;
  if (!build_param_bindings(ctx, child, instance, &params)) {
    return 0;
  }
  if (!build_instance_bindings(ctx, child, instance, &bindings)) {
    param_free(params);
    binding_free(bindings);
    return 0;
  }

  prefix = instance_prefix(ctx->prefix, instance->name);
  child_ctx.design = ctx->design;
  child_ctx.flat = ctx->flat;
  child_ctx.module = child;
  child_ctx.prefix = prefix;
  child_ctx.bindings = bindings;
  child_ctx.params = params;
  child_ctx.error = ctx->error;
  child_ctx.error_size = ctx->error_size;

  ok = inline_module(&child_ctx, stack);
  free(prefix);
  param_free(params);
  binding_free(bindings);
  return ok;
}

static int gen_cmp_holds(VlogGenCmp cmp, int value, int limit)
{
  if (cmp == VLOG_GEN_CMP_LT) return value < limit;
  if (cmp == VLOG_GEN_CMP_LE) return value <= limit;
  if (cmp == VLOG_GEN_CMP_GT) return value > limit;
  return value >= limit;
}

static char *gen_iteration_prefix(const char *parent_prefix,
                                  const char *block_name,
                                  int value)
{
  StrBuf sb;

  sb_init(&sb);
  if (parent_prefix != NULL) {
    sb_append(&sb, parent_prefix);
  }
  if (block_name != NULL && block_name[0] != '\0') {
    sb_appendf(&sb, "%s%d_", block_name, value);
  } else {
    sb_appendf(&sb, "gen%d_", value);
  }
  return sb_take(&sb);
}

static char *gen_named_prefix(const char *parent_prefix, const char *block_name)
{
  StrBuf sb;

  sb_init(&sb);
  if (parent_prefix != NULL) {
    sb_append(&sb, parent_prefix);
  }
  if (block_name != NULL && block_name[0] != '\0') {
    sb_appendf(&sb, "%s_", block_name);
  }
  return sb_take(&sb);
}

static char *gen_prefixed_instance_name(const char *gen_prefix, const char *name)
{
  StrBuf sb;

  sb_init(&sb);
  if (gen_prefix != NULL) {
    sb_append(&sb, gen_prefix);
  }
  sb_append(&sb, name);
  return sb_take(&sb);
}

static ParamBinding *param_push_temp(const ParamBinding *params,
                                     const char *name,
                                     int value)
{
  ParamBinding *node;

  node = (ParamBinding *)xmalloc(sizeof(ParamBinding));
  node->name = vlog_strdup(name);
  node->value = value;
  node->next = (ParamBinding *)params;
  return node;
}

static void param_pop_temp(ParamBinding *node)
{
  if (node == NULL) {
    return;
  }
  free(node->name);
  free(node);
}

static int process_generate_list(const ElabContext *ctx,
                                 const VlogGenerate *items,
                                 const ModuleStack *stack,
                                 const char *gen_prefix);

static int process_generate_for(const ElabContext *ctx,
                                const VlogGenerate *item,
                                const ModuleStack *stack,
                                const char *gen_prefix)
{
  int value;
  int limit;
  int step;
  int guard;

  if (!eval_int_expr(ctx->params,
                     item->init_expr,
                     &value,
                     ctx->error,
                     ctx->error_size) ||
      !eval_int_expr(ctx->params,
                     item->limit_expr,
                     &limit,
                     ctx->error,
                     ctx->error_size) ||
      !eval_int_expr(ctx->params,
                     item->step_expr,
                     &step,
                     ctx->error,
                     ctx->error_size)) {
    return 0;
  }
  if (step <= 0) {
    set_error(ctx->error,
              ctx->error_size,
              "line %d: generate for step must be positive before applying its sign",
              item->line);
    return 0;
  }
  step *= item->step_sign < 0 ? -1 : 1;
  if (step == 0) {
    set_error(ctx->error, ctx->error_size, "line %d: generate for step is zero", item->line);
    return 0;
  }

  guard = 0;
  while (gen_cmp_holds(item->cmp, value, limit)) {
    ParamBinding *genvar_binding;
    ElabContext loop_ctx;
    char *iter_prefix;
    int ok;

    if (++guard > 10000) {
      set_error(ctx->error,
                ctx->error_size,
                "line %d: generate for appears not to converge",
                item->line);
      return 0;
    }

    genvar_binding = param_push_temp(ctx->params, item->genvar, value);
    loop_ctx = *ctx;
    loop_ctx.params = genvar_binding;
    iter_prefix = gen_iteration_prefix(gen_prefix, item->block_name, value);
    ok = process_generate_list(&loop_ctx, item->body, stack, iter_prefix);
    free(iter_prefix);
    param_pop_temp(genvar_binding);
    if (!ok) {
      return 0;
    }
    value += step;
  }
  return 1;
}

static int process_generate_if(const ElabContext *ctx,
                               const VlogGenerate *item,
                               const ModuleStack *stack,
                               const char *gen_prefix)
{
  int cond;
  const VlogGenerate *body;
  const char *block_name;
  char *next_prefix;
  int ok;

  if (!eval_int_expr(ctx->params,
                     item->cond_expr,
                     &cond,
                     ctx->error,
                     ctx->error_size)) {
    return 0;
  }

  if (cond != 0) {
    body = item->body;
    block_name = item->block_name;
  } else {
    body = item->else_body;
    block_name = item->else_block_name;
  }
  if (body == NULL) {
    return 1;
  }

  next_prefix = gen_named_prefix(gen_prefix, block_name);
  ok = process_generate_list(ctx, body, stack, next_prefix);
  free(next_prefix);
  return ok;
}

static int process_generate_case(const ElabContext *ctx,
                                 const VlogGenerate *item,
                                 const ModuleStack *stack,
                                 const char *gen_prefix)
{
  const VlogGenCaseItem *case_item;
  const VlogGenerate *body;
  const char *block_name;
  char *next_prefix;
  int selector;
  int ok;

  if (!eval_int_expr(ctx->params,
                     item->case_expr,
                     &selector,
                     ctx->error,
                     ctx->error_size)) {
    return 0;
  }

  body = NULL;
  block_name = NULL;
  for (case_item = item->case_items; case_item != NULL; case_item = case_item->next) {
    int label;

    if (!eval_int_expr(ctx->params,
                       case_item->label_expr,
                       &label,
                       ctx->error,
                       ctx->error_size)) {
      return 0;
    }
    if (label == selector) {
      body = case_item->body;
      block_name = case_item->block_name;
      break;
    }
  }
  if (body == NULL) {
    body = item->default_body;
    block_name = item->default_block_name;
  }
  if (body == NULL) {
    return 1;
  }

  next_prefix = gen_named_prefix(gen_prefix, block_name);
  ok = process_generate_list(ctx, body, stack, next_prefix);
  free(next_prefix);
  return ok;
}

static int process_generate_list(const ElabContext *ctx,
                                 const VlogGenerate *items,
                                 const ModuleStack *stack,
                                 const char *gen_prefix)
{
  const VlogGenerate *item;

  for (item = items; item != NULL; item = item->next) {
    if (item->kind == VLOG_GEN_ASSIGN) {
      VlogRef target;
      VlogExpr *expr;

      if (!rewrite_lvalue_ref(ctx, &item->target, item->line, &target)) {
        return 0;
      }
      expr = rewrite_expr(ctx, item->expr);
      if (expr == NULL) {
        vlog_ref_free(&target);
        return 0;
      }
      vlog_module_add_assign(ctx->flat, target, expr, item->line);
    } else if (item->kind == VLOG_GEN_INSTANCE) {
      VlogInstance instance;
      char *name;
      int ok;

      name = gen_prefixed_instance_name(gen_prefix, item->instance_name);
      instance.module_name = item->module_name;
      instance.name = name;
      instance.param_overrides = item->param_overrides;
      instance.conns = item->conns;
      instance.line = item->line;
      instance.next = NULL;
      ok = inline_instance(ctx, &instance, stack);
      free(name);
      if (!ok) {
        return 0;
      }
    } else if (item->kind == VLOG_GEN_FOR) {
      if (!process_generate_for(ctx, item, stack, gen_prefix)) {
        return 0;
      }
    } else if (item->kind == VLOG_GEN_IF) {
      if (!process_generate_if(ctx, item, stack, gen_prefix)) {
        return 0;
      }
    } else if (item->kind == VLOG_GEN_CASE) {
      if (!process_generate_case(ctx, item, stack, gen_prefix)) {
        return 0;
      }
    }
  }
  return 1;
}

static int inline_instances(const ElabContext *ctx, const ModuleStack *stack)
{
  const VlogInstance *instance;

  for (instance = ctx->module->instances; instance != NULL; instance = instance->next) {
    if (!inline_instance(ctx, instance, stack)) {
      return 0;
    }
  }
  return 1;
}

static int inline_module(const ElabContext *ctx, const ModuleStack *stack)
{
  ModuleStack node;

  node.module = ctx->module;
  node.next = stack;

  if (ctx->prefix != NULL && ctx->prefix[0] != '\0') {
    if (!add_local_child_signals(ctx)) {
      return 0;
    }
  }
  if (!copy_assigns(ctx)) {
    return 0;
  }
  if (!copy_reg_drivers(ctx)) {
    return 0;
  }
  if (!process_generate_list(ctx, ctx->module->generates, &node, "")) {
    return 0;
  }
  return inline_instances(ctx, &node);
}

static int module_is_instantiated(const VlogDesign *design, const char *module_name)
{
  const VlogModule *module;

  for (module = design->modules; module != NULL; module = module->next) {
    const VlogInstance *instance;

    for (instance = module->instances; instance != NULL; instance = instance->next) {
      if (strcmp(instance->module_name, module_name) == 0) {
        return 1;
      }
    }
  }
  return 0;
}

static const VlogModule *select_top(const VlogDesign *design,
                                    const char *top_name,
                                    char *error,
                                    unsigned int error_size)
{
  const VlogModule *module;
  const VlogModule *candidate;
  int count;

  if (top_name != NULL) {
    module = vlog_design_find_module(design, top_name);
    if (module == NULL) {
      set_error(error, error_size, "requested top module '%s' was not found", top_name);
    }
    return module;
  }

  count = 0;
  candidate = NULL;
  for (module = design->modules; module != NULL; module = module->next) {
    count++;
    candidate = module;
  }
  if (count == 0) {
    set_error(error, error_size, "no module found");
    return NULL;
  }
  if (count == 1) {
    return candidate;
  }

  candidate = NULL;
  for (module = design->modules; module != NULL; module = module->next) {
    if (!module_is_instantiated(design, module->name)) {
      if (candidate != NULL) {
        set_error(error,
                  error_size,
                  "multiple possible top modules; pass -top <module>");
        return NULL;
      }
      candidate = module;
    }
  }
  if (candidate == NULL) {
    set_error(error, error_size, "cannot infer top module; pass -top <module>");
  }
  return candidate;
}

int vlog_elaborate_design(const VlogDesign *design,
                          const char *top_name,
                          VlogModule *flat,
                          char *error,
                          unsigned int error_size)
{
  const VlogModule *top;
  ElabContext ctx;
  ParamBinding *params;

  top = select_top(design, top_name, error, error_size);
  if (top == NULL) {
    return 0;
  }

  vlog_module_init(flat);
  flat->name = vlog_strdup(top->name);

  ctx.design = design;
  ctx.flat = flat;
  ctx.module = top;
  ctx.prefix = "";
  ctx.bindings = NULL;
  ctx.params = NULL;
  ctx.error = error;
  ctx.error_size = error_size;

  params = NULL;
  if (!build_param_bindings(&ctx, top, NULL, &params)) {
    vlog_module_free(flat);
    return 0;
  }
  ctx.params = params;

  if (!copy_top_signals(&ctx)) {
    param_free(params);
    vlog_module_free(flat);
    return 0;
  }

  if (!inline_module(&ctx, NULL)) {
    param_free(params);
    vlog_module_free(flat);
    return 0;
  }
  param_free(params);
  return 1;
}
