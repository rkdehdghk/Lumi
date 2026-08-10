/* Lumi 언어 인터프리터 (C) — 공용 선언
 * ==========================================================
 * interpreter.py 를 그대로 옮긴 것입니다. 실행 과정은 같습니다:
 *   1) 렉서 : 소스코드 -> 토큰 (NEWLINE/INDENT/DEDENT 포함)
 *   2) 파서 : 토큰 -> AST
 *   3) 인터프리터 : AST 를 따라가며 실행
 *
 * 문자열은 파이썬처럼 '코드포인트'의 나열입니다(UTF-32).  그래서
 * len("한글") 은 2 이고 s[0] 은 "한" 입니다.  파일 입출력에서만
 * UTF-8 로 바꿉니다.
 */
#ifndef LUMI_H
#define LUMI_H

#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>          /* 열어 둔 파일 값(FileObj) 이 FILE* 를 담습니다 */

/* ============================================================
 * 0. 값 (Value)
 * ============================================================ */

/* 이 언어의 판 번호.  적는 곳은 여기 하나뿐입니다 (예전에 셋으로 흩어져 있었습니다).
 * 0.x 인 동안은 문법이 바뀔 수 있습니다 — 규칙은 docs/Lumi_Language_Spec.md 19장. */
#define LUMI_VERSION "0.9.1"

typedef enum {
    V_NONE, V_BOOL, V_INT, V_FLOAT,          /* 그 자리에 담는 값 */
    V_STR, V_LIST, V_TUPLE, V_DICT, V_BYTES, /* 힙에 담는 값 */
    V_FUNC, V_MODULE, V_CLASS, V_INSTANCE, V_BOUND, V_SUPER,
    V_FILE,                                  /* open() 이 돌려주는 '열어 둔 파일' */
    V_ERROR,                                 /* catch 가 잡아 주는 '난 오류' */
    V_TASK                                   /* start() 가 돌려주는 '따로 도는 일감' */
} VKind;

/* 실마다 따로 두는 값 (일감이 저마다의 오류 자리를 갖도록).
 * ponytail: C11 _Thread_local 대신 컴파일러 확장을 씁니다 — MSVC 의 C 모드에서
 *           _Thread_local 이 판마다 들쭉날쭉해서, 두 줄이 가장 튼튼합니다. */
#if defined(_MSC_VER)
#  define LUMI_TLS __declspec(thread)
#else
#  define LUMI_TLS __thread
#endif

/* 붙여 넣지 말라고 컴파일러에게 이르는 표시.
 * 재귀를 타는 함수(exec_node / eval_call)는 갈래마다의 지역 변수를 **한 칸에 모아**
 * 잡습니다 — 안 쓰는 갈래의 버퍼까지 겹마다 쌓입니다.  그래서 큰 버퍼를 쓰는 갈래를
 * 따로 떼어 두는데, 한 번만 부르는 함수라 그냥 두면 컴파일러가 도로 붙여 버립니다.
 * 자세한 것은 docs/Lumi_Changes.md 의 '재귀 깊이' 항목에 적어 두었습니다. */
#if defined(_MSC_VER)
#  define LUMI_NOINLINE __declspec(noinline)
#else
#  define LUMI_NOINLINE __attribute__((noinline))
#endif

typedef struct Obj {
    int rc;
    VKind kind;
    struct Obj *gc_next;
    struct Obj *gc_prev;
    int gc_refs;
    bool gc_tracked;
    bool gc_visited;   /* 이번 수집에서 '살아 있다'고 훑고 지나갔는가.
                        * gc_refs 로 대신하면 안 됩니다 — 밖에서 붙들려 있는 값은
                        * 처음부터 gc_refs 가 0 이 아니라서, 그것을 '이미 봤다'로
                        * 읽으면 그 **속에 든 값들을 안 훑고** 지나갑니다. */
} Obj;

typedef struct Value {
    VKind kind;
    union {
        bool      b;
        long long i;
        double    f;
        Obj      *o;
    } as;
} Value;

/* --- 힙 객체들 --- */
typedef struct Str   { Obj h; uint32_t *cp; size_t len; } Str;   /* 코드포인트 배열 */
typedef struct Seq   { Obj h; Value *items; size_t len, cap; } Seq; /* list / tuple */
typedef struct BytesObj { Obj h; uint8_t *data; size_t len; } BytesObj;

