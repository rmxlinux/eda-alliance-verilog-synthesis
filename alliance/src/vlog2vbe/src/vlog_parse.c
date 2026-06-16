#include "vlog_parse.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  TOK_EOF = 0,
  TOK_IDENT = 256,
  TOK_NUMBER,
  TOK_MODULE,
  TOK_ENDMODULE,
  TOK_INPUT,
  TOK_OUTPUT,
  TOK_INOUT,
  TOK_WIRE,
  TOK_REG,
  TOK_ASSIGN,
  TOK_SIGNED,
  TOK_ALWAYS,
  TOK_BEGIN,
  TOK_END,
  TOK_IF,
  TOK_ELSE,
  TOK_CASE,
  TOK_ENDCASE,
  TOK_DEFAULT,
  TOK_POSEDGE,
  TOK_NEGEDGE,
  TOK_OROR,
  TOK_ANDAND,
  TOK_EQEQ,
  TOK_NEQ,
  TOK_LE
};

typedef struct Token {
  int type;
  char *text;
  int line;
  int col;
} Token;

typedef struct Lexer {
  const char *src;
  unsigned int len;
  unsigned int pos;
  int line;
  int col;
  Token tok;
} Lexer;

typedef struct Parser {
  Lexer lexer;
  VlogModule *module;
  char *error;
  unsigned int error_size;
  int failed;
} Parser;

typedef enum ProcStmtKind {
  PROC_STMT_ASSIGN = 0,
  PROC_STMT_BLOCK,
  PROC_STMT_IF,
  PROC_STMT_CASE
} ProcStmtKind;

typedef struct ProcStmt ProcStmt;
typedef struct ProcStmtList ProcStmtList;
typedef struct ProcCaseItem ProcCaseItem;

struct ProcStmtList {
  ProcStmt *stmt;
  ProcStmtList *next;
};

struct ProcCaseItem {
  VlogExprList *labels;
  ProcStmt *stmt;
  int line;
  ProcCaseItem *next;
};

struct ProcStmt {
  ProcStmtKind kind;
  int line;
  VlogRef target;
  VlogExpr *expr;
  VlogExpr *cond;
  ProcStmt *then_stmt;
  ProcStmt *else_stmt;
  ProcStmtList *stmts;
  ProcCaseItem *case_items;
  ProcStmt *default_stmt;
};

typedef struct NameList {
  char *name;
  struct NameList *next;
} NameList;

typedef struct ProcEnv {
  char *name;
  VlogExpr *expr;
  struct ProcEnv *next;
} ProcEnv;

static void parser_error(Parser *parser, const char *fmt, ...)
{
  va_list args;
  unsigned int used;

  if (parser->failed) {
    return;
  }

  parser->failed = 1;
  if (parser->error_size == 0) {
    return;
  }

  used = (unsigned int)snprintf(parser->error,
                                parser->error_size,
                                "line %d, column %d: ",
                                parser->lexer.tok.line,
                                parser->lexer.tok.col);
  if (used >= parser->error_size) {
    parser->error[parser->error_size - 1] = '\0';
    return;
  }

  va_start(args, fmt);
  vsnprintf(parser->error + used, parser->error_size - used, fmt, args);
  va_end(args);
}

static void parser_error_at(Parser *parser, int line, const char *fmt, ...)
{
  va_list args;
  unsigned int used;

  if (parser->failed) {
    return;
  }

  parser->failed = 1;
  if (parser->error_size == 0) {
    return;
  }

  used = (unsigned int)snprintf(parser->error,
                                parser->error_size,
                                "line %d: ",
                                line);
  if (used >= parser->error_size) {
    parser->error[parser->error_size - 1] = '\0';
    return;
  }

  va_start(args, fmt);
  vsnprintf(parser->error + used, parser->error_size - used, fmt, args);
  va_end(args);
}

static char lexer_peek(Lexer *lexer)
{
  if (lexer->pos >= lexer->len) {
    return '\0';
  }
  return lexer->src[lexer->pos];
}

static char lexer_peek_next(Lexer *lexer)
{
  if (lexer->pos + 1 >= lexer->len) {
    return '\0';
  }
  return lexer->src[lexer->pos + 1];
}

static char lexer_get(Lexer *lexer)
{
  char ch;

  if (lexer->pos >= lexer->len) {
    return '\0';
  }

  ch = lexer->src[lexer->pos++];
  if (ch == '\n') {
    lexer->line++;
    lexer->col = 1;
  } else {
    lexer->col++;
  }
  return ch;
}

static void token_clear(Token *tok)
{
  if (tok->text != NULL) {
    free(tok->text);
  }
  tok->text = NULL;
}

static void lexer_skip_space(Lexer *lexer)
{
  int again;

  do {
    again = 0;
    while (isspace((unsigned char)lexer_peek(lexer))) {
      lexer_get(lexer);
    }

    if (lexer_peek(lexer) == '`') {
      while (lexer_peek(lexer) != '\0' && lexer_peek(lexer) != '\n') {
        lexer_get(lexer);
      }
      again = 1;
    } else if (lexer_peek(lexer) == '/' && lexer_peek_next(lexer) == '/') {
      while (lexer_peek(lexer) != '\0' && lexer_peek(lexer) != '\n') {
        lexer_get(lexer);
      }
      again = 1;
    } else if (lexer_peek(lexer) == '/' && lexer_peek_next(lexer) == '*') {
      lexer_get(lexer);
      lexer_get(lexer);
      while (lexer_peek(lexer) != '\0') {
        if (lexer_peek(lexer) == '*' && lexer_peek_next(lexer) == '/') {
          lexer_get(lexer);
          lexer_get(lexer);
          break;
        }
        lexer_get(lexer);
      }
      again = 1;
    } else if (lexer_peek(lexer) == '(' &&
               lexer_peek_next(lexer) == '*' &&
               lexer->pos + 2 < lexer->len &&
               lexer->src[lexer->pos + 2] != ')') {
      lexer_get(lexer);
      lexer_get(lexer);
      while (lexer_peek(lexer) != '\0') {
        if (lexer_peek(lexer) == '*' && lexer_peek_next(lexer) == ')') {
          lexer_get(lexer);
          lexer_get(lexer);
          break;
        }
        lexer_get(lexer);
      }
      again = 1;
    }
  } while (again);
}

