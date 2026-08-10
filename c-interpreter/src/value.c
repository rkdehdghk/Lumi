/* 값(Value) 만들기 · 세어 두기(참조 계수) · 문자열 · 리스트 · 딕셔너리 · 환경 */
#include "lumi.h"
#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------- 작은 도우미들 ---------------- */

void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) { fputs("out of memory\n", stderr); exit(1); }
    return p;
}

void *xrealloc(void *p, size_t n)
{
    void *q = realloc(p, n ? n : 1);
    if (!q) { fputs("out of memory\n", stderr); exit(1); }
    return q;
}

char *xstrdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = (char *)xmalloc(n);
    memcpy(p, s, n);
    return p;
}

char *xsprintf(const char *fmt, ...)
{
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) n = 0;
    char *out = (char *)xmalloc((size_t)n + 1);
    vsnprintf(out, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    return out;
}

/* 파이썬의 repr(실수) 와 같은 표기: 되돌려 읽으면 같은 값이 되는 가장 짧은 글자 */
char *fmt_double(double d)
{
    char buf[64];
    if (d != d) return xstrdup("nan");
    if (d > 1.7976931348623157e308) return xstrdup("inf");
    if (d < -1.7976931348623157e308) return xstrdup("-inf");
    for (int prec = 1; prec <= 17; prec++) {
        snprintf(buf, sizeof buf, "%.*g", prec, d);
        if (strtod(buf, NULL) == d) break;
    }
    if (!strpbrk(buf, ".eEn")) strcat(buf, ".0");   /* 실수처럼 보이게 */
    return xstrdup(buf);
}

/* ---------------- 값 만들기 ---------------- */

Value bool_val(bool b)      { Value v; v.kind = V_BOOL;  v.as.b = b; return v; }
Value int_val(long long i)  { Value v; v.kind = V_INT;   v.as.i = i; return v; }
Value float_val(double f)   { Value v; v.kind = V_FLOAT; v.as.f = f; return v; }

Value obj_val(void *o)
{
    Value v;
    v.kind = ((Obj *)o)->kind;
    v.as.o = (Obj *)o;
    return v;
}

static Obj *g_gc_head = NULL;
/* 마지막으로 치운 뒤로 늘어난 '남을 담을 수 있는 값'의 수.  LUMI_GC_CHECK 가 봅니다. */
long lumi_gc_pending = 0;

static Obj *obj_alloc(size_t size, VKind kind)
{
    Obj *o = (Obj *)xmalloc(size);
    memset(o, 0, size);
    o->rc = 1;
    o->kind = kind;
    if (kind == V_LIST || kind == V_TUPLE || kind == V_DICT ||
        kind == V_FUNC || kind == V_MODULE || kind == V_CLASS ||
        kind == V_INSTANCE || kind == V_BOUND || kind == V_SUPER) {
        o->gc_tracked = true;
        o->gc_next = g_gc_head;
        if (g_gc_head) g_gc_head->gc_prev = o;
        g_gc_head = o;
        lumi_gc_pending++;
    }
    return o;
}

static void obj_free(Obj *o);

static void obj_release(void *p)
{
    Obj *o = (Obj *)p;
    if (!o) return;
    if (--o->rc <= 0) obj_free(o);
}

Value retain(Value v)
{
    if (v.kind >= V_STR && v.as.o) v.as.o->rc++;
    return v;
}

void release(Value v)
{
    if (v.kind >= V_STR && v.as.o) obj_release(v.as.o);
}

