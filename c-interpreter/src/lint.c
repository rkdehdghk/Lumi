/* lint.c — 돌리기 전에 걱정거리 찾아 주기 (lumi lint)
 * ==========================================================
 *   lumi lint            지금 폴더 아래 모든 .lumi
 *   lumi lint app.lumi   파일 하나만
 *
 * 문법 오류는 여기서 다루지 않습니다 (그건 실행하거나 편집기가 바로 알려 줍니다).
 * 여기서 찾는 것은 **문법은 맞는데 아마 뜻이 어긋난 것**들입니다.
 *
 * 규칙 일곱:
 *   L1  내가 만든 이름이 내장 함수를 가림   func sort(...) 처럼
 *   L2  catch 에 적은 오류 종류가 낯설다     catch FileNotFund e:  (오타)
 *   L3  절대 닿지 않는 줄                    return 뒤에 적힌 것
 *   L4  매개변수 이름이 겹침                 func f(a, a)
 *   L5  딕셔너리에 같은 키가 두 번           {"a": 1, "a": 2}
 *   L6  가져와 놓고 안 쓰는 라이브러리       bring math 뒤 math 를 안 씀
 *   L7  적어 둔 자료형과 어긋나는 값         func f(int a) 를 f("가") 로 부름
 *
 * 규칙을 고를 때의 잣대: **'조용히 틀리는 것'을 잡되, 멀쩡한 코드를 나무라지 않기.**
 * 그래서 헷갈릴 여지가 있는 것(안 쓰는 변수 등)은 일부러 뺐습니다 —
 * 거짓 경고가 잦으면 사람들이 lint 를 통째로 꺼 버립니다.
 */
#include "lumi.h"
#include "lumiwords.h"
#include "platform.h"

#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *read_text_file(const char *path);

/* ============================================================
 * 걱정거리 모으기
 * ============================================================ */

typedef struct {
    LintReport report;
    void *ud;
    int   found;

    /* L2 를 위해: 이 파일이 스스로 만들어 내는 오류 종류들 */
    char **kinds;
    size_t nkinds, ckinds;

    /* L6 를 위해: 가져온 이름과 쓴 이름 */
    struct { char *name; int line; bool used; } *brings;
    size_t nbrings, cbrings;

    /* L1 을 위해: 이 파일이 '맨이름으로' 부르는 것들 (첫 바퀴에 모읍니다).
     * math.pow(2,3) 같은 멤버 호출은 내장으로 새지 않으니 여기 들어오지 않습니다. */
    char **called;
    size_t ncalled, ccalled;

    /* L7 을 위해: 이 파일이 만든 함수들 (첫 바퀴에 모읍니다).
     * 같은 이름이 두 번 나오면 어느 쪽인지 알 수 없으니 fd 를 NULL 로 지워 둡니다. */
    struct { char *name; FuncDefNode *fd; } *funcs;
    size_t nfuncs, cfuncs;

    const char *cur_ret;             /* L7 — 지금 훑는 함수가 돌려주기로 적어 둔 자료형 */

    bool collecting;                 /* 첫 바퀴에는 아무 말도 하지 않습니다 */
} Lint;

static void say(Lint *L, int line, const char *fmt, ...)
{
    if (L->collecting) return;
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    L->found++;
    L->report(L->ud, line, buf);
}

static void add_kind(Lint *L, const char *k)
{
    for (size_t i = 0; i < L->nkinds; i++) if (strcmp(L->kinds[i], k) == 0) return;
    if (L->nkinds == L->ckinds) {
        L->ckinds = L->ckinds ? L->ckinds * 2 : 8;
        L->kinds = (char **)xrealloc(L->kinds, L->ckinds * sizeof(char *));
    }
    L->kinds[L->nkinds++] = xstrdup(k);
}

static void add_bring(Lint *L, const char *name, int line)
{
    if (L->nbrings == L->cbrings) {
        L->cbrings = L->cbrings ? L->cbrings * 2 : 8;
        L->brings = xrealloc(L->brings, L->cbrings * sizeof(*L->brings));
    }
    L->brings[L->nbrings].name = xstrdup(name);
    L->brings[L->nbrings].line = line;
    L->brings[L->nbrings].used = false;
    L->nbrings++;
}