typedef struct DictEntry { uint32_t hash; Value key, val; } DictEntry;
/* 콤팩트 순서 보장 해시 테이블:
 * e: 입력 순서대로 보관하는 밀집 배열 (Dense Array)
 * htable: 해시 버킷 테이블 (인덱스 저장, -1: 빈칸, -2: 삭제된 칸) */
typedef struct Dict  { Obj h; DictEntry *e; int32_t *htable; size_t len, cap, hcap; } Dict;

typedef struct Env Env;
typedef struct FuncDefNode FuncDefNode;
typedef struct ClassDefNode ClassDefNode;

/* file 은 이 함수가 '적혀 있는' 파일입니다 (스택 트레이스용).
 * Interp 가 들고 있는 문자열을 가리키기만 하므로 따로 free 하지 않습니다. */
typedef struct Func     { Obj h; FuncDefNode *node; Env *closure; const char *file; } Func;
typedef struct Module   { Obj h; char *name; Env *vars; } Module;

typedef struct LumiClass {
    Obj h;
    char *name;
    struct LumiClass *parent;   /* 강한 참조 */
    Env *methods;               /* 이름 -> Func */
    Env *vars;                  /* 공유 값 (선언 자료형은 Env 의 type 칸에) */
    Env *closure;
} LumiClass;

/* 열어 둔 파일.  close 하면 fp 가 NULL 이 되고, 닫는 것을 잊어도
 * 값이 사라질 때 obj_free 가 대신 닫아 줍니다. */
typedef struct FileObj {
    Obj h;
    FILE *fp;
    char *path;
    const char *codec;      /* encoding_canonical 이 준 고정 문자열 */
    bool writing;           /* write/append 로 열었는가 */
    bool appending;         /* append 로 열었는가 (자리를 옮길 수 없습니다) */
} FileObj;

/* 난 오류 하나.  catch 가 이 값을 건네줍니다.
 *   type     오류 종류 이름 ("FileError" 처럼). 없으면 "Error"
 *   message  사람이 읽는 설명 (줄 번호는 빼고)
 *   line     난 줄, file 난 파일 (모르면 NO_LINE / NULL) */
typedef struct ErrorObj { Obj h; char *type; Str *message; int line; char *file; } ErrorObj;

/* 따로 도는 일감 하나 (start / wait).
 *   fn/args   무엇을 어떤 값으로 부를지 (일감이 세어 들고 있습니다)
 *   in        이 일감만의 인터프리터.  globals 는 부모와 **함께 씁니다**
 *   err/kind  실패했으면 그 말 — wait 이 부르는 쪽에서 다시 냅니다 */
typedef struct TaskObj {
    Obj    h;
    void  *thread;
    Value  fn;
    Value *args;
    size_t nargs;
    Value  result;
    struct Interp *in;
    bool   joined, failed;
    int    err_line;
    char   err[1024];
    char   kind[64];
} TaskObj;

typedef struct Instance { Obj h; LumiClass *cls; Env *fields; } Instance;
typedef struct Bound    { Obj h; Func *func; Instance *inst; LumiClass *owner; } Bound;
typedef struct SuperRef { Obj h; LumiClass *start; Instance *inst; } SuperRef;

/* --- 값 만들기 --- */
#define NONE_VAL   ((Value){V_NONE,  {0}})
Value bool_val(bool b);
Value int_val(long long i);
Value float_val(double f);
Value obj_val(void *o);          /* Obj* 를 그대로 담습니다 (소유권 이동) */

#define IS_NUM(v)  ((v).kind == V_INT || (v).kind == V_FLOAT)
#define AS_STR(v)   ((Str*)(v).as.o)
#define AS_SEQ(v)   ((Seq*)(v).as.o)
#define AS_DICT(v)  ((Dict*)(v).as.o)
#define AS_BYTES(v) ((BytesObj*)(v).as.o)
#define AS_FUNC(v)  ((Func*)(v).as.o)
#define AS_MOD(v)   ((Module*)(v).as.o)
#define AS_CLASS(v) ((LumiClass*)(v).as.o)
#define AS_INST(v)  ((Instance*)(v).as.o)
#define AS_BOUND(v) ((Bound*)(v).as.o)
#define AS_SUPER(v) ((SuperRef*)(v).as.o)
#define AS_FILE(v)  ((FileObj*)(v).as.o)
#define AS_ERR(v)   ((ErrorObj*)(v).as.o)
#define AS_TASK(v)  ((TaskObj*)(v).as.o)

