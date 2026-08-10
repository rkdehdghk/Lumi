/* 인터프리터 — AST 를 따라가며 실제로 실행합니다.
 * interpreter.py 의 Interpreter 클래스를 그대로 옮긴 것입니다. */
#include "lumi.h"
#include "platform.h"
#include "pkg.h"


#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wctype.h>

/* 시간 관련 내장 함수(builtins.inc)만 아직 운영체제 API 를 직접 씁니다.
 * 파일·경로·터미널은 모두 platform.h 를 거칩니다. */
#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

/* ============================================================
 * 0. 오류
 * ============================================================ */

/* 전부 **실마다 따로**입니다 — 일감(start)이 저마다 제 오류를 들고 돌아야 합니다 */
LUMI_TLS char lumi_err_msg[2048];
LUMI_TLS int  lumi_err_line;
LUMI_TLS char lumi_err_trace[4096];
LUMI_TLS char lumi_err_kind[64] = "Error";
/* 줄 번호를 붙이지 않은 알맹이 메시지 (오류 값의 message 로 씁니다) */
static LUMI_TLS char lumi_err_bare[1900];
LUMI_TLS jmp_buf lumi_jmp;
LUMI_TLS Interp *g_current_interp = NULL;
/* 이 실이 지금 큰 자물쇠를 쥐고 Lumi 코드를 도는가. 오류가 맨 위까지
 * 새면 raise_error 가 자물쇠를 놓아 다음 실행이 굳지 않게 합니다. */
static LUMI_TLS bool g_execution_locked = false;

bool lumi_gil_is_held(void) { return g_execution_locked; }

/* 운영체제를 기다리는 동안에는 큰 자물쇠를 놓습니다 — 그동안 다른 일감이 Lumi 코드를
 * 돌립니다.  놓았다 다시 잡는 사이에서는 **Lumi 값을 만지면 안 됩니다** (참조 세기와
 * 순환 수집기가 자물쇠 안에서만 안전합니다).  거기서 해도 되는 것은 malloc·free 와
 * 운영체제 부르기뿐입니다.  쓰는 곳: sleep · shell · 웹서버의 accept/recv/send. */
#define GIL_RELEASE() do { g_execution_locked = false; plat_gil_unlock(); } while (0)
#define GIL_ACQUIRE() do { plat_gil_lock(); g_execution_locked = true; } while (0)

/* 경로에서 파일 이름만 (트레이스를 짧게 유지합니다) */
static const char *base_name_of(const char *path)
{
    if (!path) return NULL;
    const char *a = strrchr(path, '\\');
    const char *b = strrchr(path, '/');
    const char *slash = a > b ? a : b;
    return slash ? slash + 1 : path;
}

/* 지금 쌓여 있는 호출 겹을 안쪽부터 바깥쪽으로 적습니다.
 * 맨 안쪽 겹의 줄 번호는 '오류가 난 줄'로 바꿔 씁니다. */
static void build_trace(Interp *in, int line)
{
    lumi_err_trace[0] = 0;
    if (!in) return;

    int top = in->depth;
    if (top > LUMI_MAX_DEPTH) top = LUMI_MAX_DEPTH;
    if (line != NO_LINE) in->frames[top].line = line;

    size_t used = 0;
    for (int i = top; i >= 0; i--) {
        const CallFrame *f = &in->frames[i];
        const char *who  = f->fn_name ? f->fn_name : "<main>";
        const char *file = base_name_of(f->file);

        /* 재귀가 깊으면 가운데는 접습니다 — 위 8 겹과 아래 4 겹만 보여 줍니다 */
        if (top > 14 && i == top - 8) {
            int hidden = top - 8 - 4;
            used += (size_t)snprintf(lumi_err_trace + used, sizeof lumi_err_trace - used,
                                     "  ... %d more frame(s) ...\n", hidden);
            i = 4;                              /* 아래 4 겹으로 건너뜁니다 */
            f = &in->frames[i];
            who  = f->fn_name ? f->fn_name : "<main>";
            file = base_name_of(f->file);
        }

        if (file && f->line > 0)
            used += (size_t)snprintf(lumi_err_trace + used, sizeof lumi_err_trace - used,
                                     "  at %s (%s:%d)\n", who, file, f->line);
        else if (file)
            used += (size_t)snprintf(lumi_err_trace + used, sizeof lumi_err_trace - used,
                                     "  at %s (%s)\n", who, file);
        else
            used += (size_t)snprintf(lumi_err_trace + used, sizeof lumi_err_trace - used,
                                     "  at %s\n", who);

        if (used >= sizeof lumi_err_trace - 64) break;
    }
}

/* 오류 하나를 기록하고 맨 가까운 try (없으면 main) 로 뛰어오릅니다. */
static void raise_error(int line, const char *kind, const char *body)
{
    lumi_err_line = line;
    snprintf(lumi_err_kind, sizeof lumi_err_kind, "%s", kind && *kind ? kind : "Error");
    snprintf(lumi_err_bare, sizeof lumi_err_bare, "%s", body);
    /* 화면에 보일 줄.  종류가 그냥 "Error" 가 아니면 알려 줍니다
     * (print(e) 와 같은 모양으로 맞춰 둡니다). */
    bool named = strcmp(lumi_err_kind, "Error") != 0;
    if (line != NO_LINE && named)
        snprintf(lumi_err_msg, sizeof lumi_err_msg, "[line %d] [%s] %s",
                 line, lumi_err_kind, body);
    else if (line != NO_LINE)
        snprintf(lumi_err_msg, sizeof lumi_err_msg, "[line %d] %s", line, body);
    else if (named)
        snprintf(lumi_err_msg, sizeof lumi_err_msg, "[%s] %s", lumi_err_kind, body);
    else
        snprintf(lumi_err_msg, sizeof lumi_err_msg, "%s", body);
    build_trace(g_current_interp, line);
    if (g_current_interp && g_current_interp->top_handler) {
        longjmp(g_current_interp->top_handler->jmp, 1);
    }
    if (g_execution_locked) {
        g_execution_locked = false;
        plat_gil_unlock();
    }
    longjmp(lumi_jmp, 1);
}

void lumi_error(int line, const char *fmt, ...)
{
    char body[1900];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(body, sizeof body, fmt, ap);
    va_end(ap);
    raise_error(line, "Error", body);
}

void lumi_error_kind(int line, const char *kind, const char *fmt, ...)
{
    char body[1900];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(body, sizeof body, fmt, ap);
    va_end(ap);
    raise_error(line, kind, body);
}

/* 파일 이름을 인터프리터가 한 번만 담아 두고, 그 자리를 돌려줍니다.
 * Func 과 CallFrame 은 이 자리를 가리키기만 하므로 프로그램이 끝날 때까지 살아 있어야 합니다. */
static const char *intern_file(Interp *in, const char *path)
{
    if (!in || !path) return NULL;
    for (size_t i = 0; i < in->nfiles; i++)
        if (strcmp(in->files[i], path) == 0) return in->files[i];
    if (in->nfiles == in->cfiles) {
        in->cfiles = in->cfiles ? in->cfiles * 2 : 8;
        in->files = (char **)xrealloc(in->files, in->cfiles * sizeof(char *));
    }
    in->files[in->nfiles] = xstrdup(path);
    return in->files[in->nfiles++];
}

void interp_set_script(Interp *in, const char *path)
{
    if (!in) return;
    in->cur_file = (char *)intern_file(in, path);
    in->frames[0].fn_name = NULL;
    in->frames[0].file    = in->cur_file;
    in->frames[0].line    = 0;
}

void interp_set_args(Interp *in, char **argv, size_t argc)
{
    if (!in) return;
    in->argv = argv;
    in->argc = argc;
}

/* 공백으로 치는 글자 (스페이스, 탭, 줄바꿈, 캐리지 리턴, 전각 공백) */
static bool is_strip_space(uint32_t c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r'
        || c == 0x0B || c == 0x0C || c == 0x3000;
}
static bool is_split_space(uint32_t c)
{
    return c < 0x10000 ? (iswspace((wint_t)c) != 0) : false;
}

/* ============================================================
 * 1. 실행 흐름
 * ============================================================ */

Flow exec_node(Interp *in, Node *n, Env *env, Value *ret);
static Flow run_block(Interp *in, NodeList *body, Env *env, Value *ret);
static Value eval(Interp *in, Node *n, Env *env);
static Value eval_call(Interp *in, Node *n, Env *env);
Value call_value(Interp *in, Value callee, Value *args, size_t nargs,
                    const char *name, int line);
Value call_value_kw(Interp *in, Value callee, Value *args, size_t nargs,
                    char **arg_names, const char *name, int line);
static Value instantiate_kw(Interp *in, LumiClass *cls, Value *args, size_t nargs,
                            char **arg_names, int line);
static Value invoke_function(Interp *in, Func *fn, Value *args, size_t nargs,
                             const char *name, int line,
                             Instance *this_, LumiClass *owner);
static Value invoke_function_kw(Interp *in, Func *fn, Value *args, size_t nargs,
                                char **arg_names, const char *name, int line,
                                Instance *this_, LumiClass *owner);
static void bind_undo(Value *slot, bool *got, size_t np);
Value member_of(Interp *in, Value obj, const char *name, Env *env, int line);
static bool values_equal(Interp *in, Value a, Value b, int line);
static Value coerce_to_type(Interp *in, const char *type_name, Value v, int line);
static void check_dict_key(Interp *in, Value key, int line);
static Value lumi_json_parse(Interp *in, Value input, int line);
static Value lumi_json_stringify(Interp *in, Value v, int line);

/* 한 곳에서만 정합니다 — 예전엔 lumi.h 와 여기 둘로 적혀 있었습니다 */
#define MAX_DEPTH LUMI_MAX_DEPTH

/* 새 이름 자리를 열 때마다 수집기에게 '여기 살아 있다'고 알려 둡니다.
 * 한 블록에 하나씩만 씁니다 (이름이 고정이라 두 번 쓰면 컴파일러가 잡아 줍니다).
 * 오류로 뛰어넘어 가면 SCOPE_LEAVE 가 건너뛰어지는데, 그건 exec_try_stmt 가
 * top_scope 를 통째로 되돌려 놓습니다 — top_handler 와 같은 방식입니다. */
#define SCOPE_ENTER(in, e) \
    ScopeGuard _scope = { (e), (in)->top_scope }; (in)->top_scope = &_scope
#define SCOPE_LEAVE(in)    ((in)->top_scope = _scope.prev)

/* ============================================================
 * 2. 값 보기 (to_str / repr / type)
 * ============================================================ */

bool value_truthy(Value v)
{
    switch (v.kind) {
    case V_NONE:  return false;
    case V_BOOL:  return v.as.b;
    case V_INT:   return v.as.i != 0;
    case V_FLOAT: return v.as.f != 0;
    case V_STR:   return AS_STR(v)->len > 0;
    case V_LIST: case V_TUPLE: return AS_SEQ(v)->len > 0;
    case V_DICT:  return AS_DICT(v)->len > 0;
    case V_BYTES: return AS_BYTES(v)->len > 0;
    default:      return true;
    }
}

const char *type_name_of(Value v)
{
    switch (v.kind) {
    case V_BOOL:  return "bool";
    case V_INT:   return "int";
    case V_FLOAT: return "float";
    case V_STR:   return AS_STR(v)->len == 1 ? "char" : "str";
    case V_LIST:  return "list";
    case V_TUPLE: return "tuple";
    case V_DICT:  return "dict";
    case V_BYTES: return "bytes";
    case V_MODULE: return "library";
    case V_FUNC:  return "function";
    case V_BOUND: return "method";
    case V_CLASS: return "class";
    case V_INSTANCE: return AS_INST(v)->cls->name;
    case V_FILE:  return "file";
    case V_TASK:  return "task";
    case V_ERROR: return AS_ERR(v)->type;   /* type(e) 는 오류 종류를 돌려줍니다 */
    case V_NONE:  return "none";
    default:      return "unknown";
    }
}

/* 글자를 이어 붙이는 작은 그릇 (UTF-8) */
typedef struct { char *p; size_t len, cap; } Buf;

static void buf_add(Buf *b, const char *s, size_t n)
{
    if (b->len + n + 1 > b->cap) {
        b->cap = b->cap ? b->cap * 2 : 64;
        while (b->cap < b->len + n + 1) b->cap *= 2;
        b->p = (char *)xrealloc(b->p, b->cap);
    }
    memcpy(b->p + b->len, s, n);
    b->len += n;
    b->p[b->len] = 0;
}
static void buf_str(Buf *b, const char *s) { buf_add(b, s, strlen(s)); }
static void buf_take(Buf *b, char *s) { buf_str(b, s); free(s); }
static char *buf_done(Buf *b) { return b->p ? b->p : xstrdup(""); }

static void to_str_into(Interp *in, Value v, Buf *b);
static void repr_into(Interp *in, Value v, Buf *b);

static void bytes_hex_into(BytesObj *by, Buf *b)
{
    buf_str(b, "bytes[");
    for (size_t i = 0; i < by->len; i++) {
        char t[4];
        snprintf(t, sizeof t, "%02x", by->data[i]);
        if (i) buf_str(b, " ");
        buf_str(b, t);
    }
    buf_str(b, "]");
}

static void instance_str_into(Interp *in, Instance *inst, Buf *b);

static void to_str_into(Interp *in, Value v, Buf *b)
{
    switch (v.kind) {
    case V_BOOL: buf_str(b, v.as.b ? "true" : "false"); return;
    case V_NONE: buf_str(b, "none"); return;
    case V_INT:  buf_take(b, xsprintf("%lld", v.as.i)); return;
    case V_FLOAT: {
        double d = v.as.f;
        /* 소수점 아래가 없으면 정수처럼 보여 줍니다 (파이썬 쪽과 같게) */
        if (d == floor(d) && !isinf(d) && !isnan(d)) {
            if (d >= -9.2e18 && d <= 9.2e18) buf_take(b, xsprintf("%lld", (long long)d));
            else buf_take(b, xsprintf("%.0f", d));
        } else {
            buf_take(b, fmt_double(d));
        }
        return;
    }
    case V_STR: buf_take(b, str_to_utf8(AS_STR(v))); return;
    case V_LIST: {
        Seq *s = AS_SEQ(v);
        buf_str(b, "[");
        for (size_t i = 0; i < s->len; i++) {
            if (i) buf_str(b, ", ");
            repr_into(in, s->items[i], b);
        }
        buf_str(b, "]");
        return;
    }
    case V_TUPLE: {
        Seq *s = AS_SEQ(v);
        buf_str(b, "(");
        for (size_t i = 0; i < s->len; i++) {
            if (i) buf_str(b, ", ");
            repr_into(in, s->items[i], b);
        }
        if (s->len == 1) buf_str(b, ",");       /* (1,) 로 튜플임을 보이게 */
        buf_str(b, ")");
        return;
    }
    case V_DICT: {
        Dict *d = AS_DICT(v);
        buf_str(b, "{");
        for (size_t i = 0; i < d->len; i++) {
            if (i) buf_str(b, ", ");
            repr_into(in, d->e[i].key, b);
            buf_str(b, ": ");
            repr_into(in, d->e[i].val, b);
        }
        buf_str(b, "}");
        return;
    }
    case V_BYTES: bytes_hex_into(AS_BYTES(v), b); return;
    case V_MODULE: buf_take(b, xsprintf("<library %s>", AS_MOD(v)->name)); return;
    case V_FUNC: buf_take(b, xsprintf("<function %s>", AS_FUNC(v)->node->name)); return;
    case V_BOUND: buf_take(b, xsprintf("<method %s>", AS_BOUND(v)->func->node->name)); return;
    case V_CLASS: buf_take(b, xsprintf("<class %s>", AS_CLASS(v)->name)); return;
    case V_INSTANCE: instance_str_into(in, AS_INST(v), b); return;
    case V_FILE: buf_take(b, xsprintf("<file %s%s>", AS_FILE(v)->path,
                                      AS_FILE(v)->fp ? "" : " (closed)")); return;
    case V_TASK: buf_str(b, AS_TASK(v)->joined ? "<task done>" : "<task running>"); return;
    /* 오류를 print 하면 "[line N] [종류] 메시지" 로 보입니다.
     * 종류는 값 안에 따로 담겨 있고(e.type), 여기서는 읽기 좋으라고 붙여만 줍니다.
     * 그냥 "Error" 일 때는 알려 줄 것이 없으니 뺍니다. */
    case V_ERROR: {
        ErrorObj *e = AS_ERR(v);
        char *msg = str_to_utf8(e->message);
        bool named = strcmp(e->type, "Error") != 0;
        if (e->line != NO_LINE && named)
            buf_take(b, xsprintf("[line %d] [%s] %s", e->line, e->type, msg));
        else if (e->line != NO_LINE)
            buf_take(b, xsprintf("[line %d] %s", e->line, msg));
        else if (named)
            buf_take(b, xsprintf("[%s] %s", e->type, msg));
        else
            buf_str(b, msg);
        free(msg);
        return;
    }
    default: buf_str(b, "unknown"); return;
    }
}

static void repr_into(Interp *in, Value v, Buf *b)
{
    if (v.kind == V_STR) {
        buf_str(b, "\"");
        buf_take(b, str_to_utf8(AS_STR(v)));
        buf_str(b, "\"");
        return;
    }
    to_str_into(in, v, b);
}

char *value_to_utf8(Interp *in, Value v)
{
    Buf b = {0};
    to_str_into(in, v, &b);
    return buf_done(&b);
}

static char *value_repr_utf8(Interp *in, Value v)
{
    Buf b = {0};
    repr_into(in, v, &b);
    return buf_done(&b);
}

Str *value_to_str(Interp *in, Value v)
{
    char *u = value_to_utf8(in, v);
    Str *s = str_from_utf8(u);
    free(u);
    return s;
}

Str *value_repr(Interp *in, Value v)
{
    char *u = value_repr_utf8(in, v);
    Str *s = str_from_utf8(u);
    free(u);
    return s;
}

/* 클래스에서 이름으로 메서드/공유값 찾기 */
static Func *class_find_method(LumiClass *c, const char *name, LumiClass **owner)
{
    for (; c; c = c->parent) {
        Value *v = env_find(c->methods, name);
        if (v) { if (owner) *owner = c; return AS_FUNC(*v); }
    }
    if (owner) *owner = NULL;
    return NULL;
}

static Value *class_find_var(LumiClass *c, const char *name, LumiClass **owner)
{
    for (; c; c = c->parent) {
        Value *v = env_find(c->vars, name);
        if (v) { if (owner) *owner = c; return v; }
    }
    return NULL;
}