static void add_called(Lint *L, const char *name)
{
    for (size_t i = 0; i < L->ncalled; i++) if (strcmp(L->called[i], name) == 0) return;
    if (L->ncalled == L->ccalled) {
        L->ccalled = L->ccalled ? L->ccalled * 2 : 16;
        L->called = (char **)xrealloc(L->called, L->ccalled * sizeof(char *));
    }
    L->called[L->ncalled++] = xstrdup(name);
}

static void add_func(Lint *L, const char *name, FuncDefNode *fd)
{
    for (size_t i = 0; i < L->nfuncs; i++)
        if (strcmp(L->funcs[i].name, name) == 0) { L->funcs[i].fd = NULL; return; }
    if (L->nfuncs == L->cfuncs) {
        L->cfuncs = L->cfuncs ? L->cfuncs * 2 : 8;
        L->funcs = xrealloc(L->funcs, L->cfuncs * sizeof(*L->funcs));
    }
    L->funcs[L->nfuncs].name = xstrdup(name);
    L->funcs[L->nfuncs].fd   = fd;
    L->nfuncs++;
}

static FuncDefNode *find_func(Lint *L, const char *name)
{
    for (size_t i = 0; i < L->nfuncs; i++)
        if (strcmp(L->funcs[i].name, name) == 0) return L->funcs[i].fd;
    return NULL;
}

/* ---- L7 : 돌려보지 않고도 알 수 있는 자료형 어긋남 ----------------
 * **글자로 그 자리에 적어 둔 값**만 봅니다 (변수는 무엇이 들었는지 모릅니다).
 * 규칙은 interp.c 의 coerce_to_type 을 따라 적은 것입니다 — 거기를 고치면
 * 여기도 같이 고치세요.  헷갈리면 조용히 넘어갑니다 (거짓 경고 금지). */

/* 이 노드가 '보면 바로 아는 값' 입니까?  아니면 NULL 을 돌려줍니다. */
static const char *literal_kind(Node *n)
{
    if (!n) return NULL;
    switch (n->kind) {
    case N_NUMBER: return n->v.num.is_float ? "float" : "int";
    case N_STRING: return n->v.str.value && n->v.str.value->len == 1 ? "char" : "str";
    case N_BOOL:   return "bool";
    case N_NONE:   return "none";
    case N_LIST:   return "list";
    case N_TUPLE:  return "tuple";
    case N_DICT:   return "dict";
    default:       return NULL;
    }
}

/* 적어 둔 자료형 want 가 글자로 적힌 got 값을 받아 줍니까? */
static bool type_takes(const char *want, const char *got)
{
    bool num  = strcmp(got, "int") == 0 || strcmp(got, "float") == 0;
    bool text = strcmp(got, "str") == 0 || strcmp(got, "char") == 0;

    if (!strcmp(want, "int"))                          return num || text;
    if (!strcmp(want, "float") || !strcmp(want, "num")) return num;
    if (!strcmp(want, "char"))                         return num || text;
    if (!strcmp(want, "str") || !strcmp(want, "text")) return text;
    if (!strcmp(want, "seq"))
        return text || !strcmp(got, "list") || !strcmp(got, "tuple");
    if (!strcmp(want, "bytes"))                        return false;  /* 바이트는 적을 길이 없습니다 */
    if (!strcmp(want, "bool"))                         return !strcmp(got, "bool");
    if (!strcmp(want, "list"))                         return !strcmp(got, "list");
    if (!strcmp(want, "tuple"))                        return !strcmp(got, "tuple");
    if (!strcmp(want, "dict"))                         return !strcmp(got, "dict");
    return true;                                       /* 모르는 이름이면 조용히 */
}

/* lead 는 "parameter 'a'" 처럼 무엇을 가리키는지 적은 말입니다 */
static void check_typed_value(Lint *L, const char *want, Node *value, const char *lead)
{
    if (!want || !value) return;
    const char *got = literal_kind(value);
    if (!got || type_takes(want, got)) return;
    say(L, value->line, "%s is written as '%s', but the value given here is %s",
        lead, want, got);
}