Value retain(Value v);
void  release(Value v);

/* --- 문자열 --- */
Str  *str_new(const uint32_t *cp, size_t len);   /* 복사해서 만듭니다 */
Str  *str_from_utf8(const char *s);
Str  *str_from_utf8_n(const char *s, size_t nbytes);
char *str_to_utf8(const Str *s);                 /* malloc: 다 쓰면 free */
Value str_value(const char *utf8);
bool  str_eq(const Str *a, const Str *b);
int   str_cmp(const Str *a, const Str *b);
Str  *str_concat(const Str *a, const Str *b);
Str  *str_slice(const Str *s, size_t start, size_t end);

/* --- 리스트 / 튜플 --- */
Seq  *seq_new(VKind kind);
void  seq_push(Seq *s, Value v);                 /* v 의 소유권을 가져갑니다 */
void  seq_insert(Seq *s, size_t at, Value v);
Value seq_pop(Seq *s);
Seq  *seq_copy(const Seq *s, VKind kind);

/* --- 힙 객체 생성자 --- */
Func      *func_new(FuncDefNode *node, Env *closure, const char *file);
Module    *module_new(const char *name, Env *vars);
LumiClass *class_new(const char *name, LumiClass *parent, Env *methods, Env *vars, Env *closure);
Instance  *instance_new(LumiClass *cls);
Bound     *bound_new(Func *func, Instance *inst, LumiClass *owner);
SuperRef  *super_new(LumiClass *start, Instance *inst);

/* --- 바이트 --- */
BytesObj *bytes_new(const uint8_t *data, size_t len);

/* --- 열어 둔 파일 --- */
FileObj *file_new(FILE *fp, const char *path, const char *codec,
                  bool writing, bool appending);

/* --- 따로 도는 일감 --- */
TaskObj *task_new(void);

/* --- 난 오류 --- */
ErrorObj *error_new(const char *type, const char *message, int line, const char *file);
/* kind 가 want 이거나 want 를 부모로 두고 있습니까? ("Error" 는 모든 것의 부모) */
bool error_kind_is(const char *kind, const char *want);

/* --- 딕셔너리 --- */
Dict  *dict_new(void);
Value *dict_find(Dict *d, Value key);            /* 없으면 NULL */
void   dict_set(Dict *d, Value key, Value val);  /* 둘 다 소유권을 가져갑니다 */
bool   dict_remove(Dict *d, Value key);

/* --- 환경 (변수 이름표) --- */
struct Env {
    int rc;
    Env *parent;                                 /* 강한 참조 */
    struct EnvSlot { char *name; Value val; const char *type; } *slots;
    size_t len, cap;
    /* 순환 수집기가 '이번 바퀴에 이 자리를 이미 훑었나'를 적어 둡니다.
     * 하나의 환경을 여러 값이 함께 쓸 수 있기 때문입니다 — 클래스의 메서드 넷은
     * 같은 closure 하나를 봅니다.  표시가 없으면 그 환경에 든 값을 **넷 번** 빼서
     * 멀쩡히 쓰이는 값이 쓰레기로 보입니다 (value.c 의 gc_visit_env 참고). */
    unsigned gc_seen;
};
Env  *env_new(Env *parent);
Env  *env_retain(Env *e);
void  env_release(Env *e);
Value *env_find(Env *e, const char *name);              /* 이 환경만 */
Value *env_lookup(Env *e, const char *name, Env **owner); /* 부모까지 */
void  env_declare(Env *e, const char *name, Value v, const char *type);
const char *env_declared_type(Env *e, const char *name);

/* ============================================================
 * 1. 오류
 * ============================================================ */

/* LumiError: 메시지와 줄 번호.  longjmp 로 맨 위까지 올라갑니다. */
void lumi_error(int line, const char *fmt, ...);   /* 돌아오지 않습니다 */
/* 종류를 붙여서 내는 오류 — catch 가 그 종류로 골라 잡을 수 있습니다.
 * 종류 이름은 value.c 의 ERROR_PARENT 표를 따릅니다 (없는 이름은 부모가 "Error"). */
void lumi_error_kind(int line, const char *kind, const char *fmt, ...);
#define NO_LINE (-1)