static void obj_free(Obj *o)
{
    if (o->gc_tracked) {
        if (o->gc_prev) o->gc_prev->gc_next = o->gc_next;
        else if (g_gc_head == o) g_gc_head = o->gc_next;
        if (o->gc_next) o->gc_next->gc_prev = o->gc_prev;
        o->gc_tracked = false;
        lumi_gc_pending--;          /* 참조 세기가 제때 없앤 것은 문턱에 안 셉니다 */
    }

    switch (o->kind) {
    case V_STR:
        free(((Str *)o)->cp);
        break;
    case V_LIST: case V_TUPLE: {
        Seq *s = (Seq *)o;
        for (size_t i = 0; i < s->len; i++) release(s->items[i]);
        free(s->items);
        break;
    }
    case V_BYTES:
        free(((BytesObj *)o)->data);
        break;
    case V_DICT: {
        Dict *d = (Dict *)o;
        for (size_t i = 0; i < d->len; i++) {
            release(d->e[i].key);
            release(d->e[i].val);
        }
        free(d->e);
        free(d->htable);
        break;
    }
    case V_FUNC:
        env_release(((Func *)o)->closure);
        break;
    case V_MODULE:
        free(((Module *)o)->name);
        env_release(((Module *)o)->vars);
        break;
    case V_CLASS: {
        LumiClass *c = (LumiClass *)o;
        free(c->name);
        obj_release(c->parent);
        env_release(c->methods);
        env_release(c->vars);
        env_release(c->closure);
        break;
    }
    case V_INSTANCE:
        obj_release(((Instance *)o)->cls);
        env_release(((Instance *)o)->fields);
        break;
    case V_BOUND: {
        Bound *b = (Bound *)o;
        obj_release(b->func);
        obj_release(b->inst);
        obj_release(b->owner);
        break;
    }
    case V_SUPER: {
        SuperRef *s = (SuperRef *)o;
        obj_release(s->start);
        obj_release(s->inst);
        break;
    }
    case V_FILE: {
        FileObj *f = (FileObj *)o;
        if (f->fp) fclose(f->fp);      /* close 를 잊어도 여기서 닫힙니다 */
        free(f->path);
        break;
    }
    case V_TASK: {
        TaskObj *t = (TaskObj *)o;
        /* 놓기 전에 끝까지 기다립니다. 실행 중인 실이 이 일감의 값들을
         * 계속 들고 있기 때문입니다. */
        /* task_wait 과 같은 까닭으로 손잡이를 먼저 챙기고 지웁니다 (두 번 join 금지) */
        void *th = t->thread;
        t->thread = NULL;
        if (th && !t->joined) {
            /* 실행 중인 일감이 큰 자물쇠를 기다릴 수 있습니다. 그 상태로
             * join 하면 서로 기다리므로, 정리하는 잠깐만 양보합니다. */
            bool had_gil = lumi_gil_is_held();
            if (had_gil) plat_gil_unlock();
            plat_thread_join(th);
            if (had_gil) plat_gil_lock();
        }
        release(t->fn);
        for (size_t i = 0; i < t->nargs; i++) release(t->args[i]);
        free(t->args);
        release(t->result);
        if (t->in) {
            gc_drop_interp(t->in);      /* 더는 뿌리로 삼지 않습니다 */
            env_release(t->in->globals);
            free(t->in->base_dir);
            free(t->in);
        }
        break;
    }
    case V_ERROR: {
        ErrorObj *e = (ErrorObj *)o;
        free(e->type);
        obj_release((Obj *)e->message);
        free(e->file);
        break;
    }
    default:
        break;
    }
    free(o);
}

/* ---------------- 문자열 ---------------- */

Str *str_new(const uint32_t *cp, size_t len)
{
    Str *s = (Str *)obj_alloc(sizeof(Str), V_STR);
    s->len = len;
    s->cp = (uint32_t *)xmalloc((len + 1) * sizeof(uint32_t));
    if (len) memcpy(s->cp, cp, len * sizeof(uint32_t));
    s->cp[len] = 0;
    return s;
}

Str *str_from_utf8_n(const char *u, size_t nbytes)
{
    size_t len = 0;
    uint32_t *cp = utf8_to_cp(u, nbytes, &len);
    Str *s = (Str *)obj_alloc(sizeof(Str), V_STR);
    s->cp = cp;
    s->len = len;
    return s;
}

Str *str_from_utf8(const char *u) { return str_from_utf8_n(u, strlen(u)); }

char *str_to_utf8(const Str *s) { return cp_to_utf8(s->cp, s->len, NULL); }

Value str_value(const char *utf8) { return obj_val(str_from_utf8(utf8)); }

bool str_eq(const Str *a, const Str *b)
{
    if (a->len != b->len) return false;
    return memcmp(a->cp, b->cp, a->len * sizeof(uint32_t)) == 0;
}

int str_cmp(const Str *a, const Str *b)
{
    size_t n = a->len < b->len ? a->len : b->len;
    for (size_t i = 0; i < n; i++) {
        if (a->cp[i] != b->cp[i]) return a->cp[i] < b->cp[i] ? -1 : 1;
    }
    if (a->len == b->len) return 0;
    return a->len < b->len ? -1 : 1;
}

Str *str_concat(const Str *a, const Str *b)
{
    Str *s = (Str *)obj_alloc(sizeof(Str), V_STR);
    s->len = a->len + b->len;
    s->cp = (uint32_t *)xmalloc((s->len + 1) * sizeof(uint32_t));
    memcpy(s->cp, a->cp, a->len * sizeof(uint32_t));
    memcpy(s->cp + a->len, b->cp, b->len * sizeof(uint32_t));
    s->cp[s->len] = 0;
    return s;
}

Str *str_slice(const Str *s, size_t start, size_t end)
{
    if (end < start) end = start;
    return str_new(s->cp + start, end - start);
}

/* ---------------- 리스트 / 튜플 ---------------- */

Seq *seq_new(VKind kind)
{
    return (Seq *)obj_alloc(sizeof(Seq), kind);
}

static void seq_grow(Seq *s, size_t need)
{
    if (need <= s->cap) return;
    size_t cap = s->cap ? s->cap * 2 : 8;
    while (cap < need) cap *= 2;
    s->items = (Value *)xrealloc(s->items, cap * sizeof(Value));
    s->cap = cap;
}