static const char *class_find_var_type(LumiClass *c, const char *name)
{
    for (; c; c = c->parent) {
        Value *v = env_find(c->vars, name);
        if (v) {
            for (size_t i = 0; i < c->vars->len; i++)
                if (strcmp(c->vars->slots[i].name, name) == 0)
                    return c->vars->slots[i].type;
            return NULL;
        }
    }
    return NULL;
}

static bool class_inherits(LumiClass *c, LumiClass *other)
{
    for (; c; c = c->parent) if (c == other) return true;
    return false;
}

static void instance_str_into(Interp *in, Instance *inst, Buf *b)
{
    LumiClass *owner = NULL;
    Func *m = class_find_method(inst->cls, "text", &owner);
    if (m && m->node->nparams == 0) {
        char *name = xsprintf("%s.text", inst->cls->name);
        Value r = invoke_function(in, m, NULL, 0, name, NO_LINE, inst, owner);
        free(name);
        to_str_into(in, r, b);
        release(r);
        return;
    }
    buf_take(b, xsprintf("<%s object>", inst->cls->name));
}

char *R(Interp *in, Value v) { return value_repr_utf8(in, v); }
char *S(Interp *in, Value v) { return value_to_utf8(in, v); }

/* ============================================================
 * 3. 숫자 도우미
 * ============================================================ */

static void require_number(Interp *in, Value v, int line)
{
    if (v.kind == V_BOOL || !IS_NUM(v)) {
        char *s = S(in, v);
        lumi_error(line, "Expected a number but got '%s'", s);
    }
}

static double num_as_double(Value v)
{
    if (v.kind == V_INT) return (double)v.as.i;
    if (v.kind == V_FLOAT) return v.as.f;
    return v.as.b ? 1 : 0;
}

static long long require_int(Interp *in, Value v, int line, const char *opname)
{
    if (v.kind == V_INT) return v.as.i;
    if (v.kind == V_FLOAT && v.as.f == floor(v.as.f)
        && v.as.f >= -9.2e18 && v.as.f <= 9.2e18)
        return (long long)v.as.f;
    char *s = R(in, v);
    lumi_error(line, "%s needs whole numbers but got %s", opname, s);
    return 0;
}

static void int_overflow(int line)
{
    lumi_error_kind(line, "MathError",
                    "this whole number is too big for Lumi "
                    "(whole numbers go up to 9223372036854775807); "
                    "if you do not need it exact, use a decimal like 1.0 * x");
}

static long long add_ll(long long a, long long b, int line)
{
    if ((b > 0 && a > 0x7FFFFFFFFFFFFFFFLL - b)
        || (b < 0 && a < (-0x7FFFFFFFFFFFFFFFLL - 1) - b)) int_overflow(line);
    return a + b;
}
static long long sub_ll(long long a, long long b, int line)
{
    if ((b < 0 && a > 0x7FFFFFFFFFFFFFFFLL + b)
        || (b > 0 && a < (-0x7FFFFFFFFFFFFFFFLL - 1) + b)) int_overflow(line);
    return a - b;
}
static long long mul_ll(long long a, long long b, int line)
{
    if (a == 0 || b == 0) return 0;
    long long r = a * b;
    if (r / b != a) int_overflow(line);
    return r;
}

/* 파이썬의 % : 결과의 부호는 나누는 수를 따라갑니다 */
static long long mod_ll(long long a, long long b)
{
    long long r = a % b;
    if (r != 0 && ((r < 0) != (b < 0))) r += b;
    return r;
}
static double mod_d(double a, double b)
{
    double r = fmod(a, b);
    if (r != 0 && ((r < 0) != (b < 0))) r += b;
    return r;
}

/* ============================================================
 * 4. 견주기 (== 와 < >)
 * ============================================================ */

static bool raw_equal(Interp *in, Value a, Value b)
{
    bool an = (a.kind == V_INT || a.kind == V_FLOAT || a.kind == V_BOOL);
    bool bn = (b.kind == V_INT || b.kind == V_FLOAT || b.kind == V_BOOL);
    if (an && bn) {
        if (a.kind == V_INT && b.kind == V_INT) return a.as.i == b.as.i;
        return num_as_double(a) == num_as_double(b);
    }
    if (a.kind != b.kind) return false;
    switch (a.kind) {
    case V_NONE: return true;
    case V_STR: return str_eq(AS_STR(a), AS_STR(b));
    case V_BYTES: {
        BytesObj *x = AS_BYTES(a), *y = AS_BYTES(b);
        return x->len == y->len && memcmp(x->data, y->data, x->len) == 0;
    }
    case V_LIST: case V_TUPLE: {
        Seq *x = AS_SEQ(a), *y = AS_SEQ(b);
        if (x->len != y->len) return false;
        for (size_t i = 0; i < x->len; i++)
            if (!raw_equal(in, x->items[i], y->items[i])) return false;
        return true;
    }
    case V_DICT: {
        Dict *x = AS_DICT(a), *y = AS_DICT(b);
        if (x->len != y->len) return false;
        for (size_t i = 0; i < x->len; i++) {
            Value *found = dict_find(y, x->e[i].key);
            if (!found || !raw_equal(in, x->e[i].val, *found)) return false;
        }
        return true;
    }
    default: return a.as.o == b.as.o;
    }
}

/* '==' : 클래스가 equals(다른값) 을 정해 두었으면 그 뜻을 따릅니다 */
static bool values_equal(Interp *in, Value a, Value b, int line)
{
    Value pair[2] = { a, b };
    for (int k = 0; k < 2; k++) {
        Value x = pair[k], y = pair[1 - k];
        if (x.kind == V_INSTANCE) {
            LumiClass *owner = NULL;
            Func *m = class_find_method(AS_INST(x)->cls, "equals", &owner);
            if (m && m->node->nparams == 1) {
                char *name = xsprintf("%s.equals", AS_INST(x)->cls->name);
                Value arg = retain(y);
                Value r = invoke_function(in, m, &arg, 1, name, line,
                                          AS_INST(x), owner);
                free(name);
                bool ok = value_truthy(r);
                release(r);
                return ok;
            }
        }
    }
    return raw_equal(in, a, b);
}

/* 크고 작음.  견줄 수 없으면 *ok 를 false 로 둡니다. */
static int cmp_values(Interp *in, Value a, Value b, bool *ok)
{
    *ok = true;
    bool an = (a.kind == V_INT || a.kind == V_FLOAT || a.kind == V_BOOL);
    bool bn = (b.kind == V_INT || b.kind == V_FLOAT || b.kind == V_BOOL);
    if (an && bn) {
        if (a.kind == V_INT && b.kind == V_INT)
            return a.as.i < b.as.i ? -1 : (a.as.i > b.as.i ? 1 : 0);
        double x = num_as_double(a), y = num_as_double(b);
        return x < y ? -1 : (x > y ? 1 : 0);
    }
    if (a.kind == V_STR && b.kind == V_STR) return str_cmp(AS_STR(a), AS_STR(b));
    if (a.kind == V_BYTES && b.kind == V_BYTES) {
        BytesObj *x = AS_BYTES(a), *y = AS_BYTES(b);
        size_t n = x->len < y->len ? x->len : y->len;
        int c = n ? memcmp(x->data, y->data, n) : 0;
        if (c) return c < 0 ? -1 : 1;
        return x->len == y->len ? 0 : (x->len < y->len ? -1 : 1);
    }
    if ((a.kind == V_LIST && b.kind == V_LIST)
        || (a.kind == V_TUPLE && b.kind == V_TUPLE)) {
        Seq *x = AS_SEQ(a), *y = AS_SEQ(b);
        size_t n = x->len < y->len ? x->len : y->len;
        for (size_t i = 0; i < n; i++) {
            if (raw_equal(in, x->items[i], y->items[i])) continue;
            int c = cmp_values(in, x->items[i], y->items[i], ok);
            if (!*ok) return 0;
            return c;
        }
        return x->len == y->len ? 0 : (x->len < y->len ? -1 : 1);
    }
    *ok = false;
    return 0;
}

static Value do_compare(Interp *in, const char *op, Value l, Value r, int line)
{
    bool ok = true;
    int c = cmp_values(in, l, r, &ok);
    if (!ok) {
        char *a = R(in, l), *b = R(in, r);
        lumi_error(line, "'%s' cannot compare %s with %s: they are different kinds "
                         "of value, so there is no bigger or smaller between them",
                   op, a, b);
    }
    if (strcmp(op, "<") == 0)  return bool_val(c < 0);
    if (strcmp(op, ">") == 0)  return bool_val(c > 0);
    if (strcmp(op, "<=") == 0) return bool_val(c <= 0);
    return bool_val(c >= 0);
}

/* ============================================================
 * 5. 자리 지정 (upper / lower / strip / erase 의 '어디를')
 * ============================================================ */

typedef enum { PLACE_ALL, PLACE_RANGE, PLACE_VALUE } PlaceKind;
typedef struct {
    PlaceKind kind;
    bool has_start, has_end;
    long long start, end;
    Value value;                /* PLACE_VALUE 일 때 (빌린 참조) */
} Place;

static long long slice_bound(Interp *in, Value v, int line)
{
    if (v.kind == V_BOOL || !IS_NUM(v))
        lumi_error(line, "A slice bound must be a whole number");
    if (v.kind == V_FLOAT) {
        if (v.as.f != floor(v.as.f))
            lumi_error(line, "A slice bound must be a whole number");
        return (long long)v.as.f;
    }
    return v.as.i;
}

/* 파이썬 slice(start, end).indices(length) 와 같은 규칙 */
static void slice_indices(bool has_start, long long start, bool has_end, long long end,
                          size_t length, size_t *lo, size_t *hi)
{
    long long n = (long long)length;
    long long s = has_start ? start : 0;
    long long e = has_end ? end : n;
    if (s < 0) { s += n; if (s < 0) s = 0; }
    else if (s > n) s = n;
    if (e < 0) { e += n; if (e < 0) e = 0; }
    else if (e > n) e = n;
    if (e < s) e = s;
    *lo = (size_t)s;
    *hi = (size_t)e;
}

static size_t place_index(Interp *in, const char *what, Value v, size_t length,
                          int line, const char *noun)
{
    if (v.kind == V_BOOL || !IS_NUM(v)) {
        char *s = R(in, v);
        lumi_error(line, "%s needs a whole number for the place to work on "
                         "(or a list of them) but got %s", what, s);
    }
    if (v.kind == V_FLOAT && v.as.f != floor(v.as.f)) {
        char *s = S(in, v);
        lumi_error(line, "%s needs a whole number for the place to work on but got %s",
                   what, s);
    }
    long long idx = v.kind == V_FLOAT ? (long long)v.as.f : v.as.i;
    if (idx < 0) idx += (long long)length;
    if (idx < 0 || idx >= (long long)length) {
        char *s = S(in, v);
        lumi_error(line, "%s: place %s is outside the %s (its length is %zu)",
                   what, s, noun, length);
    }
    return (size_t)idx;
}

/* '어디를' 을 실제 자리 번호들로 (malloc 배열) */
static size_t *places_of(Interp *in, const char *what, Place *where, size_t length,
                         int line, const char *noun, size_t *out_n)
{
    if (where->kind == PLACE_ALL) {
        size_t *out = (size_t *)xmalloc((length ? length : 1) * sizeof(size_t));
        for (size_t i = 0; i < length; i++) out[i] = i;
        *out_n = length;
        return out;
    }
    if (where->kind == PLACE_RANGE) {
        size_t lo, hi;
        slice_indices(where->has_start, where->start, where->has_end, where->end,
                      length, &lo, &hi);
        size_t n = hi - lo;
        size_t *out = (size_t *)xmalloc((n ? n : 1) * sizeof(size_t));
        for (size_t i = 0; i < n; i++) out[i] = lo + i;
        *out_n = n;
        return out;
    }
    Value v = where->value;
    if (v.kind == V_LIST || v.kind == V_TUPLE) {
        Seq *s = AS_SEQ(v);
        size_t *out = (size_t *)xmalloc((s->len ? s->len : 1) * sizeof(size_t));
        for (size_t i = 0; i < s->len; i++)
            out[i] = place_index(in, what, s->items[i], length, line, noun);
        *out_n = s->len;
        return out;
    }
    if (v.kind == V_DICT)
        lumi_error(line, "%s needs a place number, a range like 0:3, or a list of "
                         "place numbers; a dictionary has no places", what);
    size_t *out = (size_t *)xmalloc(sizeof(size_t));
    out[0] = place_index(in, what, v, length, line, noun);
    *out_n = 1;
    return out;
}

static Str *place_text(Interp *in, const char *what, Value v, int line)
{
    if (v.kind != V_STR) {
        char *s = R(in, v);
        lumi_error(line, "%s needs text (a string) to work on but got %s", what, s);
    }
    return AS_STR(v);
}

/* ============================================================
 * 6. 자료형 검사 / 기본값 / 형변환
 * ============================================================ */

static Value default_for_type(const char *t)
{
    if (strcmp(t, "int") == 0) return int_val(0);
    if (strcmp(t, "float") == 0) return float_val(0.0);
    if (strcmp(t, "num") == 0) return int_val(0);
    if (strcmp(t, "char") == 0) return str_value(" ");
    if (strcmp(t, "str") == 0) return str_value("");
    if (strcmp(t, "text") == 0) return str_value("");
    if (strcmp(t, "seq") == 0) return obj_val(seq_new(V_LIST));
    if (strcmp(t, "bytes") == 0) return obj_val(bytes_new(NULL, 0));
    if (strcmp(t, "bool") == 0) return bool_val(false);
    if (strcmp(t, "list") == 0) return obj_val(seq_new(V_LIST));
    if (strcmp(t, "tuple") == 0) return obj_val(seq_new(V_TUPLE));
    if (strcmp(t, "dict") == 0) return obj_val(dict_new());
    return NONE_VAL;
}

/* 바꾸지 않고 갈래만 보는 자료형 (bool/list/tuple/dict).
 * int/char 처럼 값을 고쳐 주지 않습니다 — 맞으면 그대로, 아니면 오류입니다. */
static bool check_only_type(const char *t, VKind *want)
{
    if (strcmp(t, "bool")  == 0) { *want = V_BOOL;  return true; }
    if (strcmp(t, "list")  == 0) { *want = V_LIST;  return true; }
    if (strcmp(t, "tuple") == 0) { *want = V_TUPLE; return true; }
    if (strcmp(t, "dict")  == 0) { *want = V_DICT;  return true; }
    return false;
}

static Value coerce_to_type(Interp *in, const char *t, Value v, int line)
{
    VKind want;
    if (check_only_type(t, &want)) {
        if (v.kind == want) return retain(v);
        char *s = R(in, v);
        lumi_error(line, "%s needs a %s but got %s", t, t, s);
    }
    if (strcmp(t, "int") == 0) {
        if (v.kind == V_BOOL) {
            char *s = R(in, v);
            lumi_error(line, "int needs a number or a character but got %s", s);
        }
        if (v.kind == V_STR) {
            Str *s = AS_STR(v);
            if (s->len == 0)
                lumi_error(line, "int needs a character to turn into an ASCII code, "
                                 "but got an empty string");
            return int_val((long long)s->cp[0]);
        }
        if (v.kind == V_INT) return retain(v);
        if (v.kind == V_FLOAT) return int_val((long long)v.as.f);
        char *s = R(in, v);
        lumi_error(line, "int needs a number or a character but got %s", s);
    }
    if (strcmp(t, "float") == 0) {
        if (v.kind == V_BOOL || !IS_NUM(v)) {
            char *s = R(in, v);
            lumi_error(line, "float needs a number but got %s", s);
        }
        return float_val(num_as_double(v));
    }
    if (strcmp(t, "num") == 0) {
        if (v.kind == V_BOOL || !IS_NUM(v)) {
            char *s = R(in, v);
            lumi_error(line, "num needs a number but got %s", s);
        }
        return retain(v);
    }
    if (strcmp(t, "char") == 0) {
        if (v.kind == V_BOOL) {
            char *s = R(in, v);
            lumi_error(line, "char needs a character or a number but got %s", s);
        }
        if (IS_NUM(v)) {
            long long code = v.kind == V_FLOAT ? (long long)v.as.f : v.as.i;
            if (code < 0 || code > 0x10FFFF)
                lumi_error(line, "char code %lld is out of range (0 to 1114111)", code);
            uint32_t c = (uint32_t)code;
            return obj_val(str_new(&c, 1));
        }
        if (v.kind == V_STR) {
            Str *s = AS_STR(v);
            if (s->len == 0)
                lumi_error(line, "char needs at least one character but got an empty string");
            return obj_val(str_new(s->cp, 1));
        }
        char *s = R(in, v);
        lumi_error(line, "char needs a character or a number but got %s", s);
    }
    if (strcmp(t, "str") == 0 || strcmp(t, "text") == 0) {
        if (v.kind != V_STR) {
            char *s = R(in, v);
            lumi_error(line, "%s needs text (a string) but got %s", t, s);
        }
        return retain(v);
    }
    if (strcmp(t, "seq") == 0) {
        if (v.kind == V_LIST || v.kind == V_TUPLE || v.kind == V_STR || v.kind == V_BYTES)
            return retain(v);
        char *s = R(in, v);
        lumi_error(line, "seq needs a sequence (a list, tuple, string, or bytes) "
                         "but got %s", s);
    }
    if (strcmp(t, "bytes") == 0) {
        if (v.kind == V_BYTES) return retain(v);
        if (v.kind == V_STR)
            lumi_error(line, "bytes needs a bytes object, and text is not one by "
                             "itself; turn the text into bytes first with encode(...), "
                             "like bytes b = encode(\"hi\")");
        char *s = R(in, v);
        lumi_error(line, "bytes needs a bytes object but got %s", s);
    }
    lumi_error(line, "Unknown type '%s'", t);
    return NONE_VAL;
}

/* "0b1011" 처럼 다른 진법으로 적은 글자 -> 정수 (아니면 false) */
static bool based_text_to_int(const Str *s, long long *out)
{
    size_t i = 0;
    bool negative = false;
    if (s->len > 0 && (s->cp[0] == '+' || s->cp[0] == '-')) {
        negative = s->cp[0] == '-';
        i = 1;
    }
    if (s->len < i + 3) return false;
    if (s->cp[i] != '0') return false;
    uint32_t p = s->cp[i + 1];
    int base;
    if (p == 'b' || p == 'B') base = 2;
    else if (p == 'x' || p == 'X') base = 16;
    else return false;
    i += 2;
    long long value = 0;
    size_t digits = 0;
    for (; i < s->len; i++) {
        uint32_t c = s->cp[i];
        int d;
        if (c >= '0' && c <= '9') d = (int)(c - '0');
        else if (c >= 'a' && c <= 'f') d = (int)(c - 'a') + 10;
        else if (c >= 'A' && c <= 'F') d = (int)(c - 'A') + 10;
        else return false;
        if (d >= base) return false;
        if (value > (0x7FFFFFFFFFFFFFFFLL - d) / base) return false;
        value = value * base + d;
        digits++;
    }
    if (!digits) return false;
    *out = negative ? -value : value;
    return true;
}