/* 실마다 따로입니다 — 일감이 저마다 제 오류를 들고 있어야 하기 때문입니다 */
extern LUMI_TLS char lumi_err_msg[2048];
extern LUMI_TLS int  lumi_err_line;
extern LUMI_TLS char lumi_err_kind[64];    /* 마지막 오류의 종류 ("Error" 가 기본) */

/* 오류가 난 자리까지 어떻게 왔는지 (lumi_error 가 채웁니다).
 * try 로 잡히면 그냥 버려지고, 아무도 안 잡으면 main 이 메시지 뒤에 붙여 찍습니다.
 * 오류가 난 적이 없으면 첫 글자가 0 입니다. */
extern LUMI_TLS char lumi_err_trace[4096];
extern LUMI_TLS jmp_buf lumi_jmp;

/* ============================================================
 * 2. 유니코드 / 인코딩
 * ============================================================ */
uint32_t *utf8_to_cp(const char *s, size_t nbytes, size_t *out_len);
char     *cp_to_utf8(const uint32_t *cp, size_t len, size_t *out_bytes);
size_t    cp_utf8_size(uint32_t c);

/* 이름 -> 코드페이지 (없으면 NULL) */
const char *encoding_canonical(const char *name);
/* 성공하면 BytesObj*, 실패하면 NULL (담을 수 없는 글자가 있을 때) */
BytesObj *encode_str(const Str *s, const char *codec);
Str      *decode_bytes(const BytesObj *b, const char *codec);

/* ============================================================
 * 2-1. 무늬 찾기 (정규표현식) — regex.c
 * ============================================================ */
typedef struct Regex Regex;
/* 무늬를 읽어 둡니다.  잘못된 무늬면 NULL 을 주고 errbuf 에 까닭을 적습니다. */
Regex *regex_compile(const uint32_t *pat, size_t len, char *errbuf, size_t errn);
void   regex_free(Regex *re);
int    regex_group_count(const Regex *re);
/* from 자리부터 처음 맞는 곳을 찾습니다.  찾으면 true 와 함께
 * starts[0]/ends[0] 에 전체, starts[i]/ends[i] 에 i 번째 묶음 자리를 채웁니다.
 * 안 맞은 묶음은 -1.  두 배열은 (묶음 수 + 1) 칸이 있어야 합니다. */
/* gave_up 이 true 로 오면 무늬가 너무 복잡해 도중에 그만둔 것입니다
 * (못 찾은 것과 다릅니다 — 부르는 쪽에서 오류로 알려 주세요). */
bool   regex_search(const Regex *re, const uint32_t *s, size_t slen, size_t from,
                    long *starts, long *ends, bool *gave_up);

/* ============================================================
 * 3. 렉서
 * ============================================================ */
typedef enum {
    T_NUMBER, T_STRING, T_FSTRING, T_IDENT, T_KEYWORD, T_OP,
    T_NEWLINE, T_INDENT, T_DEDENT, T_EOF,
    T_COMMENT          /* tokenize_keep_source 로 읽을 때만 나옵니다 (lumi fmt 용) */
} TokKind;

typedef struct {
    TokKind kind;
    char   *text;      /* IDENT/KEYWORD/OP: 이름.  COMMENT: 주석 원문.
                        * NUMBER/STRING/FSTRING: 평소엔 안 쓰고,
                        * tokenize_keep_source 로 읽을 때만 원문이 담깁니다. */
    Str    *str;       /* STRING/FSTRING 의 값 */
    bool    is_float;
    long long inum;    /* COMMENT: 이 주석이 시작하는 칸 (바이트 기준) */
    bool    own_line;  /* COMMENT: 이 줄에 주석뿐인가 (코드 뒤에 붙은 것이 아닌가) */
    int     col;       /* 이 토큰이 시작하는 칸 (바이트).  tokenize_keep_source 에서만 */
    double  fnum;
    int     line;
} Token;

typedef struct { Token *items; size_t len, cap; } TokenList;

TokenList *tokenize(const char *source);
/* 주석과 숫자·글자의 **원문 그대로**를 함께 담아 읽습니다 (lumi fmt 전용).
 * 파서에 넘기지 마세요 — T_COMMENT 는 파서가 모르는 토큰입니다. */
TokenList *tokenize_keep_source(const char *source);
void tokenlist_free(TokenList *t);

/* ============================================================
 * 4. AST
 * ============================================================ */
