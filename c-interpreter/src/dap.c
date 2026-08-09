/* dap.c — Lumi 디버거 (Debug Adapter Protocol)
 * ==========================================================
 *   lumi dap
 * 로 켜면 표준입출력으로 편집기(VS Code 등)와 이야기합니다.
 * 봉투는 LSP 와 똑같은 "Content-Length: N\r\n\r\n{json}" 이라,
 * 주고받는 배관은 lsp.c 와 같은 모양으로 적었습니다.
 *
 * 해 주는 일:
 *   중단점 · 이어 하기 · 한 줄씩(넘어가기/들어가기/나가기)
 *   호출 자취 · 변수 들여다보기(펼치기까지) · 식 값 보기
 *
 * **어떻게 멈추나**: interp.c 의 `lumi_debug_hook` 이 문장 하나를 실행하기
 * 직전마다 불립니다.  멈출 자리면 그 안에서 편집기와 이야기하는 작은
 * 되돌이를 돌고, '이어 하기' 를 받으면 갈고리에서 빠져나옵니다.
 * 그래서 **프로그램은 디버거와 같은 실 위에서** 돕니다 — 실이 하나뿐입니다.
 *
 * ponytail: 그래서 '돌고 있는 도중에 멈춤 누르기'(pause)는 안 됩니다.
 *   멈춰 있을 때만 편집기의 말을 듣습니다.  진짜로 필요해지면 읽는 쪽을
 *   따로 실 하나에 태우세요 — 그전에는 실 하나가 훨씬 안전합니다.
 * ponytail: 프로그램의 input() 은 EOF 입니다 — 표준입력이 편집기와 이야기하는
 *   통로라 나눠 쓸 수 없습니다.  입력이 필요한 프로그램은 터미널에서 돌리세요.
 */
#include "lumi.h"
#include "platform.h"

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/* ============================================================
 * 1. 주고받기 (JSON 은 인터프리터 것을 빌려 씁니다)
 * ============================================================ */

static Interp *g_json;             /* 봉투 안의 JSON 을 읽고 쓰는 데만 씁니다 */
static Interp *g_prog;             /* 사용자의 프로그램을 돌리는 인터프리터 */
static long    g_seq = 1;

static void sink_out(const char *t) { (void)t; }
static char *sink_in(const char *p) { (void)p; return NULL; }

static Value obj_get(Value v, const char *key)
{
    if (v.kind != V_DICT) return NONE_VAL;
    Value k = str_value(key);
    Value *found = dict_find(AS_DICT(v), k);
    release(k);
    return found ? *found : NONE_VAL;
}

static char *obj_dup(Value v, const char *key)      /* malloc, 없으면 NULL */
{
    Value f = obj_get(v, key);
    return f.kind == V_STR ? str_to_utf8(AS_STR(f)) : NULL;
}

static long obj_int(Value v, const char *key, long dflt)
{
    Value f = obj_get(v, key);
    if (f.kind == V_INT) return (long)f.as.i;
    if (f.kind == V_FLOAT) return (long)f.as.f;
    return dflt;
}

static bool obj_bool(Value v, const char *key)
{
    Value f = obj_get(v, key);
    return f.kind == V_BOOL && f.as.b;
}

static void put(Dict *d, const char *key, Value v) { dict_set(d, str_value(key), v); }

static void send_value(Value v)
{
    Value txt = lumi_json_stringify_value(g_json, v);
    char *u = str_to_utf8(AS_STR(txt));
    release(txt);
    printf("Content-Length: %zu\r\n\r\n%s", strlen(u), u);
    fflush(stdout);
    free(u);
}

/* 물음에 답하기.  body 가 NONE 이면 몸통 없이 보냅니다. */
static void send_response(long request_seq, const char *command, Value body)
{
    Dict *m = dict_new();
    put(m, "seq", int_val(g_seq++));
    put(m, "type", str_value("response"));
    put(m, "request_seq", int_val(request_seq));
    put(m, "success", bool_val(true));
    put(m, "command", str_value(command));
    if (body.kind != V_NONE) put(m, "body", body);
    Value v = obj_val(m);
    send_value(v);
    release(v);
}

static void send_error(long request_seq, const char *command, const char *why)
{
    Dict *m = dict_new();
    put(m, "seq", int_val(g_seq++));
    put(m, "type", str_value("response"));
    put(m, "request_seq", int_val(request_seq));
    put(m, "success", bool_val(false));
    put(m, "command", str_value(command));
    put(m, "message", str_value(why));
    Value v = obj_val(m);
    send_value(v);
    release(v);
}

