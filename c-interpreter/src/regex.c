/* regex.c — 무늬 찾기(정규표현식) 엔진
 * ==========================================================
 * Lumi 의 문자열은 코드포인트(UTF-32)의 나열이므로, 이 엔진도 바이트가 아니라
 * 코드포인트 위에서 돕니다.  그래서 "한글"[0] 과 무늬가 같은 눈금을 씁니다.
 *
 * 되돌아가며 맞춰 보는(backtracking) 방식입니다.  작고 읽기 쉬운 대신,
 * (a+)+b 같은 지독한 무늬에 아주 긴 글자를 물리면 느려질 수 있습니다.
 * 그럴 일이 생기면 그때 NFA 로 바꾸세요.
 *
 * 할 수 있는 것:
 *   글자 그대로          가 나 다
 *   아무 글자 하나       .        (줄바꿈은 빼고)
 *   글자 묶음            [abc] [^abc] [a-z0-9]
 *   미리 정한 묶음       \d \D \w \W \s \S
 *   자리                 ^ $
 *   묶기                 (...)  (?:...)   <- (?: 는 담지 않는 묶기
 *   또는                 a|b
 *   되풀이               * + ? {n} {n,} {n,m}   그리고 뒤에 ? 를 붙이면 게으르게
 *   벗어나기             \. \* \\ \n \t \r 등
 */
#include "lumi.h"

#include <stdlib.h>
#include <string.h>

/* ============================================================
 * 1. 무늬를 나무(트리)로
 * ============================================================ */

enum {
    RN_CHAR,     /* 글자 하나 */
    RN_ANY,      /* .        */
    RN_CLASS,    /* [...]    */
    RN_GROUP,    /* (...)    */
    RN_GEND,     /* 묶음이 끝나는 자리 (담을 때 끝 위치를 적어 둡니다) */
    RN_ALT,      /* a|b      */
    RN_REP,      /* 되풀이   */
    RN_BOL,      /* ^        */
    RN_EOL       /* $        */
};

typedef struct { uint32_t lo, hi; } CRange;

typedef struct {
    CRange *r;
    size_t  n, cap;
    bool    negate;
} CClass;

typedef struct RNode RNode;
struct RNode {
    int     kind;
    RNode  *next;          /* 이어서 맞출 것 (없으면 NULL) */
    uint32_t ch;           /* RN_CHAR */
    CClass *cls;           /* RN_CLASS */
    RNode  *body;          /* RN_GROUP 안쪽 */
    int     gidx;          /* RN_GROUP / RN_GEND : 몇 번째 묶음인가 (-1 = 안 담음) */
    RNode **alts;          /* RN_ALT */
    size_t  nalts;
    RNode  *sub;           /* RN_REP 이 되풀이할 것 */
    int     min, max;      /* RN_REP : max < 0 이면 끝없이 */
    bool    lazy;
};

struct Regex {
    RNode *root;
    int    ngroups;        /* 담는 묶음 개수 (0번 = 전체는 따로) */
    RNode **owned;         /* 만든 노드를 모두 적어 두었다가 한꺼번에 놓아 줍니다 */
    size_t nowned, cowned;
    CClass **classes;
    size_t nclasses, cclasses;
};

typedef struct {
    const uint32_t *p;
    size_t n, i;
    Regex *re;
    char  *err;
    size_t errn;
    bool   failed;
} Rp;

static void rp_fail(Rp *rp, const char *msg)
{
    if (!rp->failed) {
        snprintf(rp->err, rp->errn, "%s (at position %zu of the pattern)", msg, rp->i);
        rp->failed = true;
    }
}

static RNode *rnode(Rp *rp, int kind)
{
    RNode *n = (RNode *)xmalloc(sizeof(RNode));
    memset(n, 0, sizeof(*n));
    n->kind = kind;
    n->gidx = -1;
    n->max = -1;
    Regex *re = rp->re;
    if (re->nowned == re->cowned) {
        re->cowned = re->cowned ? re->cowned * 2 : 32;
        re->owned = (RNode **)xrealloc(re->owned, re->cowned * sizeof(RNode *));
    }
    re->owned[re->nowned++] = n;
    return n;
}

