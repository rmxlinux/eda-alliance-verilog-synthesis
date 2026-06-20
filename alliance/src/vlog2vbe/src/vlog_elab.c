#include "vlog_elab.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct PortBinding {
  char *port_name;
  VlogExpr *expr;
  int line;
  struct PortBinding *next;
} PortBinding;

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
  return out;
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
  char *name;
  VlogRef out_ref;
  VlogExpr *out;

  binding = binding_find_const(ctx->bindings, ref->name);
  if (binding != NULL && binding->expr != NULL) {
    out = vlog_expr_clone(binding->expr);
    if (ref->has_select) {
      if (out->kind != VLOG_EXPR_REF) {
        set_error(ctx->error,
                  ctx->error_size,
                  "line %d: selected port '%s' is connected to a non-reference expression",
                  line,
                  ref->name);
        vlog_expr_free(out);
        return NULL;
      }
      if (!apply_child_select(&out->ref,
                              ref,
                              line,
                              ctx->error,
                              ctx->error_size)) {
        vlog_expr_free(out);
        return NULL;
      }
    }
    return out;
  }

  name = prefixed_name(ctx->prefix, ref->name);
  out_ref = ref_clone_with_name(ref, name);
  free(name);
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
  char *name;

  binding = binding_find_const(ctx->bindings, ref->name);
  if (binding != NULL && binding->expr != NULL) {
    if (binding->expr->kind != VLOG_EXPR_REF) {
      set_error(ctx->error,
                ctx->error_size,
                "line %d: output port '%s' is connected to a non-assignable expression",
                line,
                ref->name);
      return 0;
    }
    *out = ref_clone_with_name(&binding->expr->ref, binding->expr->ref.name);
    if (!apply_child_select(out, ref, line, ctx->error, ctx->error_size)) {
      vlog_ref_free(out);
      return 0;
    }
    return 1;
  }

  name = prefixed_name(ctx->prefix, ref->name);
  *out = ref_clone_with_name(ref, name);
  free(name);
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

static int copy_top_signals(const VlogModule *top, VlogModule *flat)
{
  const VlogPort *port;
  const VlogSignal *signal;

  for (port = top->ports; port != NULL; port = port->next) {
    signal = find_signal_const(top, port->name);
    if (signal != NULL) {
      vlog_module_update_signal(flat,
                                signal->name,
                                signal->dir,
                                1,
                                signal->is_reg,
                                signal->range);
    }
  }

  for (signal = top->signals; signal != NULL; signal = signal->next) {
    if (!signal->is_port) {
      vlog_module_update_signal(flat,
                                signal->name,
                                VLOG_DIR_NONE,
                                0,
                                signal->is_reg,
                                signal->range);
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
    vlog_module_update_signal(ctx->flat,
                              name,
                              VLOG_DIR_NONE,
                              0,
                              signal->is_reg,
                              signal->range);
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
  if (!build_instance_bindings(ctx, child, instance, &bindings)) {
    binding_free(bindings);
    return 0;
  }

  prefix = instance_prefix(ctx->prefix, instance->name);
  child_ctx.design = ctx->design;
  child_ctx.flat = ctx->flat;
  child_ctx.module = child;
  child_ctx.prefix = prefix;
  child_ctx.bindings = bindings;
  child_ctx.error = ctx->error;
  child_ctx.error_size = ctx->error_size;

  ok = inline_module(&child_ctx, stack);
  free(prefix);
  binding_free(bindings);
  return ok;
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

  top = select_top(design, top_name, error, error_size);
  if (top == NULL) {
    return 0;
  }

  vlog_module_init(flat);
  flat->name = vlog_strdup(top->name);
  copy_top_signals(top, flat);

  ctx.design = design;
  ctx.flat = flat;
  ctx.module = top;
  ctx.prefix = "";
  ctx.bindings = NULL;
  ctx.error = error;
  ctx.error_size = error_size;

  if (!inline_module(&ctx, NULL)) {
    vlog_module_free(flat);
    return 0;
  }
  return 1;
}