static void send_event(const char *event, Value body)
{
    Dict *m = dict_new();
    put(m, "seq", int_val(g_seq++));
    put(m, "type", str_value("event"));
    put(m, "event", str_value(event));
    if (body.kind != V_NONE) put(m, "body", body);
    Value v = obj_val(m);
    send_value(v);
    release(v);
}

static void send_output(const char *category, const char *text)
{
    Dict *b = dict_new();
    put(b, "category", str_value(category));
    put(b, "output", str_value(text));
    send_event("output", obj_val(b));
}

/* 프로그램이 찍는 것은 그대로 편집기 콘솔로 흘려보냅니다 */
static void prog_out(const char *t) { send_output("stdout", t); }

static char *read_message(void)
{
    char line[512];
    long len = -1;
    for (;;) {
        if (!fgets(line, sizeof line, stdin)) return NULL;
        if (line[0] == '\r' || line[0] == '\n') break;
        if (strncmp(line, "Content-Length:", 15) == 0) len = strtol(line + 15, NULL, 10);
    }
    if (len <= 0) return NULL;
    char *buf = (char *)xmalloc((size_t)len + 1);
    size_t got = fread(buf, 1, (size_t)len, stdin);
    buf[got] = 0;
    return buf;
}

/* ============================================================
 * 2. 중단점
 * ============================================================ */

/* 파일은 **이름만** 견줍니다 (폴더는 빼고, 대소문자 무시).
 * 편집기가 주는 경로와 우리가 아는 경로가 늘 같은 모양이 아니기 때문입니다.
 * ponytail: 같은 이름의 파일이 여러 폴더에 있으면 둘 다 멈춥니다.
 *           그게 문제가 될 만큼 큰 프로젝트가 생기면 그때 온전한 경로로 견주세요. */
typedef struct { char *file; int line; } Bp;

static Bp   *g_bps;
static size_t g_nbps, g_cbps;

static const char *base_name(const char *path)
{
    if (!path) return "";
    const char *s = path;
    for (const char *p = path; *p; p++)
        if (*p == '/' || *p == '\\') s = p + 1;
    return s;
}

static bool same_file(const char *a, const char *b)
{
    a = base_name(a); b = base_name(b);
    for (; *a && *b; a++, b++) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
        if (x != y) return false;
    }
    return *a == *b;
}

static void bps_clear_file(const char *file)
{
    size_t w = 0;
    for (size_t i = 0; i < g_nbps; i++) {
        if (same_file(g_bps[i].file, file)) { free(g_bps[i].file); continue; }
        g_bps[w++] = g_bps[i];
    }
    g_nbps = w;
}

static void bps_add(const char *file, int line)
{
    if (g_nbps == g_cbps) {
        g_cbps = g_cbps ? g_cbps * 2 : 16;
        g_bps = (Bp *)xrealloc(g_bps, g_cbps * sizeof(Bp));
    }
    g_bps[g_nbps].file = xstrdup(file);
    g_bps[g_nbps].line = line;
    g_nbps++;
}

static bool bp_hit(const char *file, int line)
{
    for (size_t i = 0; i < g_nbps; i++)
        if (g_bps[i].line == line && same_file(g_bps[i].file, file)) return true;
    return false;
}

/* ============================================================
 * 3. 변수 손잡이
 * ============================================================ */

/* 편집기는 '이 변수를 펼쳐 줘' 라고 번호로 물어 옵니다.  그 번호가 무엇이었는지
 * 여기에 적어 둡니다.  프로그램이 멈출 때마다 비웁니다 (번호는 그때까지만 뜻이 있습니다). */
typedef struct {
    enum { H_ENV, H_GLOBALS, H_VALUE } kind;
    Env  *env;          /* H_ENV: 여기서부터 globals 전까지 */
    Value value;        /* H_VALUE: 세어 둔 참조 */
} Handle;

static Handle *g_handles;
static size_t  g_nhandles, g_chandles;

static void handles_clear(void)
{
    for (size_t i = 0; i < g_nhandles; i++)
        if (g_handles[i].kind == H_VALUE) release(g_handles[i].value);
    g_nhandles = 0;
}