/* 이 함수를 이렇게 부르면 자료형이 어긋나는가 */
static void check_call_types(Lint *L, const char *name, NodeList *args)
{
    FuncDefNode *fd = find_func(L, name);
    if (!fd || !fd->ptypes) return;
    size_t at = 0;
    for (size_t i = 0; i < args->len; i++) {
        Node *a = args->items[i];
        if (a && a->kind == N_KWARG) {
            for (size_t k = 0; k < fd->nparams; k++)
                if (strcmp(fd->params[k], a->v.kwarg.name) == 0) {
                    char *lead = xsprintf("parameter '%s' of '%s'", fd->params[k], name);
                    check_typed_value(L, fd->ptypes[k], a->v.kwarg.value, lead);
                    free(lead);
                }
            continue;
        }
        if (at >= fd->nparams) return;              /* 개수는 실행할 때 봅니다 */
        char *lead = xsprintf("parameter '%s' of '%s'", fd->params[at], name);
        check_typed_value(L, fd->ptypes[at], a, lead);
        free(lead);
        at++;
    }
}

static bool is_called_bare(Lint *L, const char *name)
{
    for (size_t i = 0; i < L->ncalled; i++) if (strcmp(L->called[i], name) == 0) return true;
    return false;
}

static void mark_used(Lint *L, const char *name)
{
    for (size_t i = 0; i < L->nbrings; i++)
        if (strcmp(L->brings[i].name, name) == 0) L->brings[i].used = true;
}

/* 이 이름이 내장 낱말입니까? (내장이면 그 갈래를 돌려줍니다) */
static const LumiWordDef *builtin_named(const char *name)
{
    for (size_t i = 0; i < LUMI_WORD_COUNT; i++)
        if (strcmp(LUMI_WORDS[i].name, name) == 0) return &LUMI_WORDS[i];
    return NULL;
}

/* 우리가 아는 오류 종류들 (value.c 의 ERROR_PARENT 와 짝을 맞춥니다) */
static const char *KNOWN_KINDS[] = {
    "Error", "LookupError", "IndexError", "KeyError", "FileError", "FileNotFound",
    "TypeError", "ValueError", "NameError", "MathError", "ArgumentError",
    "AssertError", "NetworkError", "DatabaseError", NULL
};

static bool kind_is_known(Lint *L, const char *k)
{
    for (size_t i = 0; KNOWN_KINDS[i]; i++) if (strcmp(KNOWN_KINDS[i], k) == 0) return true;
    for (size_t i = 0; i < L->nkinds; i++) if (strcmp(L->kinds[i], k) == 0) return true;
    return false;
}

/* ============================================================
 * AST 훑기
 * ============================================================ */

static void walk(Lint *L, Node *n);

static void walk_list(Lint *L, NodeList *l)
{
    if (!l) return;
    for (size_t i = 0; i < l->len; i++) walk(L, l->items[i]);
}

/* L3 — 줄줄이 늘어선 문장 가운데 return/break/continue 뒤가 있는지 */
static void check_unreachable(Lint *L, NodeList *body)
{
    if (!body) return;
    for (size_t i = 0; i + 1 < body->len; i++) {
        Node *s = body->items[i];
        const char *what = s->kind == N_RETURN   ? "return"
                         : s->kind == N_BREAK    ? "break"
                         : s->kind == N_CONTINUE ? "continue" : NULL;
        if (!what) continue;
        say(L, body->items[i + 1]->line,
            "this line can never run: '%s' on line %d always leaves first",
            what, s->line);
        break;                       /* 한 덩어리에 한 번만 알려 줍니다 */
    }
}

/* L1 — 내가 지은 이름이 내장을 가리는가.
 * 이 파일이 그 이름을 맨이름으로 부를 때만 말합니다 — 라이브러리의 func pow 는
 * math.pow(2,3) 로만 불리므로 내장에 가려지지 않고, 나무랄 일도 아닙니다. */
static void check_shadow(Lint *L, const char *name, int line, const char *what)
{
    if (!name) return;
    const LumiWordDef *w = builtin_named(name);
    if (!w) return;
    if (w->kind == LW_KEYWORD) return;         /* 문법 낱말은 파서가 이미 막습니다 */
    if (!is_called_bare(L, name)) return;
    say(L, line,
        "this %s is called '%s', which is also a built-in - yours will never be called. "
        "Pick another name.", what, name);
}