/* 글자 -> 숫자 (int/float/num 이 함께 쓰는 규칙) */
static Value text_to_number(Interp *in, Str *s, const char *what, int line)
{
    /* 앞뒤 공백을 털어낸 부분만 봅니다 */
    size_t a = 0, b = s->len;
    while (a < b && is_split_space(s->cp[a])) a++;
    while (b > a && is_split_space(s->cp[b - 1])) b--;
    Str *trimmed = str_slice(s, a, b);

    long long based;
    if (based_text_to_int(trimmed, &based)) {
        release(obj_val(trimmed));
        return int_val(based);
    }

    char *u = str_to_utf8(trimmed);
    release(obj_val(trimmed));

    /* 정수로 읽어 보기 */
    if (*u) {
        char *end = NULL;
        bool all_int = true;
        for (const char *q = u + ((*u == '+' || *u == '-') ? 1 : 0); *q; q++)
            if (*q < '0' || *q > '9') { all_int = false; break; }
        if (all_int && u[(*u == '+' || *u == '-') ? 1 : 0]) {
            long long n = strtoll(u, &end, 10);
            if (end && *end == 0) { free(u); return int_val(n); }
        }
        end = NULL;
        double d = strtod(u, &end);
        if (end && *end == 0 && end != u) { free(u); return float_val(d); }
    }
    free(u);

    if (s->len == 1) return int_val((long long)s->cp[0]);   /* 한 글자 -> 코드 번호 */
    char *r = R(in, obj_val(s));
    lumi_error(line, "%s cannot turn %s into a number", what, r);
    return NONE_VAL;
}

static Value to_number_for(Interp *in, const char *what, Value v, int line)
{
    if (v.kind == V_BOOL) return int_val(v.as.b ? 1 : 0);
    if (IS_NUM(v)) return retain(v);
    if (v.kind == V_STR) return text_to_number(in, AS_STR(v), what, line);
    char *s = R(in, v);
    lumi_error(line, "%s cannot turn %s into a number", what, s);
    return NONE_VAL;
}

static Seq *items_of(Interp *in, Value v, const char *what, int line)
{
    Seq *out = seq_new(V_LIST);
    if (v.kind == V_STR) {
        Str *s = AS_STR(v);
        for (size_t i = 0; i < s->len; i++)
            seq_push(out, obj_val(str_new(s->cp + i, 1)));
        return out;
    }
    if (v.kind == V_LIST || v.kind == V_TUPLE) {
        Seq *s = AS_SEQ(v);
        for (size_t i = 0; i < s->len; i++) seq_push(out, retain(s->items[i]));
        return out;
    }
    if (v.kind == V_BYTES) {
        BytesObj *b = AS_BYTES(v);
        for (size_t i = 0; i < b->len; i++) seq_push(out, int_val(b->data[i]));
        return out;
    }
    if (v.kind == V_DICT) {
        Dict *d = AS_DICT(v);
        for (size_t i = 0; i < d->len; i++) seq_push(out, retain(d->e[i].key));
        return out;
    }
    release(obj_val(out));
    char *s = R(in, v);
    lumi_error(line, "%s needs a string, list, tuple, bytes, or dictionary but got %s",
               what, s);
    return NULL;
}

static Value to_bytes_value(Interp *in, Value v, int line)
{
    if (v.kind == V_BYTES) {
        BytesObj *b = AS_BYTES(v);
        return obj_val(bytes_new(b->data, b->len));
    }
    if (v.kind == V_STR)
        lumi_error(line, "bytes cannot turn text into bytes on its own, because that "
                         "needs an encoding; use encode(...) instead, like "
                         "encode(\"hi\") or encode(\"cp949\", \"hi\")");
    if (v.kind == V_LIST || v.kind == V_TUPLE) {
        Seq *s = AS_SEQ(v);
        uint8_t *buf = (uint8_t *)xmalloc(s->len ? s->len : 1);
        for (size_t i = 0; i < s->len; i++) {
            Value it = s->items[i];
            if (it.kind == V_BOOL || !IS_NUM(it)) {
                char *r = R(in, it);
                free(buf);
                lumi_error(line, "bytes needs whole numbers from 0 to 255 but got %s", r);
            }
            if (it.kind == V_FLOAT && it.as.f != floor(it.as.f)) {
                char *r = S(in, it);
                free(buf);
                lumi_error(line, "bytes needs whole numbers but got %s", r);
            }
            long long n = it.kind == V_FLOAT ? (long long)it.as.f : it.as.i;
            if (n < 0 || n > 255) {
                free(buf);
                lumi_error(line, "bytes needs numbers from 0 to 255 but got %lld", n);
            }
            buf[i] = (uint8_t)n;
        }
        Value out = obj_val(bytes_new(buf, s->len));
        free(buf);
        return out;
    }
    char *s = R(in, v);
    lumi_error(line, "bytes needs a bytes object or a list of numbers from 0 to 255 "
                     "but got %s", s);
    return NONE_VAL;
}

static Value convert_value(Interp *in, const char *type_name, Value v, int line)
{
    if (strcmp(type_name, "str") == 0 || strcmp(type_name, "text") == 0)
        return obj_val(value_to_str(in, v));
    if (strcmp(type_name, "bool") == 0) return bool_val(value_truthy(v));
    if (strcmp(type_name, "num") == 0) return to_number_for(in, "num", v, line);
    if (strcmp(type_name, "float") == 0) {
        Value n = to_number_for(in, "float", v, line);
        double d = num_as_double(n);
        release(n);
        return float_val(d);
    }
    if (strcmp(type_name, "int") == 0) {
        Value n = to_number_for(in, "int", v, line);
        if (n.kind == V_FLOAT) {
            double d = n.as.f;
            release(n);
            if (isnan(d) || isinf(d))
                lumi_error(line, "int cannot convert a number that is not a finite value");
            return int_val((long long)d);
        }
        return n;
    }
    if (strcmp(type_name, "char") == 0) {
        if (v.kind == V_BOOL) {
            char *s = R(in, v);
            lumi_error(line, "char cannot turn %s into a character", s);
        }
        if (IS_NUM(v)) {
            long long code = v.kind == V_FLOAT ? (long long)v.as.f : v.as.i;
            if (code < 0 || code > 0x10FFFF)
                lumi_error(line, "char code %lld is out of range (0 to 1114111)", code);
            uint32_t c = (uint32_t)code;
            return obj_val(str_new(&c, 1));
        }
        if (v.kind == V_STR) {
            Str *s = AS_STR(v);
            if (s->len == 0)
                lumi_error(line, "char needs at least one character but got an empty string");
            return obj_val(str_new(s->cp, 1));
        }
        char *s = R(in, v);
        lumi_error(line, "char cannot turn %s into a character", s);
    }
    if (strcmp(type_name, "list") == 0)
        return obj_val(items_of(in, v, "list", line));
    if (strcmp(type_name, "tuple") == 0) {
        Seq *s = items_of(in, v, "tuple", line);
        s->h.kind = V_TUPLE;
        return obj_val(s);
    }
    if (strcmp(type_name, "seq") == 0) {
        if (v.kind == V_LIST || v.kind == V_TUPLE || v.kind == V_STR || v.kind == V_BYTES)
            return retain(v);
        return obj_val(items_of(in, v, "seq", line));
    }
    if (strcmp(type_name, "bytes") == 0) return to_bytes_value(in, v, line);
    if (strcmp(type_name, "dict") == 0) {
        if (v.kind == V_DICT) {
            Dict *src = AS_DICT(v), *d = dict_new();
            for (size_t i = 0; i < src->len; i++)
                dict_set(d, retain(src->e[i].key), retain(src->e[i].val));
            return obj_val(d);
        }
        if (v.kind == V_LIST || v.kind == V_TUPLE) {
            Seq *s = AS_SEQ(v);
            Dict *d = dict_new();
            for (size_t i = 0; i < s->len; i++) {
                Value pair = s->items[i];
                if ((pair.kind != V_LIST && pair.kind != V_TUPLE)
                    || AS_SEQ(pair)->len != 2) {
                    char *r = R(in, pair);
                    release(obj_val(d));
                    lumi_error(line, "dict needs a list of key-value pairs, for example "
                                     "[[\"a\", 1], [\"b\", 2]], but got %s", r);
                }
                Value key = AS_SEQ(pair)->items[0];
                check_dict_key(in, key, line);
                dict_set(d, retain(key), retain(AS_SEQ(pair)->items[1]));
            }
            return obj_val(d);
        }
        char *s = R(in, v);
        lumi_error(line, "dict needs a dictionary or a list of key-value pairs but got %s", s);
    }
    lumi_error(line, "Unknown type '%s'", type_name);
    return NONE_VAL;
}

static void check_dict_key(Interp *in, Value key, int line)
{
    if (key.kind == V_LIST || key.kind == V_TUPLE || key.kind == V_DICT
        || key.kind == V_INSTANCE || key.kind == V_CLASS)
        lumi_error(line, "A dictionary key must be text, a number, or true/false "
                         "(not a list, tuple, dictionary, class, or object)");
}

/* ============================================================
 * 7. 자리 잡기 / 자르기
 * ============================================================ */

Value do_index(Interp *in, Value target, Value index, int line)
{
    if (target.kind == V_INSTANCE) {
        LumiClass *owner = NULL;
        Func *m = class_find_method(AS_INST(target)->cls, "get", &owner);
        if (m && m->node->nparams == 1) {
            char *fname = xsprintf("%s.get", AS_INST(target)->cls->name);
            Value arg = retain(index);
            Value r = invoke_function(in, m, &arg, 1, fname, line, AS_INST(target), owner);
            free(fname);
            return r;
        }
    }
    if (target.kind == V_DICT) {
        check_dict_key(in, index, line);
        Value *found = dict_find(AS_DICT(target), index);
        if (!found) {
            char *r = R(in, index);
            lumi_error_kind(line, "KeyError", "Key %s is not in the dictionary", r);
        }
        return retain(*found);
    }
    if (!IS_NUM(index) || index.kind == V_BOOL)
        lumi_error(line, "List index must be a number");
    if (index.kind == V_FLOAT && index.as.f != floor(index.as.f))
        lumi_error(line, "List index must be a whole number");
    long long idx = index.kind == V_FLOAT ? (long long)index.as.f : index.as.i;

    size_t n;
    if (target.kind == V_LIST || target.kind == V_TUPLE) n = AS_SEQ(target)->len;
    else if (target.kind == V_STR) n = AS_STR(target)->len;
    else if (target.kind == V_BYTES) n = AS_BYTES(target)->len;
    else {
        char *s = S(in, target);
        lumi_error(line, "Cannot index into '%s' (only lists, tuples, strings, "
                         "bytes, dictionaries)", s);
        return NONE_VAL;
    }
    if (idx < 0) idx += (long long)n;
    if (idx < 0 || idx >= (long long)n) lumi_error_kind(line, "IndexError", "Index out of range");

    if (target.kind == V_STR) return obj_val(str_new(AS_STR(target)->cp + idx, 1));
    if (target.kind == V_BYTES) return int_val(AS_BYTES(target)->data[idx]);
    return retain(AS_SEQ(target)->items[idx]);
}

Value do_slice(Interp *in, Value target, Value *start, Value *end, int line)
{
    size_t n;
    if (target.kind == V_LIST || target.kind == V_TUPLE) n = AS_SEQ(target)->len;
    else if (target.kind == V_STR) n = AS_STR(target)->len;
    else if (target.kind == V_BYTES) n = AS_BYTES(target)->len;
    else {
        char *s = S(in, target);
        lumi_error(line, "Cannot slice '%s' (only lists, tuples, strings, bytes)", s);
        return NONE_VAL;
    }
    bool hs = start != NULL, he = end != NULL;
    long long s0 = hs ? slice_bound(in, *start, line) : 0;
    long long e0 = he ? slice_bound(in, *end, line) : 0;
    size_t lo, hi;
    slice_indices(hs, s0, he, e0, n, &lo, &hi);

    if (target.kind == V_STR) return obj_val(str_slice(AS_STR(target), lo, hi));
    if (target.kind == V_BYTES)
        return obj_val(bytes_new(AS_BYTES(target)->data + lo, hi - lo));
    Seq *src = AS_SEQ(target);
    Seq *out = seq_new(target.kind);
    for (size_t i = lo; i < hi; i++) seq_push(out, retain(src->items[i]));
    return obj_val(out);
}

void set_index(Interp *in, Value target, Value index, Value value, int line)
{
    if (target.kind == V_INSTANCE) {
        LumiClass *owner = NULL;
        Func *m = class_find_method(AS_INST(target)->cls, "set", &owner);
        if (m && m->node->nparams == 2) {
            char *fname = xsprintf("%s.set", AS_INST(target)->cls->name);
            Value args[2] = { retain(index), retain(value) };
            Value r = invoke_function(in, m, args, 2, fname, line, AS_INST(target), owner);
            free(fname);
            release(r);
            return;
        }
    }
    if (target.kind == V_DICT) {
        check_dict_key(in, index, line);
        dict_set(AS_DICT(target), retain(index), retain(value));
        return;
    }
    if (!IS_NUM(index) || index.kind == V_BOOL)
        lumi_error(line, "List index must be a number");
    if (index.kind == V_FLOAT && index.as.f != floor(index.as.f))
        lumi_error(line, "List index must be a whole number");
    long long idx = index.kind == V_FLOAT ? (long long)index.as.f : index.as.i;

    if (target.kind == V_STR)
        lumi_error(line, "Text (a string) cannot be changed by index (it is read-only)");
    if (target.kind == V_TUPLE)
        lumi_error(line, "A tuple cannot be changed (it is read-only); use a list instead");
    if (target.kind == V_BYTES)
        lumi_error(line, "Bytes cannot be changed by index (they are read-only); make "
                         "new bytes instead, for example with bytes(list(b))");
    if (target.kind == V_LIST) {
        Seq *s = AS_SEQ(target);
        long long n = (long long)s->len;
        if (idx < 0) idx += n;
        if (idx < 0 || idx >= n) lumi_error_kind(line, "IndexError", "Index out of range");
        release(s->items[idx]);
        s->items[idx] = retain(value);
        return;
    }
    char *s = S(in, target);
    lumi_error(line, "Cannot assign into '%s' by index (only lists)", s);
}

/* ============================================================
 * 8. 시퀀스 도우미
 * ============================================================ */

static size_t seq_length(Value v)
{
    if (v.kind == V_STR) return AS_STR(v)->len;
    if (v.kind == V_LIST || v.kind == V_TUPLE) return AS_SEQ(v)->len;
    if (v.kind == V_BYTES) return AS_BYTES(v)->len;
    if (v.kind == V_DICT) return AS_DICT(v)->len;
    return 0;
}

static Value repeat_sequence(Interp *in, Value seq, Value count, int line)
{
    if (count.kind == V_BOOL || !IS_NUM(count)) {
        char *r = R(in, count);
        lumi_error(line, "To repeat a sequence with '*', the other side must be a "
                         "whole number (for example: \"ab\" * 3), but got %s", r);
    }
    if (count.kind == V_FLOAT && count.as.f != floor(count.as.f)) {
        char *r = S(in, count);
        lumi_error(line, "The repeat count for '*' must be a whole number but got %s", r);
    }
    long long n = count.kind == V_FLOAT ? (long long)count.as.f : count.as.i;
    if (n < 0) n = 0;
    if (n > 100000000LL) int_overflow(line);

    if (seq.kind == V_STR) {
        Str *s = AS_STR(seq);
        size_t total = s->len * (size_t)n;
        uint32_t *buf = (uint32_t *)xmalloc((total + 1) * sizeof(uint32_t));
        for (long long k = 0; k < n; k++)
            memcpy(buf + (size_t)k * s->len, s->cp, s->len * sizeof(uint32_t));
        Value out = obj_val(str_new(buf, total));
        free(buf);
        return out;
    }
    if (seq.kind == V_BYTES) {
        BytesObj *b = AS_BYTES(seq);
        size_t total = b->len * (size_t)n;
        uint8_t *buf = (uint8_t *)xmalloc(total ? total : 1);
        for (long long k = 0; k < n; k++)
            memcpy(buf + (size_t)k * b->len, b->data, b->len);
        Value out = obj_val(bytes_new(buf, total));
        free(buf);
        return out;
    }
    Seq *s = AS_SEQ(seq);
    Seq *out = seq_new(seq.kind);
    for (long long k = 0; k < n; k++)
        for (size_t i = 0; i < s->len; i++) seq_push(out, retain(s->items[i]));
    return obj_val(out);
}

static bool membership(Interp *in, Value item, Value container, int line)
{
    if (container.kind == V_STR) {
        if (item.kind != V_STR) {
            char *r = R(in, item);
            lumi_error(line, "'in' on text (a string) checks whether a smaller string "
                             "is inside it, so the left side must be a string, but got %s", r);
        }
        Str *h = AS_STR(container), *nd = AS_STR(item);
        if (nd->len == 0) return true;
        if (nd->len > h->len) return false;
        for (size_t i = 0; i + nd->len <= h->len; i++)
            if (memcmp(h->cp + i, nd->cp, nd->len * sizeof(uint32_t)) == 0) return true;
        return false;
    }
    if (container.kind == V_BYTES) {
        BytesObj *h = AS_BYTES(container);
        if (item.kind == V_BYTES) {
            BytesObj *nd = AS_BYTES(item);
            if (nd->len == 0) return true;
            if (nd->len > h->len) return false;
            for (size_t i = 0; i + nd->len <= h->len; i++)
                if (memcmp(h->data + i, nd->data, nd->len) == 0) return true;
            return false;
        }
        if (item.kind != V_BOOL && IS_NUM(item)) {
            long long n = require_int(in, item, line, "'in' on bytes");
            if (n < 0 || n > 255) return false;
            for (size_t i = 0; i < h->len; i++) if (h->data[i] == n) return true;
            return false;
        }
        char *r = R(in, item);
        lumi_error(line, "'in' on bytes looks for a number from 0 to 255 or for smaller "
                         "bytes, so the left side cannot be %s", r);
    }
    if (container.kind == V_LIST || container.kind == V_TUPLE) {
        Seq *s = AS_SEQ(container);
        for (size_t i = 0; i < s->len; i++)
            if (values_equal(in, item, s->items[i], line)) return true;
        return false;
    }
    if (container.kind == V_DICT) {
        Dict *d = AS_DICT(container);
        for (size_t i = 0; i < d->len; i++)
            if (values_equal(in, item, d->e[i].key, line)) return true;
        return false;
    }
    char *r = R(in, container);
    lumi_error(line, "'in' needs a sequence (string, list, tuple, or bytes) or a "
                     "dictionary on the right side, but got %s", r);
    return false;
}

