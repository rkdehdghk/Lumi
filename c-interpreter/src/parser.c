/* 파서 — 토큰을 문법 규칙에 맞춰 AST(구문 트리)로 만듭니다. 재귀 하강 방식.
 *
 * ponytail: AST 는 한 번 만들면 프로그램이 끝날 때까지 살아 있으므로
 * 따로 free 하지 않습니다. 함수/클래스가 자기 몸통 노드를 계속 붙들고
 * 있어서, 해제 시점을 관리하는 값보다 그냥 두는 쪽이 간단합니다. */
#include "lumi.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    TokenList *tokens;
    size_t pos;
} Parser;

/* 축약 대입: 짝이 되는 보통 연산자 */
static const struct { const char *shorthand, *op; } COMPOUND_OPS[] = {
    {"+=", "+"}, {"-=", "-"}, {"*=", "*"}, {"/=", "/"}, {"%=", "%"}, {"**=", "**"},
    {"&=", "&"}, {"|=", "|"}, {"^=", "^"}, {"<<=", "<<"}, {">>=", ">>"},
};

static const char *compound_op(const char *s)
{
    for (size_t i = 0; i < sizeof(COMPOUND_OPS) / sizeof(COMPOUND_OPS[0]); i++)
        if (strcmp(COMPOUND_OPS[i].shorthand, s) == 0) return COMPOUND_OPS[i].op;
    return NULL;
}

void nodelist_push(NodeList *l, Node *n)
{
    if (l->len == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 8;
        l->items = (Node **)xrealloc(l->items, l->cap * sizeof(Node *));
    }
    l->items[l->len++] = n;
}

static Node *node_new(NodeKind kind, int line)
{
    Node *n = (Node *)xmalloc(sizeof(Node));
    memset(n, 0, sizeof(Node));
    n->kind = kind;
    n->line = line;
    return n;
}

/* ---------------- 토큰 보기 ---------------- */

static Token *peek(Parser *p) { return &p->tokens->items[p->pos]; }
static Token *at(Parser *p, size_t off)
{
    size_t i = p->pos + off;
    if (i >= p->tokens->len) i = p->tokens->len - 1;
    return &p->tokens->items[i];
}
static Token *advance(Parser *p) { return &p->tokens->items[p->pos++]; }

static bool check(Parser *p, TokKind kind, const char *value)
{
    Token *t = peek(p);
    if (t->kind != kind) return false;
    if (!value) return true;
    return t->text && strcmp(t->text, value) == 0;
}

/* off 칸 앞의 토큰이 이것입니까? (지금 자리는 그대로 둡니다) */
static bool check_ahead(Parser *p, size_t off, TokKind kind, const char *value)
{
    Token *t = at(p, off);
    if (t->kind != kind) return false;
    if (!value) return true;
    return t->text && strcmp(t->text, value) == 0;
}

static Token *match(Parser *p, TokKind kind, const char *value)
{
    if (check(p, kind, value)) return advance(p);
    return NULL;
}

/* 파이썬의 repr() 처럼 보이게 만든 토큰 값 (오류 문구를 똑같이 맞추려고) */
static char *tok_repr(Token *t)
{
    if (t->kind == T_STRING) {
        char *u = str_to_utf8(t->str);
        char *out = xsprintf("'%s'", u);
        free(u);
        return out;
    }
    if (t->kind == T_NUMBER)
        return t->is_float ? fmt_double(t->fnum) : xsprintf("%lld", t->inum);
    if (t->text) return xsprintf("'%s'", t->text);
    return xstrdup("None");
}

static Token *expect(Parser *p, TokKind kind, const char *value)
{
    if (check(p, kind, value)) return advance(p);
    Token *t = peek(p);
    static const char *kind_names[] = {
        "NUMBER", "STRING", "IDENT", "KEYWORD", "OP",
        "NEWLINE", "INDENT", "DEDENT", "EOF"
    };
    char *got = tok_repr(t);
    if (value)
        lumi_error(t->line, "Expected '%s' but found %s", value, got);
    else
        lumi_error(t->line, "Expected '%s' but found %s", kind_names[kind], got);
    return NULL;
}

static void end_statement(Parser *p)
{
    if (check(p, T_NEWLINE, NULL)) { advance(p); return; }
    if (check(p, T_EOF, NULL) || check(p, T_DEDENT, NULL)) return;
    Token *t = peek(p);
    char *got = tok_repr(t);
    lumi_error(t->line, "Unexpected token at end of statement: %s", got);
}

/* ---------------- 앞선 선언들 ---------------- */
static Node *statement(Parser *p);
static Node *expression(Parser *p);
static Node *postfix(Parser *p);
static Node *unary(Parser *p);
static NodeList block(Parser *p);
static NodeList call_args(Parser *p);
static Node *if_statement(Parser *p);
static Node *func_statement(Parser *p);
static Node *test_statement(Parser *p);

/* ---------------- 대입 도우미 ---------------- */

static Node *make_assign(const char *name, Node *value, bool is_let, int line,
                         const char *type_name, const char *scope)
{
    Node *n = node_new(N_ASSIGN, line);
    n->v.assign.name = xstrdup(name);
    n->v.assign.value = value;
    n->v.assign.is_let = is_let;
    n->v.assign.type_name = type_name;
    n->v.assign.scope = scope;
    return n;
}

static Node *make_binop(const char *op, Node *l, Node *r, int line)
{
    Node *n = node_new(N_BINOP, line);
    n->v.binop.op = op;
    n->v.binop.left = l;
    n->v.binop.right = r;
    return n;
}

/* target <op>= value  ->  target = target <op> value */
static Node *compound_assign(Parser *p, Node *target)
{
    Token *op_tok = advance(p);
    const char *op = compound_op(op_tok->text);
    int line = op_tok->line;
    Node *rhs = expression(p);
    Node *combined = make_binop(op, target, rhs, line);
    if (target->kind == N_VAR)
        return make_assign(target->v.var.name, combined, false, target->line, NULL, NULL);
    if (target->kind == N_INDEX) {
        Node *n = node_new(N_INDEXASSIGN, line);
        n->v.indexassign.target = target->v.index.target;
        n->v.indexassign.index = target->v.index.index;
        n->v.indexassign.value = combined;
        return n;
    }
    if (target->kind == N_MEMBER) {
        Node *n = node_new(N_MEMBERASSIGN, line);
        n->v.memberassign.obj = target->v.member.obj;
        n->v.memberassign.name = target->v.member.name;
        n->v.memberassign.value = combined;
        return n;
    }
    lumi_error(line, "The left side of '%s' is not something you can assign to",
               op_tok->text);
    return NULL;
}