static CClass *cclass_new(Rp *rp)
{
    CClass *c = (CClass *)xmalloc(sizeof(CClass));
    memset(c, 0, sizeof(*c));
    Regex *re = rp->re;
    if (re->nclasses == re->cclasses) {
        re->cclasses = re->cclasses ? re->cclasses * 2 : 8;
        re->classes = (CClass **)xrealloc(re->classes, re->cclasses * sizeof(CClass *));
    }
    re->classes[re->nclasses++] = c;
    return c;
}

static void cls_add(CClass *c, uint32_t lo, uint32_t hi)
{
    if (c->n == c->cap) {
        c->cap = c->cap ? c->cap * 2 : 8;
        c->r = (CRange *)xrealloc(c->r, c->cap * sizeof(CRange));
    }
    c->r[c->n].lo = lo;
    c->r[c->n].hi = hi;
    c->n++;
}

/* \d \w \s 같은 미리 정한 묶음을 c 에 부어 넣습니다.  아는 글자면 true. */
static bool cls_add_shorthand(CClass *c, uint32_t esc)
{
    switch (esc) {
    case 'd': cls_add(c, '0', '9'); return true;
    case 'w': cls_add(c, 'a', 'z'); cls_add(c, 'A', 'Z');
              cls_add(c, '0', '9'); cls_add(c, '_', '_'); return true;
    case 's': cls_add(c, ' ', ' '); cls_add(c, '\t', '\t'); cls_add(c, '\n', '\n');
              cls_add(c, '\r', '\r'); cls_add(c, 0x0B, 0x0C); return true;
    default:  return false;
    }
}

/* 벗어나기 글자 하나가 뜻하는 진짜 글자 */
static uint32_t esc_char(uint32_t e)
{
    switch (e) {
    case 'n': return '\n';
    case 't': return '\t';
    case 'r': return '\r';
    case 'f': return '\f';
    case 'v': return 0x0B;
    case '0': return 0;
    default:  return e;          /* \. \* \\ ... 는 그 글자 그대로 */
    }
}

static RNode *parse_alt(Rp *rp);

/* [ ... ] */
static RNode *parse_class(Rp *rp)
{
    RNode *n = rnode(rp, RN_CLASS);
    CClass *c = cclass_new(rp);
    n->cls = c;
    if (rp->i < rp->n && rp->p[rp->i] == '^') { c->negate = true; rp->i++; }

    bool first = true;
    while (rp->i < rp->n && (rp->p[rp->i] != ']' || first)) {
        first = false;
        uint32_t lo = rp->p[rp->i++];
        if (lo == '\\' && rp->i < rp->n) {
            uint32_t e = rp->p[rp->i++];
            if (cls_add_shorthand(c, e)) continue;
            if (e == 'D' || e == 'W' || e == 'S') {
                /* 묶음 안의 대문자 판은 다루기 까다로워 막아 둡니다 */
                rp_fail(rp, "\\D, \\W and \\S cannot be used inside [ ]");
                return n;
            }
            lo = esc_char(e);
        }
        /* a-z 처럼 사이 */
        if (rp->i + 1 < rp->n && rp->p[rp->i] == '-' && rp->p[rp->i + 1] != ']') {
            rp->i++;
            uint32_t hi = rp->p[rp->i++];
            if (hi == '\\' && rp->i < rp->n) hi = esc_char(rp->p[rp->i++]);
            if (hi < lo) { rp_fail(rp, "the range in [ ] goes backwards"); return n; }
            cls_add(c, lo, hi);
        } else {
            cls_add(c, lo, lo);
        }
    }
    if (rp->i >= rp->n) { rp_fail(rp, "[ was never closed with ]"); return n; }
    rp->i++;                      /* ']' */
    if (c->n == 0) rp_fail(rp, "[ ] needs at least one letter inside");
    return n;
}