static long handle_new(int kind, Env *env, Value v)
{
    if (g_nhandles == g_chandles) {
        g_chandles = g_chandles ? g_chandles * 2 : 32;
        g_handles = (Handle *)xrealloc(g_handles, g_chandles * sizeof(Handle));
    }
    g_handles[g_nhandles].kind  = (int)kind;
    g_handles[g_nhandles].env   = env;
    g_handles[g_nhandles].value = kind == H_VALUE ? retain(v) : NONE_VAL;
    g_nhandles++;
    return (long)g_nhandles;                 /* 1 부터 셉니다 (0 = 못 펼침) */
}

/* 펼칠 수 있는 값이면 손잡이를, 아니면 0 */
static long expandable(Value v)
{
    switch (v.kind) {
    case V_LIST: case V_TUPLE:
        return AS_SEQ(v)->len ? handle_new(H_VALUE, NULL, v) : 0;
    case V_DICT:
        return AS_DICT(v)->len ? handle_new(H_VALUE, NULL, v) : 0;
    case V_INSTANCE:
        return handle_new(H_VALUE, NULL, v);
    default:
        return 0;
    }
}

static Value var_entry(const char *name, Value v)
{
    Str *s = value_repr(g_prog, v);
    char *u = str_to_utf8(s);
    release(obj_val(s));
    Dict *d = dict_new();
    put(d, "name", str_value(name));
    put(d, "value", str_value(u));
    put(d, "type", str_value(type_name_of(v)));
    put(d, "variablesReference", int_val(expandable(v)));
    free(u);
    return obj_val(d);
}

/* ============================================================
 * 4. 멈춤과 이어 하기
 * ============================================================ */

typedef enum { RUN_GO, RUN_STEP_IN, RUN_STEP_OVER, RUN_STEP_OUT, RUN_STOP } RunMode;

static RunMode g_mode = RUN_GO;
static int     g_step_depth = 0;         /* 넘어가기·나가기를 잴 기준 깊이 */
static bool    g_stopped;                /* 지금 멈춰 있습니까 (되돌이 안입니까) */
static bool    g_quit;                   /* disconnect/terminate 를 받았습니까 */
static bool    g_stop_on_entry;
static bool    g_launched, g_configured;
static char   *g_program;                /* 돌릴 .lumi 경로 */

static const char *frame_file(Interp *in, int depth)
{
    const char *f = in->frames[depth].file;
    return f ? f : (in->cur_file ? in->cur_file : "");
}

static void handle_message(char *body);       /* 아래에 있습니다 */

/* 멈춘 채로 편집기 말을 듣습니다.  이어 하기·한 줄씩을 받으면 돌아갑니다. */
static void debug_loop(const char *reason)
{
    handles_clear();
    Dict *b = dict_new();
    put(b, "reason", str_value(reason));
    put(b, "threadId", int_val(1));
    put(b, "allThreadsStopped", bool_val(true));
    send_event("stopped", obj_val(b));

    g_stopped = true;
    while (g_stopped) {
        char *msg = read_message();
        if (!msg) { g_quit = true; g_mode = RUN_STOP; break; }
        handle_message(msg);
    }
}

/* 문장 하나를 실행하기 직전마다 불립니다 */
static void debug_hook(Interp *in, Node *n, Env *env)
{
    /* 지금 겹이 어디까지 왔는지 적어 둡니다 — 호출 자취와 변수 보기가 이것을 봅니다 */
    in->frames[in->depth].line = n->line;
    in->frames[in->depth].env  = env;

    if (g_mode == RUN_STOP) return;

    bool stop = false;
    switch (g_mode) {
    case RUN_STEP_IN:   stop = true; break;
    case RUN_STEP_OVER: stop = in->depth <= g_step_depth; break;
    case RUN_STEP_OUT:  stop = in->depth <  g_step_depth; break;
    default: break;
    }
    if (!stop && bp_hit(frame_file(in, in->depth), n->line)) stop = true;
    if (!stop) return;

    debug_loop(g_mode == RUN_GO ? "breakpoint" : "step");
}

/* ============================================================
 * 5. 물음에 답하기
 * ============================================================ */

static Value make_capabilities(void)
{
    Dict *c = dict_new();
    put(c, "supportsConfigurationDoneRequest", bool_val(true));
    put(c, "supportsEvaluateForHovers", bool_val(true));
    put(c, "supportsTerminateRequest", bool_val(true));
    put(c, "supportsStepInTargetsRequest", bool_val(false));
    return obj_val(c);
}

static Value make_source(const char *path)
{
    Dict *s = dict_new();
    put(s, "name", str_value(base_name(path)));
    put(s, "path", str_value(path));
    return obj_val(s);
}