/* target++ / ++target  ->  target = target + 1 */
static Node *inc_dec(Node *target, const char *op_value, int line)
{
    const char *op = strcmp(op_value, "++") == 0 ? "+" : "-";
    Node *one = node_new(N_NUMBER, line);
    one->v.num.i = 1;
    Node *combined = make_binop(op, target, one, line);
    if (target->kind == N_VAR)
        return make_assign(target->v.var.name, combined, false, target->line, NULL, NULL);
    if (target->kind == N_INDEX) {
        Node *n = node_new(N_INDEXASSIGN, line);
        n->v.indexassign.target = target->v.index.target;
        n->v.indexassign.index = target->v.index.index;
        n->v.indexassign.value = combined;
        return n;
    }
    if (target->kind == N_MEMBER) {
        Node *n = node_new(N_MEMBERASSIGN, line);
        n->v.memberassign.obj = target->v.member.obj;
        n->v.memberassign.name = target->v.member.name;
        n->v.memberassign.value = combined;
        return n;
    }
    lumi_error(line, "'%s' can only be used on a variable, a list/dict element, "
                     "or an object member (for example: i++ or nums[0]-- or this.age++)",
               op_value);
    return NULL;
}

/* ---------------- 자료형 선언 ---------------- */

static bool is_typed_decl(Parser *p)
{
    Token *t = peek(p);
    if (!(t->kind == T_IDENT && t->text && is_type_name(t->text))) return false;
    if (p->pos + 1 >= p->tokens->len) return false;
    Token *nx = at(p, 1);
    if (nx->kind != T_IDENT) return false;
    if (p->pos + 2 >= p->tokens->len) return true;
    Token *nx2 = at(p, 2);
    if (nx2->kind == T_NEWLINE || nx2->kind == T_EOF || nx2->kind == T_DEDENT)
        return true;
    return nx2->kind == T_OP && nx2->text
           && (strcmp(nx2->text, "=") == 0 || strcmp(nx2->text, ",") == 0);
}

static Node *typed_decl_no_end(Parser *p)
{
    Token *type_tok = advance(p);
    char *type_name = xstrdup(type_tok->text);
    Token *name = expect(p, T_IDENT, NULL);
    char *nm = xstrdup(name->text);
    expect(p, T_OP, "=");
    Node *value = expression(p);
    Node *n = make_assign(nm, value, true, type_tok->line, type_name, NULL);
    free(nm);
    return n;
}

/* 이 문장 안에서 괄호 밖에 있는 '=' 의 개수 */
static int count_top_level_eq(Parser *p)
{
    int depth = 0, count = 0;
    for (size_t i = p->pos; i < p->tokens->len; i++) {
        Token *t = &p->tokens->items[i];
        if (t->kind == T_NEWLINE || t->kind == T_EOF || t->kind == T_DEDENT) break;
        if (t->kind == T_OP && t->text) {
            if (strcmp(t->text, "(") == 0 || strcmp(t->text, "[") == 0
                || strcmp(t->text, "{") == 0) depth++;
            else if (strcmp(t->text, ")") == 0 || strcmp(t->text, "]") == 0
                     || strcmp(t->text, "}") == 0) depth--;
            else if (strcmp(t->text, "=") == 0 && depth == 0) count++;
        }
    }
    return count;
}

static Node *typed_declaration(Parser *p)
{
    Token *type_tok = advance(p);
    char *type_name = xstrdup(type_tok->text);
    int line = type_tok->line;
    int eq_count = count_top_level_eq(p);

    NodeList assigns;
    memset(&assigns, 0, sizeof(assigns));

    if (eq_count == 0) {
        /* 이름만 적음 -> 자료형의 기본값 */
        do {
            Token *nm = expect(p, T_IDENT, NULL);
            nodelist_push(&assigns, make_assign(nm->text, NULL, true, line, type_name, NULL));
        } while (match(p, T_OP, ","));
    } else if (eq_count == 1) {
        /* 이름들 = 값들 (순서대로 짝짓기) */
        char **names = NULL;
        size_t nnames = 0;
        do {
            Token *nm = expect(p, T_IDENT, NULL);
            names = (char **)xrealloc(names, (nnames + 1) * sizeof(char *));
            names[nnames++] = xstrdup(nm->text);
        } while (match(p, T_OP, ","));
        expect(p, T_OP, "=");
        NodeList values;
        memset(&values, 0, sizeof(values));
        nodelist_push(&values, expression(p));
        while (match(p, T_OP, ",")) nodelist_push(&values, expression(p));
        if (nnames != values.len)
            lumi_error(line, "%zu name(s) but %zu value(s); the number of names "
                             "and values must match", nnames, values.len);
        for (size_t i = 0; i < nnames; i++)
            nodelist_push(&assigns, make_assign(names[i], values.items[i], true,
                                                line, type_name, NULL));
        for (size_t i = 0; i < nnames; i++) free(names[i]);
        free(names);
        free(values.items);
    } else {
        /* 이름마다 '= 값' 을 따로 적음 */
        for (;;) {
            Token *nm = expect(p, T_IDENT, NULL);
            char *name = xstrdup(nm->text);
            expect(p, T_OP, "=");
            Node *val = expression(p);
            nodelist_push(&assigns, make_assign(name, val, true, line, type_name, NULL));
            free(name);
            if (!match(p, T_OP, ",")) break;
        }
    }

    if (assigns.len == 1) {
        Node *only = assigns.items[0];
        free(assigns.items);
        return only;
    }
    Node *n = node_new(N_MULTI, line);
    n->v.multi.body = assigns;
    return n;
}

static Node *let_statement_no_end(Parser *p)
{
    int line = advance(p)->line;            /* 'val' */
    if (check(p, T_OP, "(") || check(p, T_OP, "[")) {
        Token *open_tok = advance(p);
        const char *close_str = strcmp(open_tok->text, "(") == 0 ? ")" : "]";
        char **names = NULL;
        size_t nnames = 0;
        do {
            Token *nm = expect(p, T_IDENT, NULL);
            names = (char **)xrealloc(names, (nnames + 1) * sizeof(char *));
            names[nnames++] = xstrdup(nm->text);
        } while (match(p, T_OP, ","));
        expect(p, T_OP, close_str);
        expect(p, T_OP, "=");
        Node *value = expression(p);
        Node *n = node_new(N_MULTI_ASSIGN, line);
        n->v.multi_assign.names = names;
        n->v.multi_assign.nnames = nnames;
        n->v.multi_assign.value = value;
        n->v.multi_assign.is_let = true;
        return n;
    }
    Token *name = expect(p, T_IDENT, NULL);
    char *nm = xstrdup(name->text);
    expect(p, T_OP, "=");
    Node *value = expression(p);
    Node *n = make_assign(nm, value, true, line, NULL, NULL);
    free(nm);
    return n;
}

/* ---------------- 블록 ---------------- */

static NodeList block(Parser *p)
{
    expect(p, T_OP, ":");
    expect(p, T_NEWLINE, NULL);
    expect(p, T_INDENT, NULL);
    NodeList body;
    memset(&body, 0, sizeof(body));
    while (!check(p, T_DEDENT, NULL) && !check(p, T_EOF, NULL)) {
        if (check(p, T_NEWLINE, NULL)) { advance(p); continue; }
        nodelist_push(&body, statement(p));
    }
    match(p, T_DEDENT, NULL);
    return body;
}

/* ---------------- 각 문장 ---------------- */

