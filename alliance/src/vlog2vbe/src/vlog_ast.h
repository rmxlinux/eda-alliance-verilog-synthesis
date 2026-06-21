#ifndef VLOG_AST_H
#define VLOG_AST_H

#include <stdio.h>

typedef enum VlogDir {
  VLOG_DIR_NONE = 0,
  VLOG_DIR_INPUT,
  VLOG_DIR_OUTPUT,
  VLOG_DIR_INOUT
} VlogDir;

typedef struct VlogRange {
  int has_range;
  int msb;
  int lsb;
  struct VlogIntExpr *msb_expr;
  struct VlogIntExpr *lsb_expr;
} VlogRange;

typedef struct VlogRef {
  char *name;
  int has_select;
  int select_msb;
  int select_lsb;
  struct VlogIntExpr *select_msb_expr;
  struct VlogIntExpr *select_lsb_expr;
} VlogRef;

typedef enum VlogIntExprKind {
  VLOG_INT_CONST = 0,
  VLOG_INT_REF,
  VLOG_INT_UNARY,
  VLOG_INT_BINARY
} VlogIntExprKind;

typedef enum VlogIntOp {
  VLOG_INT_OP_NONE = 0,
  VLOG_INT_OP_NEG,
  VLOG_INT_OP_ADD,
  VLOG_INT_OP_SUB,
  VLOG_INT_OP_MUL,
  VLOG_INT_OP_DIV,
  VLOG_INT_OP_MOD
} VlogIntOp;

typedef struct VlogIntExpr {
  VlogIntExprKind kind;
  VlogIntOp op;
  int value;
  char *name;
  int line;
  struct VlogIntExpr *left;
  struct VlogIntExpr *right;
} VlogIntExpr;

typedef enum VlogExprKind {
  VLOG_EXPR_REF = 0,
  VLOG_EXPR_CONST,
  VLOG_EXPR_UNARY,
  VLOG_EXPR_BINARY,
  VLOG_EXPR_TERNARY,
  VLOG_EXPR_CONCAT
} VlogExprKind;

typedef enum VlogOp {
  VLOG_OP_NONE = 0,
  VLOG_OP_NOT,
  VLOG_OP_AND,
  VLOG_OP_OR,
  VLOG_OP_XOR,
  VLOG_OP_LOGIC_AND,
  VLOG_OP_LOGIC_OR,
  VLOG_OP_EQ,
  VLOG_OP_NE,
  VLOG_OP_ADD,
  VLOG_OP_SUB,
  VLOG_OP_MUL
} VlogOp;

typedef struct VlogExpr VlogExpr;
typedef struct VlogExprList VlogExprList;

struct VlogExprList {
  VlogExpr *expr;
  VlogExprList *next;
};

struct VlogExpr {
  VlogExprKind kind;
  VlogOp op;
  VlogRef ref;
  char *text;
  int line;
  VlogExpr *left;
  VlogExpr *right;
  VlogExpr *third;
  VlogExprList *items;
};

typedef struct VlogSignal {
  char *name;
  VlogDir dir;
  int is_port;
  int is_reg;
  VlogRange range;
  struct VlogSignal *next;
} VlogSignal;

typedef struct VlogPort {
  char *name;
  struct VlogPort *next;
} VlogPort;

typedef struct VlogAssign {
  VlogRef target;
  VlogExpr *expr;
  int line;
  struct VlogAssign *next;
} VlogAssign;

typedef struct VlogConn {
  char *port_name;
  int is_named;
  VlogExpr *expr;
  int line;
  struct VlogConn *next;
} VlogConn;

typedef struct VlogParam {
  char *name;
  VlogIntExpr *expr;
  int is_local;
  int line;
  struct VlogParam *next;
} VlogParam;

typedef struct VlogParamOverride {
  char *param_name;
  int is_named;
  VlogIntExpr *expr;
  int line;
  struct VlogParamOverride *next;
} VlogParamOverride;

typedef struct VlogInstance {
  char *module_name;
  char *name;
  VlogParamOverride *param_overrides;
  VlogConn *conns;
  int line;
  struct VlogInstance *next;
} VlogInstance;

typedef struct VlogRegDriver {
  VlogRef target;
  char *clock;
  int clock_posedge;
  VlogExpr *guard;
  VlogExpr *expr;
  int line;
  struct VlogRegDriver *next;
} VlogRegDriver;

typedef enum VlogGenKind {
  VLOG_GEN_ASSIGN = 0,
  VLOG_GEN_INSTANCE,
  VLOG_GEN_FOR
} VlogGenKind;

typedef enum VlogGenCmp {
  VLOG_GEN_CMP_LT = 0,
  VLOG_GEN_CMP_LE,
  VLOG_GEN_CMP_GT,
  VLOG_GEN_CMP_GE
} VlogGenCmp;

typedef struct VlogGenerate {
  VlogGenKind kind;
  VlogRef target;
  VlogExpr *expr;
  char *module_name;
  char *instance_name;
  VlogParamOverride *param_overrides;
  VlogConn *conns;
  char *genvar;
  VlogIntExpr *init_expr;
  VlogIntExpr *limit_expr;
  VlogIntExpr *step_expr;
  int step_sign;
  VlogGenCmp cmp;
  char *block_name;
  struct VlogGenerate *body;
  int line;
  struct VlogGenerate *next;
} VlogGenerate;