static RNode *parse_atom(Rp *rp)
{
    if (rp->i >= rp->n) return NULL;
    uint32_t ch = rp->p[rp->i];

    if (ch == '(') {
        rp->i++;
        int gidx = -1;
        if (rp->i + 1 < rp->n && rp->p[rp->i] == '?' && rp->p[rp->i + 1] == ':') {
            rp->i += 2;                       /* (?: 는 담지 않습니다 */
        } else if (rp->i < rp->n && rp->p[rp->i] == '?') {
            rp_fail(rp, "only (?: ...) is supported after (?");
            return NULL;
        } else {
            gidx = ++rp->re->ngroups;
        }
        RNode *g = rnode(rp, RN_GROUP);
        g->gidx = gidx;
        RNode *inner = parse_alt(rp);
        if (rp->failed) return g;
        if (rp->i >= rp->n || rp->p[rp->i] != ')') { rp_fail(rp, "( was never closed with )"); return g; }
        rp->i++;
        /* 묶음 끝을 알리는 표시를 안쪽 끝에 매답니다 */
        RNode *ge = rnode(rp, RN_GEND);
        ge->gidx = gidx;
        if (!inner) { inner = ge; }
        else { RNode *t = inner; while (t->next) t = t->next; t->next = ge; }
        g->body = inner;
        return g;
    }
    if (ch == '[') { rp->i++; return parse_class(rp); }
    if (ch == '.') { rp->i++; return rnode(rp, RN_ANY); }
    if (ch == '^') { rp->i++; return rnode(rp, RN_BOL); }
    if (ch == '$') { rp->i++; return rnode(rp, RN_EOL); }
    if (ch == '\\') {
        rp->i++;
        if (rp->i >= rp->n) { rp_fail(rp, "the pattern ends with a lone \\"); return NULL; }
        uint32_t e = rp->p[rp->i++];
        if (e == 'd' || e == 'w' || e == 's' || e == 'D' || e == 'W' || e == 'S') {
            RNode *n = rnode(rp, RN_CLASS);
            CClass *c = cclass_new(rp);
            n->cls = c;
            uint32_t low = (e >= 'A' && e <= 'Z') ? (uint32_t)(e + 32) : e;
            cls_add_shorthand(c, low);
            c->negate = (e >= 'A' && e <= 'Z');
            return n;
        }
        RNode *n = rnode(rp, RN_CHAR);
        n->ch = esc_char(e);
        return n;
    }
    if (ch == ')' || ch == '|') return NULL;      /* 위에서 다룹니다 */
    if (ch == '*' || ch == '+' || ch == '?') {
        rp_fail(rp, "there is nothing in front of this * + or ? to repeat");
        return NULL;
    }
    rp->i++;
    RNode *n = rnode(rp, RN_CHAR);
    n->ch = ch;
    return n;
}

/* {n} {n,} {n,m} 을 읽습니다.  숫자 꼴이 아니면 false (그러면 { 는 그냥 글자). */
static bool parse_brace(Rp *rp, int *mn, int *mx)
{
    size_t save = rp->i;
    rp->i++;                                   /* '{' */
    int a = 0; bool got = false;
    while (rp->i < rp->n && rp->p[rp->i] >= '0' && rp->p[rp->i] <= '9') {
        a = a * 10 + (int)(rp->p[rp->i++] - '0'); got = true;
        if (a > 100000) { rp->i = save; return false; }
    }
    if (!got) { rp->i = save; return false; }
    int b = a;
    if (rp->i < rp->n && rp->p[rp->i] == ',') {
        rp->i++;
        if (rp->i < rp->n && rp->p[rp->i] == '}') { b = -1; }
        else {
            b = 0; bool got2 = false;
            while (rp->i < rp->n && rp->p[rp->i] >= '0' && rp->p[rp->i] <= '9') {
                b = b * 10 + (int)(rp->p[rp->i++] - '0'); got2 = true;
            }
            if (!got2) { rp->i = save; return false; }
        }
    }
    if (rp->i >= rp->n || rp->p[rp->i] != '}') { rp->i = save; return false; }
    rp->i++;
    *mn = a; *mx = b;
    return true;
}