static int keyword_type(const char *text)
{
  if (strcmp(text, "module") == 0) return TOK_MODULE;
  if (strcmp(text, "endmodule") == 0) return TOK_ENDMODULE;
  if (strcmp(text, "input") == 0) return TOK_INPUT;
  if (strcmp(text, "output") == 0) return TOK_OUTPUT;
  if (strcmp(text, "inout") == 0) return TOK_INOUT;
  if (strcmp(text, "wire") == 0) return TOK_WIRE;
  if (strcmp(text, "reg") == 0) return TOK_REG;
  if (strcmp(text, "assign") == 0) return TOK_ASSIGN;
  if (strcmp(text, "signed") == 0) return TOK_SIGNED;
  if (strcmp(text, "always") == 0) return TOK_ALWAYS;
  if (strcmp(text, "begin") == 0) return TOK_BEGIN;
  if (strcmp(text, "end") == 0) return TOK_END;
  if (strcmp(text, "if") == 0) return TOK_IF;
  if (strcmp(text, "else") == 0) return TOK_ELSE;
  if (strcmp(text, "case") == 0) return TOK_CASE;
  if (strcmp(text, "endcase") == 0) return TOK_ENDCASE;
  if (strcmp(text, "default") == 0) return TOK_DEFAULT;
  if (strcmp(text, "posedge") == 0) return TOK_POSEDGE;
  if (strcmp(text, "negedge") == 0) return TOK_NEGEDGE;
  return TOK_IDENT;
}

static void lexer_next(Lexer *lexer)
{
  unsigned int start;
  int start_line;
  int start_col;
  char ch;

  token_clear(&lexer->tok);
  lexer_skip_space(lexer);

  start = lexer->pos;
  start_line = lexer->line;
  start_col = lexer->col;
  ch = lexer_get(lexer);

  lexer->tok.line = start_line;
  lexer->tok.col = start_col;

  if (ch == '\0') {
    lexer->tok.type = TOK_EOF;
    return;
  }

  if (isalpha((unsigned char)ch) || ch == '_' || ch == '$') {
    while (isalnum((unsigned char)lexer_peek(lexer)) ||
           lexer_peek(lexer) == '_' ||
           lexer_peek(lexer) == '$') {
      lexer_get(lexer);
    }
    lexer->tok.text = vlog_strndup(lexer->src + start, lexer->pos - start);
    lexer->tok.type = keyword_type(lexer->tok.text);
    return;
  }

  if (ch == '\\') {
    while (lexer_peek(lexer) != '\0' &&
           !isspace((unsigned char)lexer_peek(lexer))) {
      lexer_get(lexer);
    }
    lexer->tok.text = vlog_strndup(lexer->src + start + 1,
                                   lexer->pos - start - 1);
    lexer->tok.type = TOK_IDENT;
    return;
  }

  if (isdigit((unsigned char)ch)) {
    while (isalnum((unsigned char)lexer_peek(lexer)) ||
           lexer_peek(lexer) == '_' ||
           lexer_peek(lexer) == '\'') {
      lexer_get(lexer);
    }
    lexer->tok.text = vlog_strndup(lexer->src + start, lexer->pos - start);
    lexer->tok.type = TOK_NUMBER;
    return;
  }

  if (ch == '|' && lexer_peek(lexer) == '|') {
    lexer_get(lexer);
    lexer->tok.type = TOK_OROR;
    return;
  }
  if (ch == '&' && lexer_peek(lexer) == '&') {
    lexer_get(lexer);
    lexer->tok.type = TOK_ANDAND;
    return;
  }
  if (ch == '=' && lexer_peek(lexer) == '=') {
    lexer_get(lexer);
    lexer->tok.type = TOK_EQEQ;
    return;
  }
  if (ch == '!' && lexer_peek(lexer) == '=') {
    lexer_get(lexer);
    lexer->tok.type = TOK_NEQ;
    return;
  }
  if (ch == '<' && lexer_peek(lexer) == '=') {
    lexer_get(lexer);
    lexer->tok.type = TOK_LE;
    return;
  }

  lexer->tok.type = (unsigned char)ch;
}

static int token_is_direction(int token)
{
  return token == TOK_INPUT || token == TOK_OUTPUT || token == TOK_INOUT;
}

static VlogDir token_to_dir(int token)
{
  if (token == TOK_INPUT) return VLOG_DIR_INPUT;
  if (token == TOK_OUTPUT) return VLOG_DIR_OUTPUT;
  if (token == TOK_INOUT) return VLOG_DIR_INOUT;
  return VLOG_DIR_NONE;
}

static int parser_accept(Parser *parser, int token)
{
  if (parser->lexer.tok.type == token) {
    lexer_next(&parser->lexer);
    return 1;
  }
  return 0;
}

static int parser_expect(Parser *parser, int token, const char *what)
{
  if (parser_accept(parser, token)) {
    return 1;
  }
  parser_error(parser, "expected %s", what);
  return 0;
}

static int parse_range_number(Parser *parser, int *value)
{
  char *endptr;
  long number;

  if (parser->lexer.tok.type != TOK_NUMBER) {
    parser_error(parser, "expected a decimal range bound");
    return 0;
  }
  number = strtol(parser->lexer.tok.text, &endptr, 10);
  if (*endptr != '\0') {
    parser_error(parser, "range bounds must be decimal constants in this version");
    return 0;
  }
  *value = (int)number;
  lexer_next(&parser->lexer);
  return 1;
}

static int parse_optional_range(Parser *parser, VlogRange *range)
{
  range->has_range = 0;
  range->msb = 0;
  range->lsb = 0;

  if (!parser_accept(parser, '[')) {
    return 1;
  }
  range->has_range = 1;
  if (!parse_range_number(parser, &range->msb)) return 0;
  if (!parser_expect(parser, ':', "':' in range")) return 0;
  if (!parse_range_number(parser, &range->lsb)) return 0;
  if (!parser_expect(parser, ']', "']' after range")) return 0;
  return 1;
}