/* ============================================================
 * 9. 정렬 · 뒤섞기
 * ============================================================ */

static const char *sort_kind(Value v)
{
    if (v.kind == V_BOOL || v.kind == V_INT || v.kind == V_FLOAT) return "numbers";
    if (v.kind == V_STR) return "text";
    if (v.kind == V_LIST) return "lists";
    if (v.kind == V_TUPLE) return "tuples";
    if (v.kind == V_BYTES) return "bytes";
    return NULL;
}

static void check_sort_kinds(Interp *in, const char *what, Value *vals, size_t n, int line)
{
    const char *kinds[5];
    size_t nk = 0;
    for (size_t i = 0; i < n; i++) {
        const char *k = sort_kind(vals[i]);
        if (!k) {
            char *r = R(in, vals[i]);
            lumi_error(line, "%s cannot put %s in order: there is no bigger or smaller "
                             "between values like that", what, r);
        }
        bool seen = false;
        for (size_t j = 0; j < nk; j++) if (kinds[j] == k) { seen = true; break; }
        if (!seen && nk < 5) kinds[nk++] = k;
    }
    if (nk > 1) {
        Buf b = {0};
        for (size_t j = 0; j < nk; j++) {
            if (j) buf_str(&b, " and ");
            buf_str(&b, kinds[j]);
        }
        lumi_error(line, "%s needs every value to be of one kind, so that they can be "
                         "compared, but this has %s mixed together", what, buf_done(&b));
    }
}

/* 순서를 지키는 정렬 (merge sort). keys 가 NULL 이면 값 자체로 견줍니다. */
static void merge_sort(Interp *in, Value *items, Value *keys, size_t n, bool reverse,
                       const char *what, int line)
{
    if (n < 2) return;
    Value *tmp_i = (Value *)xmalloc(n * sizeof(Value));
    Value *tmp_k = keys ? (Value *)xmalloc(n * sizeof(Value)) : NULL;
    for (size_t width = 1; width < n; width *= 2) {
        for (size_t lo = 0; lo < n; lo += 2 * width) {
            size_t mid = lo + width < n ? lo + width : n;
            size_t hi = lo + 2 * width < n ? lo + 2 * width : n;
            size_t i = lo, j = mid, k = lo;
            while (i < mid && j < hi) {
                bool ok = true;
                int c = cmp_values(in, keys ? keys[i] : items[i],
                                   keys ? keys[j] : items[j], &ok);
                if (!ok)
                    lumi_error(line, "%s cannot put these in order: comparing %s ends up "
                                     "comparing different kinds of value, so there is no "
                                     "bigger or smaller between them", what,
                               keys ? "the values at that place" : "them");
                bool take_left = reverse ? (c >= 0) : (c <= 0);
                if (take_left) {
                    if (tmp_k) tmp_k[k] = keys[i];
                    tmp_i[k++] = items[i++];
                } else {
                    if (tmp_k) tmp_k[k] = keys[j];
                    tmp_i[k++] = items[j++];
                }
            }
            while (i < mid) { if (tmp_k) tmp_k[k] = keys[i]; tmp_i[k++] = items[i++]; }
            while (j < hi)  { if (tmp_k) tmp_k[k] = keys[j]; tmp_i[k++] = items[j++]; }
            for (size_t x = lo; x < hi; x++) {
                items[x] = tmp_i[x];
                if (keys) keys[x] = tmp_k[x];
            }
        }
    }
    free(tmp_i);
    free(tmp_k);
}

static bool sort_direction(Interp *in, bool given, Value v, int line)
{
    if (!given) return false;                    /* 안 적으면 오름차순 */
    if (v.kind != V_BOOL) {
        char *r = R(in, v);
        lumi_error(line, "sort needs true or false for the direction (true puts the "
                         "smallest first, false puts the biggest first) but got %s", r);
    }
    return !v.as.b;                              /* 내림차순일 때만 뒤집습니다 */
}

static unsigned long long rng_state;
static double lumi_time_clock(void);      /* builtins.inc 에 있습니다 */

static void rng_init(void)
{
    /* 씨앗은 '매번 달라지기만 하면' 됩니다 — 시계 두 개와 주소 하나를 섞습니다. */
    rng_state = (unsigned long long)(lumi_time_clock() * 1e9)
              ^ ((unsigned long long)(uintptr_t)&rng_state << 16)
              ^ (unsigned long long)time(NULL);
    if (!rng_state) rng_state = 0x9E3779B97F4A7C15ULL;
}

static unsigned long long rng_next(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return rng_state;
}

static size_t rng_below(size_t n) { return (size_t)(rng_next() % (n ? n : 1)); }

/* ============================================================
 * 10. 환경 도우미
 * ============================================================ */

static Value env_get(Env *e, const char *name, int line)
{
    Value *v = env_lookup(e, name, NULL);
    if (!v) lumi_error_kind(line, "NameError", "Variable '%s' is not defined", name);
    return retain(*v);
}

static void env_set_existing(Env *e, const char *name, Value v, int line)
{
    Env *owner = NULL;
    Value *slot = env_lookup(e, name, &owner);
    if (!slot)
        lumi_error_kind(line, "NameError", "'%s' is not defined yet. Create it first with 'val %s = ...'",
                   name, name);
    release(*slot);
    *slot = v;
}

/* ============================================================
 * 11. 클래스 / 객체
 * ============================================================ */

static void check_private(Interp *in, const char *name, LumiClass *owner, Env *env, int line)
{
    if (name[0] != '_') return;
    Value *this_ = env_lookup(env, "this", NULL);
    if (this_ && this_->kind == V_INSTANCE) {
        LumiClass *tc = AS_INST(*this_)->cls;
        if (class_inherits(tc, owner) || class_inherits(owner, tc)) return;
    }
    lumi_error(line, "'%s' is private to class '%s': a name starting with '_' can only "
                     "be used inside that class's own methods", name, owner->name);
}

Value member_of(Interp *in, Value obj, const char *name, Env *env, int line)
{
    /* 난 오류에서 꺼내 볼 수 있는 것들 */
    if (obj.kind == V_ERROR) {
        ErrorObj *e = AS_ERR(obj);
        if (strcmp(name, "type") == 0)    return str_value(e->type);
        if (strcmp(name, "message") == 0) return retain(obj_val(e->message));
        if (strcmp(name, "line") == 0)
            return e->line == NO_LINE ? NONE_VAL : int_val(e->line);
        if (strcmp(name, "file") == 0)
            return e->file ? str_value(e->file) : NONE_VAL;
        lumi_error(line, "an error has only .type, .message, .line and .file, but got '%s'",
                   name);
    }
    if (obj.kind == V_MODULE) {
        Value *v = env_find(AS_MOD(obj)->vars, name);
        if (v) return retain(*v);
        if (strcmp(AS_MOD(obj)->name, "json") == 0) {
            if (strcmp(name, "parse") == 0 || strcmp(name, "stringify") == 0) {
                FuncDefNode *fd = (FuncDefNode *)xmalloc(sizeof(FuncDefNode));
                memset(fd, 0, sizeof(*fd));
                fd->name = xstrdup(strcmp(name, "parse") == 0 ? "json.parse" : "json.stringify");
                fd->nparams = 1;
                fd->params = (char **)xmalloc(sizeof(char *));
                fd->params[0] = xstrdup("v");
                Func *fn = func_new(fd, env, NULL);
                return obj_val(fn);
            }
        }
        lumi_error_kind(line, "NameError", "'%s' is not defined in library '%s'", name, AS_MOD(obj)->name);
    }
    if (obj.kind == V_INSTANCE) {
        Instance *inst = AS_INST(obj);
        check_private(in, name, inst->cls, env, line);
        Value *f = env_find(inst->fields, name);
        if (f) return retain(*f);
        LumiClass *owner = NULL;
        Func *m = class_find_method(inst->cls, name, &owner);
        if (m) {
            Bound *b = bound_new(m, inst, owner);
            return obj_val(b);
        }
        Value *cv = class_find_var(inst->cls, name, NULL);
        if (cv) return retain(*cv);
        lumi_error(line, "'%s' object has no member named '%s'", inst->cls->name, name);
    }
    if (obj.kind == V_CLASS) {
        LumiClass *c = AS_CLASS(obj);
        check_private(in, name, c, env, line);
        Value *cv = class_find_var(c, name, NULL);
        if (cv) return retain(*cv);
        LumiClass *owner = NULL;
        Func *m = class_find_method(c, name, &owner);
        if (m) {
            Bound *b = bound_new(m, NULL, owner);
            return obj_val(b);
        }
        lumi_error(line, "class '%s' has no member named '%s'", c->name, name);
    }
    if (obj.kind == V_SUPER) {
        SuperRef *sr = AS_SUPER(obj);
        LumiClass *owner = NULL;
        Func *m = sr->start ? class_find_method(sr->start, name, &owner) : NULL;
        if (m) {
            Bound *b = bound_new(m, sr->inst, owner);
            return obj_val(b);
        }
        if (!sr->start)
            lumi_error(line, "'super.%s' cannot be used here because this class has no "
                             "parent class (write 'class Name from Parent:' to give it one)",
                       name);
        Value *cv = class_find_var(sr->start, name, NULL);
        if (cv) return retain(*cv);
        lumi_error(line, "the parent class '%s' has no member named '%s'",
                   sr->start->name, name);
    }
    char *r = R(in, obj);
    lumi_error(line, "'.%s' can only be used on a library, a class, or an object, "
                     "but got %s", name, r);
    return NONE_VAL;
}

void set_member(Interp *in, Value obj, const char *name, Value value,
                Env *env, int line)
{
    if (obj.kind == V_INSTANCE) {
        Instance *inst = AS_INST(obj);
        check_private(in, name, inst->cls, env, line);
        const char *declared = class_find_var_type(inst->cls, name);
        Value stored = declared ? coerce_to_type(in, declared, value, line) : retain(value);
        env_declare(inst->fields, name, stored, declared);
        return;
    }
    if (obj.kind == V_CLASS) {
        LumiClass *c = AS_CLASS(obj);
        check_private(in, name, c, env, line);
        const char *declared = class_find_var_type(c, name);
        Value stored = declared ? coerce_to_type(in, declared, value, line) : retain(value);
        env_declare(c->vars, name, stored, declared);
        return;
    }
    if (obj.kind == V_MODULE)
        lumi_error(line, "a library cannot be changed: '%s.%s' is read-only",
                   AS_MOD(obj)->name, name);
    if (obj.kind == V_SUPER)
        lumi_error(line, "'super' cannot be assigned to; use 'this.%s = ...' instead", name);
    char *r = R(in, obj);
    lumi_error(line, "cannot set '.%s' on %s (only objects and classes have members "
                     "you can change)", name, r);
}

static Value invoke_function_kw(Interp *in, Func *fn, Value *args, size_t nargs,
                                char **arg_names, const char *name, int line,
                                Instance *this_, LumiClass *owner)
{
    if (strcmp(fn->node->name, "json.parse") == 0) {
        if (nargs != 1) lumi_error(line, "json.parse expects 1 argument");
        Value out = lumi_json_parse(in, args[0], line);
        release(args[0]);
        return out;
    }
    if (strcmp(fn->node->name, "json.stringify") == 0) {
        if (nargs != 1) lumi_error(line, "json.stringify expects 1 argument");
        Value out = lumi_json_stringify(in, args[0], line);
        release(args[0]);
        return out;
    }
    if (fn->node->is_abstract) {
        for (size_t k = 0; k < nargs; k++) release(args[k]);
        lumi_error(line, "cannot call abstract method '%s' directly", name);
    }
    /* 인자를 매개변수 자리에 놓습니다:
     *   1) 이름 없이 준 것은 앞에서부터 차례로
     *   2) 이름 붙여 준 것(f(칸 = 3))은 그 이름의 자리에
     *   3) 그래도 빈 자리는 기본값을 그 함수의 환경에서 새로 셈해서 채웁니다 */
    size_t np = fn->node->nparams;
    Value *slot = np ? (Value *)xmalloc(np * sizeof(Value)) : NULL;
    bool  *got  = np ? (bool  *)xmalloc(np * sizeof(bool))  : NULL;
    for (size_t i = 0; i < np; i++) { slot[i] = NONE_VAL; got[i] = false; }

    /* 실패할 때는 이미 자리에 놓은 것만 놓아 주고 오류를 냅니다.
     * 아직 자리에 못 놓은 인자는 부르는 쪽에서 미리 놓아 주고 옵니다. */
    /* 실패할 때는 이미 자리에 놓은 것만 놓아 주고 오류를 냅니다.
     * 아직 자리에 못 놓은 인자는 부르는 쪽에서 미리 놓아 주고 옵니다. */
    /* 인자를 자리에 못 놓은 것은 전부 ArgumentError 입니다 — 갈래 표에 그 이름을
     * 두고도 아무도 안 내고 있었습니다 (명세를 적다가 찾았습니다). catch Error 로는
     * 그대로 잡히므로 예전 코드가 깨지지 않습니다. */
    #define BIND_FAIL(...) do { bind_undo(slot, got, np); \
                                lumi_error_kind(line, "ArgumentError", __VA_ARGS__); } while (0)

    size_t at = 0;                       /* 다음 이름 없는 인자가 놓일 자리 */
    bool saw_named = false;
    for (size_t i = 0; i < nargs; i++) {
        const char *an = arg_names ? arg_names[i] : NULL;
        if (!an) {
            if (saw_named) {
                release(args[i]);
                for (size_t k = i + 1; k < nargs; k++) release(args[k]);
                BIND_FAIL("Function '%s' got a plain argument after a named one; "
                          "put the named ones (name = value) last", name);
            }
            if (at >= np) {
                for (size_t k = i; k < nargs; k++) release(args[k]);
                BIND_FAIL("Function '%s' takes %zu argument(s) but got %zu",
                          name, np, nargs);
            }
            slot[at] = args[i]; got[at] = true; at++;
            continue;
        }
        saw_named = true;
        size_t at_name = np;
        for (size_t k = 0; k < np; k++)
            if (strcmp(fn->node->params[k], an) == 0) { at_name = k; break; }
        if (at_name == np) {
            for (size_t k = i; k < nargs; k++) release(args[k]);
            BIND_FAIL("Function '%s' has no parameter named '%s'", name, an);
        }
        if (got[at_name]) {
            for (size_t k = i; k < nargs; k++) release(args[k]);
            BIND_FAIL("Function '%s' got two values for '%s'", name, an);
        }
        slot[at_name] = args[i]; got[at_name] = true;
    }

    for (size_t i = 0; i < np; i++) {
        if (got[i]) continue;
        if (!fn->node->defaults || !fn->node->defaults[i])
            BIND_FAIL("Function '%s' needs a value for '%s'", name, fn->node->params[i]);
        slot[i] = eval(in, fn->node->defaults[i], fn->closure);
        got[i] = true;
    }
    #undef BIND_FAIL
    /* 부르는 쪽 겹에 '지금 이 줄에서 불렀다'를 적어 두고, 새 겹을 하나 쌓습니다. */
    in->frames[in->depth].line = line;
    if (++in->depth > MAX_DEPTH) {
        /* 깊이를 0 으로 되돌리지 않습니다.  되돌리면 이 오류를 catch 로 잡았을 때
         * 인터프리터가 '지금 맨 바깥' 이라고 잘못 알아, 이미 쌓여 있는 C 스택 위로
         * 다시 MAX_DEPTH 만큼 파고들어 진짜로 스택이 터졌습니다.
         * 깊이는 try 가 원래대로 돌려놓습니다 (exec_try_stmt 의 saved_depth). */
        in->depth--;
        lumi_error(line, "too much recursion (a function keeps calling itself)");
    }
    in->frames[in->depth].fn_name = fn->node->name;
    in->frames[in->depth].file    = fn->file ? fn->file : in->cur_file;
    in->frames[in->depth].line    = line;

    Env *local = env_new(fn->closure);
    SCOPE_ENTER(in, local);
    if (this_) {
        env_declare(local, "this", retain(obj_val(this_)), NULL);
        SuperRef *sr = super_new(owner && owner->parent ? owner->parent : NULL, this_);
        env_declare(local, "super", obj_val(sr), NULL);
    }
    for (size_t i = 0; i < np; i++) {
        /* 'func f(int a)' 처럼 자료형을 적어 두었으면 여기서 봅니다.
         * 규칙은 'int a = ...' 선언과 똑같습니다 (같은 coerce_to_type). */
        const char *pt = fn->node->ptypes ? fn->node->ptypes[i] : NULL;
        if (pt) {
            Value fixed = coerce_to_type(in, pt, slot[i], line);
            release(slot[i]);
            slot[i] = fixed;
        }
        env_declare(local, fn->node->params[i], slot[i], NULL);   /* 소유권 이동 */
    }
    free(slot); free(got);

    Value ret = NONE_VAL;
    Flow flow = run_block(in, &fn->node->body, local, &ret);
    SCOPE_LEAVE(in);
    env_release(local);
    in->depth--;
    if (fn->node->ret_type) {
        Value fixed = coerce_to_type(in, fn->node->ret_type, ret, line);
        release(ret);
        ret = fixed;
    }

    if (flow == FLOW_BREAK) lumi_error(line, "'break' can only be used inside a loop");
    if (flow == FLOW_CONTINUE) lumi_error(line, "'continue' can only be used inside a loop");
    return ret;
}

/* 하나씩 꺼내 볼 수 있는 값을 리스트로 펼칩니다.
 * for-in 과 컴프리헨션이 똑같이 이 함수를 씁니다 (그래서 셋의 규칙이 항상 같습니다).
 *
 * 클래스로 만든 것은 두 가지 방법 중 하나로 꺼냅니다:
 *   iter() 또는 items()  — 한꺼번에 리스트/튜플로 돌려주기
 *   next()               — 하나씩 돌려주다가 더 없으면 none
 * 새 Seq 를 돌려줍니다 (다 쓰면 놓아 주세요).  iterable 은 건드리지 않습니다. */