static Value make_stack_trace(void)
{
    Seq *frames = seq_new(V_LIST);
    if (g_prog) {
        for (int d = g_prog->depth; d >= 0; d--) {
            CallFrame *f = &g_prog->frames[d];
            Dict *fr = dict_new();
            put(fr, "id", int_val(d));
            put(fr, "name", str_value(f->fn_name ? f->fn_name : "<main>"));
            put(fr, "source", make_source(frame_file(g_prog, d)));
            put(fr, "line", int_val(f->line > 0 ? f->line : 1));
            put(fr, "column", int_val(1));
            seq_push(frames, obj_val(fr));
        }
    }
    long total = (long)frames->len;
    Dict *b = dict_new();
    put(b, "stackFrames", obj_val(frames));
    put(b, "totalFrames", int_val(total));
    return obj_val(b);
}

static Value make_scopes(long frame_id)
{
    Env *env = NULL;
    if (g_prog && frame_id >= 0 && frame_id <= g_prog->depth)
        env = g_prog->frames[frame_id].env;

    Seq *out = seq_new(V_LIST);

    Dict *local = dict_new();
    put(local, "name", str_value("Locals"));
    put(local, "variablesReference", int_val(env ? handle_new(H_ENV, env, NONE_VAL) : 0));
    put(local, "expensive", bool_val(false));
    seq_push(out, obj_val(local));

    Dict *glob = dict_new();
    put(glob, "name", str_value("Globals"));
    put(glob, "variablesReference", int_val(handle_new(H_GLOBALS, NULL, NONE_VAL)));
    put(glob, "expensive", bool_val(false));
    seq_push(out, obj_val(glob));

    Dict *b = dict_new();
    put(b, "scopes", obj_val(out));
    return obj_val(b);
}

/* 한 환경(과 그 위로 globals 전까지)의 이름들.  안쪽 것이 바깥 것을 가립니다. */
static void env_into(Seq *out, Env *e, Env *stop_at)
{
    for (; e && e != stop_at; e = e->parent) {
        for (size_t i = 0; i < e->len; i++) {
            const char *nm = e->slots[i].name;
            if (strcmp(nm, "super") == 0) continue;      /* 사람이 볼 것이 없습니다 */
            bool shadowed = false;
            for (size_t k = 0; k < out->len && !shadowed; k++) {
                Value ent = out->items[k];
                Value got = obj_get(ent, "name");
                if (got.kind == V_STR) {
                    char *u = str_to_utf8(AS_STR(got));
                    shadowed = strcmp(u, nm) == 0;
                    free(u);
                }
            }
            if (!shadowed) seq_push(out, var_entry(nm, e->slots[i].val));
        }
    }
}

static Value make_variables(long ref)
{
    Seq *out = seq_new(V_LIST);
    if (ref >= 1 && (size_t)ref <= g_nhandles && g_prog) {
        Handle *h = &g_handles[ref - 1];
        switch (h->kind) {
        case H_ENV:     env_into(out, h->env, g_prog->globals); break;
        case H_GLOBALS: env_into(out, g_prog->globals, NULL);   break;
        case H_VALUE: {
            Value v = h->value;
            if (v.kind == V_LIST || v.kind == V_TUPLE) {
                Seq *s = AS_SEQ(v);
                for (size_t i = 0; i < s->len; i++) {
                    char nm[32];
                    snprintf(nm, sizeof nm, "%zu", i);
                    seq_push(out, var_entry(nm, s->items[i]));
                }
            } else if (v.kind == V_DICT) {
                Dict *d = AS_DICT(v);
                for (size_t i = 0; i < d->len; i++) {
                    Str *ks = value_to_str(g_prog, d->e[i].key);
                    char *u = str_to_utf8(ks);
                    release(obj_val(ks));
                    seq_push(out, var_entry(u, d->e[i].val));
                    free(u);
                }
            } else if (v.kind == V_INSTANCE) {
                Env *fields = AS_INST(v)->fields;
                for (size_t i = 0; i < fields->len; i++)
                    seq_push(out, var_entry(fields->slots[i].name, fields->slots[i].val));
            }
            break;
        }
        }
    }
    Dict *b = dict_new();
    put(b, "variables", obj_val(out));
    return obj_val(b);
}