static void parse_optional_type_words(Parser *parser, int *is_reg)
{
  int keep_going;

  keep_going = 1;
  while (keep_going) {
    keep_going = 0;
    if (parser_accept(parser, TOK_SIGNED)) {
      keep_going = 1;
    } else if (parser_accept(parser, TOK_WIRE)) {
      keep_going = 1;
    } else if (parser_accept(parser, TOK_REG)) {
      *is_reg = 1;
      keep_going = 1;
    }
  }
}

static char *parse_identifier(Parser *parser, const char *what)
{
  char *name;

  if (parser->lexer.tok.type != TOK_IDENT) {
    parser_error(parser, "expected %s", what);
    return NULL;
  }
  name = vlog_strdup(parser->lexer.tok.text);
  lexer_next(&parser->lexer);
  return name;
}

static int parse_port_list(Parser *parser)
{
  VlogDir current_dir;
  VlogRange current_range;
  int current_is_reg;
  int ansi_seen;

  current_dir = VLOG_DIR_NONE;
  current_range = vlog_range_none();
  current_is_reg = 0;
  ansi_seen = 0;

  if (parser_accept(parser, ')')) {
    return 1;
  }

  while (!parser->failed) {
    VlogDir dir;
    VlogRange range;
    int is_reg;
    char *name;

    dir = VLOG_DIR_NONE;
    range = vlog_range_none();
    is_reg = 0;

    if (token_is_direction(parser->lexer.tok.type)) {
      dir = token_to_dir(parser->lexer.tok.type);
      lexer_next(&parser->lexer);
      parse_optional_type_words(parser, &is_reg);
      if (!parse_optional_range(parser, &range)) return 0;
      current_dir = dir;
      current_range = range;
      current_is_reg = is_reg;
      ansi_seen = 1;
    } else if (ansi_seen && current_dir != VLOG_DIR_NONE) {
      dir = current_dir;
      range = current_range;
      is_reg = current_is_reg;
    }

    name = parse_identifier(parser, "port name");
    if (name == NULL) return 0;
    vlog_module_add_port(parser->module, name);
    vlog_module_update_signal(parser->module, name, dir, 1, is_reg, range);
    free(name);

    if (parser_accept(parser, ',')) {
      continue;
    }
    if (parser_expect(parser, ')', "')' after port list")) {
      return 1;
    }
    return 0;
  }
  return 0;
}

static int parse_ref(Parser *parser, VlogRef *ref)
{
  char *name;

  name = parse_identifier(parser, "signal name");
  if (name == NULL) {
    return 0;
  }

  *ref = vlog_ref_make(name);
  free(name);

  if (parser_accept(parser, '[')) {
    ref->has_select = 1;
    if (!parse_range_number(parser, &ref->select_msb)) return 0;
    if (parser_accept(parser, ':')) {
      if (!parse_range_number(parser, &ref->select_lsb)) return 0;
    } else {
      ref->select_lsb = ref->select_msb;
    }
    if (!parser_expect(parser, ']', "']' after select")) return 0;
  }
  return 1;
}

static VlogExpr *parse_expr(Parser *parser);

static VlogExpr *parse_primary(Parser *parser)
{
  int line;

  line = parser->lexer.tok.line;

  if (parser->lexer.tok.type == TOK_IDENT) {
    VlogRef ref;
    if (!parse_ref(parser, &ref)) return NULL;
    return vlog_expr_ref(ref, line);
  }

  if (parser->lexer.tok.type == TOK_NUMBER) {
    char *text;
    VlogExpr *expr;

    text = vlog_strdup(parser->lexer.tok.text);
    lexer_next(&parser->lexer);
    expr = vlog_expr_const(text, line);
    free(text);
    return expr;
  }

  if (parser_accept(parser, '(')) {
    VlogExpr *expr;
    expr = parse_expr(parser);
    if (expr == NULL) return NULL;
    if (!parser_expect(parser, ')', "')' after expression")) {
      vlog_expr_free(expr);
      return NULL;
    }
    return expr;
  }

  if (parser_accept(parser, '{')) {
    VlogExprList *items;
    VlogExpr *item;

    items = NULL;
    do {
      item = parse_expr(parser);
      if (item == NULL) {
        vlog_expr_list_free(items);
        return NULL;
      }
      items = vlog_expr_list_append(items, item);
    } while (parser_accept(parser, ','));

    if (!parser_expect(parser, '}', "'}' after concatenation")) {
      vlog_expr_list_free(items);
      return NULL;
    }
    return vlog_expr_concat(items, line);
  }

  parser_error(parser, "expected expression");
  return NULL;
}

static VlogExpr *parse_unary(Parser *parser)
{
  int line;

  line = parser->lexer.tok.line;
  if (parser_accept(parser, '~') || parser_accept(parser, '!')) {
    VlogExpr *child;
    child = parse_unary(parser);
    if (child == NULL) return NULL;
    return vlog_expr_unary(VLOG_OP_NOT, child, line);
  }
  return parse_primary(parser);
}

static VlogExpr *parse_binary_level(Parser *parser,
                                    VlogExpr *(*next_level)(Parser *),
                                    const int *tokens,
                                    const VlogOp *ops,
                                    int count)
{
  VlogExpr *left;

  left = next_level(parser);
  if (left == NULL) {
    return NULL;
  }

  while (!parser->failed) {
    int i;
    int matched;
    int line;

    matched = -1;
    for (i = 0; i < count; i++) {
      if (parser->lexer.tok.type == tokens[i]) {
        matched = i;
        break;
      }
    }
    if (matched < 0) {
      break;
    }

    line = parser->lexer.tok.line;
    lexer_next(&parser->lexer);
    {
      VlogExpr *right;
      right = next_level(parser);
      if (right == NULL) {
        vlog_expr_free(left);
        return NULL;
      }
      left = vlog_expr_binary(ops[matched], left, right, line);
    }
  }

  return left;
}

static VlogExpr *parse_bitwise_and(Parser *parser)
{
  static const int tokens[] = {'&'};
  static const VlogOp ops[] = {VLOG_OP_AND};
  return parse_binary_level(parser, parse_unary, tokens, ops, 1);
}