static Node *if_statement(Parser *p)
{
    advance(p);                              /* 'if' 또는 'elif' */
    Node *n = node_new(N_IF, peek(p)->line);
    n->v.if_.cond = expression(p);
    n->v.if_.then_body = block(p);
    if (check(p, T_KEYWORD, "elif")) {
        n->v.if_.has_else = true;
        nodelist_push(&n->v.if_.else_body, if_statement(p));
    } else if (match(p, T_KEYWORD, "else")) {
        n->v.if_.has_else = true;
        n->v.if_.else_body = block(p);
    }
    return n;
}

static Node *while_statement(Parser *p)
{
    int line = advance(p)->line;
    Node *n = node_new(N_WHILE, line);
    n->v.while_.cond = expression(p);
    n->v.while_.body = block(p);
    return n;
}

static Node *switch_statement(Parser *p)
{
    int line = advance(p)->line;             /* 'switch' */
    Node *n = node_new(N_SWITCH, line);
    n->v.switch_.subject = expression(p);
    expect(p, T_OP, ":");
    expect(p, T_NEWLINE, NULL);
    expect(p, T_INDENT, NULL);

    size_t cap = 0;
    while (!check(p, T_DEDENT, NULL) && !check(p, T_EOF, NULL)) {
        if (check(p, T_NEWLINE, NULL)) { advance(p); continue; }
        if (match(p, T_KEYWORD, "case")) {
            if (n->v.switch_.ncases == cap) {
                cap = cap ? cap * 2 : 4;
                n->v.switch_.cases = (struct SwitchCase *)xrealloc(
                    n->v.switch_.cases, cap * sizeof(struct SwitchCase));
            }
            struct SwitchCase *c = &n->v.switch_.cases[n->v.switch_.ncases++];
            memset(c, 0, sizeof(*c));
            c->is_default = false;
            nodelist_push(&c->values, expression(p));
            while (match(p, T_OP, ",")) nodelist_push(&c->values, expression(p));
            c->body = block(p);
        } else if (match(p, T_KEYWORD, "default")) {
            if (n->v.switch_.has_default)
                lumi_error(line, "switch can have only one 'default'");
            n->v.switch_.has_default = true;
            if (n->v.switch_.ncases == cap) {
                cap = cap ? cap * 2 : 4;
                n->v.switch_.cases = (struct SwitchCase *)xrealloc(
                    n->v.switch_.cases, cap * sizeof(struct SwitchCase));
            }
            struct SwitchCase *c = &n->v.switch_.cases[n->v.switch_.ncases++];
            memset(c, 0, sizeof(*c));
            c->is_default = true;
            c->body = block(p);
        } else {
            Token *t = peek(p);
            char *got = tok_repr(t);
            lumi_error(t->line, "inside 'switch' only 'case ...:' or 'default:' "
                                "are allowed, but got %s", got);
        }
    }
    match(p, T_DEDENT, NULL);
    return n;
}

/* C 스타일 for 의 한 칸 (초기화 / 증감) */
static Node *for_init_or_step(Parser *p)
{
    if (check(p, T_KEYWORD, "val")) return let_statement_no_end(p);
    if (is_typed_decl(p)) return typed_decl_no_end(p);
    if (check(p, T_OP, "++") || check(p, T_OP, "--")) {
        Token *op = advance(p);
        Node *target = postfix(p);
        return inc_dec(target, op->text, op->line);
    }
    if (peek(p)->kind == T_IDENT && at(p, 1)->kind == T_OP
        && at(p, 1)->text && strcmp(at(p, 1)->text, "=") == 0) {
        Token *t = peek(p);
        char *name = xstrdup(advance(p)->text);
        expect(p, T_OP, "=");
        Node *value = expression(p);
        Node *n = make_assign(name, value, false, t->line, NULL, NULL);
        free(name);
        return n;
    }
    Node *expr = expression(p);
    if (peek(p)->kind == T_OP && peek(p)->text
        && (strcmp(peek(p)->text, "++") == 0 || strcmp(peek(p)->text, "--") == 0)) {
        Token *op = advance(p);
        return inc_dec(expr, op->text, op->line);
    }
    if (peek(p)->kind == T_OP && peek(p)->text && compound_op(peek(p)->text))
        return compound_assign(p, expr);
    return expr;
}

static Node *for_statement(Parser *p)
{
    int line = advance(p)->line;             /* 'for' */

    if (check(p, T_KEYWORD, "val") && at(p, 2)->kind == T_KEYWORD
        && at(p, 2)->text && strcmp(at(p, 2)->text, "in") == 0) {
        advance(p);                          /* skip 'val' */
    }

    bool c_style = check(p, T_KEYWORD, "val");
    if (!c_style && peek(p)->kind == T_IDENT) {
        Token *nx = at(p, 1);
        c_style = !(nx->kind == T_KEYWORD && nx->text && strcmp(nx->text, "in") == 0);
    }
    if (c_style) {
        Node *n = node_new(N_FORC, line);
        n->v.forc.init = for_init_or_step(p);
        expect(p, T_OP, ",");
        n->v.forc.cond = expression(p);
        expect(p, T_OP, ",");
        n->v.forc.step = for_init_or_step(p);
        n->v.forc.body = block(p);
        return n;
    }

    /* 파이썬 스타일:  for x in 식:  */
    Node *n = node_new(N_FORIN, line);
    Token *var = expect(p, T_IDENT, NULL);
    n->v.forin.var_name = xstrdup(var->text);
    expect(p, T_KEYWORD, "in");
    n->v.forin.iterable = expression(p);
    n->v.forin.body = block(p);
    return n;
}

static Node *func_statement_abstract(Parser *p, bool is_abstract)
{
    int line = advance(p)->line;             /* 'func' */
    Token *name = expect(p, T_IDENT, NULL);
    FuncDefNode *fd = (FuncDefNode *)xmalloc(sizeof(FuncDefNode));
    memset(fd, 0, sizeof(*fd));
    fd->name = xstrdup(name->text);
    fd->is_abstract = is_abstract;
    if (check(p, T_OP, ":") || check(p, T_OP, ","))
        lumi_error(peek(p)->line,
                   "a nameless function is a value, so keep it somewhere: "
                   "val name = func %s: ...", name->text);
    expect(p, T_OP, "(");
    if (!check(p, T_OP, ")")) {
        bool seen_default = false;
        do {
            /* 'int a' 처럼 자료형을 앞에 적을 수 있습니다 (안 적어도 됩니다) */
            char *ptype = NULL;
            if (check(p, T_IDENT, NULL) && is_type_name(peek(p)->text)
                && check_ahead(p, 1, T_IDENT, NULL))
                ptype = xstrdup(advance(p)->text);
            Token *pm = expect(p, T_IDENT, NULL);
            fd->params   = (char **)xrealloc(fd->params, (fd->nparams + 1) * sizeof(char *));
            fd->defaults = (Node **)xrealloc(fd->defaults, (fd->nparams + 1) * sizeof(Node *));
            fd->ptypes   = (char **)xrealloc(fd->ptypes, (fd->nparams + 1) * sizeof(char *));
            fd->params[fd->nparams]   = xstrdup(pm->text);
            fd->defaults[fd->nparams] = NULL;
            fd->ptypes[fd->nparams]   = ptype;
            /* 'a = 1' 처럼 기본값을 적을 수 있습니다 */
            if (match(p, T_OP, "=")) {
                fd->defaults[fd->nparams] = expression(p);
                seen_default = true;
            } else if (seen_default) {
                lumi_error(pm->line,
                           "'%s' has no default value, but a parameter before it does; "
                           "put the ones with '=' last, like func f(a, b = 2)", pm->text);
            }
            fd->nparams++;
        } while (match(p, T_OP, ","));
    }
    expect(p, T_OP, ")");
    /* ')' 뒤에 적으면 돌려주는 값의 자료형입니다:  func f(int a) str: */
    if (check(p, T_IDENT, NULL) && is_type_name(peek(p)->text))
        fd->ret_type = xstrdup(advance(p)->text);
    if (is_abstract) {
        if (check(p, T_OP, ":")) {
            advance(p);
            if (check(p, T_NEWLINE, NULL)) advance(p);
        }
    } else {
        fd->body = block(p);
    }
    Node *n = node_new(N_FUNCDEF, line);
    n->v.funcdef = fd;
    return n;
}