/* 식 하나를 그 겹의 환경에서 셈합니다.  오류는 글자로 돌려줍니다. */
static Value make_evaluate(const char *expr, long frame_id, bool *ok)
{
    *ok = false;
    Dict *b = dict_new();
    if (!g_prog || !expr || !*expr) { put(b, "result", str_value("")); return obj_val(b); }

    Env *env = (frame_id >= 0 && frame_id <= g_prog->depth)
             ? g_prog->frames[frame_id].env : g_prog->globals;
    if (!env) env = g_prog->globals;

    jmp_buf saved;
    memcpy(saved, lumi_jmp, sizeof saved);
    if (setjmp(lumi_jmp) != 0) {
        memcpy(lumi_jmp, saved, sizeof saved);
        put(b, "result", str_value(lumi_err_msg));
        put(b, "variablesReference", int_val(0));
        return obj_val(b);
    }
    TokenList *toks = tokenize(expr);
    NodeList prog = parse_program(toks);
    tokenlist_free(toks);
    if (prog.len != 1) {
        memcpy(lumi_jmp, saved, sizeof saved);
        free(prog.items);
        put(b, "result", str_value("write one expression here"));
        put(b, "variablesReference", int_val(0));
        return obj_val(b);
    }
    Value v = lumi_eval_expr(g_prog, prog.items[0], env);
    memcpy(lumi_jmp, saved, sizeof saved);
    free(prog.items);                       /* ponytail: 노드는 그대로 둡니다 —
                                             * 이 인터프리터에는 노드를 놓아 주는 곳이 없습니다 */
    Str *s = value_repr(g_prog, v);
    char *u = str_to_utf8(s);
    release(obj_val(s));
    put(b, "result", str_value(u));
    put(b, "type", str_value(type_name_of(v)));
    put(b, "variablesReference", int_val(expandable(v)));
    free(u);
    release(v);
    *ok = true;
    return obj_val(b);
}

/* ============================================================
 * 6. 프로그램 돌리기
 * ============================================================ */

/* 경로에서 폴더만 (bring 이 라이브러리를 찾는 기준).  main.c 것과 같은 규칙입니다. */
static char *dir_of(const char *path)
{
    const char *s = base_name(path);
    if (s == path) return xstrdup(".");
    size_t n = (size_t)(s - path) - 1;
    char *d = (char *)xmalloc(n + 1);
    memcpy(d, path, n);
    d[n] = 0;
    return d;
}

static void run_program(void)
{
    char *code = read_text_file(g_program);
    if (!code) {
        char *m = xsprintf("File not found: %s\n", g_program);
        send_output("stderr", m);
        free(m);
        send_event("terminated", NONE_VAL);
        return;
    }
    char *base = dir_of(g_program);
    g_prog = interp_new(prog_out, sink_in, base);
    interp_set_script(g_prog, g_program);
    free(base);

    g_mode = g_stop_on_entry ? RUN_STEP_IN : RUN_GO;
    lumi_debug_hook = debug_hook;

    jmp_buf saved;
    memcpy(saved, lumi_jmp, sizeof saved);
    if (setjmp(lumi_jmp) == 0) {
        interp_run(g_prog, code);
    } else {
        char *m = xsprintf("\nError: %s\n", lumi_err_msg);
        send_output("stderr", m);
        free(m);
        if (lumi_err_trace[0]) send_output("stderr", lumi_err_trace);
    }
    memcpy(lumi_jmp, saved, sizeof saved);

    lumi_debug_hook = NULL;
    handles_clear();
    free(code);
    send_event("exited", NONE_VAL);
    send_event("terminated", NONE_VAL);
}

/* ============================================================
 * 7. 메시지 한 통
 * ============================================================ */