static VlogExpr *parse_bitwise_xor(Parser *parser)
{
  static const int tokens[] = {'^'};
  static const VlogOp ops[] = {VLOG_OP_XOR};
  return parse_binary_level(parser, parse_bitwise_and, tokens, ops, 1);
}

static VlogExpr *parse_bitwise_or(Parser *parser)
{
  static const int tokens[] = {'|'};
  static const VlogOp ops[] = {VLOG_OP_OR};
  return parse_binary_level(parser, parse_bitwise_xor, tokens, ops, 1);
}

static VlogExpr *parse_equality(Parser *parser)
{
  static const int tokens[] = {TOK_EQEQ, TOK_NEQ};
  static const VlogOp ops[] = {VLOG_OP_EQ, VLOG_OP_NE};
  return parse_binary_level(parser, parse_bitwise_or, tokens, ops, 2);
}

static VlogExpr *parse_logic_and(Parser *parser)
{
  static const int tokens[] = {TOK_ANDAND};
  static const VlogOp ops[] = {VLOG_OP_LOGIC_AND};
  return parse_binary_level(parser, parse_equality, tokens, ops, 1);
}

static VlogExpr *parse_logic_or(Parser *parser)
{
  static const int tokens[] = {TOK_OROR};
  static const VlogOp ops[] = {VLOG_OP_LOGIC_OR};
  return parse_binary_level(parser, parse_logic_and, tokens, ops, 1);
}

static VlogExpr *parse_expr(Parser *parser)
{
  VlogExpr *cond;

  cond = parse_logic_or(parser);
  if (cond == NULL) {
    return NULL;
  }

  if (parser_accept(parser, '?')) {
    VlogExpr *true_expr;
    VlogExpr *false_expr;
    int line;

    line = cond->line;
    true_expr = parse_expr(parser);
    if (true_expr == NULL) {
      vlog_expr_free(cond);
      return NULL;
    }
    if (!parser_expect(parser, ':', "':' in conditional expression")) {
      vlog_expr_free(cond);
      vlog_expr_free(true_expr);
      return NULL;
    }
    false_expr = parse_expr(parser);
    if (false_expr == NULL) {
      vlog_expr_free(cond);
      vlog_expr_free(true_expr);
      return NULL;
    }
    return vlog_expr_ternary(cond, true_expr, false_expr, line);
  }

  return cond;
}

static ProcStmt *proc_stmt_new(ProcStmtKind kind, int line)
{
  ProcStmt *stmt;

  stmt = (ProcStmt *)malloc(sizeof(ProcStmt));
  if (stmt == NULL) {
    fprintf(stderr, "vlog2vbe: out of memory\n");
    exit(2);
  }
  memset(stmt, 0, sizeof(ProcStmt));
  stmt->kind = kind;
  stmt->line = line;
  stmt->target.name = NULL;
  return stmt;
}

static ProcStmtList *proc_stmt_list_append(ProcStmtList *list, ProcStmt *stmt)
{
  ProcStmtList *node;
  ProcStmtList **tail;

  node = (ProcStmtList *)malloc(sizeof(ProcStmtList));
  if (node == NULL) {
    fprintf(stderr, "vlog2vbe: out of memory\n");
    exit(2);
  }
  node->stmt = stmt;
  node->next = NULL;

  tail = &list;
  while (*tail != NULL) {
    tail = &(*tail)->next;
  }
  *tail = node;
  return list;
}

static ProcCaseItem *proc_case_item_append(ProcCaseItem *list,
                                           VlogExprList *labels,
                                           ProcStmt *stmt,
                                           int line)
{
  ProcCaseItem *node;
  ProcCaseItem **tail;

  node = (ProcCaseItem *)malloc(sizeof(ProcCaseItem));
  if (node == NULL) {
    fprintf(stderr, "vlog2vbe: out of memory\n");
    exit(2);
  }
  node->labels = labels;
  node->stmt = stmt;
  node->line = line;
  node->next = NULL;

  tail = &list;
  while (*tail != NULL) {
    tail = &(*tail)->next;
  }
  *tail = node;
  return list;
}

static void proc_stmt_free(ProcStmt *stmt)
{
  ProcStmtList *stmt_node;
  ProcStmtList *next_stmt;
  ProcCaseItem *case_node;
  ProcCaseItem *next_case;

  if (stmt == NULL) {
    return;
  }

  vlog_ref_free(&stmt->target);
  vlog_expr_free(stmt->expr);
  vlog_expr_free(stmt->cond);
  proc_stmt_free(stmt->then_stmt);
  proc_stmt_free(stmt->else_stmt);
  proc_stmt_free(stmt->default_stmt);

  stmt_node = stmt->stmts;
  while (stmt_node != NULL) {
    next_stmt = stmt_node->next;
    proc_stmt_free(stmt_node->stmt);
    free(stmt_node);
    stmt_node = next_stmt;
  }

  case_node = stmt->case_items;
  while (case_node != NULL) {
    next_case = case_node->next;
    vlog_expr_list_free(case_node->labels);
    proc_stmt_free(case_node->stmt);
    free(case_node);
    case_node = next_case;
  }

  free(stmt);
}

static int name_list_contains(const NameList *list, const char *name)
{
  while (list != NULL) {
    if (strcmp(list->name, name) == 0) {
      return 1;
    }
    list = list->next;
  }
  return 0;
}

static void name_list_add_unique(NameList **list, const char *name)
{
  NameList *node;
  NameList **tail;

  if (name_list_contains(*list, name)) {
    return;
  }

  node = (NameList *)malloc(sizeof(NameList));
  if (node == NULL) {
    fprintf(stderr, "vlog2vbe: out of memory\n");
    exit(2);
  }
  node->name = vlog_strdup(name);
  node->next = NULL;

  tail = list;
  while (*tail != NULL) {
    tail = &(*tail)->next;
  }
  *tail = node;
}

static void name_list_merge(NameList **dst, const NameList *src)
{
  while (src != NULL) {
    name_list_add_unique(dst, src->name);
    src = src->next;
  }
}