static Node *func_statement(Parser *p)
{
    bool is_abstract = false;
    if (match(p, T_KEYWORD, "abstract")) is_abstract = true;
    return func_statement_abstract(p, is_abstract);
}

static Node *class_statement(Parser *p)
{
    int line = advance(p)->line;             /* 'class' */
    Token *name = expect(p, T_IDENT, NULL);
    ClassDefNode *cd = (ClassDefNode *)xmalloc(sizeof(ClassDefNode));
    memset(cd, 0, sizeof(*cd));
    cd->name = xstrdup(name->text);
    if (match(p, T_KEYWORD, "from"))
        cd->parent_name = xstrdup(expect(p, T_IDENT, NULL)->text);

    expect(p, T_OP, ":");
    expect(p, T_NEWLINE, NULL);
    expect(p, T_INDENT, NULL);

    size_t cap = 0;
    while (!check(p, T_DEDENT, NULL) && !check(p, T_EOF, NULL)) {
        if (check(p, T_NEWLINE, NULL)) { advance(p); continue; }
        if (check(p, T_KEYWORD, "func") || check(p, T_KEYWORD, "abstract")) {
            bool is_abs = match(p, T_KEYWORD, "abstract");
            if (cd->nmethods == cap) {
                cap = cap ? cap * 2 : 4;
                cd->methods = (Node **)xrealloc(cd->methods, cap * sizeof(Node *));
            }
            cd->methods[cd->nmethods++] = func_statement_abstract(p, is_abs);
            continue;
        }
        if (check(p, T_KEYWORD, "val") || is_typed_decl(p)) {
            nodelist_push(&cd->fields, statement(p));
            continue;
        }
        Token *t = peek(p);
        char *got = tok_repr(t);
        lumi_error(t->line, "inside 'class %s' only methods ('func ...') and shared "
                            "values ('val ...') are allowed, but got %s", cd->name, got);
    }
    match(p, T_DEDENT, NULL);

    for (size_t i = 0; i < cd->nmethods; i++)
        for (size_t j = 0; j < i; j++)
            if (strcmp(cd->methods[i]->v.funcdef->name,
                       cd->methods[j]->v.funcdef->name) == 0)
                lumi_error(line, "class '%s' defines the method '%s' twice",
                           cd->name, cd->methods[i]->v.funcdef->name);

    Node *n = node_new(N_CLASSDEF, line);
    n->v.classdef = cd;
    return n;
}

static Node *return_statement(Parser *p)
{
    int line = advance(p)->line;
    Node *n = node_new(N_RETURN, line);
    if (!(check(p, T_NEWLINE, NULL) || check(p, T_EOF, NULL) || check(p, T_DEDENT, NULL)))
        n->v.ret.value = expression(p);
    end_statement(p);
    return n;
}

static Node *scoped_let_statement(Parser *p, const char *scope)
{
    int line = advance(p)->line;             /* 'global' 또는 'local' */
    Token *name = expect(p, T_IDENT, NULL);
    char *nm = xstrdup(name->text);
    expect(p, T_OP, "=");
    Node *value = expression(p);
    end_statement(p);
    Node *n = make_assign(nm, value, true, line, NULL, scope);
    free(nm);
    return n;
}

/* bring 라이브러리
 * bring "폴더/파일"            <- 경로는 따옴표로 (파일 함수들과 같은 방식)
 * bring 이름 = "폴더/파일"      <- 담을 이름 고르기 (use 블록과 같은 모양)
 * bring 무엇 up 이것, 저것      <- 골라 오기 */
static Node *bring_statement(Parser *p)
{
    int line = advance(p)->line;             /* 'bring' */
    Node *n = node_new(N_BRING, line);

    /* '이름 =' 이 앞에 붙었으면 담을 이름을 먼저 읽습니다 */
    if (check(p, T_IDENT, NULL) && check_ahead(p, 1, T_OP, "=")) {
        n->v.bring.as_name = xstrdup(advance(p)->text);
        advance(p);                          /* '=' */
    }

    if (check(p, T_STRING, NULL)) {
        /* 문자열 토큰의 값은 text 가 아니라 str(코드포인트)에 들어 있습니다 */
        n->v.bring.lib = str_to_utf8(advance(p)->str);
        n->v.bring.is_path = true;
        if (!*n->v.bring.lib)
            lumi_error(line, "'bring' needs a file path inside the quotes, but it was empty");
    } else {
        n->v.bring.lib = xstrdup(expect(p, T_IDENT, NULL)->text);
    }
    n->v.bring.all = true;
    if (match(p, T_KEYWORD, "up")) {
        n->v.bring.all = false;
        while (check(p, T_IDENT, NULL) || check(p, T_OP, ",")) {
            if (match(p, T_OP, ",")) continue;
            Token *t = advance(p);
            n->v.bring.names = (char **)xrealloc(
                n->v.bring.names, (n->v.bring.nnames + 1) * sizeof(char *));
            n->v.bring.names[n->v.bring.nnames++] = xstrdup(t->text);
        }
        if (n->v.bring.nnames == 0)
            lumi_error(line, "'bring ... up' needs at least one function name");
    }
    end_statement(p);
    return n;
}

/* use f = open(read, "note.txt"):  — 블록이 끝나면 저절로 닫습니다 */
static Node *use_statement(Parser *p)
{
    int line = advance(p)->line;             /* 'use' */
    Node *n = node_new(N_USE, line);
    n->v.use.var_name = xstrdup(expect(p, T_IDENT, NULL)->text);
    expect(p, T_OP, "=");
    n->v.use.value = expression(p);
    n->v.use.body = block(p);
    return n;
}