void seq_push(Seq *s, Value v)
{
    seq_grow(s, s->len + 1);
    s->items[s->len++] = v;
}

void seq_insert(Seq *s, size_t at, Value v)
{
    seq_grow(s, s->len + 1);
    memmove(s->items + at + 1, s->items + at, (s->len - at) * sizeof(Value));
    s->items[at] = v;
    s->len++;
}

Value seq_pop(Seq *s)
{
    return s->items[--s->len];          /* 참조를 그대로 넘겨줍니다 */
}

Seq *seq_copy(const Seq *s, VKind kind)
{
    Seq *out = seq_new(kind);
    seq_grow(out, s->len);
    for (size_t i = 0; i < s->len; i++) out->items[i] = retain(s->items[i]);
    out->len = s->len;
    return out;
}

/* ---------------- 힙 객체 생성자 ---------------- */

Func *func_new(FuncDefNode *node, Env *closure, const char *file)
{
    Func *f = (Func *)obj_alloc(sizeof(Func), V_FUNC);
    f->node = node;
    f->closure = env_retain(closure);
    f->file = file;                 /* Interp 소유의 문자열 — 여기서 free 하지 않습니다 */
    return f;
}

Module *module_new(const char *name, Env *vars)
{
    Module *m = (Module *)obj_alloc(sizeof(Module), V_MODULE);
    m->name = xstrdup(name);
    m->vars = env_retain(vars);
    return m;
}

LumiClass *class_new(const char *name, LumiClass *parent, Env *methods, Env *vars, Env *closure)
{
    LumiClass *c = (LumiClass *)obj_alloc(sizeof(LumiClass), V_CLASS);
    c->name = xstrdup(name);
    c->parent = parent ? (LumiClass *)retain(obj_val(parent)).as.o : NULL;
    c->methods = env_retain(methods);
    c->vars = env_retain(vars);
    c->closure = env_retain(closure);
    return c;
}

Instance *instance_new(LumiClass *cls)
{
    Instance *inst = (Instance *)obj_alloc(sizeof(Instance), V_INSTANCE);
    inst->cls = cls ? (LumiClass *)retain(obj_val(cls)).as.o : NULL;
    inst->fields = env_new(NULL);
    return inst;
}

Bound *bound_new(Func *func, Instance *inst, LumiClass *owner)
{
    Bound *b = (Bound *)obj_alloc(sizeof(Bound), V_BOUND);
    b->func = func ? (Func *)retain(obj_val(func)).as.o : NULL;
    b->inst = inst ? (Instance *)retain(obj_val(inst)).as.o : NULL;
    b->owner = owner ? (LumiClass *)retain(obj_val(owner)).as.o : NULL;
    return b;
}

SuperRef *super_new(LumiClass *start, Instance *inst)
{
    SuperRef *s = (SuperRef *)obj_alloc(sizeof(SuperRef), V_SUPER);
    s->start = start ? (LumiClass *)retain(obj_val(start)).as.o : NULL;
    s->inst = inst ? (Instance *)retain(obj_val(inst)).as.o : NULL;
    return s;
}

/* ---------------- 바이트 ---------------- */

BytesObj *bytes_new(const uint8_t *data, size_t len)
{
    BytesObj *b = (BytesObj *)obj_alloc(sizeof(BytesObj), V_BYTES);
    b->len = len;
    b->data = (uint8_t *)xmalloc(len + 1);
    if (len) memcpy(b->data, data, len);
    b->data[len] = 0;
    return b;
}

/* ---------------- 열어 둔 파일 ---------------- */

FileObj *file_new(FILE *fp, const char *path, const char *codec,
                  bool writing, bool appending)
{
    FileObj *f = (FileObj *)obj_alloc(sizeof(FileObj), V_FILE);
    f->fp = fp;
    f->path = xstrdup(path);
    f->codec = codec;
    f->writing = writing;
    f->appending = appending;
    return f;
}

TaskObj *task_new(void)
{
    return (TaskObj *)obj_alloc(sizeof(TaskObj), V_TASK);
}

/* ---------------- 난 오류 ---------------- */

ErrorObj *error_new(const char *type, const char *message, int line, const char *file)
{
    ErrorObj *e = (ErrorObj *)obj_alloc(sizeof(ErrorObj), V_ERROR);
    e->type = xstrdup(type && *type ? type : "Error");
    e->message = str_from_utf8(message ? message : "");
    e->line = line;
    e->file = file ? xstrdup(file) : NULL;
    return e;
}

/* 오류 종류의 부모.  왼쪽이 오른쪽의 한 갈래입니다.
 * 여기 없는 이름(사용자가 error("MyKind", ...) 로 지은 것 포함)은 부모가 "Error" 입니다.
 * "Error" 는 맨 위라서 catch Error e 로 모든 오류를 잡습니다. */