static void name_list_free(NameList *list)
{
  NameList *next;

  while (list != NULL) {
    next = list->next;
    free(list->name);
    free(list);
    list = next;
  }
}

static ProcEnv *proc_env_find(ProcEnv *env, const char *name)
{
  while (env != NULL) {
    if (strcmp(env->name, name) == 0) {
      return env;
    }
    env = env->next;
  }
  return NULL;
}

static VlogExpr *proc_env_get(ProcEnv *env, const char *name)
{
  ProcEnv *entry;

  entry = proc_env_find(env, name);
  return entry == NULL ? NULL : entry->expr;
}

static void proc_env_set_take(ProcEnv **env, const char *name, VlogExpr *expr)
{
  ProcEnv *entry;
  ProcEnv **tail;

  entry = proc_env_find(*env, name);
  if (entry != NULL) {
    vlog_expr_free(entry->expr);
    entry->expr = expr;
    return;
  }

  entry = (ProcEnv *)malloc(sizeof(ProcEnv));
  if (entry == NULL) {
    fprintf(stderr, "vlog2vbe: out of memory\n");
    exit(2);
  }
  entry->name = vlog_strdup(name);
  entry->expr = expr;
  entry->next = NULL;

  tail = env;
  while (*tail != NULL) {
    tail = &(*tail)->next;
  }
  *tail = entry;
}

static ProcEnv *proc_env_clone(ProcEnv *env)
{
  ProcEnv *copy;

  copy = NULL;
  while (env != NULL) {
    proc_env_set_take(&copy, env->name, vlog_expr_clone(env->expr));
    env = env->next;
  }
  return copy;
}

static void proc_env_free(ProcEnv *env)
{
  ProcEnv *next;

  while (env != NULL) {
    next = env->next;
    free(env->name);
    vlog_expr_free(env->expr);
    free(env);
    env = next;
  }
}

static ProcStmt *parse_proc_stmt(Parser *parser);

static ProcStmt *parse_proc_assignment(Parser *parser)
{
  ProcStmt *stmt;
  int line;

  line = parser->lexer.tok.line;
  stmt = proc_stmt_new(PROC_STMT_ASSIGN, line);

  if (!parse_ref(parser, &stmt->target)) {
    proc_stmt_free(stmt);
    return NULL;
  }

  if (parser_accept(parser, TOK_LE)) {
    parser_error_at(parser,
                    line,
                    "nonblocking assignments are planned for the sequential milestone");
    proc_stmt_free(stmt);
    return NULL;
  }
  if (!parser_expect(parser, '=', "'=' in procedural assignment")) {
    proc_stmt_free(stmt);
    return NULL;
  }

  stmt->expr = parse_expr(parser);
  if (stmt->expr == NULL) {
    proc_stmt_free(stmt);
    return NULL;
  }
  if (!parser_expect(parser, ';', "';' after procedural assignment")) {
    proc_stmt_free(stmt);
    return NULL;
  }
  return stmt;
}

static ProcStmt *parse_proc_block(Parser *parser)
{
  ProcStmt *stmt;
  int line;

  line = parser->lexer.tok.line;
  if (!parser_expect(parser, TOK_BEGIN, "'begin'")) {
    return NULL;
  }

  stmt = proc_stmt_new(PROC_STMT_BLOCK, line);
  while (!parser->failed && parser->lexer.tok.type != TOK_END) {
    ProcStmt *child;

    if (parser->lexer.tok.type == TOK_EOF) {
      parser_error(parser, "unexpected end of file in procedural block");
      proc_stmt_free(stmt);
      return NULL;
    }
    child = parse_proc_stmt(parser);
    if (child == NULL) {
      proc_stmt_free(stmt);
      return NULL;
    }
    stmt->stmts = proc_stmt_list_append(stmt->stmts, child);
  }

  if (!parser_expect(parser, TOK_END, "'end'")) {
    proc_stmt_free(stmt);
    return NULL;
  }
  return stmt;
}

static ProcStmt *parse_proc_if(Parser *parser)
{
  ProcStmt *stmt;
  int line;

  line = parser->lexer.tok.line;
  if (!parser_expect(parser, TOK_IF, "'if'")) {
    return NULL;
  }
  if (!parser_expect(parser, '(', "'(' after if")) {
    return NULL;
  }

  stmt = proc_stmt_new(PROC_STMT_IF, line);
  stmt->cond = parse_expr(parser);
  if (stmt->cond == NULL) {
    proc_stmt_free(stmt);
    return NULL;
  }
  if (!parser_expect(parser, ')', "')' after if condition")) {
    proc_stmt_free(stmt);
    return NULL;
  }

  stmt->then_stmt = parse_proc_stmt(parser);
  if (stmt->then_stmt == NULL) {
    proc_stmt_free(stmt);
    return NULL;
  }
  if (parser_accept(parser, TOK_ELSE)) {
    stmt->else_stmt = parse_proc_stmt(parser);
    if (stmt->else_stmt == NULL) {
      proc_stmt_free(stmt);
      return NULL;
    }
  }
  return stmt;
}

