#include "vlog_ast.h"

#include <stdlib.h>
#include <string.h>

static void *vlog_xmalloc(unsigned int size)
{
  void *ptr;

  ptr = malloc(size == 0 ? 1 : size);
  if (ptr == NULL) {
    fprintf(stderr, "vlog2vbe: out of memory\n");
    exit(2);
  }
  return ptr;
}

char *vlog_strdup(const char *text)
{
  if (text == NULL) {
    return NULL;
  }
  return vlog_strndup(text, (unsigned int)strlen(text));
}

char *vlog_strndup(const char *text, unsigned int length)
{
  char *copy;

  copy = (char *)vlog_xmalloc(length + 1);
  memcpy(copy, text, length);
  copy[length] = '\0';
  return copy;
}

VlogRange vlog_range_none(void)
{
  VlogRange range;

  range.has_range = 0;
  range.msb = 0;
  range.lsb = 0;
  return range;
}

void vlog_module_init(VlogModule *module)
{
  module->name = NULL;
  module->signals = NULL;
  module->ports = NULL;
  module->assigns = NULL;
}

void vlog_ref_free(VlogRef *ref)
{
  if (ref->name != NULL) {
    free(ref->name);
  }
  ref->name = NULL;
  ref->has_select = 0;
  ref->select_msb = 0;
  ref->select_lsb = 0;
}

static void vlog_signal_free(VlogSignal *signal)
{
  VlogSignal *next;

  while (signal != NULL) {
    next = signal->next;
    free(signal->name);
    free(signal);
    signal = next;
  }
}

static void vlog_port_free(VlogPort *port)
{
  VlogPort *next;

  while (port != NULL) {
    next = port->next;
    free(port->name);
    free(port);
    port = next;
  }
}

static void vlog_assign_free(VlogAssign *assign)
{
  VlogAssign *next;

  while (assign != NULL) {
    next = assign->next;
    vlog_ref_free(&assign->target);
    vlog_expr_free(assign->expr);
    free(assign);
    assign = next;
  }
}

void vlog_module_free(VlogModule *module)
{
  if (module == NULL) {
    return;
  }
  free(module->name);
  vlog_signal_free(module->signals);
  vlog_port_free(module->ports);
  vlog_assign_free(module->assigns);
  vlog_module_init(module);
}

VlogSignal *vlog_module_find_signal(VlogModule *module, const char *name)
{
  VlogSignal *signal;

  for (signal = module->signals; signal != NULL; signal = signal->next) {
    if (strcmp(signal->name, name) == 0) {
      return signal;
    }
  }
  return NULL;
}

VlogSignal *vlog_module_ensure_signal(VlogModule *module, const char *name)
{
  VlogSignal *signal;
  VlogSignal **tail;

  signal = vlog_module_find_signal(module, name);
  if (signal != NULL) {
    return signal;
  }

  signal = (VlogSignal *)vlog_xmalloc(sizeof(VlogSignal));
  signal->name = vlog_strdup(name);
  signal->dir = VLOG_DIR_NONE;
  signal->is_port = 0;
  signal->is_reg = 0;
  signal->range = vlog_range_none();
  signal->next = NULL;

  tail = &module->signals;
  while (*tail != NULL) {
    tail = &(*tail)->next;
  }
  *tail = signal;
  return signal;
}

static int vlog_module_has_port(VlogModule *module, const char *name)
{
  VlogPort *port;

  for (port = module->ports; port != NULL; port = port->next) {
    if (strcmp(port->name, name) == 0) {
      return 1;
    }
  }
  return 0;
}

int vlog_module_add_port(VlogModule *module, const char *name)
{
  VlogPort *port;
  VlogPort **tail;

  if (vlog_module_has_port(module, name)) {
    return 1;
  }

  port = (VlogPort *)vlog_xmalloc(sizeof(VlogPort));
  port->name = vlog_strdup(name);
  port->next = NULL;

  tail = &module->ports;
  while (*tail != NULL) {
    tail = &(*tail)->next;
  }
  *tail = port;
  return 1;
}

int vlog_module_update_signal(VlogModule *module,
                              const char *name,
                              VlogDir dir,
                              int is_port,
                              int is_reg,
                              VlogRange range)
{
  VlogSignal *signal;

  signal = vlog_module_ensure_signal(module, name);
  if (dir != VLOG_DIR_NONE) {
    signal->dir = dir;
  }
  if (is_port) {
    signal->is_port = 1;
  }
  if (is_reg) {
    signal->is_reg = 1;
  }
  if (range.has_range) {
    signal->range = range;
  }
  if (is_port) {
    vlog_module_add_port(module, name);
  }
  return 1;
}