static const char *ERROR_PARENT[][2] = {
    { "FileNotFound", "FileError"   },
    { "FileError",    "Error"       },
    { "IndexError",   "LookupError" },
    { "KeyError",     "LookupError" },
    { "LookupError",  "Error"       },
    { "TypeError",    "Error"       },
    { "ValueError",   "Error"       },
    { "NameError",    "Error"       },
    { "MathError",    "Error"       },
    { "NetworkError", "Error"       },
    { "DatabaseError","Error"       },
    { "ArgumentError","Error"       },
    { NULL, NULL }
};

bool error_kind_is(const char *kind, const char *want)
{
    if (!kind || !want) return false;
    /* 부모를 따라 위로 올라가며 봅니다.  줄이 끊기면 "Error" 로 한 번 더 올라갑니다. */
    for (int guard = 0; guard < 16 && kind; guard++) {
        if (strcmp(kind, want) == 0) return true;
        if (strcmp(kind, "Error") == 0) return false;
        const char *up = "Error";
        for (size_t i = 0; ERROR_PARENT[i][0]; i++)
            if (strcmp(kind, ERROR_PARENT[i][0]) == 0) { up = ERROR_PARENT[i][1]; break; }
        kind = up;
    }
    return false;
}

/* ---------------- 딕셔너리 & 해시 테이블 ---------------- */

static uint32_t fnv1a_32(const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < len; i++) {
        hash = (hash ^ p[i]) * 16777619u;
    }
    return hash;
}

uint32_t value_hash(Value v)
{
    /* 참·거짓은 숫자와 다른 키이므로(key_equal 참고) 해시도 갈라 둡니다.
     * 안 그러면 1 과 true 가 같은 칸을 놓고 다투기만 하고 뜻은 없습니다. */
    if (v.kind == V_BOOL) {
        unsigned char tag[2] = { 'b', (unsigned char)(v.as.b ? 1 : 0) };
        return fnv1a_32(tag, sizeof(tag));
    }
    if (v.kind == V_INT || v.kind == V_FLOAT) {
        double d = v.kind == V_INT ? (double)v.as.i : v.as.f;
        long long i = (long long)d;
        if ((double)i == d) {
            return fnv1a_32(&i, sizeof(i));
        }
        return fnv1a_32(&d, sizeof(d));
    }
    if (v.kind == V_STR) {
        Str *s = AS_STR(v);
        return fnv1a_32(s->cp, s->len * sizeof(uint32_t));
    }
    if (v.kind == V_BYTES) {
        BytesObj *b = AS_BYTES(v);
        return fnv1a_32(b->data, b->len);
    }
    if (v.kind == V_NONE) {
        uint32_t h = 0x9e3779b9u;
        return h;
    }
    uintptr_t ptr = (uintptr_t)v.as.o;
    return fnv1a_32(&ptr, sizeof(ptr));
}

/* 키가 같은지: 파이썬처럼 true == 1 == 1.0 은 같은 키입니다. */
bool key_equal(Value a, Value b)
{
    /* 키가 같다는 것 = 값도 같고 '종류'도 같다는 것.
     * 그래서 1 과 true, 0 과 false 는 서로 다른 키입니다 (파이썬과 다릅니다).
     * 숫자끼리는 값으로만 봅니다 — 2 와 2.0 은 같은 키입니다.
     * (`==` 는 이 규칙과 별개라 1 == true 는 그대로 참입니다) */
    bool abool = (a.kind == V_BOOL), bbool = (b.kind == V_BOOL);
    if (abool || bbool) {
        if (abool != bbool) return false;          /* 참·거짓은 숫자와 섞이지 않습니다 */
        return a.as.b == b.as.b;
    }
    bool an = (a.kind == V_INT || a.kind == V_FLOAT);
    bool bn = (b.kind == V_INT || b.kind == V_FLOAT);
    if (an && bn) {
        if (a.kind == V_INT && b.kind == V_INT) return a.as.i == b.as.i;
        double x = a.kind == V_INT ? (double)a.as.i : a.as.f;
        double y = b.kind == V_INT ? (double)b.as.i : b.as.f;
        return x == y;
    }
    if (a.kind != b.kind) return false;
    if (a.kind == V_STR) return str_eq(AS_STR(a), AS_STR(b));
    if (a.kind == V_BYTES) {
        BytesObj *x = AS_BYTES(a), *y = AS_BYTES(b);
        return x->len == y->len && memcmp(x->data, y->data, x->len) == 0;
    }
    if (a.kind == V_NONE) return true;
    return a.as.o == b.as.o;
}

Dict *dict_new(void) { return (Dict *)obj_alloc(sizeof(Dict), V_DICT); }