static ProcStmt *parse_proc_case(Parser *parser)
{
  ProcStmt *stmt;
  int line;

  line = parser->lexer.tok.line;
  if (!parser_expect(parser, TOK_CASE, "'case'")) {
    return NULL;
  }
  if (!parser_expect(parser, '(', "'(' after case")) {
    return NULL;
  }

  stmt = proc_stmt_new(PROC_STMT_CASE, line);
  stmt->cond = parse_expr(parser);
  if (stmt->cond == NULL) {
    proc_stmt_free(stmt);
    return NULL;
  }
  if (!parser_expect(parser, ')', "')' after case expression")) {
    proc_stmt_free(stmt);
    return NULL;
  }

  while (!parser->failed && parser->lexer.tok.type != TOK_ENDCASE) {
    ProcStmt *item_stmt;
    VlogExprList *labels;
    int item_line;

    if (parser->lexer.tok.type == TOK_EOF) {
      parser_error(parser, "unexpected end of file in case statement");
      proc_stmt_free(stmt);
      return NULL;
    }

    labels = NULL;
    item_line = parser->lexer.tok.line;
    if (parser_accept(parser, TOK_DEFAULT)) {
      if (!parser_expect(parser, ':', "':' after default")) {
        proc_stmt_free(stmt);
        return NULL;
      }
      item_stmt = parse_proc_stmt(parser);
      if (item_stmt == NULL) {
        proc_stmt_free(stmt);
        return NULL;
      }
      if (stmt->default_stmt != NULL) {
        parser_error_at(parser, item_line, "duplicate default case item");
        proc_stmt_free(item_stmt);
        proc_stmt_free(stmt);
        return NULL;
      }
      stmt->default_stmt = item_stmt;
      continue;
    }

    do {
      VlogExpr *label;

      label = parse_expr(parser);
      if (label == NULL) {
        vlog_expr_list_free(labels);
        proc_stmt_free(stmt);
        return NULL;
      }
      labels = vlog_expr_list_append(labels, label);
    } while (parser_accept(parser, ','));

    if (!parser_expect(parser, ':', "':' after case label")) {
      vlog_expr_list_free(labels);
      proc_stmt_free(stmt);
      return NULL;
    }
    item_stmt = parse_proc_stmt(parser);
    if (item_stmt == NULL) {
      vlog_expr_list_free(labels);
      proc_stmt_free(stmt);
      return NULL;
    }
    stmt->case_items = proc_case_item_append(stmt->case_items,
                                             labels,
                                             item_stmt,
                                             item_line);
  }

  if (!parser_expect(parser, TOK_ENDCASE, "'endcase'")) {
    proc_stmt_free(stmt);
    return NULL;
  }
  return stmt;
}

static ProcStmt *parse_proc_stmt(Parser *parser)
{
  if (parser->lexer.tok.type == TOK_BEGIN) {
    return parse_proc_block(parser);
  }
  if (parser->lexer.tok.type == TOK_IF) {
    return parse_proc_if(parser);
  }
  if (parser->lexer.tok.type == TOK_CASE) {
    return parse_proc_case(parser);
  }
  if (parser->lexer.tok.type == TOK_IDENT) {
    return parse_proc_assignment(parser);
  }
  if (parser_accept(parser, ';')) {
    return proc_stmt_new(PROC_STMT_BLOCK, parser->lexer.tok.line);
  }

  parser_error(parser, "unsupported procedural statement");
  return NULL;
}

static int lower_proc_stmt(Parser *parser,
                           ProcStmt *stmt,
                           ProcEnv **env,
                           NameList **assigned);

static int lower_proc_block(Parser *parser,
                            ProcStmt *stmt,
                            ProcEnv **env,
                            NameList **assigned)
{
  ProcStmtList *node;

  for (node = stmt->stmts; node != NULL; node = node->next) {
    NameList *child_assigned;

    child_assigned = NULL;
    if (!lower_proc_stmt(parser, node->stmt, env, &child_assigned)) {
      name_list_free(child_assigned);
      return 0;
    }
    name_list_merge(assigned, child_assigned);
    name_list_free(child_assigned);
  }
  return 1;
}

static int lower_proc_if(Parser *parser,
                         ProcStmt *stmt,
                         ProcEnv **env,
                         NameList **assigned)
{
  ProcEnv *then_env;
  ProcEnv *else_env;
  NameList *then_assigned;
  NameList *else_assigned;
  NameList *targets;
  NameList *target;

  then_env = proc_env_clone(*env);
  else_env = proc_env_clone(*env);
  then_assigned = NULL;
  else_assigned = NULL;
  targets = NULL;

  if (!lower_proc_stmt(parser, stmt->then_stmt, &then_env, &then_assigned)) {
    proc_env_free(then_env);
    proc_env_free(else_env);
    name_list_free(then_assigned);
    return 0;
  }
  if (stmt->else_stmt != NULL &&
      !lower_proc_stmt(parser, stmt->else_stmt, &else_env, &else_assigned)) {
    proc_env_free(then_env);
    proc_env_free(else_env);
    name_list_free(then_assigned);
    name_list_free(else_assigned);
    return 0;
  }

  name_list_merge(&targets, then_assigned);
  name_list_merge(&targets, else_assigned);

  for (target = targets; target != NULL; target = target->next) {
    VlogExpr *then_expr;
    VlogExpr *else_expr;
    VlogExpr *combined;

    then_expr = proc_env_get(then_env, target->name);
    else_expr = proc_env_get(else_env, target->name);
    if (then_expr == NULL || else_expr == NULL) {
      parser_error_at(parser,
                      stmt->line,
                      "incomplete assignment to '%s' in combinational if",
                      target->name);
      proc_env_free(then_env);
      proc_env_free(else_env);
      name_list_free(then_assigned);
      name_list_free(else_assigned);
      name_list_free(targets);
      return 0;
    }

    combined = vlog_expr_ternary(vlog_expr_clone(stmt->cond),
                                 vlog_expr_clone(then_expr),
                                 vlog_expr_clone(else_expr),
                                 stmt->line);
    proc_env_set_take(env, target->name, combined);
    name_list_add_unique(assigned, target->name);
  }

  proc_env_free(then_env);
  proc_env_free(else_env);
  name_list_free(then_assigned);
  name_list_free(else_assigned);
  name_list_free(targets);
  return 1;
}

typedef struct CaseLower {
  const ProcCaseItem *item;
  ProcEnv *env;
  NameList *assigned;
  struct CaseLower *next;
} CaseLower;

static void case_lower_free(CaseLower *list)
{
  CaseLower *next;

  while (list != NULL) {
    next = list->next;
    proc_env_free(list->env);
    name_list_free(list->assigned);
    free(list);
    list = next;
  }
}

static VlogExpr *build_case_condition(const VlogExpr *selector,
                                      const VlogExprList *labels,
                                      int line)
{
  VlogExpr *cond;

  cond = NULL;
  while (labels != NULL) {
    VlogExpr *eq;

    eq = vlog_expr_binary(VLOG_OP_EQ,
                          vlog_expr_clone(selector),
                          vlog_expr_clone(labels->expr),
                          line);
    if (cond == NULL) {
      cond = eq;
    } else {
      cond = vlog_expr_binary(VLOG_OP_OR, cond, eq, line);
    }
    labels = labels->next;
  }
  return cond;
}