static Seq *expand_iterable(Interp *in, Value iterable, int line)
{
    Seq *items = seq_new(V_LIST);
    switch (iterable.kind) {
    case V_STR: {
        Str *s = AS_STR(iterable);
        for (size_t i = 0; i < s->len; i++) seq_push(items, obj_val(str_new(s->cp + i, 1)));
        return items;
    }
    case V_LIST: case V_TUPLE: {
        Seq *s = AS_SEQ(iterable);
        for (size_t i = 0; i < s->len; i++) seq_push(items, retain(s->items[i]));
        return items;
    }
    case V_BYTES: {
        BytesObj *b = AS_BYTES(iterable);
        for (size_t i = 0; i < b->len; i++) seq_push(items, int_val(b->data[i]));
        return items;
    }
    case V_DICT: {
        Dict *d = AS_DICT(iterable);
        for (size_t i = 0; i < d->len; i++) seq_push(items, retain(d->e[i].key));
        return items;
    }
    case V_INSTANCE: {
        LumiClass *owner = NULL;
        Instance *inst = AS_INST(iterable);
        Func *m = class_find_method(inst->cls, "iter", &owner);
        const char *used = "iter";
        if (!m) { m = class_find_method(inst->cls, "items", &owner); used = "items"; }
        if (m) {
            Value res = invoke_function(in, m, NULL, 0, used, line, inst, owner);
            if (res.kind != V_LIST && res.kind != V_TUPLE) {
                char *got = R(in, res);
                release(res);
                release(obj_val(items));
                lumi_error_kind(line, "TypeError",
                                "%s() of class '%s' must give back a list or a tuple, but gave %s",
                                used, inst->cls->name, got);
            }
            Seq *s = AS_SEQ(res);
            for (size_t i = 0; i < s->len; i++) seq_push(items, retain(s->items[i]));
            release(res);
            return items;
        }
        Func *next_m = class_find_method(inst->cls, "next", &owner);
        if (next_m) {
            for (;;) {
                Value res = invoke_function(in, next_m, NULL, 0, "next", line, inst, owner);
                if (res.kind == V_NONE) { release(res); break; }
                seq_push(items, res);
            }
            return items;
        }
        release(obj_val(items));
        lumi_error_kind(line, "TypeError",
                        "class '%s' cannot be looped over; give it an iter() method that "
                        "gives back a list, or a next() method that gives none when it ends",
                        inst->cls->name);
        return NULL;
    }
    default: {
        char *s = S(in, iterable);
        release(obj_val(items));
        lumi_error_kind(line, "TypeError",
                        "Cannot loop over '%s' (only lists, tuples, strings, bytes, "
                        "dictionaries, and classes with iter() or next())", s);
        return NULL;
    }
    }
}

/* 부르는 자리의 인자들을 값으로 만듭니다.
 * '이름 = 값' 으로 적은 것은 이름을 따로 모아 두고 값만 넘깁니다.
 * 이름을 하나도 안 붙였으면 out_names 는 NULL 로 둡니다 (흔한 경우라 할 일을 줄입니다). */
static void eval_call_args(Interp *in, NodeList *arglist, Env *env,
                           Value **out_args, char ***out_names)
{
    size_t na = arglist->len;
    *out_args  = na ? (Value *)xmalloc(na * sizeof(Value)) : NULL;
    *out_names = NULL;
    for (size_t i = 0; i < na; i++) {
        Node *a = arglist->items[i];
        if (a->kind == N_KWARG) {
            if (!*out_names) {
                *out_names = (char **)xmalloc(na * sizeof(char *));
                for (size_t k = 0; k < na; k++) (*out_names)[k] = NULL;
            }
            (*out_names)[i] = a->v.kwarg.name;
            (*out_args)[i]  = eval(in, a->v.kwarg.value, env);
        } else {
            (*out_args)[i] = eval(in, a, env);
        }
    }
}

/* 인자 묶기에 실패했을 때 뒷정리 (BIND_FAIL 이 씁니다) */
static void bind_undo(Value *slot, bool *got, size_t np)
{
    for (size_t k = 0; k < np; k++) if (got[k]) release(slot[k]);
    free(slot);
    free(got);
}

/* 이름 붙인 인자 없이 부르기 (특수 메서드 등 안에서 부르는 자리들이 씁니다) */
static Value invoke_function(Interp *in, Func *fn, Value *args, size_t nargs,
                             const char *name, int line,
                             Instance *this_, LumiClass *owner)
{
    return invoke_function_kw(in, fn, args, nargs, NULL, name, line, this_, owner);
}

static Value instantiate_kw(Interp *in, LumiClass *cls, Value *args, size_t nargs,
                            char **arg_names, int line)
{
    /* Check for unimplemented abstract methods in cls or its parent hierarchy */
    for (LumiClass *c = cls; c; c = c->parent) {
        for (size_t i = 0; i < c->methods->len; i++) {
            const char *mname = c->methods->slots[i].name;
            LumiClass *owner = NULL;
            Func *m = class_find_method(cls, mname, &owner);
            if (m && m->node->is_abstract) {
                for (size_t k = 0; k < nargs; k++) release(args[k]);
                lumi_error(line, "cannot make an object of class '%s' because abstract "
                                 "method '%s' from class '%s' was not implemented",
                           cls->name, mname, owner->name);
            }
        }
    }

    Instance *inst = instance_new(cls);

    LumiClass *owner = NULL;
    Func *init = class_find_method(cls, "init", &owner);
    if (init) {
        char *name = xsprintf("%s.init", cls->name);
        Value r = invoke_function_kw(in, init, args, nargs, arg_names, name, line, inst, owner);
        free(name);
        release(r);
    } else if (nargs) {
        for (size_t i = 0; i < nargs; i++) release(args[i]);
        release(obj_val(inst));
        lumi_error(line, "class '%s' has no 'init' method, so '%s(...)' takes no "
                         "values, but got %zu", cls->name, cls->name, nargs);
    }
    return obj_val(inst);
}


Value call_value_kw(Interp *in, Value callee, Value *args, size_t nargs,
                    char **arg_names, const char *name, int line)
{
    if (callee.kind == V_BOUND) {
        Bound *b = AS_BOUND(callee);
        if (!b->inst)
            lumi_error(line, "'%s' is a method, so it needs an object: make one first "
                             "with the class, then call it on that object", name);
        return invoke_function_kw(in, b->func, args, nargs, arg_names, name, line,
                                  b->inst, b->owner);
    }
    if (callee.kind == V_CLASS)
        return instantiate_kw(in, AS_CLASS(callee), args, nargs, arg_names, line);
    if (callee.kind == V_FUNC)
        return invoke_function_kw(in, AS_FUNC(callee), args, nargs, arg_names, name, line,
                                  NULL, NULL);
    for (size_t i = 0; i < nargs; i++) release(args[i]);
    lumi_error(line, "'%s' is not a function", name);
    return NONE_VAL;
}

Value call_value(Interp *in, Value callee, Value *args, size_t nargs,
                        const char *name, int line)
{
    return call_value_kw(in, callee, args, nargs, NULL, name, line);
}

/* 기능 값 하나 만들기 (이름 붙은 기능 / 메서드 / 이름 없는 기능 모두 같은 모양) */
Value make_func_value(Interp *in, FuncDefNode *fd, Env *env)
{
    Func *f = func_new(fd, env, in ? in->cur_file : NULL);
    return obj_val(f);
}

void define_class(Interp *in, ClassDefNode *cd, Env *env, int line)
{
    LumiClass *parent = NULL;
    if (cd->parent_name) {
        Value pv = env_get(env, cd->parent_name, line);
        if (pv.kind != V_CLASS) {
            release(pv);
            lumi_error(line, "'%s' is not a class, so '%s' cannot inherit from it",
                       cd->parent_name, cd->name);
        }
        parent = AS_CLASS(pv);              /* 참조를 그대로 가져갑니다 */
    }

    Env *methods = env_new(NULL);
    Env *vars = env_new(NULL);
    LumiClass *cls = class_new(cd->name, parent, methods, vars, env);
    env_release(methods);
    env_release(vars);

    for (size_t i = 0; i < cd->nmethods; i++) {
        FuncDefNode *fd = cd->methods[i]->v.funcdef;
        env_declare(cls->methods, fd->name, make_func_value(in, fd, env), NULL);
    }

    /* 공유 값은 작은 환경에서 한 번 만들어 클래스로 옮깁니다 */
    Env *field_env = env_new(env);
    SCOPE_ENTER(in, field_env);
    Value ret = NONE_VAL;
    for (size_t i = 0; i < cd->fields.len; i++)
        exec_node(in, cd->fields.items[i], field_env, &ret);
    for (size_t i = 0; i < field_env->len; i++)
        env_declare(cls->vars, field_env->slots[i].name,
                    retain(field_env->slots[i].val), field_env->slots[i].type);
    SCOPE_LEAVE(in);
    env_release(field_env);

    env_declare(env, cd->name, obj_val(cls), NULL);
}

/* ============================================================
 * 12. 라이브러리 (bring)
 * ============================================================ */

static char *exe_dir(void)
{
    static char *dir = NULL;
    if (!dir) {
        dir = plat_exe_dir();
        if (!dir) dir = xstrdup(".");
    }
    return dir;
}

static bool file_exists(const char *path)
{
    /* lumi build 로 묶어 놓은 프로그램 안에 있으면 진짜 파일이 없어도 있는 셈입니다 */
    if (pack_find(path)) return true;
    return plat_file_exists(path);
}

char *read_text_file(const char *path)      /* main.c 도 씁니다 */
{
    const char *packed = pack_find(path);
    if (packed) return xstrdup(packed);     /* 묶음 안의 것을 먼저 봅니다 */
    FILE *f = plat_fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)xmalloc((size_t)n + 1);
    size_t got = fread(buf, 1, (size_t)n, f);
    buf[got] = 0;
    fclose(f);
    /* UTF-8 BOM 은 건너뜁니다 */
    if (got >= 3 && (unsigned char)buf[0] == 0xEF && (unsigned char)buf[1] == 0xBB
        && (unsigned char)buf[2] == 0xBF)
        memmove(buf, buf + 3, got - 2);
    return buf;
}

/* bring "폴더/파일" 의 경로를 실제 파일 자리로 폅니다.
 * 규칙은 read/write 같은 파일 함수(file_path_of)와 똑같습니다:
 *   C:\... 나 /... 로 시작하면 그대로, 아니면 지금 파일이 있는 폴더(base_dir) 기준.
 * 확장자를 안 적었으면 .lumi 를 붙여 봅니다. */
static char *resolve_path_module(Interp *in, const char *path, int line)
{
    for (const char *p = path; *p; p++) {
        if ((unsigned char)*p < 0x20)
            lumi_error(line, "bring got a path with an invisible letter in it; a \\ was read "
                             "as an escape, so write \\\\ or / instead of \\, or write the "
                             "path as '...' text");
    }

    bool whole = path[0] == '\\' || path[0] == '/'
              || (path[0] && path[1] == ':' && (path[2] == '\\' || path[2] == '/'));
    char *base = (whole || !in->base_dir)
        ? xstrdup(path)
        : xsprintf("%s%s%s", in->base_dir, plat_dir_sep(), path);

    if (file_exists(base)) return base;

    char *dotted = xsprintf("%s.lumi", base);
    free(base);
    if (file_exists(dotted)) return dotted;

    lumi_error(line, "bring cannot find '%s'; looked for '%s' too. Paths are counted from the "
                     "folder of the file doing the bring", path, dotted);
    free(dotted);
    return NULL;
}

/* 경로에서 폴더와 확장자를 뗀 이름 ("models/user.lumi" -> "user").
 * bring "폴더/파일" 을 이름 없이 적었을 때 담길 이름입니다. */
static char *module_name_of_path(const char *path)
{
    const char *start = path;
    for (const char *p = path; *p; p++)
        if (plat_is_sep(*p)) start = p + 1;

    size_t len = strlen(start);
    if (len > 5 && strcmp(start + len - 5, ".lumi") == 0) len -= 5;
    char *out = (char *)xmalloc(len + 1);
    memcpy(out, start, len);
    out[len] = 0;
    return out;
}

static char *resolve_library(Interp *in, const char *name, int line)
{
    /* 찾는 곳:
     * 1) 실행 중인 파일 폴더 (base_dir) 의 name.lumi
     * 2) base_dir\lumi_packages\ 의 패키지 (lumi.json main / main.lumi / index.lumi / name.lumi)
     * 3) 전역 패키지 폴더 (~/.lumi/packages) 의 패키지
     * 4) lumi.exe 옆(및 상위) libraries 폴더
     */
    if (in->base_dir) {
        char *f1 = xsprintf("%s%s%s.lumi", in->base_dir, plat_dir_sep(), name);
        if (file_exists(f1)) return f1;
        free(f1);

        char *lumi_pkgs = xsprintf("%s%slumi_packages", in->base_dir, plat_dir_sep());
        char *pkg_entry = get_package_entry(lumi_pkgs, name);
        free(lumi_pkgs);
        if (pkg_entry) return pkg_entry;

        char *f2 = xsprintf("%s%slumi_packages%s%s.lumi", in->base_dir, plat_dir_sep(), plat_dir_sep(), name);
        if (file_exists(f2)) return f2;
        free(f2);
    }

    /* 전역 패키지 (~/.lumi/packages) */
    const char *userprofile = getenv("USERPROFILE");
    if (!userprofile) userprofile = getenv("HOME");
    if (userprofile) {
        char *global_pkgs = xsprintf("%s%s.lumi%spackages", userprofile, plat_dir_sep(), plat_dir_sep());
        char *global_entry = get_package_entry(global_pkgs, name);
        free(global_pkgs);
        if (global_entry) return global_entry;

        char *f3 = xsprintf("%s%s.lumi%spackages%s%s.lumi", userprofile, plat_dir_sep(), plat_dir_sep(), plat_dir_sep(), name);
        if (file_exists(f3)) return f3;
        free(f3);
    }

    /* 상위 libraries/ 디렉터리 탐색 */
    for (int up = 0; up <= 3; up++) {
        Buf b = {0};
        buf_str(&b, exe_dir());
        for (int k = 0; k < up; k++) {
            buf_str(&b, plat_dir_sep());
            buf_str(&b, "..");
        }
        buf_str(&b, plat_dir_sep());
        buf_str(&b, "libraries");
        char *lib_dir = buf_done(&b);

        char *pkg_entry = get_package_entry(lib_dir, name);
        if (pkg_entry) { free(lib_dir); return pkg_entry; }

        char *f4 = xsprintf("%s%s%s.lumi", lib_dir, plat_dir_sep(), name);
        free(lib_dir);
        if (file_exists(f4)) return f4;
        free(f4);
    }

    lumi_error(line, "Library '%s' not found (looked in base dir, lumi_packages, ~/.lumi/packages, and libraries)", name);
    return NULL;
}

static Env *load_module(Interp *in, const char *path)
{
    char *full = plat_fullpath(path);
    for (size_t i = 0; i < in->nmodules; i++)
        if (plat_path_equal(in->modules[i].path, full)) {
            free(full);
            return in->modules[i].env;
        }

    char *code = read_text_file(path);
    if (!code) { free(full); lumi_error(NO_LINE, "Cannot read library file: %s", path); }

    TokenList *tokens = tokenize(code);
    NodeList program = parse_program(tokens);
    tokenlist_free(tokens);
    free(code);

    Env *module_env = env_new(NULL);
    if (in->nmodules == in->cmodules) {
        in->cmodules = in->cmodules ? in->cmodules * 2 : 8;
        in->modules = xrealloc(in->modules, in->cmodules * sizeof(*in->modules));
    }
    in->modules[in->nmodules].path = xstrdup(full);
    in->modules[in->nmodules].env = module_env;
    in->nmodules++;

    char *old_base = in->base_dir;
    char *dir = xstrdup(full);
    for (size_t i = strlen(dir); i > 0; i--) {
        if (plat_is_sep(dir[i - 1])) { dir[i - 1] = 0; break; }
    }
    in->base_dir = dir;

    /* 라이브러리를 읽는 동안은 '지금 파일'이 그 라이브러리입니다.
     * 여기서 만들어지는 func 들이 이 이름을 기억해 뒀다가 트레이스에 씁니다. */
    char *old_file = in->cur_file;
    CallFrame old_frame = in->frames[in->depth];
    in->cur_file = (char *)intern_file(in, full);
    in->frames[in->depth].fn_name = NULL;
    in->frames[in->depth].file    = in->cur_file;
    in->frames[in->depth].line    = 0;

    /* 라이브러리에서는 정의(func/class/val)와 bring 만 가져옵니다.
     * bring 이 있어야 라이브러리가 또 다른 라이브러리를 쓸 수 있습니다
     * (services/auth.lumi 안에서 bring "../models/user" 처럼). */
    Value ret = NONE_VAL;
    for (size_t i = 0; i < program.len; i++) {
        Node *st = program.items[i];
        if (st->kind == N_FUNCDEF || st->kind == N_CLASSDEF || st->kind == N_BRING
            || (st->kind == N_ASSIGN && st->v.assign.is_let))
            exec_node(in, st, module_env, &ret);
    }

    in->cur_file = old_file;
    in->frames[in->depth] = old_frame;
    free(in->base_dir);
    in->base_dir = old_base;
    free(full);
    return module_env;
}

/* ============================================================
 * 13. 이항 연산
 * ============================================================ */

static bool try_instance_binop(Interp *in, const char *op, Value l, Value r, int line, Value *out)
{
    if (l.kind != V_INSTANCE) return false;
    const char *mname = NULL;
    if (strcmp(op, "+") == 0) mname = "add";
    else if (strcmp(op, "-") == 0) mname = "sub";
    else if (strcmp(op, "*") == 0) mname = "mul";
    else if (strcmp(op, "/") == 0) mname = "div";
    else if (strcmp(op, "%") == 0) mname = "mod";
    else if (strcmp(op, "<") == 0) mname = "lt";
    else if (strcmp(op, ">") == 0) mname = "gt";
    else if (strcmp(op, "==") == 0) mname = "eq";
    else if (strcmp(op, "!=") == 0) mname = "ne";
    else if (strcmp(op, "<=") == 0) mname = "le";
    else if (strcmp(op, ">=") == 0) mname = "ge";
    if (!mname) return false;

    LumiClass *owner = NULL;
    Func *m = class_find_method(AS_INST(l)->cls, mname, &owner);
    if (!m || m->node->nparams != 1) return false;

    char *fname = xsprintf("%s.%s", AS_INST(l)->cls->name, mname);
    Value arg = retain(r);
    Value res = invoke_function(in, m, &arg, 1, fname, line, AS_INST(l), owner);
    free(fname);
    if (strcmp(op, "<") == 0 || strcmp(op, ">") == 0 || strcmp(op, "==") == 0
        || strcmp(op, "!=") == 0 || strcmp(op, "<=") == 0 || strcmp(op, ">=") == 0) {
        bool b = value_truthy(res);
        release(res);
        *out = bool_val(b);
    } else {
        *out = res;
    }
    return true;
}