/* 함수 하나 (이름 있는 것, 이름 없는 것, 메서드 모두) */
static void walk_funcdef(Lint *L, FuncDefNode *fd, int line, bool is_method)
{
    if (!fd) return;
    if (fd->name && strcmp(fd->name, "nameless") != 0 && !is_method)
        check_shadow(L, fd->name, line, "function");
    if (L->collecting && fd->name && !is_method && strcmp(fd->name, "nameless") != 0)
        add_func(L, fd->name, fd);

    /* L4 — 매개변수 이름 겹침 */
    for (size_t i = 0; i < fd->nparams; i++)
        for (size_t j = i + 1; j < fd->nparams; j++)
            if (strcmp(fd->params[i], fd->params[j]) == 0) {
                say(L, line, "'%s' is used twice as a parameter of '%s'",
                    fd->params[i], fd->name ? fd->name : "this function");
                break;
            }

    if (fd->defaults)
        for (size_t i = 0; i < fd->nparams; i++)
            if (fd->defaults[i]) walk(L, fd->defaults[i]);

    check_unreachable(L, &fd->body);
    const char *outer = L->cur_ret;
    L->cur_ret = fd->ret_type;
    walk_list(L, &fd->body);
    L->cur_ret = outer;
}

static void walk(Lint *L, Node *n)
{
    if (!n) return;
    switch (n->kind) {
    case N_NUMBER: case N_STRING: case N_BOOL: case N_NONE:
    case N_BREAK: case N_CONTINUE: case N_THIS: case N_SUPER:
        return;

    case N_VAR:
        mark_used(L, n->v.var.name);
        return;

    case N_FSTRING: walk_list(L, &n->v.fstring.parts); return;
    case N_BINOP:   walk(L, n->v.binop.left); walk(L, n->v.binop.right); return;
    case N_UNARY:   walk(L, n->v.unary.operand); return;

    case N_ASSIGN:
        /* val sum = 0 은 괜찮습니다 — 값은 불리지 않으니까요.
         * 하지만 val sum = func x: ... 은 sum(1) 이 내장으로 가 버립니다. */
        if (n->v.assign.is_let && n->v.assign.value
            && n->v.assign.value->kind == N_FUNCDEF)
            check_shadow(L, n->v.assign.name, n->line, "function");
        if (n->v.assign.type_name) {
            char *lead = xsprintf("'%s'", n->v.assign.name);
            check_typed_value(L, n->v.assign.type_name, n->v.assign.value, lead);
            free(lead);
        }
        walk(L, n->v.assign.value);
        return;

    case N_MULTI_ASSIGN: walk(L, n->v.multi_assign.value); return;
    case N_MULTI:        walk_list(L, &n->v.multi.body); return;

    case N_CALL:
        mark_used(L, n->v.call.name);
        if (L->collecting) add_called(L, n->v.call.name);
        else check_call_types(L, n->v.call.name, &n->v.call.args);
        walk_list(L, &n->v.call.args);
        return;

    case N_CALLV:  walk(L, n->v.callv.callee); walk_list(L, &n->v.callv.args); return;
    case N_KWARG:  walk(L, n->v.kwarg.value); return;

    case N_MEMBER:
        if (n->v.member.obj && n->v.member.obj->kind == N_VAR)
            mark_used(L, n->v.member.obj->v.var.name);
        walk(L, n->v.member.obj);
        return;

    case N_METHODCALL:
        if (n->v.methodcall.obj && n->v.methodcall.obj->kind == N_VAR)
            mark_used(L, n->v.methodcall.obj->v.var.name);
        walk(L, n->v.methodcall.obj);
        walk_list(L, &n->v.methodcall.args);
        return;

    case N_MEMBERASSIGN:
        walk(L, n->v.memberassign.obj);
        walk(L, n->v.memberassign.value);
        return;

    case N_CLASSDEF: {
        ClassDefNode *cd = n->v.classdef;
        if (!cd) return;
        check_shadow(L, cd->name, n->line, "class");
        for (size_t i = 0; i < cd->nmethods; i++) {
            Node *m = cd->methods[i];
            if (m && m->kind == N_FUNCDEF) walk_funcdef(L, m->v.funcdef, m->line, true);
        }
        walk_list(L, &cd->fields);
        return;
    }

    case N_IF:
        walk(L, n->v.if_.cond);
        check_unreachable(L, &n->v.if_.then_body);
        walk_list(L, &n->v.if_.then_body);
        if (n->v.if_.has_else) {
            check_unreachable(L, &n->v.if_.else_body);
            walk_list(L, &n->v.if_.else_body);
        }
        return;

    case N_WHILE:
        walk(L, n->v.while_.cond);
        check_unreachable(L, &n->v.while_.body);
        walk_list(L, &n->v.while_.body);
        return;

    case N_SWITCH:
        walk(L, n->v.switch_.subject);
        for (size_t i = 0; i < n->v.switch_.ncases; i++) {
            walk_list(L, &n->v.switch_.cases[i].values);
            check_unreachable(L, &n->v.switch_.cases[i].body);
            walk_list(L, &n->v.switch_.cases[i].body);
        }
        if (n->v.switch_.has_default) {
            check_unreachable(L, &n->v.switch_.default_body);
            walk_list(L, &n->v.switch_.default_body);
        }
        return;

    case N_FUNCDEF: walk_funcdef(L, n->v.funcdef, n->line, false); return;
    case N_RETURN:
        check_typed_value(L, L->cur_ret, n->v.ret.value, "the return type of this function");
        walk(L, n->v.ret.value);
        return;

    case N_BRING: {
        const char *bound = n->v.bring.as_name;
        if (!bound && !n->v.bring.is_path) bound = n->v.bring.lib;
        if (n->v.bring.all && bound && !L->collecting) add_bring(L, bound, n->line);
        return;
    }

    case N_LIST:  walk_list(L, &n->v.list.elements); return;
    case N_TUPLE: walk_list(L, &n->v.list.elements); return;

    case N_DICT: {
        /* L5 — 글자로 적은 키가 겹치는지 (같은 글자면 뒤엣것만 남습니다) */
        for (size_t i = 0; i < n->v.dict.npairs; i++) {
            Node *k = n->v.dict.pairs[i].key;
            walk(L, k);
            walk(L, n->v.dict.pairs[i].val);
            if (!k || k->kind != N_STRING) continue;
            for (size_t j = 0; j < i; j++) {
                Node *p = n->v.dict.pairs[j].key;
                if (p && p->kind == N_STRING && str_eq(p->v.str.value, k->v.str.value)) {
                    char *u = str_to_utf8(k->v.str.value);
                    say(L, k->line, "the key \"%s\" is set twice in this dictionary; "
                                    "only the last one survives", u);
                    free(u);
                    break;
                }
            }
        }
        return;
    }

    case N_LIST_COMP:
        walk(L, n->v.list_comp.iterable); walk(L, n->v.list_comp.expr);
        walk(L, n->v.list_comp.cond);
        return;
    case N_DICT_COMP:
        walk(L, n->v.dict_comp.iterable); walk(L, n->v.dict_comp.key_expr);
        walk(L, n->v.dict_comp.val_expr); walk(L, n->v.dict_comp.cond);
        return;

    case N_SLICE:
        walk(L, n->v.slice.target); walk(L, n->v.slice.start); walk(L, n->v.slice.end);
        return;
    case N_INDEX:     walk(L, n->v.index.target); walk(L, n->v.index.index); return;
    case N_RANGESPEC: walk(L, n->v.rangespec.start); walk(L, n->v.rangespec.end); return;
    case N_INDEXASSIGN:
        walk(L, n->v.indexassign.target); walk(L, n->v.indexassign.index);
        walk(L, n->v.indexassign.value);
        return;

    case N_FORC:
        walk(L, n->v.forc.init); walk(L, n->v.forc.cond); walk(L, n->v.forc.step);
        check_unreachable(L, &n->v.forc.body);
        walk_list(L, &n->v.forc.body);
        return;
    case N_FORIN:
        walk(L, n->v.forin.iterable);
        check_unreachable(L, &n->v.forin.body);
        walk_list(L, &n->v.forin.body);
        return;
    case N_FORR:
        walk(L, n->v.forr.init); walk(L, n->v.forr.cond); walk(L, n->v.forr.step);
        return;

    case N_USE:
        walk(L, n->v.use.value);
        check_unreachable(L, &n->v.use.body);
        walk_list(L, &n->v.use.body);
        return;

    case N_TRY:
        check_unreachable(L, &n->v.try_.try_body);
        walk_list(L, &n->v.try_.try_body);
        for (size_t i = 0; i < n->v.try_.ncatches; i++) {
            struct CatchClause *cc = &n->v.try_.catches[i];
            /* L2 — 낯선 종류 이름.  오타면 그 catch 는 영원히 안 잡힙니다. */
            if (cc->type_name && !kind_is_known(L, cc->type_name))
                say(L, cc->line,
                    "'catch %s' names an error kind nothing here ever raises - if that is a "
                    "typo this catch will never run. Known kinds: Error, LookupError, "
                    "IndexError, KeyError, FileError, FileNotFound, TypeError, ValueError, "
                    "NameError, MathError, ArgumentError, AssertError, NetworkError, "
                    "DatabaseError",
                    cc->type_name);
            check_unreachable(L, &cc->body);
            walk_list(L, &cc->body);
        }
        if (n->v.try_.has_safe)   { check_unreachable(L, &n->v.try_.safe_body);
                                    walk_list(L, &n->v.try_.safe_body); }
        if (n->v.try_.has_always) { check_unreachable(L, &n->v.try_.always_body);
                                    walk_list(L, &n->v.try_.always_body); }
        return;

    case N_ERROR:
        walk(L, n->v.error_.type_expr);
        walk(L, n->v.error_.msg_expr);
        return;

    case N_TEST:
        check_unreachable(L, &n->v.test_.body);
        walk_list(L, &n->v.test_.body);
        return;
    }
}