static void dict_rehash(Dict *d, size_t new_hcap)
{
    if (new_hcap < 8) new_hcap = 8;
    int32_t *new_htable = (int32_t *)xmalloc(new_hcap * sizeof(int32_t));
    for (size_t i = 0; i < new_hcap; i++) new_htable[i] = -1;

    size_t mask = new_hcap - 1;
    for (size_t i = 0; i < d->len; i++) {
        uint32_t h = d->e[i].hash;
        size_t slot = h & mask;
        while (new_htable[slot] != -1) {
            slot = (slot + 1) & mask;
        }
        new_htable[slot] = (int32_t)i;
    }

    free(d->htable);
    d->htable = new_htable;
    d->hcap = new_hcap;
}

Value *dict_find(Dict *d, Value key)
{
    if (d->len == 0 || !d->htable) return NULL;
    uint32_t h = value_hash(key);
    size_t mask = d->hcap - 1;
    size_t slot = h & mask;
    for (size_t i = 0; i < d->hcap; i++) {
        int32_t idx = d->htable[slot];
        if (idx == -1) return NULL;
        if (idx >= 0) {
            if (d->e[idx].hash == h && key_equal(d->e[idx].key, key)) {
                return &d->e[idx].val;
            }
        }
        slot = (slot + 1) & mask;
    }
    return NULL;
}

void dict_set(Dict *d, Value key, Value val)
{
    uint32_t h = value_hash(key);
    if (d->htable && d->len > 0) {
        size_t mask = d->hcap - 1;
        size_t slot = h & mask;
        for (size_t i = 0; i < d->hcap; i++) {
            int32_t idx = d->htable[slot];
            if (idx == -1) break;
            if (idx >= 0) {
                if (d->e[idx].hash == h && key_equal(d->e[idx].key, key)) {
                    release(d->e[idx].val);
                    d->e[idx].val = val;
                    release(key);
                    return;
                }
            }
            slot = (slot + 1) & mask;
        }
    }

    if (d->len >= d->cap) {
        d->cap = d->cap ? d->cap * 2 : 8;
        d->e = (DictEntry *)xrealloc(d->e, d->cap * sizeof(DictEntry));
    }
    if (!d->htable || (d->len + 1) * 4 > d->hcap * 3) {
        size_t new_hcap = d->hcap ? d->hcap * 2 : 8;
        while ((d->len + 1) * 4 > new_hcap * 3) new_hcap *= 2;
        dict_rehash(d, new_hcap);
    }

    size_t idx = d->len;
    d->e[idx].hash = h;
    d->e[idx].key = key;
    d->e[idx].val = val;
    d->len++;

    size_t mask = d->hcap - 1;
    size_t slot = h & mask;
    while (d->htable[slot] >= 0) {
        slot = (slot + 1) & mask;
    }
    d->htable[slot] = (int32_t)idx;
}

bool dict_remove(Dict *d, Value key)
{
    if (d->len == 0 || !d->htable) return false;
    uint32_t h = value_hash(key);
    size_t mask = d->hcap - 1;
    size_t slot = h & mask;
    int32_t target_idx = -1;

    for (size_t i = 0; i < d->hcap; i++) {
        int32_t idx = d->htable[slot];
        if (idx == -1) return false;
        if (idx >= 0) {
            if (d->e[idx].hash == h && key_equal(d->e[idx].key, key)) {
                target_idx = idx;
                break;
            }
        }
        slot = (slot + 1) & mask;
    }
    if (target_idx < 0) return false;

    release(d->e[target_idx].key);
    release(d->e[target_idx].val);

    memmove(d->e + target_idx, d->e + target_idx + 1, (d->len - target_idx - 1) * sizeof(DictEntry));
    d->len--;

    dict_rehash(d, d->hcap);
    return true;
}

/* ---------------- 순환 참조 가비지 컬렉터 (Cyclic GC) ---------------- */

static void gc_visit_val(Value v)
{
    if (v.kind >= V_STR && v.as.o && v.as.o->gc_tracked) {
        v.as.o->gc_refs--;
    }
}

/* 이번 바퀴 번호.  한 바퀴 안에서 같은 환경을 두 번 훑지 않으려고 씁니다. */
static unsigned g_gc_pass = 0;

/* 이 환경을 이번 바퀴에 처음 보는가 (보는 것으로 표시하고 참/거짓을 돌려줍니다) */
static bool gc_first_look(Env *e)
{
    if (e->gc_seen == g_gc_pass) return false;
    e->gc_seen = g_gc_pass;
    return true;
}

/* 환경에 든 값들의 '안에서 온 참조'를 하나씩 뺍니다.
 *
 * **환경마다 딱 한 번만 빼야 합니다.**  환경은 여럿이 함께 볼 수 있어서 —
 * 클래스의 메서드 넷은 같은 closure 하나를 봅니다 — 표시 없이 부르면 같은 값을
 * 넷 번 빼게 됩니다.  게다가 parent 를 따라 올라가므로 프로그램 안 모든 메서드가
 * 전역을 한 번씩 더 빼서, 멀쩡히 쓰이는 값이 쓰레기로 보였습니다.  그것이
 * 클래스 상속을 쓴 코드에서 gc() 가 살아 있는 값을 놓아 버리던 까닭입니다
 * (main 부터 있던 버그입니다 — docs/Lumi_Changes.md 참고). */