/* 되풀이 표시가 붙었으면 감싸 줍니다 */
static RNode *parse_repeat(Rp *rp)
{
    RNode *a = parse_atom(rp);
    if (!a || rp->failed) return a;
    for (;;) {
        if (rp->i >= rp->n) return a;
        uint32_t c = rp->p[rp->i];
        int mn, mx;
        if (c == '*')      { rp->i++; mn = 0; mx = -1; }
        else if (c == '+') { rp->i++; mn = 1; mx = -1; }
        else if (c == '?') { rp->i++; mn = 0; mx = 1; }
        else if (c == '{') { if (!parse_brace(rp, &mn, &mx)) return a; }
        else return a;

        if (a->kind == RN_BOL || a->kind == RN_EOL) {
            rp_fail(rp, "^ and $ mark a place, so they cannot be repeated");
            return a;
        }
        RNode *r = rnode(rp, RN_REP);
        r->sub = a;
        r->min = mn;
        r->max = mx;
        if (rp->i < rp->n && rp->p[rp->i] == '?') { r->lazy = true; rp->i++; }
        a = r;
    }
}

/* 이어 붙이기 */
static RNode *parse_concat(Rp *rp)
{
    RNode *head = NULL, *tail = NULL;
    while (rp->i < rp->n && rp->p[rp->i] != '|' && rp->p[rp->i] != ')') {
        RNode *n = parse_repeat(rp);
        if (rp->failed) return head;
        if (!n) break;
        if (!head) { head = tail = n; }
        else { tail->next = n; tail = n; }
        while (tail->next) tail = tail->next;
    }
    return head;
}

static RNode *parse_alt(Rp *rp)
{
    RNode *first = parse_concat(rp);
    if (rp->failed) return first;
    if (rp->i >= rp->n || rp->p[rp->i] != '|') return first;

    RNode *n = rnode(rp, RN_ALT);
    n->nalts = 0;
    size_t cap = 4;
    n->alts = (RNode **)xmalloc(cap * sizeof(RNode *));
    n->alts[n->nalts++] = first;
    while (rp->i < rp->n && rp->p[rp->i] == '|') {
        rp->i++;
        RNode *b = parse_concat(rp);
        if (rp->failed) break;
        if (n->nalts == cap) { cap *= 2; n->alts = (RNode **)xrealloc(n->alts, cap * sizeof(RNode *)); }
        n->alts[n->nalts++] = b;
    }
    return n;
}

Regex *regex_compile(const uint32_t *pat, size_t len, char *errbuf, size_t errn)
{
    Regex *re = (Regex *)xmalloc(sizeof(Regex));
    memset(re, 0, sizeof(*re));
    Rp rp = { pat, len, 0, re, errbuf, errn, false };
    errbuf[0] = 0;
    re->root = parse_alt(&rp);
    if (!rp.failed && rp.i < rp.n) {
        if (pat[rp.i] == ')') rp_fail(&rp, ") has no ( to match");
        else                  rp_fail(&rp, "the pattern has leftover letters");
    }
    if (rp.failed) { regex_free(re); return NULL; }
    return re;
}

void regex_free(Regex *re)
{
    if (!re) return;
    for (size_t i = 0; i < re->nowned; i++) { free(re->owned[i]->alts); free(re->owned[i]); }
    free(re->owned);
    for (size_t i = 0; i < re->nclasses; i++) { free(re->classes[i]->r); free(re->classes[i]); }
    free(re->classes);
    free(re);
}

int regex_group_count(const Regex *re) { return re->ngroups; }

/* ============================================================
 * 2. 맞춰 보기
 * ============================================================ */

enum { CONT_NODE, CONT_REP };

typedef struct Cont {
    int kind;
    RNode *node;            /* CONT_NODE */
    RNode *rep;             /* CONT_REP */
    int    count;
    size_t last;            /* 빈 되풀이를 막으려고 지난 자리를 적어 둡니다 */
    struct Cont *up;
} Cont;

typedef struct {
    const uint32_t *s;
    size_t slen;
    long *gs, *ge;
    long  steps;            /* 너무 오래 되돌아가는 것을 막습니다 */
    bool  gave_up;          /* 한도에 걸렸는가 (틀린 답 대신 오류로 알려 주려고) */
} MCtx;

#define REGEX_MAX_STEPS 4000000L

static bool m_node(MCtx *c, RNode *n, size_t pos, Cont *k);

static bool cls_has(const CClass *cl, uint32_t ch)
{
    bool in = false;
    for (size_t i = 0; i < cl->n; i++)
        if (ch >= cl->r[i].lo && ch <= cl->r[i].hi) { in = true; break; }
    return cl->negate ? !in : in;
}

