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
    } else if (lexer_peek(lexer) == '(' && lexer_peek_next(lexer) == '*') {
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
      parser_error(parser,
                   "always blocks are planned for the next milestone; "
                   "use continuous assign in this first version");
      return 0;
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