static void gc_visit_env(Env *e)
{
    if (!e || !gc_first_look(e)) return;
    for (size_t i = 0; i < e->len; i++) {
        gc_visit_val(e->slots[i].val);
    }
    if (e->parent) gc_visit_env(e->parent);
}

static void gc_traverse_children(Obj *o)
{
    switch (o->kind) {
    case V_LIST: case V_TUPLE: {
        Seq *s = (Seq *)o;
        for (size_t i = 0; i < s->len; i++) gc_visit_val(s->items[i]);
        break;
    }
    case V_DICT: {
        Dict *d = (Dict *)o;
        for (size_t i = 0; i < d->len; i++) {
            gc_visit_val(d->e[i].key);
            gc_visit_val(d->e[i].val);
        }
        break;
    }
    case V_FUNC:
        gc_visit_env(((Func *)o)->closure);
        break;
    case V_MODULE:
        gc_visit_env(((Module *)o)->vars);
        break;
    case V_CLASS: {
        LumiClass *c = (LumiClass *)o;
        if (c->parent) gc_visit_val(obj_val(c->parent));
        gc_visit_env(c->methods);
        gc_visit_env(c->vars);
        gc_visit_env(c->closure);
        break;
    }
    case V_INSTANCE: {
        Instance *inst = (Instance *)o;
        if (inst->cls) gc_visit_val(obj_val(inst->cls));
        gc_visit_env(inst->fields);
        break;
    }
    case V_BOUND: {
        Bound *b = (Bound *)o;
        if (b->func) gc_visit_val(obj_val(b->func));
        if (b->inst) gc_visit_val(obj_val(b->inst));
        if (b->owner) gc_visit_val(obj_val(b->owner));
        break;
    }
    case V_SUPER: {
        SuperRef *sr = (SuperRef *)o;
        if (sr->start) gc_visit_val(obj_val(sr->start));
        if (sr->inst) gc_visit_val(obj_val(sr->inst));
        break;
    }
    default:
        break;
    }
}

static void gc_restore_reachable(Obj *o);
/* 되살리는 쪽도 환경마다 한 번이면 됩니다 (한 번 되살리면 그 안은 다 살아납니다) */
static void gc_restore_env(Env *e)
{
    if (!e || !gc_first_look(e)) return;
    for (size_t i = 0; i < e->len; i++) {
        Value v = e->slots[i].val;
        if (v.kind >= V_STR && v.as.o && v.as.o->gc_tracked) gc_restore_reachable(v.as.o);
    }
    if (e->parent) gc_restore_env(e->parent);
}

static void gc_restore_reachable(Obj *o)
{
    /* 되돌아 도는 것을 막는 표시는 gc_visited 입니다.  예전에는 'gc_refs > 0 이면
     * 그만' 이었는데, 밖에서 붙들려 있는 값은 처음부터 gc_refs 가 0 이 아니라서
     * **그 속에 든 값을 한 번도 안 훑고** 지나갔습니다.  그래서 전역에 담아 둔
     * [[1],[2],[3]] 이 gc() 한 번에 [[], [], []] 가 됐습니다. */
    if (o->gc_visited) return;
    o->gc_visited = true;
    if (o->gc_refs <= 0) o->gc_refs = 1;
    switch (o->kind) {
    case V_LIST: case V_TUPLE: {
        Seq *s = (Seq *)o;
        for (size_t i = 0; i < s->len; i++) {
            Value v = s->items[i];
            if (v.kind >= V_STR && v.as.o && v.as.o->gc_tracked) gc_restore_reachable(v.as.o);
        }
        break;
    }
    case V_DICT: {
        Dict *d = (Dict *)o;
        for (size_t i = 0; i < d->len; i++) {
            Value k = d->e[i].key, v = d->e[i].val;
            if (k.kind >= V_STR && k.as.o && k.as.o->gc_tracked) gc_restore_reachable(k.as.o);
            if (v.kind >= V_STR && v.as.o && v.as.o->gc_tracked) gc_restore_reachable(v.as.o);
        }
        break;
    }
    case V_FUNC:
        gc_restore_env(((Func *)o)->closure);
        break;
    case V_MODULE:
        gc_restore_env(((Module *)o)->vars);
        break;
    case V_CLASS: {
        LumiClass *c = (LumiClass *)o;
        if (c->parent && c->parent->h.gc_tracked) gc_restore_reachable((Obj *)c->parent);
        gc_restore_env(c->methods);
        gc_restore_env(c->vars);
        gc_restore_env(c->closure);
        break;
    }
    case V_INSTANCE: {
        Instance *inst = (Instance *)o;
        if (inst->cls && inst->cls->h.gc_tracked) gc_restore_reachable((Obj *)inst->cls);
        gc_restore_env(inst->fields);
        break;
    }
    case V_BOUND: {
        Bound *b = (Bound *)o;
        if (b->func && b->func->h.gc_tracked) gc_restore_reachable((Obj *)b->func);
        if (b->inst && b->inst->h.gc_tracked) gc_restore_reachable((Obj *)b->inst);
        if (b->owner && b->owner->h.gc_tracked) gc_restore_reachable((Obj *)b->owner);
        break;
    }
    case V_SUPER: {
        SuperRef *sr = (SuperRef *)o;
        if (sr->start && sr->start->h.gc_tracked) gc_restore_reachable((Obj *)sr->start);
        if (sr->inst && sr->inst->h.gc_tracked) gc_restore_reachable((Obj *)sr->inst);
        break;
    }
    default:
        break;
    }
}