/* try 블록 뒤에 catch / safe / always 를 아무 차례로나 이어 붙입니다.
 *   catch 는 여러 개 (앞에서부터 맞는 것 하나만 실행),
 *   safe 와 always 는 각각 하나씩만 둘 수 있습니다. */
static Node *try_statement(Parser *p)
{
    int line = advance(p)->line;             /* 'try' */
    Node *n = node_new(N_TRY, line);
    n->v.try_.try_body = block(p);

    size_t cap = 0;
    while (check(p, T_KEYWORD, "catch") || check(p, T_KEYWORD, "safe") || check(p, T_KEYWORD, "always")) {
        if (check(p, T_KEYWORD, "catch")) {
            int catch_line = peek(p)->line;
            advance(p);                      /* 'catch' */
            if (n->v.try_.ncatches == cap) {
                cap = cap ? cap * 2 : 2;
                n->v.try_.catches = (struct CatchClause *)xrealloc(
                    n->v.try_.catches, cap * sizeof(struct CatchClause));
            }
            struct CatchClause *cc = &n->v.try_.catches[n->v.try_.ncatches++];
            memset(cc, 0, sizeof(*cc));
            cc->line = catch_line;

            /* catch 뒤에 올 수 있는 모양: 아무것도 없음 / 이름 하나 / 타입 이름 + 이름.
             * 이름이 하나뿐일 때는 '대문자로 시작하면 타입, 아니면 담을 변수'로 봅니다
             * (클래스 이름을 대문자로 짓는 관습을 그대로 씁니다). */
            if (!check(p, T_OP, ":")) {
                Token *t1 = expect(p, T_IDENT, NULL);
                if (check(p, T_IDENT, NULL)) {
                    cc->type_name = xstrdup(t1->text);
                    Token *t2 = advance(p);
                    cc->var_name = xstrdup(t2->text);
                } else {
                    if (t1->text[0] >= 'A' && t1->text[0] <= 'Z') {
                        cc->type_name = xstrdup(t1->text);
                    } else {
                        cc->var_name = xstrdup(t1->text);
                    }
                }
            }
            cc->body = block(p);
        } else if (check(p, T_KEYWORD, "safe")) {
            advance(p);                      /* 'safe' */
            if (n->v.try_.has_safe)
                lumi_error(line, "'try' statement can have only one 'safe' block");
            n->v.try_.has_safe = true;
            n->v.try_.safe_body = block(p);
        } else if (check(p, T_KEYWORD, "always")) {
            advance(p);                      /* 'always' */
            if (n->v.try_.has_always)
                lumi_error(line, "'try' statement can have only one 'always' block");
            n->v.try_.has_always = true;
            n->v.try_.always_body = block(p);
        }
    }

    return n;
}

/* test "이름":
 *     ...확인할 것들...
 * lumi test 로 돌릴 때만 실행됩니다.  그냥 lumi 파일.lumi 로 돌리면 건너뜁니다. */
static Node *test_statement(Parser *p)
{
    int line = advance(p)->line;             /* 'test' */
    if (!check(p, T_STRING, NULL))
        lumi_error(line, "a test needs a name in quotes, like test \"adding works\":");
    Node *n = node_new(N_TEST, line);
    n->v.test_.name = str_to_utf8(advance(p)->str);
    n->v.test_.body = block(p);
    return n;
}

/* error("메시지")  또는  error("타입", "메시지")  — 괄호는 반드시 씁니다. */
static Node *error_statement(Parser *p)
{
    int line = advance(p)->line;             /* 'error' */
    Node *n = node_new(N_ERROR, line);
    expect(p, T_OP, "(");
    Node *first = expression(p);
    if (match(p, T_OP, ",")) {
        n->v.error_.type_expr = first;
        n->v.error_.msg_expr = expression(p);
    } else {
        n->v.error_.msg_expr = first;
    }
    expect(p, T_OP, ")");
    end_statement(p);
    return n;
}

static Node *statement(Parser *p)
{
    if (check(p, T_KEYWORD, "val")) {
        Node *n = let_statement_no_end(p);
        end_statement(p);
        return n;
    }
    if (check(p, T_KEYWORD, "if")) return if_statement(p);
    if (check(p, T_KEYWORD, "while")) return while_statement(p);
    if (check(p, T_KEYWORD, "switch")) return switch_statement(p);
    if (check(p, T_KEYWORD, "for")) return for_statement(p);
    if (check(p, T_KEYWORD, "func")) return func_statement(p);
    if (check(p, T_KEYWORD, "class")) return class_statement(p);
    if (check(p, T_KEYWORD, "return")) return return_statement(p);
    if (check(p, T_KEYWORD, "break")) {
        int line = advance(p)->line;
        end_statement(p);
        return node_new(N_BREAK, line);
    }
    if (check(p, T_KEYWORD, "continue")) {
        int line = advance(p)->line;
        end_statement(p);
        return node_new(N_CONTINUE, line);
    }
    if (check(p, T_KEYWORD, "use")) return use_statement(p);
    if (check(p, T_KEYWORD, "try")) return try_statement(p);
    if (check(p, T_KEYWORD, "error")) return error_statement(p);
    if (check(p, T_KEYWORD, "test")) return test_statement(p);
    if (check(p, T_KEYWORD, "bring")) return bring_statement(p);
    if (check(p, T_KEYWORD, "global")) return scoped_let_statement(p, "global");
    if (check(p, T_KEYWORD, "local")) return scoped_let_statement(p, "local");

    if (is_typed_decl(p)) {
        Node *n = typed_declaration(p);
        end_statement(p);
        return n;
    }

    /* 접두 증감:  ++i  /  --nums[k] */
    if (check(p, T_OP, "++") || check(p, T_OP, "--")) {
        Token *op = advance(p);
        Node *target = postfix(p);
        Node *n = inc_dec(target, op->text, op->line);
        end_statement(p);
        return n;
    }

    Node *expr = expression(p);
    if (check(p, T_OP, "=")) {
        int line = advance(p)->line;
        Node *value = expression(p);
        end_statement(p);
        if (expr->kind == N_VAR)
            return make_assign(expr->v.var.name, value, false, expr->line, NULL, NULL);
        if (expr->kind == N_INDEX) {
            Node *n = node_new(N_INDEXASSIGN, line);
            n->v.indexassign.target = expr->v.index.target;
            n->v.indexassign.index = expr->v.index.index;
            n->v.indexassign.value = value;
            return n;
        }
        if (expr->kind == N_MEMBER) {
            if (expr->v.member.optional)
                lumi_error(line, "'?.' only reads; write '.' on the left side of '='");
            Node *n = node_new(N_MEMBERASSIGN, line);
            n->v.memberassign.obj = expr->v.member.obj;
            n->v.memberassign.name = expr->v.member.name;
            n->v.memberassign.value = value;
            return n;
        }
        lumi_error(line, "The left side of '=' is not something you can assign to");
    }
    if (peek(p)->kind == T_OP && peek(p)->text
        && (strcmp(peek(p)->text, "++") == 0 || strcmp(peek(p)->text, "--") == 0)) {
        Token *op = advance(p);
        Node *n = inc_dec(expr, op->text, op->line);
        end_statement(p);
        return n;
    }
    if (peek(p)->kind == T_OP && peek(p)->text && compound_op(peek(p)->text)) {
        Node *n = compound_assign(p, expr);
        end_statement(p);
        return n;
    }
    end_statement(p);
    return expr;
}