static Value eval_binop(Interp *in, Node *n, Env *env)
{
    const char *op = n->v.binop.op;
    int line = n->line;

    if (strcmp(op, "and") == 0) {
        Value l = eval(in, n->v.binop.left, env);
        if (!value_truthy(l)) return l;
        release(l);
        return eval(in, n->v.binop.right, env);
    }
    if (strcmp(op, "or") == 0) {
        Value l = eval(in, n->v.binop.left, env);
        if (value_truthy(l)) return l;
        release(l);
        return eval(in, n->v.binop.right, env);
    }
    /* none 일 때만 오른쪽 — false 나 0 은 그대로 씁니다 (or 와 다른 점) */
    if (strcmp(op, "??") == 0) {
        Value l = eval(in, n->v.binop.left, env);
        if (l.kind != V_NONE) return l;
        release(l);
        return eval(in, n->v.binop.right, env);
    }

    Value l = eval(in, n->v.binop.left, env);
    Value r = eval(in, n->v.binop.right, env);
    Value out = NONE_VAL;

    if (try_instance_binop(in, op, l, r, line, &out)) {
        release(l);
        release(r);
        return out;
    }

    if (strcmp(op, "+") == 0) {
        if (l.kind == V_LIST && r.kind == V_LIST) {
            Seq *o = seq_copy(AS_SEQ(l), V_LIST);
            for (size_t i = 0; i < AS_SEQ(r)->len; i++)
                seq_push(o, retain(AS_SEQ(r)->items[i]));
            out = obj_val(o);
        } else if (l.kind == V_TUPLE && r.kind == V_TUPLE) {
            Seq *o = seq_copy(AS_SEQ(l), V_TUPLE);
            for (size_t i = 0; i < AS_SEQ(r)->len; i++)
                seq_push(o, retain(AS_SEQ(r)->items[i]));
            out = obj_val(o);
        } else if (l.kind == V_BYTES && r.kind == V_BYTES) {
            BytesObj *a = AS_BYTES(l), *b = AS_BYTES(r);
            uint8_t *buf = (uint8_t *)xmalloc(a->len + b->len + 1);
            memcpy(buf, a->data, a->len);
            memcpy(buf + a->len, b->data, b->len);
            out = obj_val(bytes_new(buf, a->len + b->len));
            free(buf);
        } else if (l.kind == V_BYTES || r.kind == V_BYTES) {
            release(l); release(r);
            lumi_error(line, "'+' can join bytes only with other bytes; turn the text "
                             "into bytes first with encode(...), or turn the bytes into "
                             "text with decode(...)");
        } else if (l.kind == V_STR || r.kind == V_STR) {
            Str *a = value_to_str(in, l), *b = value_to_str(in, r);
            out = obj_val(str_concat(a, b));
            release(obj_val(a));
            release(obj_val(b));
        } else {
            require_number(in, l, line);
            require_number(in, r, line);
            if (l.kind == V_INT && r.kind == V_INT)
                out = int_val(add_ll(l.as.i, r.as.i, line));
            else
                out = float_val(num_as_double(l) + num_as_double(r));
        }
    } else if (strcmp(op, "*") == 0) {
        bool ls = (l.kind == V_STR || l.kind == V_LIST || l.kind == V_TUPLE || l.kind == V_BYTES);
        bool rs = (r.kind == V_STR || r.kind == V_LIST || r.kind == V_TUPLE || r.kind == V_BYTES);
        if (ls && !rs) out = repeat_sequence(in, l, r, line);
        else if (rs && !ls) out = repeat_sequence(in, r, l, line);
        else {
            require_number(in, l, line);
            require_number(in, r, line);
            if (l.kind == V_INT && r.kind == V_INT)
                out = int_val(mul_ll(l.as.i, r.as.i, line));
            else
                out = float_val(num_as_double(l) * num_as_double(r));
        }
    } else if (strcmp(op, "-") == 0) {
        require_number(in, l, line);
        require_number(in, r, line);
        if (l.kind == V_INT && r.kind == V_INT) out = int_val(sub_ll(l.as.i, r.as.i, line));
        else out = float_val(num_as_double(l) - num_as_double(r));
    } else if (strcmp(op, "/") == 0) {
        require_number(in, l, line);
        require_number(in, r, line);
        if (num_as_double(r) == 0) { release(l); release(r);
            lumi_error_kind(line, "MathError", "Cannot divide by zero"); }
        out = float_val(num_as_double(l) / num_as_double(r));
    } else if (strcmp(op, "%") == 0) {
        require_number(in, l, line);
        require_number(in, r, line);
        if (num_as_double(r) == 0) { release(l); release(r);
            lumi_error_kind(line, "MathError", "Cannot divide by zero"); }
        if (l.kind == V_INT && r.kind == V_INT) out = int_val(mod_ll(l.as.i, r.as.i));
        else out = float_val(mod_d(num_as_double(l), num_as_double(r)));
    } else if (strcmp(op, "**") == 0) {
        require_number(in, l, line);
        require_number(in, r, line);
        if (num_as_double(l) == 0 && num_as_double(r) < 0) {
            release(l); release(r);
            lumi_error(line, "0 cannot be raised to a negative power");
        }
        if (l.kind == V_INT && r.kind == V_INT && r.as.i >= 0) {
            long long base = l.as.i, e = r.as.i, acc = 1;
            while (e > 0) {
                if (e & 1) acc = mul_ll(acc, base, line);
                e >>= 1;
                if (e) base = mul_ll(base, base, line);
            }
            out = int_val(acc);
        } else {
            out = float_val(pow(num_as_double(l), num_as_double(r)));
        }
    } else if (strcmp(op, "&") == 0 || strcmp(op, "|") == 0 || strcmp(op, "^") == 0
               || strcmp(op, "<<") == 0 || strcmp(op, ">>") == 0) {
        char opname[8];
        snprintf(opname, sizeof opname, "'%s'", op);
        long long a = require_int(in, l, line, opname);
        long long b = require_int(in, r, line, opname);
        if (strcmp(op, "&") == 0) out = int_val(a & b);
        else if (strcmp(op, "|") == 0) out = int_val(a | b);
        else if (strcmp(op, "^") == 0) out = int_val(a ^ b);
        else {
            if (b < 0) { release(l); release(r);
                lumi_error(line, "'%s' shift amount cannot be negative", op); }
            if (strcmp(op, "<<") == 0) {
                if (b >= 63) { release(l); release(r); int_overflow(line); }
                long long shifted = a << b;
                if ((shifted >> b) != a) { release(l); release(r); int_overflow(line); }
                out = int_val(shifted);
            } else {
                out = int_val(b >= 63 ? (a < 0 ? -1 : 0) : (a >> b));
            }
        }
    } else if (strcmp(op, "==") == 0) {
        out = bool_val(values_equal(in, l, r, line));
    } else if (strcmp(op, "!=") == 0) {
        out = bool_val(!values_equal(in, l, r, line));
    } else if (strcmp(op, "<") == 0 || strcmp(op, ">") == 0
               || strcmp(op, "<=") == 0 || strcmp(op, ">=") == 0) {
        out = do_compare(in, op, l, r, line);
    } else if (strcmp(op, "in") == 0) {
        out = bool_val(membership(in, l, r, line));
    } else if (strcmp(op, "not in") == 0) {
        out = bool_val(!membership(in, l, r, line));
    } else {
        release(l); release(r);
        lumi_error(line, "Unknown operator: %s", op);
    }

    release(l);
    release(r);
    return out;
}

/* ============================================================
 * 14. forr
 * ============================================================ */

static Value eval_forr(Interp *in, Node *n, Env *env)
{
    Env *loop = env_new(env);
    SCOPE_ENTER(in, loop);
    Value ret = NONE_VAL;
    exec_node(in, n->v.forr.init, loop, &ret);
    Seq *out = seq_new(V_LIST);
    for (;;) {
        Value c = eval(in, n->v.forr.cond, loop);
        bool go = value_truthy(c);
        release(c);
        if (!go) break;
        Value v = env_get(loop, n->v.forr.var_name, n->line);
        if (v.kind == V_BOOL || !IS_NUM(v)) {
            char *s = S(in, v);
            lumi_error(n->line, "forr variable '%s' must be a whole number but got '%s'",
                       n->v.forr.var_name, s);
        }
        if (v.kind == V_FLOAT) {
            if (v.as.f != floor(v.as.f)) {
                char *s = S(in, v);
                lumi_error(n->line, "forr variable '%s' must be a whole number but got %s",
                           n->v.forr.var_name, s);
            }
            Value iv = int_val((long long)v.as.f);
            env_declare(loop, n->v.forr.var_name, retain(iv), NULL);
            release(v);
            v = iv;
        }
        seq_push(out, v);
        exec_node(in, n->v.forr.step, loop, &ret);
    }
    SCOPE_LEAVE(in);
    env_release(loop);
    return obj_val(out);
}

/* ============================================================
 * 15. 식 평가
 * ============================================================ */

static Value eval(Interp *in, Node *n, Env *env)
{
    switch (n->kind) {
    case N_NUMBER:
        return n->v.num.is_float ? float_val(n->v.num.f) : int_val(n->v.num.i);
    case N_STRING:
        return retain(obj_val(n->v.str.value));
    case N_BOOL:
        return bool_val(n->v.bool_.value);
    case N_NONE:
        return NONE_VAL;
    case N_VAR:
        return env_get(env, n->v.var.name, n->line);

    case N_THIS: {
        Value *t = env_lookup(env, "this", NULL);
        if (!t)
            lumi_error(n->line, "'this' can only be used inside a class method "
                                "(it means: the object this method belongs to)");
        return retain(*t);
    }
    case N_SUPER: {
        Value *s = env_lookup(env, "super", NULL);
        if (!s)
            lumi_error(n->line, "'super' can only be used inside a method of a class "
                                "that was written with 'from' (for example: "
                                "class Dog from Animal)");
        return retain(*s);
    }

    case N_MEMBER: {
        Value obj = eval(in, n->v.member.obj, env);
        if (n->v.member.optional && obj.kind == V_NONE) return NONE_VAL;
        Value r = member_of(in, obj, n->v.member.name, env, n->line);
        release(obj);
        return r;
    }
    /* 값을 부르기 — 앞의 식을 값으로 만든 뒤 그 값을 부릅니다.
     * 오류 메시지에 쓸 이름은, 부를 것이 변수면 그 변수 이름을 씁니다. */
    case N_CALLV: {
        Value callee = eval(in, n->v.callv.callee, env);
        const char *display = n->v.callv.callee->kind == N_VAR
            ? n->v.callv.callee->v.var.name : "this value";
        size_t na = n->v.callv.args.len;
        Value *args = NULL; char **anames = NULL;
        eval_call_args(in, &n->v.callv.args, env, &args, &anames);
        Value r = call_value_kw(in, callee, args, na, anames, display, n->line);
        free(args); free(anames);
        release(callee);
        return r;
    }

    case N_METHODCALL: {
        Value obj = eval(in, n->v.methodcall.obj, env);
        /* a?.b() — a 가 none 이면 부르지 않습니다 (인자도 셈하지 않습니다) */
        if (n->v.methodcall.optional && obj.kind == V_NONE) return NONE_VAL;
        Value fn = member_of(in, obj, n->v.methodcall.name, env, n->line);
        char *display = obj.kind == V_MODULE
            ? xsprintf("%s.%s", AS_MOD(obj)->name, n->v.methodcall.name)
            : xstrdup(n->v.methodcall.name);
        size_t na = n->v.methodcall.args.len;
        Value *args = NULL; char **anames = NULL;
        eval_call_args(in, &n->v.methodcall.args, env, &args, &anames);
        Value r = call_value_kw(in, fn, args, na, anames, display, n->line);
        free(args); free(anames);
        free(display);
        release(fn);
        release(obj);
        return r;
    }

    /* 이름 붙인 인자는 함수를 부를 때만 뜻이 있습니다.  홀로 만나면 알려 줍니다. */
    case N_KWARG:
        lumi_error(n->line, "'%s = ...' can only be used when calling a function you wrote; "
                            "built-in functions take plain values in order",
                   n->v.kwarg.name);
        return NONE_VAL;

    case N_LIST: {
        Seq *s = seq_new(V_LIST);
        for (size_t i = 0; i < n->v.list.elements.len; i++)
            seq_push(s, eval(in, n->v.list.elements.items[i], env));
        return obj_val(s);
    }
    case N_TUPLE: {
        Seq *s = seq_new(V_TUPLE);
        for (size_t i = 0; i < n->v.list.elements.len; i++)
            seq_push(s, eval(in, n->v.list.elements.items[i], env));
        return obj_val(s);
    }
    case N_DICT: {
        Dict *d = dict_new();
        for (size_t i = 0; i < n->v.dict.npairs; i++) {
            Value k = eval(in, n->v.dict.pairs[i].key, env);
            check_dict_key(in, k, n->line);
            Value val = eval(in, n->v.dict.pairs[i].val, env);
            dict_set(d, k, val);
        }
        return obj_val(d);
    }

    case N_INDEX: {
        Value t = eval(in, n->v.index.target, env);
        Value i = eval(in, n->v.index.index, env);
        Value r = do_index(in, t, i, n->line);
        release(t); release(i);
        return r;
    }
    case N_SLICE: {
        Value t = eval(in, n->v.slice.target, env);
        Value s = NONE_VAL, e = NONE_VAL;
        bool hs = n->v.slice.start != NULL, he = n->v.slice.end != NULL;
        if (hs) s = eval(in, n->v.slice.start, env);
        if (he) e = eval(in, n->v.slice.end, env);
        Value r = do_slice(in, t, hs ? &s : NULL, he ? &e : NULL, n->line);
        release(t);
        if (hs) release(s);
        if (he) release(e);
        return r;
    }
    case N_RANGESPEC:
        lumi_error(n->line, "A range like 0:3 can only be written inside upper(...), "
                            "lower(...), strip(...), or erase(...) to say which part to "
                            "work on, for example upper(0:3, \"hello\"); to cut a piece "
                            "out of a sequence use text[0:3] instead");
        return NONE_VAL;

    case N_FORR: return eval_forr(in, n, env);

    case N_FSTRING: {
        size_t total_len = 0;
        char **chunks = (char **)xmalloc(n->v.fstring.parts.len * sizeof(char *));
        for (size_t i = 0; i < n->v.fstring.parts.len; i++) {
            Value val = eval(in, n->v.fstring.parts.items[i], env);
            chunks[i] = value_to_utf8(in, val);
            release(val);
            total_len += strlen(chunks[i]);
        }
        char *buf = (char *)xmalloc(total_len + 1);
        size_t pos = 0;
        for (size_t i = 0; i < n->v.fstring.parts.len; i++) {
            size_t clen = strlen(chunks[i]);
            memcpy(buf + pos, chunks[i], clen);
            pos += clen;
            free(chunks[i]);
        }
        buf[pos] = 0;
        free(chunks);
        Value out = str_value(buf);
        free(buf);
        return out;
    }
    case N_LIST_COMP: {
        Value iterable = eval(in, n->v.list_comp.iterable, env);
        Seq *items = expand_iterable(in, iterable, n->line);
        release(iterable);

        Seq *out_seq = seq_new(V_LIST);
        for (size_t i = 0; i < items->len; i++) {
            Env *loop = env_new(env);
            SCOPE_ENTER(in, loop);
            env_declare(loop, n->v.list_comp.var_name, retain(items->items[i]), NULL);
            bool keep = true;
            if (n->v.list_comp.cond) {
                Value c = eval(in, n->v.list_comp.cond, loop);
                keep = value_truthy(c);
                release(c);
            }
            if (keep) {
                Value res = eval(in, n->v.list_comp.expr, loop);
                seq_push(out_seq, res);
            }
            SCOPE_LEAVE(in);
            env_release(loop);
        }
        release(obj_val(items));
        return obj_val(out_seq);
    }
    case N_DICT_COMP: {
        Value iterable = eval(in, n->v.dict_comp.iterable, env);
        Seq *items = expand_iterable(in, iterable, n->line);
        release(iterable);

        Dict *out_dict = dict_new();
        for (size_t i = 0; i < items->len; i++) {
            Env *loop = env_new(env);
            SCOPE_ENTER(in, loop);
            env_declare(loop, n->v.dict_comp.var_name, retain(items->items[i]), NULL);
            bool keep = true;
            if (n->v.dict_comp.cond) {
                Value c = eval(in, n->v.dict_comp.cond, loop);
                keep = value_truthy(c);
                release(c);
            }
            if (keep) {
                Value k = eval(in, n->v.dict_comp.key_expr, loop);
                Value v = eval(in, n->v.dict_comp.val_expr, loop);
                dict_set(out_dict, k, v);
            }
            SCOPE_LEAVE(in);
            env_release(loop);
        }
        release(obj_val(items));
        return obj_val(out_dict);
    }
    case N_UNARY: {
        Value v = eval(in, n->v.unary.operand, env);
        Value out = NONE_VAL;
        if (strcmp(n->v.unary.op, "-") == 0) {
            require_number(in, v, n->line);
            out = v.kind == V_INT ? int_val(sub_ll(0, v.as.i, n->line))
                                  : float_val(-v.as.f);
        } else if (strcmp(n->v.unary.op, "not") == 0) {
            out = bool_val(!value_truthy(v));
        } else {
            out = int_val(~require_int(in, v, n->line, "'~'"));
        }
        release(v);
        return out;
    }

    case N_BINOP: return eval_binop(in, n, env);
    case N_CALL:  return eval_call(in, n, env);

    /* 이름 없는 기능:  func x: x * 2  — 값으로 그 자리에서 만들어집니다 */
    case N_FUNCDEF: return make_func_value(in, n->v.funcdef, env);

    default:
        lumi_error(n->line, "Cannot evaluate this expression");
        return NONE_VAL;
    }
}

/* ============================================================
 * 16. 문장 실행
 * ============================================================ */

static Flow run_block(Interp *in, NodeList *body, Env *env, Value *ret)
{
    for (size_t i = 0; i < body->len; i++) {
        Flow f = exec_node(in, body->items[i], env, ret);
        if (f != FLOW_NORMAL) return f;
        /* 문장 하나가 끝난 자리 — 임시 값이 다 정리된 안전한 때라, 순환 참조를
         * 치우기에 알맞습니다.  쌓인 게 없으면 셈 하나 보고 곧장 지나갑니다. */
        LUMI_GC_CHECK(in);
    }
    return FLOW_NORMAL;
}