typedef enum {
    N_NUMBER, N_STRING, N_FSTRING, N_BOOL, N_NONE, N_VAR, N_BINOP, N_UNARY,
    N_ASSIGN, N_MULTI, N_CALL, N_CALLV, N_KWARG, N_MEMBER, N_METHODCALL, N_MEMBERASSIGN,
    N_CLASSDEF, N_THIS, N_SUPER, N_IF, N_WHILE, N_SWITCH, N_FUNCDEF,
    N_RETURN, N_BREAK, N_CONTINUE, N_BRING, N_LIST, N_TUPLE, N_DICT,
    N_SLICE, N_INDEX, N_RANGESPEC, N_INDEXASSIGN, N_FORC, N_FORIN, N_FORR,
    N_USE, N_TRY, N_ERROR, N_TEST, N_LIST_COMP, N_DICT_COMP, N_MULTI_ASSIGN
} NodeKind;

typedef struct Node Node;
typedef struct { Node **items; size_t len, cap; } NodeList;
void nodelist_push(NodeList *l, Node *n);

struct Node {
    NodeKind kind;
    int line;
    union {
        struct { bool is_float; long long i; double f; } num;
        struct { Str *value; } str;
        struct { NodeList parts; } fstring;
        struct { bool value; } bool_;
        struct { char *name; } var;
        struct { const char *op; Node *left, *right; } binop;
        struct { const char *op; Node *operand; } unary;
        /* 대입: val x = v / x = v / int x = v / global x = v */
        struct { char *name; Node *value; bool is_let;
                 const char *type_name; const char *scope; } assign;
        struct { char **names; size_t nnames; Node *value; bool is_let;
                 const char *type_name; const char *scope; } multi_assign;
        struct { NodeList body; } multi;
        struct { char *name; NodeList args; } call;
        /* 값을 부르기 — 이름이 아니라 '앞의 식'을 불러냅니다.
         * xs[0](3) / outer(5)(3) / 표[i](x) 처럼 이름을 댈 수 없는 호출에 씁니다.
         * 이름으로 부르는 보통 호출(N_CALL)은 내장 함수 갈래를 그대로 타야 해서 따로 둡니다. */
        struct { Node *callee; NodeList args; } callv;
        /* 이름을 붙여 준 인자 — f(칸 = 3) 의 '칸 = 3' 한 덩어리 */
        struct { char *name; Node *value; } kwarg;
        /* test "이름": ... — lumi test 로 돌릴 때만 실행됩니다 */
        struct { char *name; NodeList body; } test_;
        /* optional: '?.' 로 적었는가 — 앞의 것이 none 이면 none 을 내놓고 멈춥니다 */
        struct { Node *obj; char *name; bool optional; } member;
        struct { Node *obj; char *name; NodeList args; bool optional; } methodcall;
        struct { Node *obj; char *name; Node *value; } memberassign;
        ClassDefNode *classdef;
        struct { Node *cond; NodeList then_body; NodeList else_body;
                 bool has_else; } if_;
        struct { Node *cond; NodeList body; } while_;
        struct { Node *subject;
                 struct SwitchCase { NodeList values; NodeList body; bool is_default; } *cases;
                 size_t ncases;
                 NodeList default_body; bool has_default; } switch_;
        FuncDefNode *funcdef;
        struct { Node *value; } ret;
        /* bring 라이브러리 / bring "폴더/파일" / bring 이름 = "폴더/파일" / ... up 골라올것
         *   lib      찾을 것 (맨이름 라이브러리, 또는 따옴표로 적은 경로)
         *   is_path  lib 이 경로입니까? (따옴표로 적었으면 true)
         *   as_name  담을 이름 ('이름 =' 을 적었으면 그것, 아니면 NULL -> lib 의 파일 이름) */
        struct { char *lib; bool is_path; char *as_name;
                 char **names; size_t nnames; bool all; } bring;
        struct { NodeList elements; } list;
        struct { struct { Node *key, *val; } *pairs; size_t npairs; } dict;
        struct { Node *expr; char *var_name; Node *iterable; Node *cond; } list_comp;
        struct { Node *key_expr, *val_expr; char *var_name; Node *iterable; Node *cond; } dict_comp;
        struct { Node *target, *start, *end; } slice;
        struct { Node *target, *index; } index;
        struct { Node *start, *end; } rangespec;
        struct { Node *target, *index, *value; } indexassign;
        struct { Node *init, *cond, *step; NodeList body; } forc;
        struct { char *var_name; Node *iterable; NodeList body; } forin;
        struct { Node *init, *cond, *step; char *var_name; } forr;
        /* use 이름 = open(...):  — 블록이 끝나면 저절로 닫습니다 */
        struct { char *var_name; Node *value; NodeList body; } use;
        /* try ... catch ... safe ... always */
        struct {
            NodeList try_body;
            struct CatchClause {
                char *type_name;
                char *var_name;
                NodeList body;
                int   line;          /* 'catch' 를 적은 줄 (lint 가 짚어 줍니다) */
            } *catches;
            size_t ncatches;
            NodeList safe_body;
            bool has_safe;
            NodeList always_body;
            bool has_always;
        } try_;
        /* error("메시지") 또는 error(Type, "메시지") */
        struct { Node *type_expr; Node *msg_expr; } error_;
    } v;
};