/* ---------------- 식 (연산자 우선순위) ---------------- */

static Node *nullish(Parser *p);
static Node *logic_and(Parser *p);
static Node *equality(Parser *p);
static Node *comparison(Parser *p);
static Node *bit_or(Parser *p);
static Node *bit_xor(Parser *p);
static Node *bit_and(Parser *p);
static Node *shift(Parser *p);
static Node *addition(Parser *p);
static Node *multiplication(Parser *p);
static Node *power(Parser *p);
static Node *primary(Parser *p);

static Node *expression(Parser *p)
{
    Node *left = nullish(p);
    while (check(p, T_KEYWORD, "or") || check(p, T_OP, "||")) {
        int line = advance(p)->line;
        left = make_binop("or", left, nullish(p), line);
    }
    return left;
}

/* a ?? b — a 가 none 일 때만 b.  or 보다 붙고 and 보다 헐겁습니다. */
static Node *nullish(Parser *p)
{
    Node *left = logic_and(p);
    while (check(p, T_OP, "??")) {
        int line = advance(p)->line;
        left = make_binop("??", left, logic_and(p), line);
    }
    return left;
}

static Node *logic_and(Parser *p)
{
    Node *left = equality(p);
    while (check(p, T_KEYWORD, "and") || check(p, T_OP, "&&")) {
        int line = advance(p)->line;
        left = make_binop("and", left, equality(p), line);
    }
    return left;
}

static Node *equality(Parser *p)
{
    Node *left = comparison(p);
    while (check(p, T_OP, "==") || check(p, T_OP, "!=")) {
        Token *op = advance(p);
        const char *o = strcmp(op->text, "==") == 0 ? "==" : "!=";
        left = make_binop(o, left, comparison(p), op->line);
    }
    return left;
}

static const char *fixed_op(const char *s)
{
    static const char *ops[] = {
        "+", "-", "*", "/", "%", "**", "&", "|", "^", "<<", ">>",
        "<", ">", "<=", ">=", "==", "!=", NULL
    };
    for (size_t i = 0; ops[i]; i++) if (strcmp(ops[i], s) == 0) return ops[i];
    return NULL;
}

static Node *comparison(Parser *p)
{
    Node *left = bit_or(p);
    for (;;) {
        if (check(p, T_OP, "<") || check(p, T_OP, ">")
            || check(p, T_OP, "<=") || check(p, T_OP, ">=")) {
            Token *op = advance(p);
            left = make_binop(fixed_op(op->text), left, bit_or(p), op->line);
        } else if (check(p, T_KEYWORD, "in")) {
            int line = advance(p)->line;
            left = make_binop("in", left, bit_or(p), line);
        } else if (check(p, T_KEYWORD, "not") && at(p, 1)->kind == T_KEYWORD
                   && at(p, 1)->text && strcmp(at(p, 1)->text, "in") == 0) {
            int line = advance(p)->line;
            advance(p);
            left = make_binop("not in", left, bit_or(p), line);
        } else {
            break;
        }
    }
    return left;
}

static Node *bit_or(Parser *p)
{
    Node *left = bit_xor(p);
    while (check(p, T_OP, "|")) {
        int line = advance(p)->line;
        left = make_binop("|", left, bit_xor(p), line);
    }
    return left;
}

static Node *bit_xor(Parser *p)
{
    Node *left = bit_and(p);
    while (check(p, T_OP, "^")) {
        int line = advance(p)->line;
        left = make_binop("^", left, bit_and(p), line);
    }
    return left;
}

static Node *bit_and(Parser *p)
{
    Node *left = shift(p);
    while (check(p, T_OP, "&")) {
        int line = advance(p)->line;
        left = make_binop("&", left, shift(p), line);
    }
    return left;
}

static Node *shift(Parser *p)
{
    Node *left = addition(p);
    while (check(p, T_OP, "<<") || check(p, T_OP, ">>")) {
        Token *op = advance(p);
        left = make_binop(fixed_op(op->text), left, addition(p), op->line);
    }
    return left;
}

static Node *addition(Parser *p)
{
    Node *left = multiplication(p);
    while (check(p, T_OP, "+") || check(p, T_OP, "-")) {
        Token *op = advance(p);
        left = make_binop(fixed_op(op->text), left, multiplication(p), op->line);
    }
    return left;
}

static Node *multiplication(Parser *p)
{
    Node *left = unary(p);
    while (check(p, T_OP, "*") || check(p, T_OP, "/") || check(p, T_OP, "%")) {
        Token *op = advance(p);
        left = make_binop(fixed_op(op->text), left, unary(p), op->line);
    }
    return left;
}

static Node *unary(Parser *p)
{
    if (check(p, T_OP, "-")) {
        Token *op = advance(p);
        Node *n = node_new(N_UNARY, op->line);
        n->v.unary.op = "-";
        n->v.unary.operand = unary(p);
        return n;
    }
    if (check(p, T_KEYWORD, "not") || check(p, T_OP, "!")) {
        Token *op = advance(p);
        Node *n = node_new(N_UNARY, op->line);
        n->v.unary.op = "not";
        n->v.unary.operand = unary(p);
        return n;
    }
    if (check(p, T_OP, "~")) {
        Token *op = advance(p);
        Node *n = node_new(N_UNARY, op->line);
        n->v.unary.op = "~";
        n->v.unary.operand = unary(p);
        return n;
    }
    return power(p);
}

static Node *power(Parser *p)
{
    Node *base = postfix(p);
    if (check(p, T_OP, "**")) {
        Token *op = advance(p);
        return make_binop("**", base, unary(p), op->line);
    }
    return base;
}

static Node *postfix(Parser *p)
{
    Node *expr = primary(p);
    while (check(p, T_OP, "[") || check(p, T_OP, ".") || check(p, T_OP, "(")
           || check(p, T_OP, "?.")) {
        /* 앞의 식이 함수 값일 때 바로 부르기: xs[0](3), outer(5)(3), 짝[i](x)
         * (이름으로 부르는 f(3) 은 primary 가 이미 N_CALL 로 만들어 둡니다) */
        if (check(p, T_OP, "(")) {
            int line = peek(p)->line;
            Node *n = node_new(N_CALLV, line);
            n->v.callv.callee = expr;
            n->v.callv.args = call_args(p);
            expr = n;
            continue;
        }
        if (check(p, T_OP, ".") || check(p, T_OP, "?.")) {
            bool optional = check(p, T_OP, "?.");
            int line = advance(p)->line;
            Token *name = expect(p, T_IDENT, NULL);
            if (check(p, T_OP, "(")) {
                Node *n = node_new(N_METHODCALL, line);
                n->v.methodcall.obj = expr;
                n->v.methodcall.name = xstrdup(name->text);
                n->v.methodcall.args = call_args(p);
                n->v.methodcall.optional = optional;
                expr = n;
            } else {
                Node *n = node_new(N_MEMBER, line);
                n->v.member.obj = expr;
                n->v.member.name = xstrdup(name->text);
                n->v.member.optional = optional;
                expr = n;
            }
            continue;
        }
        int line = advance(p)->line;         /* '[' */
        Node *start = NULL;
        if (!check(p, T_OP, ":")) start = expression(p);
        if (match(p, T_OP, ":")) {
            Node *end = NULL;
            if (!check(p, T_OP, "]")) end = expression(p);
            expect(p, T_OP, "]");
            Node *n = node_new(N_SLICE, line);
            n->v.slice.target = expr;
            n->v.slice.start = start;
            n->v.slice.end = end;
            expr = n;
        } else {
            if (!start)
                lumi_error(line, "Empty [] is not allowed; put a number inside");
            expect(p, T_OP, "]");
            Node *n = node_new(N_INDEX, line);
            n->v.index.target = expr;
            n->v.index.index = start;
            expr = n;
        }
    }
    return expr;
}