/* error("종류", "말") 로 이 파일이 스스로 만드는 종류를 먼저 걷어 둡니다.
 * (L2 가 사용자가 지은 종류를 오타로 오해하지 않도록) */
static void collect_kinds(Lint *L, Node *n);

static void collect_kinds_list(Lint *L, NodeList *l)
{
    if (!l) return;
    for (size_t i = 0; i < l->len; i++) collect_kinds(L, l->items[i]);
}

static void collect_kinds(Lint *L, Node *n)
{
    if (!n) return;
    if (n->kind == N_ERROR) {
        Node *t = n->v.error_.type_expr;
        if (t && t->kind == N_STRING) {
            char *u = str_to_utf8(t->v.str.value);
            add_kind(L, u);
            free(u);
        }
        return;
    }
    /* 블록을 가진 것들만 파고들면 충분합니다 */
    switch (n->kind) {
    case N_FUNCDEF: if (n->v.funcdef) collect_kinds_list(L, &n->v.funcdef->body); return;
    case N_CLASSDEF: {
        ClassDefNode *cd = n->v.classdef;
        if (!cd) return;
        for (size_t i = 0; i < cd->nmethods; i++) collect_kinds(L, cd->methods[i]);
        collect_kinds_list(L, &cd->fields);
        return;
    }
    case N_IF:    collect_kinds_list(L, &n->v.if_.then_body);
                  collect_kinds_list(L, &n->v.if_.else_body); return;
    case N_WHILE: collect_kinds_list(L, &n->v.while_.body); return;
    case N_FORC:  collect_kinds_list(L, &n->v.forc.body); return;
    case N_FORIN: collect_kinds_list(L, &n->v.forin.body); return;
    case N_USE:   collect_kinds_list(L, &n->v.use.body); return;
    case N_TEST:  collect_kinds_list(L, &n->v.test_.body); return;
    case N_SWITCH:
        for (size_t i = 0; i < n->v.switch_.ncases; i++)
            collect_kinds_list(L, &n->v.switch_.cases[i].body);
        collect_kinds_list(L, &n->v.switch_.default_body);
        return;
    case N_TRY:
        collect_kinds_list(L, &n->v.try_.try_body);
        for (size_t i = 0; i < n->v.try_.ncatches; i++)
            collect_kinds_list(L, &n->v.try_.catches[i].body);
        collect_kinds_list(L, &n->v.try_.safe_body);
        collect_kinds_list(L, &n->v.try_.always_body);
        return;
    default: return;
    }
}