int vlog_module_add_assign(VlogModule *module,
                           VlogRef target,
                           VlogExpr *expr,
                           int line)
{
  VlogAssign *assign;
  VlogAssign **tail;

  assign = (VlogAssign *)vlog_xmalloc(sizeof(VlogAssign));
  assign->target = target;
  assign->expr = expr;
  assign->line = line;
  assign->next = NULL;

  tail = &module->assigns;
  while (*tail != NULL) {
    tail = &(*tail)->next;
  }
  *tail = assign;
  return 1;
}

VlogRef vlog_ref_make(const char *name)
{
  VlogRef ref;

  ref.name = vlog_strdup(name);
  ref.has_select = 0;
  ref.select_msb = 0;
  ref.select_lsb = 0;
  return ref;
}

static VlogExpr *vlog_expr_alloc(VlogExprKind kind, int line)
{
  VlogExpr *expr;

  expr = (VlogExpr *)vlog_xmalloc(sizeof(VlogExpr));
  expr->kind = kind;
  expr->op = VLOG_OP_NONE;
  expr->ref.name = NULL;
  expr->ref.has_select = 0;
  expr->ref.select_msb = 0;
  expr->ref.select_lsb = 0;
  expr->text = NULL;
  expr->line = line;
  expr->left = NULL;
  expr->right = NULL;
  expr->third = NULL;
  expr->items = NULL;
  return expr;
}

VlogExpr *vlog_expr_ref(VlogRef ref, int line)
{
  VlogExpr *expr;

  expr = vlog_expr_alloc(VLOG_EXPR_REF, line);
  expr->ref = ref;
  return expr;
}

VlogExpr *vlog_expr_const(const char *text, int line)
{
  VlogExpr *expr;

  expr = vlog_expr_alloc(VLOG_EXPR_CONST, line);
  expr->text = vlog_strdup(text);
  return expr;
}

VlogExpr *vlog_expr_unary(VlogOp op, VlogExpr *child, int line)
{
  VlogExpr *expr;

  expr = vlog_expr_alloc(VLOG_EXPR_UNARY, line);
  expr->op = op;
  expr->left = child;
  return expr;
}

VlogExpr *vlog_expr_binary(VlogOp op, VlogExpr *left, VlogExpr *right, int line)
{
  VlogExpr *expr;

  expr = vlog_expr_alloc(VLOG_EXPR_BINARY, line);
  expr->op = op;
  expr->left = left;
  expr->right = right;
  return expr;
}

VlogExpr *vlog_expr_ternary(VlogExpr *cond,
                            VlogExpr *true_expr,
                            VlogExpr *false_expr,
                            int line)
{
  VlogExpr *expr;

  expr = vlog_expr_alloc(VLOG_EXPR_TERNARY, line);
  expr->left = cond;
  expr->right = true_expr;
  expr->third = false_expr;
  return expr;
}

VlogExpr *vlog_expr_concat(VlogExprList *items, int line)
{
  VlogExpr *expr;

  expr = vlog_expr_alloc(VLOG_EXPR_CONCAT, line);
  expr->items = items;
  return expr;
}

VlogExprList *vlog_expr_list_append(VlogExprList *list, VlogExpr *expr)
{
  VlogExprList *node;
  VlogExprList **tail;

  node = (VlogExprList *)vlog_xmalloc(sizeof(VlogExprList));
  node->expr = expr;
  node->next = NULL;

  tail = &list;
  while (*tail != NULL) {
    tail = &(*tail)->next;
  }
  *tail = node;
  return list;
}

void vlog_expr_list_free(VlogExprList *list)
{
  VlogExprList *next;

  while (list != NULL) {
    next = list->next;
    vlog_expr_free(list->expr);
    free(list);
    list = next;
  }
}

void vlog_expr_free(VlogExpr *expr)
{
  if (expr == NULL) {
    return;
  }
  vlog_ref_free(&expr->ref);
  free(expr->text);
  vlog_expr_free(expr->left);
  vlog_expr_free(expr->right);
  vlog_expr_free(expr->third);
  vlog_expr_list_free(expr->items);
  free(expr);
}
