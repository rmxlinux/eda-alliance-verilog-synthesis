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
  range.msb_expr = NULL;
  range.lsb_expr = NULL;
  return range;
}

VlogRange vlog_range_clone(const VlogRange *range)
{
  VlogRange copy;

  copy.has_range = range->has_range;
  copy.msb = range->msb;
  copy.lsb = range->lsb;
  copy.msb_expr = vlog_int_expr_clone(range->msb_expr);
  copy.lsb_expr = vlog_int_expr_clone(range->lsb_expr);
  return copy;
}

void vlog_range_free(VlogRange *range)
{
  if (range == NULL) {
    return;
  }
  vlog_int_expr_free(range->msb_expr);
  vlog_int_expr_free(range->lsb_expr);
  range->msb_expr = NULL;
  range->lsb_expr = NULL;
  range->has_range = 0;
  range->msb = 0;
  range->lsb = 0;
}

void vlog_module_init(VlogModule *module)
{
  module->name = NULL;
  module->parameters = NULL;
  module->signals = NULL;
  module->ports = NULL;
  module->assigns = NULL;
  module->instances = NULL;
  module->generates = NULL;
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
  vlog_int_expr_free(ref->select_msb_expr);
  vlog_int_expr_free(ref->select_lsb_expr);
  ref->name = NULL;
  ref->has_select = 0;
  ref->select_msb = 0;
  ref->select_lsb = 0;
  ref->select_msb_expr = NULL;
  ref->select_lsb_expr = NULL;
}

static void vlog_param_free(VlogParam *param)
{
  VlogParam *next;

  while (param != NULL) {
    next = param->next;
    free(param->name);
    vlog_int_expr_free(param->expr);
    free(param);
    param = next;
  }
}

static void vlog_signal_free(VlogSignal *signal)
{
  VlogSignal *next;

  while (signal != NULL) {
    next = signal->next;
    free(signal->name);
    vlog_range_free(&signal->range);
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
    vlog_param_override_free(instance->param_overrides);
    vlog_conn_free(instance->conns);
    free(instance);
    instance = next;
  }
}

void vlog_generate_free(VlogGenerate *generate)
{
  VlogGenerate *next;

  while (generate != NULL) {
    next = generate->next;
    vlog_ref_free(&generate->target);
    vlog_expr_free(generate->expr);
    free(generate->module_name);
    free(generate->instance_name);
    vlog_param_override_free(generate->param_overrides);
    vlog_conn_free(generate->conns);
    free(generate->genvar);
    vlog_int_expr_free(generate->init_expr);
    vlog_int_expr_free(generate->limit_expr);
    vlog_int_expr_free(generate->step_expr);
    vlog_int_expr_free(generate->cond_expr);
    vlog_int_expr_free(generate->case_expr);
    free(generate->block_name);
    free(generate->else_block_name);
    vlog_generate_free(generate->body);
    vlog_generate_free(generate->else_body);
    vlog_gen_case_item_free(generate->case_items);
    vlog_generate_free(generate->default_body);
    free(generate->default_block_name);
    free(generate);
    generate = next;
  }
}