/* 디버거가 걸어 두는 갈고리 (dap.c).  평소에는 NULL 입니다. */
LUMI_TLS DebugHook lumi_debug_hook = NULL;

Value lumi_eval_expr(Interp *in, Node *n, Env *env) { return eval(in, n, env); }

/* error("메시지") / error(Type, "메시지")
 * exec_node 에서 떼어 둔 갈래입니다 — 여기 있는 버퍼 2KB 가 exec_node 안에 있으면
 * 오류를 안 내는 문장까지 겹마다 그만큼을 쌓습니다. LUMI_NOINLINE 설명은 lumi.h 참고. */
static LUMI_NOINLINE void exec_error_stmt(Interp *in, Node *n, Env *env)
{
    g_current_interp = in;
    char *type_str = NULL;
    char *msg_str = NULL;

    if (n->v.error_.type_expr) {
        Value tv = eval(in, n->v.error_.type_expr, env);
        type_str = value_to_utf8(in, tv);
        release(tv);
    }
    if (n->v.error_.msg_expr) {
        Value mv = eval(in, n->v.error_.msg_expr, env);
        msg_str = value_to_utf8(in, mv);
        release(mv);
    }

    /* 종류는 오류 값에 따로 담깁니다 (메시지 안에 섞지 않습니다).
     * 그래야 catch 가 글자를 뒤지지 않고 종류로 골라 잡습니다. */
    char kind[64];
    snprintf(kind, sizeof(kind), "%s", type_str && *type_str ? type_str : "Error");
    char body[1900];
    snprintf(body, sizeof(body), "%s", msg_str && *msg_str ? msg_str : "error occurred");
    free(type_str);
    free(msg_str);
    lumi_error_kind(n->line, kind, "%s", body);
}

/* try / safe / catch / always
 * setjmp 로 '되돌아올 자리'를 만들어 두고 try 블록을 돌립니다.  그 안에서
 * lumi_error 가 나면 longjmp 로 여기(else 쪽)로 뛰어 옵니다.
 *   - 오류가 없었으면 safe 를 잇달아 돌리고,
 *   - 오류가 났으면 catch 를 앞에서부터 살펴 맞는 것 하나만 돌립니다.
 *   - always 는 어느 쪽이든 마지막에 한 번 돌립니다.
 * 이것도 exec_node 에서 떼어 둔 갈래입니다 (jmp_buf 와 버퍼 4KB).  longjmp 는
 * 그대로 동작합니다 — 뛰어올 때 이 함수는 아직 스택에 살아 있습니다. */
/* 오류가 나서 뛰어온 뒤에 하는 일 — 맞는 catch 를 찾아 돌립니다.
 * exec_try_stmt 에서 또 떼어 둔 까닭: 여기 있는 버퍼 2KB 가 try 문 자리에 붙어 있으면
 * try 를 품은 함수는 재귀 한 겹마다 그만큼을 더 씁니다 (오류가 안 나도 마찬가지입니다). */
static LUMI_NOINLINE Flow run_catches(Interp *in, Node *n, Env *env, Value *ret)
{
    Flow flow = FLOW_NORMAL;
    int saved_line = lumi_err_line;
    char saved_kind[64], saved_bare[1900];
    snprintf(saved_kind, sizeof(saved_kind), "%s", lumi_err_kind);
    snprintf(saved_bare, sizeof(saved_bare), "%s", lumi_err_bare);
    const char *saved_file = in->frames[in->depth <= LUMI_MAX_DEPTH
                                        ? in->depth : LUMI_MAX_DEPTH].file;

    bool caught = false;
    for (size_t i = 0; i < n->v.try_.ncatches; i++) {
        struct CatchClause *cc = &n->v.try_.catches[i];
        /* 종류를 적었으면 그 종류(또는 그 아래 갈래)일 때만 잡습니다.
         * 메시지 글자를 뒤지던 예전 방식과 달리 우연히 맞는 일이 없습니다. */
        if (cc->type_name != NULL && !error_kind_is(saved_kind, cc->type_name))
            continue;
        caught = true;
        Env *catch_env = env_new(env);
        SCOPE_ENTER(in, catch_env);
        if (cc->var_name != NULL) {
            ErrorObj *eo = error_new(saved_kind, saved_bare, saved_line, saved_file);
            env_declare(catch_env, cc->var_name, obj_val(eo), NULL);
        }
        flow = run_block(in, &cc->body, catch_env, ret);
        SCOPE_LEAVE(in);
        env_release(catch_env);
        break;
    }

    if (!caught) {
        /* 아무 catch 도 못 잡았으면 always 를 먼저 돌리고 바깥으로 올립니다. */
        if (n->v.try_.has_always) {
            run_block(in, &n->v.try_.always_body, env, ret);
        }
        lumi_error_kind(saved_line, saved_kind, "%s", saved_bare);
    }
    return flow;
}

static LUMI_NOINLINE Flow exec_try_stmt(Interp *in, Node *n, Env *env, Value *ret)
{
    g_current_interp = in;
    TryHandler handler;
    handler.prev = in->top_handler;
    in->top_handler = &handler;

    /* flow 는 setjmp 를 건너뛰어 살아남아야 하므로 volatile 입니다. */
    volatile Flow flow = FLOW_NORMAL;
    /* longjmp 로 뛰어오면 사이에 있던 함수들의 'in->depth--' 가 건너뛰어집니다.
     * 그래서 여기서 원래 깊이를 적어 두었다가 되돌립니다 — 안 그러면 오류를 한 번
     * 잡을 때마다 깊이가 부풀어, 멀쩡한 재귀가 'too much recursion' 을 만납니다. */
    const int saved_depth = in->depth;
    /* 환경 사슬도 같은 까닭으로 되돌립니다 — 뛰어넘어 온 SCOPE_LEAVE 들이
     * 실행되지 않아 사슬에 죽은 자리가 남아 있습니다. */
    ScopeGuard *const saved_scope = in->top_scope;

    if (setjmp(handler.jmp) == 0) {
        flow = run_block(in, &n->v.try_.try_body, env, ret);
        in->top_handler = handler.prev;

        if (flow == FLOW_NORMAL && n->v.try_.has_safe) {
            flow = run_block(in, &n->v.try_.safe_body, env, ret);
        }
    } else {
        in->top_handler = handler.prev;
        in->depth = saved_depth;
        in->top_scope = saved_scope;
        flow = run_catches(in, n, env, ret);
    }

    if (n->v.try_.has_always) {
        Flow always_flow = run_block(in, &n->v.try_.always_body, env, ret);
        if (always_flow != FLOW_NORMAL) {
            flow = always_flow;
        }
    }

    return flow;
}

Flow exec_node(Interp *in, Node *n, Env *env, Value *ret)
{
    if (lumi_debug_hook) lumi_debug_hook(in, n, env);
    switch (n->kind) {
    case N_MULTI:
        for (size_t i = 0; i < n->v.multi.body.len; i++) {
            Flow f = exec_node(in, n->v.multi.body.items[i], env, ret);
            if (f != FLOW_NORMAL) return f;
        }
        return FLOW_NORMAL;

    case N_ASSIGN: {
        Value value;
        if (!n->v.assign.value) value = default_for_type(n->v.assign.type_name);
        else value = eval(in, n->v.assign.value, env);

        if (n->v.assign.is_let) {
            if (n->v.assign.type_name) {
                Value coerced = coerce_to_type(in, n->v.assign.type_name, value, n->line);
                release(value);
                value = coerced;
            }
            if (n->v.assign.scope && strcmp(n->v.assign.scope, "global") == 0)
                env_declare(in->globals, n->v.assign.name, value, n->v.assign.type_name);
            else
                env_declare(env, n->v.assign.name, value, n->v.assign.type_name);
        } else {
            const char *declared = env_declared_type(env, n->v.assign.name);
            if (declared) {
                Value coerced = coerce_to_type(in, declared, value, n->line);
                release(value);
                value = coerced;
            }
            env_set_existing(env, n->v.assign.name, value, n->line);
        }
        return FLOW_NORMAL;
    }

    case N_MULTI_ASSIGN: {
        Value value = eval(in, n->v.multi_assign.value, env);
        if (value.kind != V_LIST && value.kind != V_TUPLE) {
            char *s = S(in, value);
            release(value);
            lumi_error(n->line, "Destructuring assignment needs a list or tuple but got '%s'", s);
        }
        Seq *s = AS_SEQ(value);
        if (s->len != n->v.multi_assign.nnames) {
            release(value);
            lumi_error(n->line, "Cannot destructure sequence of length %zu into %zu variable(s)",
                       s->len, n->v.multi_assign.nnames);
        }
        for (size_t i = 0; i < n->v.multi_assign.nnames; i++) {
            if (n->v.multi_assign.is_let)
                env_declare(env, n->v.multi_assign.names[i], retain(s->items[i]), NULL);
            else
                env_set_existing(env, n->v.multi_assign.names[i], retain(s->items[i]), n->line);
        }
        release(value);
        return FLOW_NORMAL;
    }

    case N_IF: {
        Value c = eval(in, n->v.if_.cond, env);
        bool go = value_truthy(c);
        release(c);
        if (go) return run_block(in, &n->v.if_.then_body, env, ret);
        if (n->v.if_.has_else) return run_block(in, &n->v.if_.else_body, env, ret);
        return FLOW_NORMAL;
    }

    case N_WHILE:
        for (;;) {
            Value c = eval(in, n->v.while_.cond, env);
            bool go = value_truthy(c);
            release(c);
            if (!go) break;
            Flow f = run_block(in, &n->v.while_.body, env, ret);
            if (f == FLOW_BREAK) break;
            if (f == FLOW_RETURN) return f;
        }
        return FLOW_NORMAL;

    case N_SWITCH: {
        Value subject = eval(in, n->v.switch_.subject, env);
        int matched_idx = -1;
        int default_idx = -1;

        for (size_t i = 0; i < n->v.switch_.ncases; i++) {
            struct SwitchCase *c = &n->v.switch_.cases[i];
            if (c->is_default) {
                if (default_idx < 0) default_idx = (int)i;
            } else if (matched_idx < 0) {
                for (size_t j = 0; j < c->values.len; j++) {
                    Value cv = eval(in, c->values.items[j], env);
                    bool same = values_equal(in, subject, cv, n->line);
                    release(cv);
                    if (same) {
                        matched_idx = (int)i;
                        break;
                    }
                }
            }
        }
        release(subject);

        int start_idx = (matched_idx >= 0) ? matched_idx : default_idx;
        if (start_idx >= 0) {
            for (size_t i = (size_t)start_idx; i < n->v.switch_.ncases; i++) {
                struct SwitchCase *c = &n->v.switch_.cases[i];
                Flow f = run_block(in, &c->body, env, ret);
                if (f == FLOW_BREAK) {
                    return FLOW_NORMAL;
                }
                if (f == FLOW_RETURN || f == FLOW_CONTINUE) {
                    return f;
                }
            }
        }
        return FLOW_NORMAL;
    }

    case N_FORC: {
        Env *loop = env_new(env);
        SCOPE_ENTER(in, loop);
        exec_node(in, n->v.forc.init, loop, ret);
        for (;;) {
            Value c = eval(in, n->v.forc.cond, loop);
            bool go = value_truthy(c);
            release(c);
            if (!go) break;
            Flow f = run_block(in, &n->v.forc.body, loop, ret);
            if (f == FLOW_BREAK) break;
            SCOPE_LEAVE(in);
            if (f == FLOW_RETURN) { env_release(loop); return f; }
            exec_node(in, n->v.forc.step, loop, ret);   /* continue 여도 증감은 합니다 */
        }
        env_release(loop);
        return FLOW_NORMAL;
    }

    case N_FORIN: {
        Value iterable = eval(in, n->v.forin.iterable, env);
        Seq *items = expand_iterable(in, iterable, n->line);
        release(iterable);

        Flow result = FLOW_NORMAL;
        for (size_t i = 0; i < items->len; i++) {
            Env *loop = env_new(env);
            SCOPE_ENTER(in, loop);
            env_declare(loop, n->v.forin.var_name, retain(items->items[i]), NULL);
            Flow f = run_block(in, &n->v.forin.body, loop, ret);
            SCOPE_LEAVE(in);
            env_release(loop);
            if (f == FLOW_BREAK) break;
            if (f == FLOW_RETURN) { result = f; break; }
        }
        release(obj_val(items));
        return result;
    }

    /* use f = open(...):  — 블록을 벗어나면 어떻게 나가든 닫습니다.
     * ponytail: 오류(lumi_error)는 맨 위까지 단번에 뛰어 넘어가므로 여기를
     *거치지 않습니다. 그때는 프로그램이 끝나면서 닫힙니다. */
    case N_USE: {
        Value v = eval(in, n->v.use.value, env);
        if (v.kind != V_FILE) {
            char *s = S(in, v);
            release(v);
            lumi_error(n->line, "'use' needs a file opened with open(...) but got '%s'; "
                                "write it like use %s = open(read, \"note.txt\"):",
                       s, n->v.use.var_name);
        }
        Env *inner = env_new(env);
        SCOPE_ENTER(in, inner);
        env_declare(inner, n->v.use.var_name, retain(v), NULL);
        Flow f = run_block(in, &n->v.use.body, inner, ret);
        SCOPE_LEAVE(in);
        env_release(inner);
        FileObj *fo = AS_FILE(v);
        if (fo->fp) { fclose(fo->fp); fo->fp = NULL; }
        release(v);
        return f;                 /* break/continue/return 은 그대로 밖으로 */
    }

    case N_FUNCDEF:
        env_declare(env, n->v.funcdef->name, make_func_value(in, n->v.funcdef, env), NULL);
        return FLOW_NORMAL;

    case N_CLASSDEF:
        define_class(in, n->v.classdef, env, n->line);
        return FLOW_NORMAL;

    case N_MEMBERASSIGN: {
        Value obj = eval(in, n->v.memberassign.obj, env);
        Value val = eval(in, n->v.memberassign.value, env);
        set_member(in, obj, n->v.memberassign.name, val, env, n->line);
        release(obj);
        release(val);
        return FLOW_NORMAL;
    }

    case N_RETURN:
        release(*ret);
        *ret = n->v.ret.value ? eval(in, n->v.ret.value, env) : NONE_VAL;
        return FLOW_RETURN;

    case N_BREAK:    return FLOW_BREAK;
    case N_CONTINUE: return FLOW_CONTINUE;

    case N_INDEXASSIGN: {
        Value target = eval(in, n->v.indexassign.target, env);
        Value index = eval(in, n->v.indexassign.index, env);
        Value value = eval(in, n->v.indexassign.value, env);
        set_index(in, target, index, value, n->line);
        release(target); release(index); release(value);
        return FLOW_NORMAL;
    }

    case N_BRING: {
        char *path = n->v.bring.is_path
            ? resolve_path_module(in, n->v.bring.lib, n->line)
            : resolve_library(in, n->v.bring.lib, n->line);
        Env *module_env = load_module(in, path);

        /* 담을 이름: 'bring 이름 =' 을 적었으면 그것, 경로면 파일 이름, 아니면 라이브러리 이름 */
        char *bind = n->v.bring.as_name ? xstrdup(n->v.bring.as_name)
                   : n->v.bring.is_path ? module_name_of_path(n->v.bring.lib)
                   : xstrdup(n->v.bring.lib);
        free(path);

        if (n->v.bring.all) {
            Env *mvars = env_new(NULL);
            for (size_t i = 0; i < module_env->len; i++)
                env_declare(mvars, module_env->slots[i].name,
                            retain(module_env->slots[i].val), module_env->slots[i].type);
            Module *m = module_new(bind, mvars);
            env_release(mvars);
            env_declare(env, bind, obj_val(m), NULL);
        } else {
            for (size_t i = 0; i < n->v.bring.nnames; i++) {
                Value *v = env_find(module_env, n->v.bring.names[i]);
                if (!v) {
                    char *lib = xstrdup(n->v.bring.lib);
                    free(bind);
                    lumi_error(n->line, "'%s' is not defined in library '%s'",
                               n->v.bring.names[i], lib);
                }
                env_declare(env, n->v.bring.names[i], retain(*v), NULL);
            }
        }
        free(bind);
        return FLOW_NORMAL;
    }

    /* error(...) — 내가 직접 오류를 일으킵니다.
     * 타입까지 적었으면 메시지 앞에 "[타입] " 을 붙여 두는데, catch 가 타입을
     * 가려낼 때 이 글자를 찾아 봅니다. */
    /* test "이름": ...
     * lumi test 로 돌 때만 실행합니다.  그냥 파일을 돌릴 때는 건너뜁니다 —
     * 시험이 평소 실행을 느리게 하거나 화면을 어지럽히지 않게 하려는 것입니다.
     * 몸통에서 오류가 나면(assert 실패 포함) 그 시험만 실패로 적고 다음으로 갑니다. */
    case N_TEST: {
        if (!in->testing) return FLOW_NORMAL;

        in->tests_run++;
        TryHandler handler;
        handler.prev = in->top_handler;
        in->top_handler = &handler;
        /* 시험이 실패하면 여기로 뛰어옵니다 — 그 사이의 SCOPE_LEAVE 와 depth-- 가
         * 건너뛰어지므로 try 와 똑같이 되돌려 놓습니다.  안 되돌리면 환경 사슬이
         * 이미 사라진 스택 자리를 가리킨 채 남아, 다음 gc() 가 그것을 밟습니다. */
        const int saved_depth = in->depth;
        ScopeGuard *const saved_scope = in->top_scope;

        if (setjmp(handler.jmp) == 0) {
            Env *tenv = env_new(env);
            SCOPE_ENTER(in, tenv);
            run_block(in, &n->v.test_.body, tenv, ret);
            SCOPE_LEAVE(in);
            env_release(tenv);
            in->top_handler = handler.prev;
            char *msg = xsprintf("  ok   %s\n", n->v.test_.name);
            in->output(msg);
            free(msg);
        } else {
            in->top_handler = handler.prev;
            in->depth = saved_depth;
            in->top_scope = saved_scope;
            in->tests_failed++;
            char *msg = xsprintf("  FAIL %s\n         %s\n",
                                 n->v.test_.name, lumi_err_msg);
            in->output(msg);
            free(msg);
            if (lumi_err_trace[0]) in->output(lumi_err_trace);
        }
        return FLOW_NORMAL;
    }

    case N_ERROR:
        exec_error_stmt(in, n, env);
        return FLOW_NORMAL;

    case N_TRY:
        return exec_try_stmt(in, n, env, ret);

    default: {
        Value v = eval(in, n, env);
        release(v);
        return FLOW_NORMAL;
    }
    }
}