int lint_source(const char *text, LintReport report, void *ud)
{
    Lint L;
    memset(&L, 0, sizeof L);
    L.report = report;
    L.ud = ud;

    NodeList prog;
    memset(&prog, 0, sizeof prog);

    /* 부르는 쪽(LSP)도 lumi_jmp 를 걸어 두었을 수 있으니 잠깐 빌렸다 돌려줍니다 */
    jmp_buf saved;
    memcpy(saved, lumi_jmp, sizeof saved);
    if (setjmp(lumi_jmp) != 0) {               /* 문법 오류는 여기서 다루지 않습니다 */
        memcpy(lumi_jmp, saved, sizeof saved);
        return -1;
    }
    TokenList *toks = tokenize(text);
    prog = parse_program(toks);
    tokenlist_free(toks);
    memcpy(lumi_jmp, saved, sizeof saved);

    for (size_t i = 0; i < prog.len; i++) collect_kinds(&L, prog.items[i]);

    /* 첫 바퀴: 맨이름으로 부르는 것들만 모읍니다.  둘째 바퀴에 말합니다. */
    L.collecting = true;
    for (size_t i = 0; i < prog.len; i++) walk(&L, prog.items[i]);
    L.collecting = false;

    check_unreachable(&L, &prog);
    for (size_t i = 0; i < prog.len; i++) walk(&L, prog.items[i]);

    /* L6 — 가져와 놓고 안 쓴 것 */
    for (size_t i = 0; i < L.nbrings; i++)
        if (!L.brings[i].used)
            say(&L, L.brings[i].line,
                "'%s' is brought in but never used - remove the bring line",
                L.brings[i].name);

    for (size_t i = 0; i < L.nkinds; i++) free(L.kinds[i]);
    free(L.kinds);
    for (size_t i = 0; i < L.nbrings; i++) free(L.brings[i].name);
    free(L.brings);
    for (size_t i = 0; i < L.ncalled; i++) free(L.called[i]);
    free(L.called);
    for (size_t i = 0; i < L.nfuncs; i++) free(L.funcs[i].name);
    free(L.funcs);
    free(prog.items);
    return L.found;
}

