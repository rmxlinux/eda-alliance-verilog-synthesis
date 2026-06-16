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
} VlogRange;

typedef struct VlogRef {
  char *name;
  int has_select;
  int select_msb;
  int select_lsb;
} VlogRef;

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
  VLOG_OP_NE
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

typedef struct VlogModule {
  char *name;
  VlogSignal *signals;
  VlogPort *ports;
  VlogAssign *assigns;
} VlogModule;

void vlog_module_init(VlogModule *module);
void vlog_module_free(VlogModule *module);

VlogRange vlog_range_none(void);
VlogSignal *vlog_module_find_signal(VlogModule *module, const char *name);
VlogSignal *vlog_module_ensure_signal(VlogModule *module, const char *name);
int vlog_module_add_port(VlogModule *module, const char *name);
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

char *vlog_strdup(const char *text);
char *vlog_strndup(const char *text, unsigned int length);

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
void vlog_expr_free(VlogExpr *expr);
void vlog_expr_list_free(VlogExprList *list);

#endif