static Node *range_end(Parser *p)
{
    if (check(p, T_OP, ",") || check(p, T_OP, ")")) return NULL;
    return expression(p);
}

/* 한 개의 인자.  'start:end' 처럼 구간으로, 또는 '이름 = 값' 처럼 이름을 붙여 적을 수도 있습니다. */
static Node *call_arg(Parser *p)
{
    int line = peek(p)->line;
    /* 이름 붙인 인자: f(칸 = 3).  '==' 와 헷갈리지 않도록 '=' 하나만 봅니다. */
    if (check(p, T_IDENT, NULL) && check_ahead(p, 1, T_OP, "=")) {
        Node *n = node_new(N_KWARG, line);
        n->v.kwarg.name = xstrdup(advance(p)->text);
        advance(p);                          /* '=' */
        n->v.kwarg.value = expression(p);
        return n;
    }
    if (check(p, T_OP, ":")) {
        advance(p);
        Node *n = node_new(N_RANGESPEC, line);
        n->v.rangespec.end = range_end(p);
        return n;
    }
    Node *value = expression(p);
    if (match(p, T_OP, ":")) {
        Node *n = node_new(N_RANGESPEC, line);
        n->v.rangespec.start = value;
        n->v.rangespec.end = range_end(p);
        return n;
    }
    return value;
}

static NodeList call_args(Parser *p)
{
    expect(p, T_OP, "(");
    NodeList args;
    memset(&args, 0, sizeof(args));
    if (!check(p, T_OP, ")")) {
        nodelist_push(&args, call_arg(p));
        while (match(p, T_OP, ",")) nodelist_push(&args, call_arg(p));
    }
    expect(p, T_OP, ")");
    return args;
}

static Node *forr_expr(Parser *p)
{
    int line = peek(p)->line;
    advance(p);                              /* 'forr' */
    expect(p, T_OP, "(");
    Node *init = for_init_or_step(p);
    if (!(init->kind == N_ASSIGN && init->v.assign.is_let))
        lumi_error(line, "forr's first part must be a 'val' variable declaration "
                         "(for example: forr(val i = 0, ...))");
    expect(p, T_OP, ",");
    Node *cond = expression(p);
    expect(p, T_OP, ",");
    Node *step = for_init_or_step(p);
    expect(p, T_OP, ")");
    Node *n = node_new(N_FORR, line);
    n->v.forr.init = init;
    n->v.forr.cond = cond;
    n->v.forr.step = step;
    n->v.forr.var_name = xstrdup(init->v.assign.name);
    return n;
}

/* 이름 없는 한 줄 기능:  func 인자, 인자: 식
 * (문장 자리의 'func 이름(...)' 은 func_statement 가 먼저 가져갑니다) */
static Node *lambda_expr(Parser *p)
{
    int line = advance(p)->line;             /* 'func' */
    FuncDefNode *fd = (FuncDefNode *)xmalloc(sizeof(FuncDefNode));
    memset(fd, 0, sizeof(*fd));
    fd->name = xstrdup("nameless");
    while (!check(p, T_OP, ":")) {
        if (!check(p, T_IDENT, NULL)) {
            char *got = tok_repr(peek(p));
            lumi_error(peek(p)->line,
                       "a nameless function is written 'func x, y: x + y' "
                       "(names, then ':', then one expression), but got %s", got);
        }
        Token *pm = advance(p);
        fd->params   = (char **)xrealloc(fd->params, (fd->nparams + 1) * sizeof(char *));
        fd->defaults = (Node **)xrealloc(fd->defaults, (fd->nparams + 1) * sizeof(Node *));
        fd->params[fd->nparams]   = xstrdup(pm->text);
        fd->defaults[fd->nparams] = NULL;
        fd->nparams++;
        if (!match(p, T_OP, ",")) break;
    }
    expect(p, T_OP, ":");
    Node *ret = node_new(N_RETURN, line);
    ret->v.ret.value = expression(p);
    nodelist_push(&fd->body, ret);
    Node *n = node_new(N_FUNCDEF, line);
    n->v.funcdef = fd;
    return n;
}

static Node *parse_fstring(Parser *p, Token *t)
{
    advance(p);
    char *utf8 = str_to_utf8(t->str);
    size_t len = strlen(utf8);
    Node *n = node_new(N_FSTRING, t->line);
    memset(&n->v.fstring.parts, 0, sizeof(n->v.fstring.parts));

    size_t i = 0;
    while (i < len) {
        if (utf8[i] == '{') {
            if (i + 1 < len && utf8[i + 1] == '{') {
                Node *st = node_new(N_STRING, t->line);
                st->v.str.value = str_from_utf8_n("{", 1);
                nodelist_push(&n->v.fstring.parts, st);
                i += 2;
                continue;
            }
            i++;
            size_t start = i;
            int depth = 1;
            while (i < len && depth > 0) {
                if (utf8[i] == '{') depth++;
                else if (utf8[i] == '}') depth--;
                if (depth > 0) i++;
            }
            if (depth > 0)
                lumi_error(t->line, "Unclosed '{' in f-string");
            size_t expr_len = i - start;
            i++;
            char *expr_str = (char *)xmalloc(expr_len + 1);
            memcpy(expr_str, utf8 + start, expr_len);
            expr_str[expr_len] = 0;

            TokenList *expr_tokens = tokenize(expr_str);
            free(expr_str);
            Parser inner_p;
            memset(&inner_p, 0, sizeof(inner_p));
            inner_p.tokens = expr_tokens;
            inner_p.pos = 0;

            Node *expr_node = expression(&inner_p);
            tokenlist_free(expr_tokens);
            nodelist_push(&n->v.fstring.parts, expr_node);
        } else if (utf8[i] == '}') {
            if (i + 1 < len && utf8[i + 1] == '}') {
                Node *st = node_new(N_STRING, t->line);
                st->v.str.value = str_from_utf8_n("}", 1);
                nodelist_push(&n->v.fstring.parts, st);
                i += 2;
                continue;
            }
            lumi_error(t->line, "Unmatched '}' in f-string");
        } else {
            size_t start = i;
            while (i < len && utf8[i] != '{' && utf8[i] != '}') i++;
            Node *st = node_new(N_STRING, t->line);
            st->v.str.value = str_from_utf8_n(utf8 + start, i - start);
            nodelist_push(&n->v.fstring.parts, st);
        }
    }
    free(utf8);
    return n;
}