/* defaults[i] 는 i 번째 매개변수의 기본값 식입니다 (없으면 NULL).
 * 기본값은 부를 때마다 그 함수의 환경에서 새로 셈합니다 —
 * 그래야 func f(xs = []) 가 부를 때마다 새 리스트를 받습니다. */
/* ptypes[i] 는 i 번째 매개변수에 적어 둔 자료형 이름입니다 (안 적었으면 NULL).
 * ret_type 은 ')' 뒤에 적은 돌려주는 값의 자료형입니다 (안 적었으면 NULL).
 * 둘 다 선택입니다 — 안 적으면 지금까지와 똑같이 아무 값이나 오갑니다. */
struct FuncDefNode { char *name; char **params; Node **defaults; char **ptypes;
                     size_t nparams; char *ret_type;
                     NodeList body; bool is_abstract; };
struct ClassDefNode {
    char *name; char *parent_name;
    Node **methods; size_t nmethods;      /* N_FUNCDEF 노드들 */
    NodeList fields;
};

NodeList parse_program(TokenList *tokens);

/* ============================================================
 * 5. 인터프리터
 * ============================================================ */
typedef void (*OutputFn)(const char *utf8_text);
/* 한 줄을 읽어 옵니다. malloc 된 UTF-8 문자열, 더 읽을 게 없으면 NULL */
typedef char *(*InputFn)(const char *prompt_utf8);

typedef struct TryHandler {
    jmp_buf jmp;
    struct TryHandler *prev;
} TryHandler;

/* 지금 살아 있는 이름 자리(환경) 하나.  C 스택을 따라 사슬로 엮입니다.
 * 순환 수집기가 이 사슬을 '뿌리'로 삼습니다 — 이것이 없으면 실행 중인 함수의
 * 지역 값이 아무도 안 쓰는 값으로 보여 수집기가 그것을 치워 버립니다
 * (실제로 그랬습니다: docs/Lumi_Changes.md 의 '순환 수집기' 항목). */
typedef struct ScopeGuard {
    Env *env;
    struct ScopeGuard *prev;
} ScopeGuard;

/* 호출 한 겹. 오류가 났을 때 '어디서 어떻게 왔는지'를 되짚는 데 씁니다.
 *   fn_name  불려 온 함수 이름 (맨 바깥 = NULL, 즉 파일 본문)
 *   file     그 함수가 적혀 있는 파일
 *   line     이 겹에서 지금 실행 중인 줄 (더 안쪽을 부른 자리, 맨 안쪽은 오류가 난 줄) */
typedef struct CallFrame {
    const char *fn_name;
    const char *file;
    int         line;
    Env        *env;    /* 디버거만 씁니다 — 갈고리가 문장마다 지금 환경을 적어 둡니다.
                         * 세지 않는(약한) 참조입니다: 살아 있는 겹만 들여다봅니다. */
} CallFrame;

/* 재귀 깊이 한도.  이 수를 넘으면 'too much recursion' 오류가 납니다 — **잡을 수 있는
 * 오류**이지, 프로세스가 죽는 것이 아닙니다.  그 약속을 지키려면
 *     한도 × (한 겹이 쓰는 C 스택) < 실제 스택 (윈도우 /STACK 8MB, 리눅스·맥 ulimit 8MB)
 * 이어야 합니다.  재어 본 값: 그냥 재귀 3.3KB, try 를 품은 재귀 5.6KB, 거기에 내장
 * 함수를 거치면 6.0KB.  1000 × 6.0KB = 6MB 로 2MB 가 남습니다.
 * (파이썬의 기본 한도도 1000 입니다.)
 *
 * 겹의 크기를 어떻게 재는지, 왜 예전에 460 겹에서 소리 없이 죽었는지는
 * docs/Lumi_Changes.md 의 '재귀 깊이' 항목에 적어 두었습니다.
 * 겹이 다시 뚱뚱해지면 이 수를 같이 내려야 합니다. */