static VlogExpr *combine_case_chain(Parser *parser,
                                    const CaseLower *node,
                                    const char *target,
                                    const VlogExpr *selector,
                                    VlogExpr *fallback,
                                    int line)
{
  VlogExpr *rest;
  VlogExpr *branch;
  VlogExpr *cond;

  if (node == NULL) {
    return fallback;
  }

  rest = combine_case_chain(parser, node->next, target, selector, fallback, line);
  if (parser->failed) {
    return rest;
  }

  branch = proc_env_get(node->env, target);
  if (branch == NULL) {
    parser_error_at(parser,
                    node->item->line,
                    "incomplete assignment to '%s' in combinational case",
                    target);
    vlog_expr_free(rest);
    return NULL;
  }

  cond = build_case_condition(selector, node->item->labels, line);
  if (cond == NULL) {
    parser_error_at(parser, node->item->line, "empty case item");
    vlog_expr_free(rest);
    return NULL;
  }

  return vlog_expr_ternary(cond, vlog_expr_clone(branch), rest, line);
}

static int lower_proc_case(Parser *parser,
                           ProcStmt *stmt,
                           ProcEnv **env,
                           NameList **assigned)
{
  ProcEnv *default_env;
  NameList *default_assigned;
  NameList *targets;
  NameList *target;
  CaseLower *case_lowers;
  CaseLower **case_tail;
  ProcCaseItem *item;

  default_env = proc_env_clone(*env);
  default_assigned = NULL;
  targets = NULL;
  case_lowers = NULL;
  case_tail = &case_lowers;

  if (stmt->default_stmt != NULL &&
      !lower_proc_stmt(parser, stmt->default_stmt, &default_env, &default_assigned)) {
    proc_env_free(default_env);
    name_list_free(default_assigned);
    return 0;
  }
  name_list_merge(&targets, default_assigned);

  for (item = stmt->case_items; item != NULL; item = item->next) {
    CaseLower *node;

    node = (CaseLower *)malloc(sizeof(CaseLower));
    if (node == NULL) {
      fprintf(stderr, "vlog2vbe: out of memory\n");
      exit(2);
    }
    node->item = item;
    node->env = proc_env_clone(*env);
    node->assigned = NULL;
    node->next = NULL;

    if (!lower_proc_stmt(parser, item->stmt, &node->env, &node->assigned)) {
      proc_env_free(default_env);
      name_list_free(default_assigned);
      name_list_free(targets);
      proc_env_free(node->env);
      name_list_free(node->assigned);
      free(node);
      case_lower_free(case_lowers);
      return 0;
    }
    name_list_merge(&targets, node->assigned);
    *case_tail = node;
    case_tail = &node->next;
  }

  for (target = targets; target != NULL; target = target->next) {
    VlogExpr *base;
    VlogExpr *combined;

    base = proc_env_get(default_env, target->name);
    if (base == NULL) {
      parser_error_at(parser,
                      stmt->line,
                      "case statement can infer a latch for '%s'; add a default assignment",
                      target->name);
      proc_env_free(default_env);
      name_list_free(default_assigned);
      name_list_free(targets);
      case_lower_free(case_lowers);
      return 0;
    }

    combined = combine_case_chain(parser,
                                  case_lowers,
                                  target->name,
                                  stmt->cond,
                                  vlog_expr_clone(base),
                                  stmt->line);
    if (combined == NULL || parser->failed) {
      proc_env_free(default_env);
      name_list_free(default_assigned);
      name_list_free(targets);
      case_lower_free(case_lowers);
      return 0;
    }
    proc_env_set_take(env, target->name, combined);
    name_list_add_unique(assigned, target->name);
  }

  proc_env_free(default_env);
  name_list_free(default_assigned);
  name_list_free(targets);
  case_lower_free(case_lowers);
  return 1;
}

static int lower_proc_stmt(Parser *parser,
                           ProcStmt *stmt,
                           ProcEnv **env,
                           NameList **assigned)
{
  if (stmt->kind == PROC_STMT_ASSIGN) {
    if (stmt->target.has_select) {
      parser_error_at(parser,
                      stmt->line,
                      "procedural bit-select and part-select assignments are not supported yet");
      return 0;
    }
    proc_env_set_take(env, stmt->target.name, vlog_expr_clone(stmt->expr));
    name_list_add_unique(assigned, stmt->target.name);
    return 1;
  }
  if (stmt->kind == PROC_STMT_BLOCK) {
    return lower_proc_block(parser, stmt, env, assigned);
  }
  if (stmt->kind == PROC_STMT_IF) {
    return lower_proc_if(parser, stmt, env, assigned);
  }
  if (stmt->kind == PROC_STMT_CASE) {
    return lower_proc_case(parser, stmt, env, assigned);
  }
  return 0;
}

static int parse_always(Parser *parser)
{
  ProcStmt *stmt;
  ProcEnv *env;
  NameList *assigned;
  ProcEnv *entry;
  int line;

  line = parser->lexer.tok.line;
  if (!parser_expect(parser, TOK_ALWAYS, "'always'")) {
    return 0;
  }
  if (!parser_expect(parser, '@', "'@' after always")) {
    return 0;
  }

  if (parser_accept(parser, '*')) {
    /* always @* */
  } else if (parser_accept(parser, '(')) {
    if (parser_accept(parser, '*')) {
      if (!parser_expect(parser, ')', "')' after always @(*)")) {
        return 0;
      }
    } else if (parser->lexer.tok.type == TOK_POSEDGE ||
               parser->lexer.tok.type == TOK_NEGEDGE) {
      parser_error_at(parser,
                      line,
                      "edge-triggered always blocks are planned for the sequential milestone");
      return 0;
    } else {
      parser_error_at(parser,
                      line,
                      "only always @* and always @(*) are supported in this milestone");
      return 0;
    }
  } else {
    parser_error_at(parser,
                    line,
                    "only always @* and always @(*) are supported in this milestone");
    return 0;
  }

  stmt = parse_proc_stmt(parser);
  if (stmt == NULL) {
    return 0;
  }

  env = NULL;
  assigned = NULL;
  if (!lower_proc_stmt(parser, stmt, &env, &assigned)) {
    proc_env_free(env);
    name_list_free(assigned);
    proc_stmt_free(stmt);
    return 0;
  }

  for (entry = env; entry != NULL; entry = entry->next) {
    VlogRef target;
    VlogExpr *expr;

    target = vlog_ref_make(entry->name);
    expr = entry->expr;
    entry->expr = NULL;
    vlog_module_add_assign(parser->module, target, expr, line);
  }

  proc_env_free(env);
  name_list_free(assigned);
  proc_stmt_free(stmt);
  return 1;
}