void vlog_gen_case_item_free(VlogGenCaseItem *item)
{
  VlogGenCaseItem *next;

  while (item != NULL) {
    next = item->next;
    vlog_int_expr_free(item->label_expr);
    free(item->block_name);
    vlog_generate_free(item->body);
    free(item);
    item = next;
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
  vlog_param_free(module->parameters);
  vlog_signal_free(module->signals);
  vlog_port_free(module->ports);
  vlog_assign_free(module->assigns);
  vlog_instance_free(module->instances);
  vlog_generate_free(module->generates);
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
  signal->is_signed = 0;
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

int vlog_module_add_param(VlogModule *module,
                          const char *name,
                          VlogIntExpr *expr,
                          int is_local,
                          int line)
{
  VlogParam *param;
  VlogParam **tail;

  param = (VlogParam *)vlog_xmalloc(sizeof(VlogParam));
  param->name = vlog_strdup(name);
  param->expr = expr;
  param->is_local = is_local;
  param->line = line;
  param->next = NULL;

  tail = &module->parameters;
  while (*tail != NULL) {
    tail = &(*tail)->next;
  }
  *tail = param;
  return 1;
}

static VlogGenerate *vlog_generate_alloc(VlogGenKind kind, int line)
{
  VlogGenerate *item;

  item = (VlogGenerate *)vlog_xmalloc(sizeof(VlogGenerate));
  item->kind = kind;
  item->target.name = NULL;
  item->target.has_select = 0;
  item->target.select_msb = 0;
  item->target.select_lsb = 0;
  item->target.select_msb_expr = NULL;
  item->target.select_lsb_expr = NULL;
  item->expr = NULL;
  item->module_name = NULL;
  item->instance_name = NULL;
  item->param_overrides = NULL;
  item->conns = NULL;
  item->genvar = NULL;
  item->init_expr = NULL;
  item->limit_expr = NULL;
  item->step_expr = NULL;
  item->cond_expr = NULL;
  item->case_expr = NULL;
  item->step_sign = 1;
  item->cmp = VLOG_GEN_CMP_LT;
  item->block_name = NULL;
  item->else_block_name = NULL;
  item->body = NULL;
  item->else_body = NULL;
  item->case_items = NULL;
  item->default_body = NULL;
  item->default_block_name = NULL;
  item->line = line;
  item->next = NULL;
  return item;
}

static VlogGenerate *vlog_generate_append_node(VlogGenerate *list,
                                               VlogGenerate *node)
{
  VlogGenerate **tail;

  tail = &list;
  while (*tail != NULL) {
    tail = &(*tail)->next;
  }
  *tail = node;
  return list;
}

VlogGenerate *vlog_generate_append_assign(VlogGenerate *list,
                                          VlogRef target,
                                          VlogExpr *expr,
                                          int line)
{
  VlogGenerate *item;

  item = vlog_generate_alloc(VLOG_GEN_ASSIGN, line);
  item->target = target;
  item->expr = expr;
  return vlog_generate_append_node(list, item);
}

VlogGenerate *vlog_generate_append_instance(VlogGenerate *list,
                                            const char *module_name,
                                            const char *instance_name,
                                            VlogParamOverride *param_overrides,
                                            VlogConn *conns,
                                            int line)
{
  VlogGenerate *item;

  item = vlog_generate_alloc(VLOG_GEN_INSTANCE, line);
  item->module_name = vlog_strdup(module_name);
  item->instance_name = vlog_strdup(instance_name);
  item->param_overrides = param_overrides;
  item->conns = conns;
  return vlog_generate_append_node(list, item);
}

VlogGenerate *vlog_generate_append_for(VlogGenerate *list,
                                       const char *genvar,
                                       VlogIntExpr *init_expr,
                                       VlogGenCmp cmp,
                                       VlogIntExpr *limit_expr,
                                       int step_sign,
                                       VlogIntExpr *step_expr,
                                       const char *block_name,
                                       VlogGenerate *body,
                                       int line)
{
  VlogGenerate *item;

  item = vlog_generate_alloc(VLOG_GEN_FOR, line);
  item->genvar = vlog_strdup(genvar);
  item->init_expr = init_expr;
  item->cmp = cmp;
  item->limit_expr = limit_expr;
  item->step_sign = step_sign;
  item->step_expr = step_expr;
  item->block_name = vlog_strdup(block_name);
  item->body = body;
  return vlog_generate_append_node(list, item);
}

VlogGenerate *vlog_generate_append_if(VlogGenerate *list,
                                      VlogIntExpr *cond_expr,
                                      const char *block_name,
                                      VlogGenerate *body,
                                      const char *else_block_name,
                                      VlogGenerate *else_body,
                                      int line)
{
  VlogGenerate *item;

  item = vlog_generate_alloc(VLOG_GEN_IF, line);
  item->cond_expr = cond_expr;
  item->block_name = vlog_strdup(block_name);
  item->body = body;
  item->else_block_name = vlog_strdup(else_block_name);
  item->else_body = else_body;
  return vlog_generate_append_node(list, item);
}

VlogGenCaseItem *vlog_gen_case_item_append(VlogGenCaseItem *list,
                                           VlogIntExpr *label_expr,
                                           const char *block_name,
                                           VlogGenerate *body)
{
  VlogGenCaseItem *item;
  VlogGenCaseItem **tail;

  item = (VlogGenCaseItem *)vlog_xmalloc(sizeof(VlogGenCaseItem));
  item->label_expr = label_expr;
  item->block_name = vlog_strdup(block_name);
  item->body = body;
  item->next = NULL;

  tail = &list;
  while (*tail != NULL) {
    tail = &(*tail)->next;
  }
  *tail = item;
  return list;
}

VlogGenerate *vlog_generate_append_case(VlogGenerate *list,
                                        VlogIntExpr *case_expr,
                                        VlogGenCaseItem *case_items,
                                        const char *default_block_name,
                                        VlogGenerate *default_body,
                                        int line)
{
  VlogGenerate *item;

  item = vlog_generate_alloc(VLOG_GEN_CASE, line);
  item->case_expr = case_expr;
  item->case_items = case_items;
  item->default_block_name = vlog_strdup(default_block_name);
  item->default_body = default_body;
  return vlog_generate_append_node(list, item);
}

int vlog_module_add_generate(VlogModule *module, VlogGenerate *items)
{
  VlogGenerate **tail;

  if (items == NULL) {
    return 1;
  }

  tail = &module->generates;
  while (*tail != NULL) {
    tail = &(*tail)->next;
  }
  *tail = items;
  return 1;
}

int vlog_module_update_signal(VlogModule *module,
                              const char *name,
                              VlogDir dir,
                              int is_port,
                              int is_reg,
                              int is_signed,
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
  if (is_signed) {
    signal->is_signed = 1;
  }
  if (range.has_range) {
    vlog_range_free(&signal->range);
    signal->range = vlog_range_clone(&range);
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
                             VlogParamOverride *param_overrides,
                             VlogConn *conns,
                             int line)
{
  VlogInstance *instance;
  VlogInstance **tail;

  instance = (VlogInstance *)vlog_xmalloc(sizeof(VlogInstance));
  instance->module_name = vlog_strdup(module_name);
  instance->name = vlog_strdup(name);
  instance->param_overrides = param_overrides;
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
  ref.select_msb_expr = NULL;
  ref.select_lsb_expr = NULL;
  return ref;
}

static VlogRef vlog_ref_clone(const VlogRef *ref)
{
  VlogRef copy;

  copy.name = vlog_strdup(ref->name);
  copy.has_select = ref->has_select;
  copy.select_msb = ref->select_msb;
  copy.select_lsb = ref->select_lsb;
  copy.select_msb_expr = vlog_int_expr_clone(ref->select_msb_expr);
  copy.select_lsb_expr = vlog_int_expr_clone(ref->select_lsb_expr);
  return copy;
}

static VlogIntExpr *vlog_int_expr_alloc(VlogIntExprKind kind, int line)
{
  VlogIntExpr *expr;

  expr = (VlogIntExpr *)vlog_xmalloc(sizeof(VlogIntExpr));
  expr->kind = kind;
  expr->op = VLOG_INT_OP_NONE;
  expr->value = 0;
  expr->name = NULL;
  expr->line = line;
  expr->left = NULL;
  expr->right = NULL;
  return expr;
}

VlogIntExpr *vlog_int_expr_const(int value, int line)
{
  VlogIntExpr *expr;

  expr = vlog_int_expr_alloc(VLOG_INT_CONST, line);
  expr->value = value;
  return expr;
}

VlogIntExpr *vlog_int_expr_ref(const char *name, int line)
{
  VlogIntExpr *expr;

  expr = vlog_int_expr_alloc(VLOG_INT_REF, line);
  expr->name = vlog_strdup(name);
  return expr;
}

VlogIntExpr *vlog_int_expr_unary(VlogIntOp op, VlogIntExpr *child, int line)
{
  VlogIntExpr *expr;

  expr = vlog_int_expr_alloc(VLOG_INT_UNARY, line);
  expr->op = op;
  expr->left = child;
  return expr;
}

VlogIntExpr *vlog_int_expr_binary(VlogIntOp op,
                                  VlogIntExpr *left,
                                  VlogIntExpr *right,
                                  int line)
{
  VlogIntExpr *expr;

  expr = vlog_int_expr_alloc(VLOG_INT_BINARY, line);
  expr->op = op;
  expr->left = left;
  expr->right = right;
  return expr;
}

VlogIntExpr *vlog_int_expr_clone(const VlogIntExpr *expr)
{
  VlogIntExpr *copy;

  if (expr == NULL) {
    return NULL;
  }

  copy = vlog_int_expr_alloc(expr->kind, expr->line);
  copy->op = expr->op;
  copy->value = expr->value;
  copy->name = vlog_strdup(expr->name);
  copy->left = vlog_int_expr_clone(expr->left);
  copy->right = vlog_int_expr_clone(expr->right);
  return copy;
}

void vlog_int_expr_free(VlogIntExpr *expr)
{
  if (expr == NULL) {
    return;
  }
  free(expr->name);
  vlog_int_expr_free(expr->left);
  vlog_int_expr_free(expr->right);
  free(expr);
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

VlogParamOverride *vlog_param_override_append(VlogParamOverride *list,
                                              const char *param_name,
                                              int is_named,
                                              VlogIntExpr *expr,
                                              int line)
{
  VlogParamOverride *node;
  VlogParamOverride **tail;

  node = (VlogParamOverride *)vlog_xmalloc(sizeof(VlogParamOverride));
  node->param_name = vlog_strdup(param_name);
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

VlogParamOverride *vlog_param_override_clone(const VlogParamOverride *list)
{
  VlogParamOverride *copy;

  copy = NULL;
  while (list != NULL) {
    copy = vlog_param_override_append(copy,
                                      list->param_name,
                                      list->is_named,
                                      vlog_int_expr_clone(list->expr),
                                      list->line);
    list = list->next;
  }
  return copy;
}

void vlog_param_override_free(VlogParamOverride *override)
{
  VlogParamOverride *next;

  while (override != NULL) {
    next = override->next;
    free(override->param_name);
    vlog_int_expr_free(override->expr);
    free(override);
    override = next;
  }
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