static void gc_clear_references(Obj *o)
{
    switch (o->kind) {
    case V_LIST: case V_TUPLE: {
        Seq *s = (Seq *)o;
        for (size_t i = 0; i < s->len; i++) release(s->items[i]);
        free(s->items); s->items = NULL;
        s->len = s->cap = 0;
        break;
    }
    case V_DICT: {
        Dict *d = (Dict *)o;
        for (size_t i = 0; i < d->len; i++) {
            release(d->e[i].key);
            release(d->e[i].val);
        }
        free(d->e); d->e = NULL;
        free(d->htable); d->htable = NULL;
        d->len = d->cap = d->hcap = 0;
        break;
    }
    case V_FUNC:
        env_release(((Func *)o)->closure);
        ((Func *)o)->closure = NULL;
        break;
    case V_MODULE:
        free(((Module *)o)->name); ((Module *)o)->name = NULL;
        env_release(((Module *)o)->vars); ((Module *)o)->vars = NULL;
        break;
    case V_CLASS: {
        LumiClass *c = (LumiClass *)o;
        free(c->name); c->name = NULL;
        obj_release(c->parent); c->parent = NULL;
        env_release(c->methods); c->methods = NULL;
        env_release(c->vars); c->vars = NULL;
        env_release(c->closure); c->closure = NULL;
        break;
    }
    case V_INSTANCE: {
        Instance *inst = (Instance *)o;
        obj_release(inst->cls); inst->cls = NULL;
        env_release(inst->fields); inst->fields = NULL;
        break;
    }
    case V_BOUND: {
        Bound *b = (Bound *)o;
        obj_release(b->func); b->func = NULL;
        obj_release(b->inst); b->inst = NULL;
        obj_release(b->owner); b->owner = NULL;
        break;
    }
    case V_SUPER: {
        SuperRef *sr = (SuperRef *)o;
        obj_release(sr->start); sr->start = NULL;
        obj_release(sr->inst); sr->inst = NULL;
        break;
    }
    default:
        break;
    }
}

/* --- 살아 있는 인터프리터 사슬 -------------------------------------------
 * 일감(start)마다 저마다의 Interp 가 있고, 저마다의 C 스택에 살아 있는 환경이
 * 있습니다.  큰 자물쇠 덕분에 Lumi 코드는 한 번에 하나만 돌지만, **멈춰 있는**
 * 일감의 지역 값도 여전히 살아 있는 값입니다.  그래서 치울 때는 전부 훑습니다. */
static Interp *g_interps = NULL;

void gc_add_interp(Interp *in)
{
    if (!in) return;
    in->next_interp = g_interps;
    g_interps = in;
}

void gc_drop_interp(Interp *in)
{
    if (!in) return;
    for (Interp **p = &g_interps; *p; p = &(*p)->next_interp) {
        if (*p == in) { *p = in->next_interp; in->next_interp = NULL; return; }
    }
}

/* 한 인터프리터가 붙들고 있는 것들을 '살아 있다'고 표시합니다.
 * 전역 + 지금 열려 있는 이름 자리들 (SCOPE_ENTER 가 엮어 둔 사슬).
 * gc_restore_env 가 부모까지 따라가므로 안쪽 자리 하나만 알려 주면 됩니다. */
static void gc_restore_interp(Interp *in)
{
    if (!in) return;
    if (in->globals) gc_restore_env(in->globals);
    for (ScopeGuard *s = in->top_scope; s; s = s->prev) gc_restore_env(s->env);
}