static void handle_message(char *body)
{
    Value msg = NONE_VAL;
    jmp_buf saved;
    memcpy(saved, lumi_jmp, sizeof saved);
    if (setjmp(lumi_jmp) == 0) {
        Value txt = str_value(body);
        msg = lumi_json_parse_value(g_json, txt);
        release(txt);
    } else {
        memcpy(lumi_jmp, saved, sizeof saved);
        free(body);
        return;                              /* 깨진 JSON 은 그 덩어리만 버립니다 */
    }
    memcpy(lumi_jmp, saved, sizeof saved);
    free(body);

    char *type = obj_dup(msg, "type");
    char *cmd  = obj_dup(msg, "command");
    long  seq  = obj_int(msg, "seq", 0);
    Value args = obj_get(msg, "arguments");

    if (!type || !cmd || strcmp(type, "request") != 0) {
        free(type); free(cmd); release(msg);
        return;
    }

    if (strcmp(cmd, "initialize") == 0) {
        send_response(seq, cmd, make_capabilities());
        send_event("initialized", NONE_VAL);

    } else if (strcmp(cmd, "launch") == 0) {
        free(g_program);
        g_program = obj_dup(args, "program");
        g_stop_on_entry = obj_bool(args, "stopOnEntry");
        if (!g_program) send_error(seq, cmd, "launch needs a 'program' path");
        else { send_response(seq, cmd, NONE_VAL); g_launched = true; }

    } else if (strcmp(cmd, "setBreakpoints") == 0) {
        Value src = obj_get(args, "source");
        char *path = obj_dup(src, "path");
        Value lines = obj_get(args, "breakpoints");
        Seq *out = seq_new(V_LIST);
        if (path) bps_clear_file(path);
        if (path && lines.kind == V_LIST) {
            Seq *s = AS_SEQ(lines);
            for (size_t i = 0; i < s->len; i++) {
                long ln = obj_int(s->items[i], "line", 0);
                if (ln > 0) bps_add(path, (int)ln);
                Dict *d = dict_new();
                put(d, "verified", bool_val(ln > 0));
                put(d, "line", int_val(ln));
                seq_push(out, obj_val(d));
            }
        }
        Dict *b = dict_new();
        put(b, "breakpoints", obj_val(out));
        send_response(seq, cmd, obj_val(b));
        free(path);

    } else if (strcmp(cmd, "configurationDone") == 0) {
        send_response(seq, cmd, NONE_VAL);
        g_configured = true;

    } else if (strcmp(cmd, "threads") == 0) {
        Dict *t = dict_new();
        put(t, "id", int_val(1));
        put(t, "name", str_value("lumi"));
        Seq *list = seq_new(V_LIST);
        seq_push(list, obj_val(t));
        Dict *b = dict_new();
        put(b, "threads", obj_val(list));
        send_response(seq, cmd, obj_val(b));

    } else if (strcmp(cmd, "stackTrace") == 0) {
        send_response(seq, cmd, make_stack_trace());

    } else if (strcmp(cmd, "scopes") == 0) {
        send_response(seq, cmd, make_scopes(obj_int(args, "frameId", 0)));

    } else if (strcmp(cmd, "variables") == 0) {
        send_response(seq, cmd, make_variables(obj_int(args, "variablesReference", 0)));

    } else if (strcmp(cmd, "evaluate") == 0) {
        char *expr = obj_dup(args, "expression");
        bool ok = false;
        Value b = make_evaluate(expr, obj_int(args, "frameId", -1), &ok);
        send_response(seq, cmd, b);
        free(expr);

    } else if (strcmp(cmd, "continue") == 0) {
        Dict *b = dict_new();
        put(b, "allThreadsContinued", bool_val(true));
        send_response(seq, cmd, obj_val(b));
        g_mode = RUN_GO;
        g_stopped = false;

    } else if (strcmp(cmd, "next") == 0 || strcmp(cmd, "stepIn") == 0
               || strcmp(cmd, "stepOut") == 0) {
        send_response(seq, cmd, NONE_VAL);
        g_step_depth = g_prog ? g_prog->depth : 0;
        g_mode = strcmp(cmd, "next") == 0   ? RUN_STEP_OVER
               : strcmp(cmd, "stepIn") == 0 ? RUN_STEP_IN : RUN_STEP_OUT;
        g_stopped = false;

    } else if (strcmp(cmd, "pause") == 0) {
        /* 돌고 있는 도중에는 못 듣습니다 (파일 맨 위 ponytail 참고) */
        send_error(seq, cmd, "lumi dap can only listen while it is stopped");

    } else if (strcmp(cmd, "disconnect") == 0 || strcmp(cmd, "terminate") == 0) {
        send_response(seq, cmd, NONE_VAL);
        g_quit = true;
        g_mode = RUN_STOP;
        g_stopped = false;

    } else {
        send_response(seq, cmd, NONE_VAL);       /* 모르는 물음에는 빈 답 */
    }

    free(type);
    free(cmd);
    release(msg);
}

int run_dap(void)
{
    g_json = interp_new(sink_out, sink_in, ".");

    while (!g_quit) {
        char *body = read_message();
        if (!body) break;
        handle_message(body);

        if (g_launched && g_configured && !g_quit) {
            g_launched = g_configured = false;
            run_program();
            break;                                /* 한 번 돌리면 끝입니다 */
        }
    }

    /* 편집기가 끊을 때까지 남은 말은 흘려보냅니다 */
    while (!g_quit) {
        char *body = read_message();
        if (!body) break;
        handle_message(body);
    }
    return 0;
}