#define LUMI_MAX_DEPTH 1000

typedef struct Interp {
    OutputFn output;
    InputFn  input;
    Env     *globals;
    char    *base_dir;             /* 실행 중인 파일이 있는 폴더 */
    char    *cur_file;             /* 지금 본문을 실행 중인 파일 (bring 중에는 그 라이브러리) */
    char   **files;                /* 이름을 본 적 있는 파일들 — Func/CallFrame 이 가리킵니다 */
    size_t   nfiles, cfiles;
    struct ModCache { char *path; Env *env; } *modules;
    size_t   nmodules, cmodules;
    int      depth;                /* 함수 호출 깊이 (재귀 제한 + frames 의 자리) */
    CallFrame frames[LUMI_MAX_DEPTH + 1];
    double   start_clock;          /* 인터프리터 시작 시각 */
    TryHandler *top_handler;       /* 활성화된 try-catch 에러 핸들러 스택 */
    ScopeGuard *top_scope;         /* 지금 살아 있는 환경들 (순환 수집기의 뿌리) */
    struct Interp *next_interp;    /* 살아 있는 인터프리터 사슬 — 일감마다 하나씩입니다 */
    char   **argv;                 /* 프로그램에 넘어온 명령행 인자 (sys.args) */
    size_t   argc;
    bool     testing;              /* lumi test 로 돌고 있습니까? */
    size_t   tests_run, tests_failed;
} Interp;


typedef enum { FLOW_NORMAL, FLOW_RETURN, FLOW_BREAK, FLOW_CONTINUE } Flow;

Interp *interp_new(OutputFn out, InputFn in, const char *base_dir);
/* 지금 Lumi 실행의 큰 자물쇠를 쥐고 있는가 (일감 정리에서만 씁니다). */
bool    lumi_gil_is_held(void);
void    interp_run(Interp *in, const char *source);   /* 오류는 lumi_error */
void    interp_set_script(Interp *in, const char *path);      /* 트레이스에 찍을 파일 이름 */
void    interp_set_args(Interp *in, char **argv, size_t argc); /* sys.args (안 복사합니다) */

/* LSP 서버가 쓰는 JSON 드나들기 (lsp.c).  깨진 JSON 은 lumi_error 로 올라옵니다. */
Value   lumi_json_parse_value(Interp *in, Value text);
Value   lumi_json_stringify_value(Interp *in, Value v);
int     run_lsp(void);

/* 디버거 (dap.c) — Debug Adapter Protocol.
 * 갈고리는 문장 하나를 실행하기 **직전**마다 불립니다.  NULL 이면 아무 값도 안 듭니다
 * (평소 실행에는 if 한 번뿐입니다).  걸어 둔 쪽이 여기서 멈추고 편집기와 이야기합니다. */
typedef void (*DebugHook)(Interp *in, Node *n, Env *env);
extern LUMI_TLS DebugHook lumi_debug_hook;
/* 식 하나를 그 환경에서 셈해 줍니다 (디버거의 '값 보기' 가 씁니다) */
Value   lumi_eval_expr(Interp *in, Node *n, Env *env);
int     run_dap(void);

/* 걱정거리 찾기 (lint.c) — 문법은 맞는데 뜻이 어긋나 보이는 것들.
 * lint_source 는 찾은 개수를 돌려줍니다 (문법 오류라 못 읽었으면 -1).
 * LSP 도 같은 것을 불러 편집기에 경고로 띄웁니다. */
typedef void (*LintReport)(void *ud, int line, const char *msg);
int     lint_source(const char *text, LintReport report, void *ud);
int     run_lint(const char *where);

/* 모양 다듬기 (fmt.c).  fmt_source 는 다듬은 글자를 malloc 해서 줍니다
 * (문법 오류면 NULL).  줄 안의 빈칸과 들여쓰기만 손대고 줄은 나누지 않습니다. */
char   *fmt_source(const char *text);
int     run_fmt(const char *where, bool check_only);

