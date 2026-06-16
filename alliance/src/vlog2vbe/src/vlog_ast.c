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
  module->instances = NULL;
  module->reg_drivers = NULL;
  module->next = NULL;
}

VlogModule *vlog_module_new(void)
{
  VlogModule *module;

  module = (VlogModule *)vlog_xmalloc(sizeof(VlogModule));
  vlog_module_init(module);
  return module;
}

void vlog_design_init(VlogDesign *design)
{
  design->modules = NULL;
}

VlogModule *vlog_design_find_module(const VlogDesign *design, const char *name)
{
  VlogModule *module;

  for (module = design->modules; module != NULL; module = module->next) {
    if (module->name != NULL && strcmp(module->name, name) == 0) {
      return module;
    }
  }
  return NULL;
}

int vlog_design_add_module(VlogDesign *design, VlogModule *module)
{
  VlogModule **tail;

  tail = &design->modules;
  while (*tail != NULL) {
    tail = &(*tail)->next;
  }
  module->next = NULL;
  *tail = module;
  return 1;
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

void vlog_conn_free(VlogConn *conn)
{
  VlogConn *next;

  while (conn != NULL) {
    next = conn->next;
    free(conn->port_name);
    vlog_expr_free(conn->expr);
    free(conn);
    conn = next;
  }
}

static void vlog_instance_free(VlogInstance *instance)
{
  VlogInstance *next;

  while (instance != NULL) {
    next = instance->next;
    free(instance->module_name);
    free(instance->name);
    vlog_conn_free(instance->conns);
    free(instance);
    instance = next;
  }
}

static void vlog_reg_driver_free(VlogRegDriver *driver)
{
  VlogRegDriver *next;

  while (driver != NULL) {
    next = driver->next;
    vlog_ref_free(&driver->target);
    free(driver->clock);
    vlog_expr_free(driver->guard);
    vlog_expr_free(driver->expr);
    free(driver);
    driver = next;
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
  vlog_instance_free(module->instances);
  vlog_reg_driver_free(module->reg_drivers);
  vlog_module_init(module);
}

void vlog_design_free(VlogDesign *design)
{
  VlogModule *module;
  VlogModule *next;

  if (design == NULL) {
    return;
  }

  module = design->modules;
  while (module != NULL) {
    next = module->next;
    module->next = NULL;
    vlog_module_free(module);
    free(module);
    module = next;
  }
  vlog_design_init(design);
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

int vlog_module_add_instance(VlogModule *module,
                             const char *module_name,
                             const char *name,
                             VlogConn *conns,
                             int line)
{
  VlogInstance *instance;
  VlogInstance **tail;

  instance = (VlogInstance *)vlog_xmalloc(sizeof(VlogInstance));
  instance->module_name = vlog_strdup(module_name);
  instance->name = vlog_strdup(name);
  instance->conns = conns;
  instance->line = line;
  instance->next = NULL;

  tail = &module->instances;
  while (*tail != NULL) {
    tail = &(*tail)->next;
  }
  *tail = instance;
  return 1;
}

int vlog_module_add_reg_driver(VlogModule *module,
                               VlogRef target,
                               const char *clock,
                               int clock_posedge,
                               VlogExpr *guard,
                               VlogExpr *expr,
                               int line)
{
  VlogRegDriver *driver;
  VlogRegDriver **tail;

  driver = (VlogRegDriver *)vlog_xmalloc(sizeof(VlogRegDriver));
  driver->target = target;
  driver->clock = vlog_strdup(clock);
  driver->clock_posedge = clock_posedge;
  driver->guard = guard;
  driver->expr = expr;
  driver->line = line;
  driver->next = NULL;

  tail = &module->reg_drivers;
  while (*tail != NULL) {
    tail = &(*tail)->next;
  }
  *tail = driver;
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

static VlogRef vlog_ref_clone(const VlogRef *ref)
{
  VlogRef copy;

  copy.name = vlog_strdup(ref->name);
  copy.has_select = ref->has_select;
  copy.select_msb = ref->select_msb;
  copy.select_lsb = ref->select_lsb;
  return copy;
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

VlogConn *vlog_conn_append(VlogConn *list,
                           const char *port_name,
                           int is_named,
                           VlogExpr *expr,
                           int line)
{
  VlogConn *node;
  VlogConn **tail;

  node = (VlogConn *)vlog_xmalloc(sizeof(VlogConn));
  node->port_name = vlog_strdup(port_name);
  node->is_named = is_named;
  node->expr = expr;
  node->line = line;
  node->next = NULL;

  tail = &list;
  while (*tail != NULL) {
    tail = &(*tail)->next;
  }
  *tail = node;
  return list;
}

static VlogExprList *vlog_expr_list_clone(const VlogExprList *list)
{
  VlogExprList *copy;

  copy = NULL;
  while (list != NULL) {
    copy = vlog_expr_list_append(copy, vlog_expr_clone(list->expr));
    list = list->next;
  }
  return copy;
}

VlogExpr *vlog_expr_clone(const VlogExpr *expr)
{
  VlogExpr *copy;

  if (expr == NULL) {
    return NULL;
  }

  copy = vlog_expr_alloc(expr->kind, expr->line);
  copy->op = expr->op;
  if (expr->ref.name != NULL) {
    copy->ref = vlog_ref_clone(&expr->ref);
  }
  copy->text = vlog_strdup(expr->text);
  copy->left = vlog_expr_clone(expr->left);
  copy->right = vlog_expr_clone(expr->right);
  copy->third = vlog_expr_clone(expr->third);
  copy->items = vlog_expr_list_clone(expr->items);
  return copy;
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