static int parse_declaration(Parser *parser)
{
  VlogDir dir;
  VlogRange range;
  int is_reg;

  dir = VLOG_DIR_NONE;
  is_reg = 0;
  range = vlog_range_none();

  if (token_is_direction(parser->lexer.tok.type)) {
    dir = token_to_dir(parser->lexer.tok.type);
    lexer_next(&parser->lexer);
  } else if (parser_accept(parser, TOK_WIRE)) {
    dir = VLOG_DIR_NONE;
  } else if (parser_accept(parser, TOK_REG)) {
    dir = VLOG_DIR_NONE;
    is_reg = 1;
  } else {
    parser_error(parser, "expected declaration");
    return 0;
  }

  parse_optional_type_words(parser, &is_reg);
  if (!parse_optional_range(parser, &range)) return 0;

  while (!parser->failed) {
    char *name;

    name = parse_identifier(parser, "declared signal name");
    if (name == NULL) return 0;

    vlog_module_update_signal(parser->module,
                              name,
                              dir,
                              dir != VLOG_DIR_NONE,
                              is_reg,
                              range);
    free(name);

    if (parser_accept(parser, ',')) {
      continue;
    }
    return parser_expect(parser, ';', "';' after declaration");
  }
  return 0;
}

static int parse_assign(Parser *parser)
{
  VlogRef target;
  VlogExpr *expr;
  int line;

  line = parser->lexer.tok.line;
  lexer_next(&parser->lexer);

  if (!parse_ref(parser, &target)) {
    return 0;
  }

  if (!parser_expect(parser, '=', "'=' in continuous assignment")) {
    vlog_ref_free(&target);
    return 0;
  }

  expr = parse_expr(parser);
  if (expr == NULL) {
    vlog_ref_free(&target);
    return 0;
  }

  if (!parser_expect(parser, ';', "';' after assignment")) {
    vlog_ref_free(&target);
    vlog_expr_free(expr);
    return 0;
  }

  vlog_module_add_assign(parser->module, target, expr, line);
  return 1;
}

static int parse_module(Parser *parser)
{
  char *name;

  if (!parser_expect(parser, TOK_MODULE, "'module'")) return 0;

  name = parse_identifier(parser, "module name");
  if (name == NULL) return 0;
  parser->module->name = name;

  if (!parser_expect(parser, '(', "'(' after module name")) return 0;
  if (!parse_port_list(parser)) return 0;
  if (!parser_expect(parser, ';', "';' after module header")) return 0;

  while (!parser->failed && parser->lexer.tok.type != TOK_ENDMODULE) {
    if (parser->lexer.tok.type == TOK_EOF) {
      parser_error(parser, "unexpected end of file inside module");
      return 0;
    }
    if (token_is_direction(parser->lexer.tok.type) ||
        parser->lexer.tok.type == TOK_WIRE ||
        parser->lexer.tok.type == TOK_REG) {
      if (!parse_declaration(parser)) return 0;
    } else if (parser->lexer.tok.type == TOK_ASSIGN) {
      if (!parse_assign(parser)) return 0;
    } else if (parser->lexer.tok.type == TOK_ALWAYS) {
      if (!parse_always(parser)) return 0;
    } else {
      parser_error(parser, "unsupported module item");
      return 0;
    }
  }

  if (!parser_expect(parser, TOK_ENDMODULE, "'endmodule'")) return 0;
  if (parser->lexer.tok.type != TOK_EOF) {
    parser_error(parser, "only one module per file is supported in this version");
    return 0;
  }
  return 1;
}

static int read_file(const char *path, char **content, unsigned int *length)
{
  FILE *file;
  long size;
  char *buffer;

  file = fopen(path, "rb");
  if (file == NULL) {
    return 0;
  }
  if (fseek(file, 0, SEEK_END) != 0) {
    fclose(file);
    return 0;
  }
  size = ftell(file);
  if (size < 0) {
    fclose(file);
    return 0;
  }
  rewind(file);
  buffer = (char *)malloc((unsigned int)size + 1);
  if (buffer == NULL) {
    fclose(file);
    return 0;
  }
  if (size > 0 && fread(buffer, 1, (unsigned int)size, file) != (unsigned int)size) {
    free(buffer);
    fclose(file);
    return 0;
  }
  buffer[size] = '\0';
  fclose(file);
  *content = buffer;
  *length = (unsigned int)size;
  return 1;
}

int vlog_parse_file(const char *path,
                    VlogModule *module,
                    char *error,
                    unsigned int error_size)
{
  Parser parser;
  char *content;
  unsigned int length;
  int ok;

  if (!read_file(path, &content, &length)) {
    snprintf(error, error_size, "cannot read '%s'", path);
    return 0;
  }

  memset(&parser, 0, sizeof(parser));
  parser.lexer.src = content;
  parser.lexer.len = length;
  parser.lexer.pos = 0;
  parser.lexer.line = 1;
  parser.lexer.col = 1;
  parser.lexer.tok.type = TOK_EOF;
  parser.lexer.tok.text = NULL;
  parser.lexer.tok.line = 1;
  parser.lexer.tok.col = 1;
  parser.module = module;
  parser.error = error;
  parser.error_size = error_size;
  parser.failed = 0;

  lexer_next(&parser.lexer);
  ok = parse_module(&parser) && !parser.failed;
  token_clear(&parser.lexer.tok);
  free(content);
  return ok;
}