typedef struct VlogModule {
  char *name;
  VlogParam *parameters;
  VlogSignal *signals;
  VlogPort *ports;
  VlogAssign *assigns;
  VlogInstance *instances;
  VlogGenerate *generates;
  VlogRegDriver *reg_drivers;
  struct VlogModule *next;
} VlogModule;

typedef struct VlogDesign {
  VlogModule *modules;
} VlogDesign;

void vlog_module_init(VlogModule *module);
void vlog_module_free(VlogModule *module);
VlogModule *vlog_module_new(void);

void vlog_design_init(VlogDesign *design);
void vlog_design_free(VlogDesign *design);
VlogModule *vlog_design_find_module(const VlogDesign *design, const char *name);
int vlog_design_add_module(VlogDesign *design, VlogModule *module);

VlogRange vlog_range_none(void);
VlogRange vlog_range_clone(const VlogRange *range);
void vlog_range_free(VlogRange *range);
VlogSignal *vlog_module_find_signal(VlogModule *module, const char *name);
VlogSignal *vlog_module_ensure_signal(VlogModule *module, const char *name);
int vlog_module_add_port(VlogModule *module, const char *name);
int vlog_module_add_param(VlogModule *module,
                          const char *name,
                          VlogIntExpr *expr,
                          int is_local,
                          int line);
int vlog_module_update_signal(VlogModule *module,
                              const char *name,
                              VlogDir dir,
                              int is_port,
                              int is_reg,
                              VlogRange range);
int vlog_module_add_assign(VlogModule *module,
                           VlogRef target,
                           VlogExpr *expr,
                           int line);
int vlog_module_add_instance(VlogModule *module,
                             const char *module_name,
                             const char *name,
                             VlogParamOverride *param_overrides,
                             VlogConn *conns,
                             int line);
int vlog_module_add_reg_driver(VlogModule *module,
                               VlogRef target,
                               const char *clock,
                               int clock_posedge,
                               VlogExpr *guard,
                               VlogExpr *expr,
                               int line);
VlogGenerate *vlog_generate_append_assign(VlogGenerate *list,
                                          VlogRef target,
                                          VlogExpr *expr,
                                          int line);
VlogGenerate *vlog_generate_append_instance(VlogGenerate *list,
                                            const char *module_name,
                                            const char *instance_name,
                                            VlogParamOverride *param_overrides,
                                            VlogConn *conns,
                                            int line);
VlogGenerate *vlog_generate_append_for(VlogGenerate *list,
                                       const char *genvar,
                                       VlogIntExpr *init_expr,
                                       VlogGenCmp cmp,
                                       VlogIntExpr *limit_expr,
                                       int step_sign,
                                       VlogIntExpr *step_expr,
                                       const char *block_name,
                                       VlogGenerate *body,
                                       int line);
int vlog_module_add_generate(VlogModule *module, VlogGenerate *items);

char *vlog_strdup(const char *text);
char *vlog_strndup(const char *text, unsigned int length);

VlogIntExpr *vlog_int_expr_const(int value, int line);
VlogIntExpr *vlog_int_expr_ref(const char *name, int line);
VlogIntExpr *vlog_int_expr_unary(VlogIntOp op, VlogIntExpr *child, int line);
VlogIntExpr *vlog_int_expr_binary(VlogIntOp op,
                                  VlogIntExpr *left,
                                  VlogIntExpr *right,
                                  int line);
VlogIntExpr *vlog_int_expr_clone(const VlogIntExpr *expr);
void vlog_int_expr_free(VlogIntExpr *expr);

VlogRef vlog_ref_make(const char *name);
void vlog_ref_free(VlogRef *ref);

VlogExpr *vlog_expr_ref(VlogRef ref, int line);
VlogExpr *vlog_expr_const(const char *text, int line);
VlogExpr *vlog_expr_unary(VlogOp op, VlogExpr *expr, int line);
VlogExpr *vlog_expr_binary(VlogOp op, VlogExpr *left, VlogExpr *right, int line);
VlogExpr *vlog_expr_ternary(VlogExpr *cond,
                            VlogExpr *true_expr,
                            VlogExpr *false_expr,
                            int line);
VlogExpr *vlog_expr_concat(VlogExprList *items, int line);
VlogExpr *vlog_expr_clone(const VlogExpr *expr);
VlogExprList *vlog_expr_list_append(VlogExprList *list, VlogExpr *expr);
VlogConn *vlog_conn_append(VlogConn *list,
                           const char *port_name,
                           int is_named,
                           VlogExpr *expr,
                           int line);
VlogParamOverride *vlog_param_override_append(VlogParamOverride *list,
                                              const char *param_name,
                                              int is_named,
                                              VlogIntExpr *expr,
                                              int line);
VlogParamOverride *vlog_param_override_clone(const VlogParamOverride *list);
void vlog_expr_free(VlogExpr *expr);
void vlog_expr_list_free(VlogExprList *list);
void vlog_conn_free(VlogConn *conn);
void vlog_param_override_free(VlogParamOverride *override);
void vlog_generate_free(VlogGenerate *generate);

#endif