/* 남의 C 라이브러리 불러 쓰기 (ffi.c).  자세한 것은 그 파일 맨 위를 보세요.
 * 여기 있는 것들은 Lumi 에서 가장 위험한 부분입니다 — 서명을 틀리면 그냥 죽습니다.
 * 보통은 이것을 감싼 Lumi 라이브러리(libraries 폴더의 .lumi)를 쓰세요. */
typedef struct { char kind; long long i; double d; } FfiRet;
long long ffi_load(const char *name, char *why, size_t whyn);  /* 0 이면 실패 */
bool      ffi_close(long long id);
int       ffi_has(long long id, const char *symbol);           /* -1 = 안 열린 손잡이 */
long long ffi_buf(long long nbytes);                           /* 0 이면 실패 */
bool      ffi_free(long long addr);                            /* 우리가 준 자리일 때만 true */
bool      ffi_call(long long id, const char *symbol, const char *sig,
                   const long long *args, size_t nargs, FfiRet *out,
                   char *why, size_t whyn);

/* 묶어 놓은 프로그램 (bundle.c) — lumi build 로 만든 단독 실행 파일 */
void        pack_open(const char *exe_path);   /* 켤 때 한 번 */
bool        pack_has_program(void);
const char *pack_main_name(void);
const char *pack_find(const char *path);       /* 담긴 파일 내용 (없으면 NULL) */
int         run_build(const char *script, const char *out_opt, const char *exe_path);
char       *read_text_file(const char *path);
Flow    exec_node(Interp *in, Node *n, Env *env, Value *ret);
Value   eval_binop_values(Interp *in, const char *op, Value a, Value b, int line);
Value   do_index(Interp *in, Value target, Value index, int line);
Value   do_slice(Interp *in, Value target, Value *start, Value *end, int line);
Value   call_value(Interp *in, Value callee, Value *args, size_t nargs, const char *name, int line);
Value   make_func_value(Interp *in, FuncDefNode *fd, Env *env);
void    define_class(Interp *in, ClassDefNode *cd, Env *env, int line);
Value   member_of(Interp *in, Value obj, const char *name, Env *env, int line);
void    set_member(Interp *in, Value obj, const char *name, Value value, Env *env, int line);
char   *S(Interp *in, Value v);
char   *R(Interp *in, Value v);

/* 값을 사람이 읽는 글자로 (malloc UTF-8) */
char *value_to_utf8(Interp *in, Value v);

/* 공용 도우미 (여러 파일에서 씁니다) */
Str  *value_to_str(Interp *in, Value v);      /* to_str */
Str  *value_repr(Interp *in, Value v);        /* _repr : 문자열에 따옴표 */
bool  value_truthy(Value v);
const char *type_name_of(Value v);

/* 이름 모음 (렉서/파서/인터프리터가 함께 씁니다) */
bool is_keyword(const char *w);
bool is_type_name(const char *w);
bool is_convert_name(const char *w);

/* 문자열 만들기 도우미: printf 처럼 써서 UTF-8 문자열을 만듭니다 */
char *xsprintf(const char *fmt, ...);
char *fmt_double(double d);      /* 파이썬 repr 처럼: 되돌려 읽히는 가장 짧은 표기 */
bool  key_equal(Value a, Value b);
uint32_t value_hash(Value v);
/* 순환 참조 치우기.  살아 있는 인터프리터 전부(일감 포함)의 전역·호출 겹·환경 사슬을
 * 뿌리로 삼습니다.  돌려주는 값은 치운 개수입니다. */
size_t gc_collect(Interp *in);
/* 저절로 치우기.  마지막으로 치운 뒤 늘어난 '남을 담을 수 있는 값'의 수가
 * 문턱을 넘으면 문장 사이에서 한 번 돕니다.  세는 값을 밖으로 내놓은 까닭은
 * 검사가 **부르는 자리에서 바로** 끝나야 하기 때문입니다 — 문장마다 함수를 부르면
 * 그것만으로 10% 가 느려졌습니다 (재 봤습니다). */
extern long lumi_gc_pending;
#define LUMI_GC_THRESHOLD 20000
#define LUMI_GC_CHECK(in) \
    do { if (lumi_gc_pending >= LUMI_GC_THRESHOLD) gc_collect(in); } while (0)
/* 살아 있는 인터프리터 사슬 (interp_new 가 넣고, 일감이 끝나면 뺍니다) */
void   gc_add_interp(Interp *in);
void   gc_drop_interp(Interp *in);
void *xmalloc(size_t n);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);

#endif /* LUMI_H */