static Node *primary(Parser *p)
{
    Token *t = peek(p);

    if (t->kind == T_NUMBER) {
        advance(p);
        Node *n = node_new(N_NUMBER, t->line);
        n->v.num.is_float = t->is_float;
        n->v.num.i = t->inum;
        n->v.num.f = t->fnum;
        return n;
    }
    if (t->kind == T_STRING) {
        advance(p);
        Node *n = node_new(N_STRING, t->line);
        n->v.str.value = (Str *)retain(obj_val(t->str)).as.o;
        return n;
    }
    if (t->kind == T_FSTRING) {
        return parse_fstring(p, t);
    }
    if (check(p, T_KEYWORD, "true") || check(p, T_KEYWORD, "false")) {
        bool value = strcmp(advance(p)->text, "true") == 0;
        Node *n = node_new(N_BOOL, t->line);
        n->v.bool_.value = value;
        return n;
    }
    if (check(p, T_KEYWORD, "none")) {
        advance(p);
        return node_new(N_NONE, t->line);
    }
    if (check(p, T_KEYWORD, "print")) {
        advance(p);
        Node *n = node_new(N_CALL, t->line);
        n->v.call.name = xstrdup("print");
        n->v.call.args = call_args(p);
        return n;
    }
    if (check(p, T_KEYWORD, "func")) return lambda_expr(p);
    if (check(p, T_KEYWORD, "this")) { advance(p); return node_new(N_THIS, t->line); }
    if (check(p, T_KEYWORD, "super")) { advance(p); return node_new(N_SUPER, t->line); }

    if (t->kind == T_IDENT && strcmp(t->text, "forr") == 0
        && at(p, 1)->kind == T_OP && at(p, 1)->text
        && strcmp(at(p, 1)->text, "(") == 0)
        return forr_expr(p);

    if (t->kind == T_IDENT) {
        char *name = xstrdup(t->text);
        advance(p);
        if (check(p, T_OP, "(")) {
            Node *n = node_new(N_CALL, t->line);
            n->v.call.name = name;
            n->v.call.args = call_args(p);
            return n;
        }
        Node *n = node_new(N_VAR, t->line);
        n->v.var.name = name;
        return n;
    }

    if (check(p, T_OP, "(")) {
        advance(p);
        if (check(p, T_OP, ")")) {           /* 빈 튜플: () */
            advance(p);
            return node_new(N_TUPLE, t->line);
        }
        Node *first = expression(p);
        if (check(p, T_OP, ",")) {           /* 튜플: (1, 2, 3) 또는 (1,) */
            Node *n = node_new(N_TUPLE, t->line);
            nodelist_push(&n->v.list.elements, first);
            while (match(p, T_OP, ",")) {
                if (check(p, T_OP, ")")) break;
                nodelist_push(&n->v.list.elements, expression(p));
            }
            expect(p, T_OP, ")");
            return n;
        }
        expect(p, T_OP, ")");
        return first;
    }

    if (check(p, T_OP, "[")) {
        int line = advance(p)->line;
        if (check(p, T_OP, "]")) {
            advance(p);
            return node_new(N_LIST, line);
        }
        Node *first = expression(p);
        if (check(p, T_KEYWORD, "for")) {
            advance(p);
            Token *var = expect(p, T_IDENT, NULL);
            char *var_name = xstrdup(var->text);
            expect(p, T_KEYWORD, "in");
            Node *iterable = expression(p);
            Node *cond = NULL;
            if (match(p, T_KEYWORD, "if")) cond = expression(p);
            expect(p, T_OP, "]");
            Node *n = node_new(N_LIST_COMP, line);
            n->v.list_comp.expr = first;
            n->v.list_comp.var_name = var_name;
            n->v.list_comp.iterable = iterable;
            n->v.list_comp.cond = cond;
            return n;
        }
        Node *n = node_new(N_LIST, line);
        nodelist_push(&n->v.list.elements, first);
        while (match(p, T_OP, ",")) {
            if (check(p, T_OP, "]")) break;
            nodelist_push(&n->v.list.elements, expression(p));
        }
        expect(p, T_OP, "]");
        return n;
    }

    if (check(p, T_OP, "{")) {
        int line = advance(p)->line;
        if (check(p, T_OP, "}")) {
            advance(p);
            return node_new(N_DICT, line);
        }
        Node *first_key = expression(p);
        expect(p, T_OP, ":");
        Node *first_val = expression(p);
        if (check(p, T_KEYWORD, "for")) {
            advance(p);
            Token *var = expect(p, T_IDENT, NULL);
            char *var_name = xstrdup(var->text);
            expect(p, T_KEYWORD, "in");
            Node *iterable = expression(p);
            Node *cond = NULL;
            if (match(p, T_KEYWORD, "if")) cond = expression(p);
            expect(p, T_OP, "}");
            Node *n = node_new(N_DICT_COMP, line);
            n->v.dict_comp.key_expr = first_key;
            n->v.dict_comp.val_expr = first_val;
            n->v.dict_comp.var_name = var_name;
            n->v.dict_comp.iterable = iterable;
            n->v.dict_comp.cond = cond;
            return n;
        }
        Node *n = node_new(N_DICT, line);
        size_t cap = 4;
        n->v.dict.pairs = xmalloc(cap * sizeof(*n->v.dict.pairs));
        n->v.dict.pairs[0].key = first_key;
        n->v.dict.pairs[0].val = first_val;
        n->v.dict.npairs = 1;
        while (match(p, T_OP, ",")) {
            if (check(p, T_OP, "}")) break;
            if (n->v.dict.npairs == cap) {
                cap *= 2;
                n->v.dict.pairs = xrealloc(n->v.dict.pairs, cap * sizeof(*n->v.dict.pairs));
            }
            Node *k = expression(p);
            expect(p, T_OP, ":");
            Node *v = expression(p);
            n->v.dict.pairs[n->v.dict.npairs].key = k;
            n->v.dict.pairs[n->v.dict.npairs].val = v;
            n->v.dict.npairs++;
        }
        expect(p, T_OP, "}");
        return n;
    }

    char *got = tok_repr(t);
    lumi_error(t->line, "Unexpected token: %s", got);
    return NULL;
}

/* ---------------- 프로그램 전체 ---------------- */

NodeList parse_program(TokenList *tokens)
{
    Parser p = { tokens, 0 };
    NodeList program;
    memset(&program, 0, sizeof(program));
    while (!check(&p, T_EOF, NULL)) {
        if (check(&p, T_NEWLINE, NULL)) { advance(&p); continue; }
        nodelist_push(&program, statement(&p));
    }
    return program;
}