/* ============================================================
 * lumi lint
 * ============================================================ */

typedef struct { const char *file; int *total; } Printer;

static void print_one(void *ud, int line, const char *msg)
{
    Printer *p = (Printer *)ud;
    printf("%s:%d: %s\n", p->file, line, msg);
    (*p->total)++;
}

static void lint_file(const char *path, int *total, int *files)
{
    char *text = read_text_file(path);
    if (!text) return;
    (*files)++;
    Printer p = { path, total };
    int n = lint_source(text, print_one, &p);
    if (n < 0) printf("%s: (skipped - it does not parse yet)\n", path);
    free(text);
}

static void lint_dir(const char *dir, int *total, int *files)
{
    char **names = plat_listdir(dir);
    if (names) {
        for (size_t i = 0; names[i]; i++) {
            size_t n = strlen(names[i]);
            if (n < 5 || strcmp(names[i] + n - 5, ".lumi") != 0) continue;
            char *full = xsprintf("%s%s%s", dir, plat_dir_sep(), names[i]);
            lint_file(full, total, files);
            free(full);
        }
        plat_free_names(names);
    }
    char **subs = plat_subdirs(dir);
    if (subs) {
        for (size_t i = 0; subs[i]; i++) {
            char *full = xsprintf("%s%s%s", dir, plat_dir_sep(), subs[i]);
            lint_dir(full, total, files);
            free(full);
        }
        plat_free_names(subs);
    }
}

int run_lint(const char *where)
{
    if (!where || !*where) where = ".";
    int total = 0, files = 0;

    if (plat_dir_exists(where))       lint_dir(where, &total, &files);
    else if (plat_file_exists(where)) lint_file(where, &total, &files);
    else {
        printf("Nothing to lint at '%s'\n", where);
        return 1;
    }

    printf("\n----------------------------------------\n");
    if (total == 0) printf("  %d file(s), nothing to worry about\n", files);
    else            printf("  %d file(s), %d thing(s) to look at\n", files, total);
    printf("----------------------------------------\n");
    return total ? 1 : 0;
}