Value eval_binop_values(Interp *in, const char *op, Value l, Value r, int line)
{
    Value out = NONE_VAL;
    if (try_instance_binop(in, op, l, r, line, &out)) {
        return out;
    }
    if (strcmp(op, "+") == 0) {
        if (l.kind == V_INT && r.kind == V_INT) return int_val(add_ll(l.as.i, r.as.i, line));
        if (IS_NUM(l) && IS_NUM(r)) return float_val(num_as_double(l) + num_as_double(r));
        if (l.kind == V_STR || r.kind == V_STR) {
            Str *a = value_to_str(in, l), *b = value_to_str(in, r);
            return obj_val(str_concat(a, b));
        }
    } else if (strcmp(op, "-") == 0) {
        if (l.kind == V_INT && r.kind == V_INT) return int_val(sub_ll(l.as.i, r.as.i, line));
        if (IS_NUM(l) && IS_NUM(r)) return float_val(num_as_double(l) - num_as_double(r));
    } else if (strcmp(op, "*") == 0) {
        if (l.kind == V_INT && r.kind == V_INT) return int_val(mul_ll(l.as.i, r.as.i, line));
        if (IS_NUM(l) && IS_NUM(r)) return float_val(num_as_double(l) * num_as_double(r));
    } else if (strcmp(op, "/") == 0) {
        if (IS_NUM(l) && IS_NUM(r)) return float_val(num_as_double(l) / num_as_double(r));
    } else if (strcmp(op, "%") == 0) {
        if (l.kind == V_INT && r.kind == V_INT && r.as.i != 0) return int_val(mod_ll(l.as.i, r.as.i));
        if (IS_NUM(l) && IS_NUM(r) && num_as_double(r) != 0) return float_val(mod_d(num_as_double(l), num_as_double(r)));
    } else if (strcmp(op, "**") == 0) {
        if (l.kind == V_INT && r.kind == V_INT && r.as.i >= 0) {
            long long base = l.as.i, e = r.as.i, acc = 1;
            while (e > 0) {
                if (e & 1) acc = mul_ll(acc, base, line);
                e >>= 1;
                if (e) base = mul_ll(base, base, line);
            }
            return int_val(acc);
        } else if (IS_NUM(l) && IS_NUM(r)) {
            return float_val(pow(num_as_double(l), num_as_double(r)));
        }
    }
    return NONE_VAL;
}

/* ============================================================
 * 17. 인터프리터 만들기 · 돌리기
 * ============================================================ */

static double lumi_time_clock(void);

Interp *interp_new(OutputFn out, InputFn input, const char *base_dir)
{
    static bool seeded = false;
    if (!seeded) { rng_init(); seeded = true; }
    Interp *in = (Interp *)xmalloc(sizeof(Interp));
    memset(in, 0, sizeof(*in));
    in->output = out;
    in->input = input;
    in->globals = env_new(NULL);
    in->base_dir = base_dir ? xstrdup(base_dir) : NULL;
    in->start_clock = lumi_time_clock();
    /* 순환 수집기가 이 인터프리터의 살아 있는 값들도 뿌리로 삼도록 알려 둡니다 */
    gc_add_interp(in);

    /* Register built-in 'json' library module */
    Module *json_mod = module_new("json", env_new(NULL));
    env_declare(in->globals, "json", obj_val(json_mod), NULL);

    return in;
}


/* LSP 서버(lsp.c)가 쓰는 JSON 드나들기.  줄 번호가 뜻이 없는 자리라 NO_LINE 입니다.
 * 잘못된 JSON 은 lumi_error 로 올라오니 부르는 쪽에서 setjmp 로 받으세요. */
Value lumi_json_parse_value(Interp *in, Value text)
{
    return lumi_json_parse(in, text, NO_LINE);
}

Value lumi_json_stringify_value(Interp *in, Value v)
{
    return lumi_json_stringify(in, v, NO_LINE);
}

void interp_run(Interp *in, const char *source)
{
    plat_gil_lock();
    g_execution_locked = true;
    g_current_interp = in;
    /* 앞서 돌린 파일이 오류로 맨 위까지 뛰어올랐다면 사슬에 죽은 자리가 남아
     * 있습니다 (lumi test 는 인터프리터 하나로 여러 파일을 돌립니다). */
    in->top_scope = NULL;
    in->top_handler = NULL;
    in->depth = 0;
    TokenList *tokens = tokenize(source);
    NodeList program = parse_program(tokens);
    tokenlist_free(tokens);

    Value ret = NONE_VAL;
    in->depth = 0;
    for (size_t i = 0; i < program.len; i++) {
        Flow f = exec_node(in, program.items[i], in->globals, &ret);
        if (f == FLOW_BREAK) lumi_error(NO_LINE, "'break' can only be used inside a loop");
        if (f == FLOW_CONTINUE) lumi_error(NO_LINE, "'continue' can only be used inside a loop");
        if (f == FLOW_RETURN) lumi_error(NO_LINE, "'return' can only be used inside a function");
    }
    release(ret);

    free(program.items);
    g_execution_locked = false;
    plat_gil_unlock();
}

/* ============================================================
 * JSON Parser & Serializer for Lumi (json.parse / json.stringify)
 * ============================================================ */

typedef struct {
    const char *src;
    size_t len;
    size_t pos;
    Interp *in;
    int line;
} JsonParser;

static void json_skip_whitespace(JsonParser *p) {
    while (p->pos < p->len) {
        char c = p->src[p->pos];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            p->pos++;
        } else {
            break;
        }
    }
}

static Value json_parse_value(JsonParser *p);

static Value json_parse_string(JsonParser *p) {
    if (p->pos >= p->len || p->src[p->pos] != '"') {
        lumi_error(p->line, "JSON parse error: expected '\"' at pos %zu", p->pos);
    }
    p->pos++; /* skip opening quote */

    Buf b = {0};
    while (p->pos < p->len) {
        char c = p->src[p->pos++];
        if (c == '"') {
            char *utf8 = buf_done(&b);
            Value out = str_value(utf8);
            free(utf8);
            return out;
        }
        if (c == '\\') {
            if (p->pos >= p->len) break;
            char esc = p->src[p->pos++];
            switch (esc) {
                case '"': buf_add(&b, "\"", 1); break;
                case '\\': buf_add(&b, "\\", 1); break;
                case '/': buf_add(&b, "/", 1); break;
                case 'b': buf_add(&b, "\b", 1); break;
                case 'f': buf_add(&b, "\f", 1); break;
                case 'n': buf_add(&b, "\n", 1); break;
                case 'r': buf_add(&b, "\r", 1); break;
                case 't': buf_add(&b, "\t", 1); break;
                case 'u': {
                    if (p->pos + 4 <= p->len) {
                        char hex[5] = { p->src[p->pos], p->src[p->pos+1], p->src[p->pos+2], p->src[p->pos+3], 0 };
                        p->pos += 4;
                        uint32_t cp = (uint32_t)strtoul(hex, NULL, 16);
                        uint32_t u32[1] = { cp };
                        Str *s = str_new(u32, 1);
                        char *utf8 = str_to_utf8(s);
                        buf_str(&b, utf8);
                        free(utf8);
                        release(obj_val(s));
                    }
                    break;
                }
                default: buf_add(&b, &esc, 1); break;
            }
        } else {
            buf_add(&b, &c, 1);
        }
    }
    free(b.p);
    lumi_error(p->line, "JSON parse error: unterminated string");
    return NONE_VAL;
}

static Value json_parse_number(JsonParser *p) {
    size_t start = p->pos;
    if (p->src[p->pos] == '-') p->pos++;
    bool is_float = false;
    while (p->pos < p->len) {
        char c = p->src[p->pos];
        if (c >= '0' && c <= '9') {
            p->pos++;
        } else if (c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-') {
            is_float = true;
            p->pos++;
        } else {
            break;
        }
    }
    size_t num_len = p->pos - start;
    char *num_str = (char *)xmalloc(num_len + 1);
    memcpy(num_str, p->src + start, num_len);
    num_str[num_len] = 0;

    Value out;
    if (is_float) {
        out = float_val(atof(num_str));
    } else {
        out = int_val(atoll(num_str));
    }
    free(num_str);
    return out;
}

static Value json_parse_array(JsonParser *p) {
    p->pos++; /* skip '[' */
    json_skip_whitespace(p);
    Seq *list = seq_new(V_LIST);
    if (p->pos < p->len && p->src[p->pos] == ']') {
        p->pos++;
        return obj_val(list);
    }
    for (;;) {
        json_skip_whitespace(p);
        Value item = json_parse_value(p);
        seq_push(list, item);
        json_skip_whitespace(p);
        if (p->pos < p->len && p->src[p->pos] == ',') {
            p->pos++;
        } else if (p->pos < p->len && p->src[p->pos] == ']') {
            p->pos++;
            break;
        } else {
            lumi_error(p->line, "JSON parse error: expected ',' or ']' in array at pos %zu", p->pos);
        }
    }
    return obj_val(list);
}

static Value json_parse_object(JsonParser *p) {
    p->pos++; /* skip '{' */
    json_skip_whitespace(p);
    Dict *dict = dict_new();
    if (p->pos < p->len && p->src[p->pos] == '}') {
        p->pos++;
        return obj_val(dict);
    }
    for (;;) {
        json_skip_whitespace(p);
        if (p->src[p->pos] != '"') {
            lumi_error(p->line, "JSON parse error: expected string key in object at pos %zu", p->pos);
        }
        Value key = json_parse_string(p);
        json_skip_whitespace(p);
        if (p->pos >= p->len || p->src[p->pos] != ':') {
            release(key);
            lumi_error(p->line, "JSON parse error: expected ':' after key at pos %zu", p->pos);
        }
        p->pos++; /* skip ':' */
        json_skip_whitespace(p);
        Value val = json_parse_value(p);
        dict_set(dict, key, val);
        json_skip_whitespace(p);
        if (p->pos < p->len && p->src[p->pos] == ',') {
            p->pos++;
        } else if (p->pos < p->len && p->src[p->pos] == '}') {
            p->pos++;
            break;
        } else {
            lumi_error(p->line, "JSON parse error: expected ',' or '}' in object at pos %zu", p->pos);
        }
    }
    return obj_val(dict);
}

static Value json_parse_value(JsonParser *p) {
    json_skip_whitespace(p);
    if (p->pos >= p->len) {
        lumi_error(p->line, "JSON parse error: unexpected end of input");
    }
    char c = p->src[p->pos];
    if (c == '{') return json_parse_object(p);
    if (c == '[') return json_parse_array(p);
    if (c == '"') return json_parse_string(p);
    if (c == '-' || (c >= '0' && c <= '9')) return json_parse_number(p);
    if (strncmp(p->src + p->pos, "true", 4) == 0) { p->pos += 4; return bool_val(true); }
    if (strncmp(p->src + p->pos, "false", 5) == 0) { p->pos += 5; return bool_val(false); }
    if (strncmp(p->src + p->pos, "null", 4) == 0) { p->pos += 4; return NONE_VAL; }

    lumi_error(p->line, "JSON parse error: unexpected character '%c' at pos %zu", c, p->pos);
    return NONE_VAL;
}

static Value lumi_json_parse(Interp *in, Value input, int line) {
    if (input.kind != V_STR) {
        lumi_error(line, "json.parse needs text (JSON string) but got %s", R(in, input));
    }
    Str *s = AS_STR(input);
    char *utf8 = str_to_utf8(s);
    JsonParser parser;
    parser.src = utf8;
    parser.len = strlen(utf8);
    parser.pos = 0;
    parser.in = in;
    parser.line = line;
    Value out = json_parse_value(&parser);
    free(utf8);
    return out;
}

static void json_stringify_helper(Interp *in, Value v, Buf *b, int line) {
    switch (v.kind) {
        case V_NONE:
            buf_str(b, "null");
            break;
        case V_BOOL:
            buf_str(b, v.as.b ? "true" : "false");
            break;
        case V_INT: {
            char tmp[64];
            snprintf(tmp, sizeof(tmp), "%lld", v.as.i);
            buf_str(b, tmp);
            break;
        }
        case V_FLOAT: {
            char tmp[64];
            snprintf(tmp, sizeof(tmp), "%.14g", v.as.f);
            buf_str(b, tmp);
            break;
        }
        case V_STR: {
            char *utf8 = value_to_utf8(in, v);
            buf_add(b, "\"", 1);
            for (size_t i = 0; utf8[i]; i++) {
                char c = utf8[i];
                if (c == '"') buf_str(b, "\\\"");
                else if (c == '\\') buf_str(b, "\\\\");
                else if (c == '\n') buf_str(b, "\\n");
                else if (c == '\r') buf_str(b, "\\r");
                else if (c == '\t') buf_str(b, "\\t");
                else buf_add(b, &c, 1);
            }
            buf_add(b, "\"", 1);
            free(utf8);
            break;
        }
        case V_LIST:
        case V_TUPLE: {
            Seq *s = AS_SEQ(v);
            buf_add(b, "[", 1);
            for (size_t i = 0; i < s->len; i++) {
                if (i > 0) buf_str(b, ", ");
                json_stringify_helper(in, s->items[i], b, line);
            }
            buf_add(b, "]", 1);
            break;
        }
        case V_DICT: {
            Dict *d = AS_DICT(v);
            buf_add(b, "{", 1);
            for (size_t i = 0; i < d->len; i++) {
                if (i > 0) buf_str(b, ", ");
                json_stringify_helper(in, d->e[i].key, b, line);
                buf_str(b, ": ");
                json_stringify_helper(in, d->e[i].val, b, line);
            }
            buf_add(b, "}", 1);
            break;
        }
        default: {
            char *utf8 = value_to_utf8(in, v);
            buf_add(b, "\"", 1);
            buf_str(b, utf8);
            buf_add(b, "\"", 1);
            free(utf8);
            break;
        }
    }
}

static Value lumi_json_stringify(Interp *in, Value v, int line) {
    Buf b = {0};
    json_stringify_helper(in, v, &b, line);
    char *text = buf_done(&b);
    Value out = str_value(text);
    free(text);
    return out;
}

/* ============================================================
 * 18. 따로 도는 일감 (start / wait)
 * ============================================================
 *
 * 모든 Lumi 값은 기존 참조 계수와 순환 수집기를 그대로 함께 씁니다. 그래서
 * 한 번에 Lumi 코드를 하나만 실행하는 큰 자물쇠를 씁니다. 대신 sleep/shell/wait
 * 처럼 운영체제를 기다리는 동안에는 자물쇠를 놓아 다른 일감이 진행할 수 있습니다.
 */

static void *task_main(void *arg)
{
    TaskObj *t = (TaskObj *)arg;
    plat_gil_lock();
    g_execution_locked = true;
    g_current_interp = t->in;

    if (setjmp(lumi_jmp) == 0) {
        Value result = call_value(t->in, t->fn, t->args, t->nargs,
                                  "the task function", NO_LINE);
        free(t->args);
        t->args = NULL;
        t->nargs = 0;
        t->result = result;
        /* 위와 같은 까닭 — 실이 끝나면 그 스택의 이름 자리는 뿌리가 아닙니다. */
        t->in->top_scope = NULL;
        t->in->top_handler = NULL;
        t->in->depth = 0;
    } else {
        /* raise_error 가 맨 위 오류일 때 이미 큰 자물쇠를 놓습니다. */
        plat_gil_lock();
        g_execution_locked = true;
        t->failed = true;
        free(t->args);              /* call_value 가 값들은 이미 정리했습니다 */
        t->args = NULL;
        t->nargs = 0;
        t->err_line = lumi_err_line;
        snprintf(t->err, sizeof t->err, "%s", lumi_err_bare);
        snprintf(t->kind, sizeof t->kind, "%s", lumi_err_kind);
        /* 이 실의 C 스택은 곧 사라집니다 — 거기 걸려 있던 이름 자리들을 뿌리 목록에서
         * 지웁니다.  안 지우면 나중에 도는 수집기가 사라진 스택을 밟습니다. */
        t->in->top_scope = NULL;
        t->in->top_handler = NULL;
        t->in->depth = 0;
        release(obj_val(t));        /* start 가 든든히 쥐어 둔 자기 참조 */
        g_execution_locked = false;
        plat_gil_unlock();
        return NULL;
    }

    release(obj_val(t));            /* start 가 든든히 쥐어 둔 자기 참조 */
    g_execution_locked = false;
    plat_gil_unlock();
    return NULL;
}

static Value task_start(Interp *in, Value fn, Value *args, size_t nargs, int line)
{
    if (fn.kind != V_FUNC && fn.kind != V_BOUND) {
        const char *got = type_name_of(fn);
        for (size_t i = 0; i < nargs; i++) release(args[i]);
        release(fn);
        lumi_error(line, "start needs a function but got '%s'", got);
    }

    TaskObj *t = task_new();
    t->fn = fn;                     /* 함수 값의 소유권도 일감으로 넘깁니다 */
    t->args = args;                 /* 인자는 이미 셈한 값, 소유권을 넘겨받습니다 */
    t->nargs = nargs;
    t->result = NONE_VAL;

    /* 이 일감만의 호출 겹·오류 자리와, 부모와 함께 쓰는 전역 환경입니다. */
    t->in = interp_new(in->output, NULL, in->base_dir);
    env_release(t->in->globals);    /* interp_new 가 만든 빈 전역 대신 */
    t->in->globals = env_retain(in->globals);
    if (in->cur_file) interp_set_script(t->in, in->cur_file);

    t->thread = plat_thread_start(task_main, t);
    if (!t->thread) {
        t->args = NULL; t->nargs = 0;
        release(t->fn); t->fn = NONE_VAL;
        gc_drop_interp(t->in);
        env_release(t->in->globals); free(t->in->base_dir); free(t->in); t->in = NULL;
        free(args);
        release(obj_val(t));
        lumi_error(line, "could not start another task (the operating system refused the new thread)");
    }
    retain(obj_val(t));             /* 사용자가 일감을 잊어도 끝날 때까지 살아 있게 */
    return obj_val(t);
}

static Value task_wait(TaskObj *t, int line)
{
    if (!t->joined) {
        /* 부른 쪽은 큰 자물쇠를 쥔 상태입니다. 그 상태로 기다리면 새 실은
         * 첫 줄도 못 실행하므로, 기다리는 동안만 양보했다가 다시 잡습니다. */
        g_execution_locked = false;
        plat_gil_unlock();
        plat_thread_join(t->thread);
        plat_gil_lock();
        g_execution_locked = true;
        t->joined = true;
        t->thread = NULL;
    }
    if (t->failed)
        lumi_error_kind(line, t->kind[0] ? t->kind : "Error", "%s",
                        t->err[0] ? t->err : "the task failed");
    return retain(t->result);
}

#include "builtins.inc"