/* 이어서 맞출 것이 없으면 성공 */
static bool m_cont(MCtx *c, size_t pos, Cont *k)
{
    if (!k) { c->ge[0] = (long)pos; return true; }
    if (k->kind == CONT_NODE) return m_node(c, k->node, pos, k->up);
    /* CONT_REP : 되풀이를 이어 갑니다 */
    RNode *r = k->rep;
    int count = k->count;
    Cont *up = k->up;
    bool more_ok = (r->max < 0 || count < r->max);
    bool enough  = (count >= r->min);
    bool empty   = (pos == k->last);          /* 한 바퀴 돌았는데 자리가 그대로면 멈춥니다 */

    Cont again = { CONT_REP, NULL, r, count + 1, pos, up };
    if (r->lazy) {
        if (enough && m_node(c, r->next, pos, up)) return true;
        if (more_ok && !empty && m_node(c, r->sub, pos, &again)) return true;
        return false;
    }
    if (more_ok && !empty && m_node(c, r->sub, pos, &again)) return true;
    if (enough) return m_node(c, r->next, pos, up);
    return false;
}

static bool m_node(MCtx *c, RNode *n, size_t pos, Cont *k)
{
    if (++c->steps > REGEX_MAX_STEPS) { c->gave_up = true; return false; }
    if (!n) return m_cont(c, pos, k);

    switch (n->kind) {
    case RN_CHAR:
        if (pos < c->slen && c->s[pos] == n->ch) return m_node(c, n->next, pos + 1, k);
        return false;
    case RN_ANY:
        if (pos < c->slen && c->s[pos] != '\n') return m_node(c, n->next, pos + 1, k);
        return false;
    case RN_CLASS:
        if (pos < c->slen && cls_has(n->cls, c->s[pos])) return m_node(c, n->next, pos + 1, k);
        return false;
    case RN_BOL:
        if (pos == 0 || c->s[pos - 1] == '\n') return m_node(c, n->next, pos, k);
        return false;
    case RN_EOL:
        if (pos == c->slen || c->s[pos] == '\n') return m_node(c, n->next, pos, k);
        return false;
    case RN_GROUP: {
        long saved = n->gidx > 0 ? c->gs[n->gidx] : 0;
        if (n->gidx > 0) c->gs[n->gidx] = (long)pos;
        Cont after = { CONT_NODE, n->next, NULL, 0, 0, k };
        if (m_node(c, n->body, pos, &after)) return true;
        if (n->gidx > 0) c->gs[n->gidx] = saved;
        return false;
    }
    case RN_GEND:
        if (n->gidx > 0) {
            long saved = c->ge[n->gidx];
            c->ge[n->gidx] = (long)pos;
            if (m_node(c, n->next, pos, k)) return true;
            c->ge[n->gidx] = saved;
            return false;
        }
        return m_node(c, n->next, pos, k);
    case RN_ALT: {
        Cont after = { CONT_NODE, n->next, NULL, 0, 0, k };
        for (size_t i = 0; i < n->nalts; i++)
            if (m_node(c, n->alts[i], pos, &after)) return true;
        return false;
    }
    case RN_REP: {
        /* 0 번째부터 시작합니다.  last 를 pos 와 다르게 두어 첫 바퀴는 늘 돌게 합니다. */
        Cont start = { CONT_REP, NULL, n, 0, (size_t)-1, k };
        return m_cont(c, pos, &start);
    }
    default:
        return false;
    }
}

bool regex_search(const Regex *re, const uint32_t *s, size_t slen, size_t from,
                  long *starts, long *ends, bool *gave_up)
{
    int ng = re->ngroups;
    if (gave_up) *gave_up = false;
    for (size_t at = from; at <= slen; at++) {
        for (int i = 0; i <= ng; i++) { starts[i] = -1; ends[i] = -1; }
        MCtx c = { s, slen, starts, ends, 0, false };
        starts[0] = (long)at;
        if (m_node(&c, re->root, at, NULL)) return true;
        if (c.gave_up) {
            /* 되돌아가기가 너무 길어졌습니다.  "못 찾았다" 고 거짓말하지 않고 알립니다. */
            if (gave_up) *gave_up = true;
            return false;
        }
    }
    return false;
}