size_t gc_collect(Interp *in)
{
    for (Obj *curr = g_gc_head; curr; curr = curr->gc_next) {
        curr->gc_refs = curr->rc;
        curr->gc_visited = false;
    }

    /* 빼는 바퀴 — 환경마다 한 번씩만 (gc_visit_env 의 주석을 보세요) */
    g_gc_pass++;
    for (Obj *curr = g_gc_head; curr; curr = curr->gc_next) {
        gc_traverse_children(curr);
    }

    /* 되살리는 바퀴 — 앞 바퀴의 표시와 섞이지 않게 번호를 새로 매깁니다 */
    g_gc_pass++;
    gc_restore_interp(in);
    for (Interp *o = g_interps; o; o = o->next_interp) {
        if (o != in) gc_restore_interp(o);
    }

    for (Obj *curr = g_gc_head; curr; curr = curr->gc_next) {
        if (curr->gc_refs > 0) {
            gc_restore_reachable(curr);
        }
    }

    size_t dead_cap = 16, dead_len = 0;
    Obj **dead = (Obj **)xmalloc(dead_cap * sizeof(Obj *));

    Obj *curr = g_gc_head;
    while (curr) {
        Obj *next = curr->gc_next;
        if (curr->gc_refs == 0) {
            if (dead_len == dead_cap) {
                dead_cap *= 2;
                dead = (Obj **)xrealloc(dead, dead_cap * sizeof(Obj *));
            }
            dead[dead_len++] = curr;

            if (curr->gc_prev) curr->gc_prev->gc_next = curr->gc_next;
            else g_gc_head = curr->gc_next;
            if (curr->gc_next) curr->gc_next->gc_prev = curr->gc_prev;
            curr->gc_tracked = false;
        }
        curr = next;
    }

    for (size_t i = 0; i < dead_len; i++) {
        dead[i]->rc = 1000000;
    }

    for (size_t i = 0; i < dead_len; i++) {
        gc_clear_references(dead[i]);
    }

    for (size_t i = 0; i < dead_len; i++) {
        free(dead[i]);
    }

    free(dead);
    lumi_gc_pending = 0;
    return dead_len;
}

/* --- 저절로 치우기 ---------------------------------------------------------
 * 참조 세기만으로는 서로를 붙든 값(순환 참조)이 영영 안 없어집니다.  예전에는
 * 사람이 gc() 를 불러야만 치웠고, 그래서 오래 도는 프로그램은 그냥 샜습니다
 * (순환 100 만 개 = 795MB).  이제는 만든 개수를 세다가 한 번씩 치웁니다.
 *
 * 문턱은 '새로 만든 그릇 - 없앤 그릇'입니다.  순환이 없으면 참조 세기가 그때그때
 * 없애 주어 이 수가 안 늘고, 그러면 수집기는 아예 안 돕니다.  순환이 쌓일 때만
 * 값을 치릅니다.  파이썬의 세대별 수집기와 같은 생각이고, 세대는 안 나눴습니다
 * (ponytail: 한 세대로 충분한지 재 보고 모자라면 그때 나누세요).
 *
 * 검사(LUMI_GC_CHECK)는 lumi.h 에 매크로로 두었습니다 — 문장마다 함수를 부르게
 * 했더니 그것만으로 10% 가 느려졌습니다. */

/* ---------------- 환경 ---------------- */

Env *env_new(Env *parent)
{
    Env *e = (Env *)xmalloc(sizeof(Env));
    memset(e, 0, sizeof(Env));
    e->rc = 1;
    e->parent = parent ? env_retain(parent) : NULL;
    return e;
}

Env *env_retain(Env *e) { if (e) e->rc++; return e; }

void env_release(Env *e)
{
    if (!e) return;
    if (--e->rc > 0) return;
    for (size_t i = 0; i < e->len; i++) {
        free(e->slots[i].name);
        release(e->slots[i].val);
    }
    free(e->slots);
    env_release(e->parent);
    free(e);
}

Value *env_find(Env *e, const char *name)
{
    for (size_t i = 0; i < e->len; i++)
        if (strcmp(e->slots[i].name, name) == 0) return &e->slots[i].val;
    return NULL;
}

Value *env_lookup(Env *e, const char *name, Env **owner)
{
    for (; e; e = e->parent) {
        for (size_t i = 0; i < e->len; i++) {
            if (strcmp(e->slots[i].name, name) == 0) {
                if (owner) *owner = e;
                return &e->slots[i].val;
            }
        }
    }
    return NULL;
}

void env_declare(Env *e, const char *name, Value v, const char *type)
{
    for (size_t i = 0; i < e->len; i++) {
        if (strcmp(e->slots[i].name, name) == 0) {
            release(e->slots[i].val);
            e->slots[i].val = v;
            e->slots[i].type = type;   /* val 로 다시 선언하면 자료형이 사라집니다 */
            return;
        }
    }
    if (e->len == e->cap) {
        e->cap = e->cap ? e->cap * 2 : 8;
        e->slots = (struct EnvSlot *)xrealloc(e->slots, e->cap * sizeof(*e->slots));
    }
    e->slots[e->len].name = xstrdup(name);
    e->slots[e->len].val = v;
    e->slots[e->len].type = type;
    e->len++;
}

const char *env_declared_type(Env *e, const char *name)
{
    for (; e; e = e->parent)
        for (size_t i = 0; i < e->len; i++)
            if (strcmp(e->slots[i].name, name) == 0) return e->slots[i].type;
    return NULL;
}
