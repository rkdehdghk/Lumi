/* fmt.c — 코드 모양 다듬기 (lumi fmt)
 * ==========================================================
 *   lumi fmt              지금 폴더 아래 모든 .lumi 를 고쳐 씁니다
 *   lumi fmt app.lumi     파일 하나만
 *   lumi fmt --check      고치지 않고 '달라질 파일'만 알려 줍니다 (CI 용, 종료 코드 1)
 *
 * ★ 하는 일과 하지 않는 일 ★
 *
 * 하는 일 — **줄 안의 빈칸과 줄의 들여쓰기만** 손봅니다.
 *   * 들여쓰기를 한 단계 4칸으로
 *   * 두 값 사이 연산자 양옆에 한 칸씩          a+1   -> a + 1
 *   * 쉼표 앞은 붙이고 뒤는 한 칸               f(1 ,2) -> f(1, 2)
 *   * 괄호 안쪽은 붙이기                        f( x )  -> f(x)
 *   * 줄 끝 빈칸 없애기, 파일 끝은 줄바꿈 하나
 *   * 빈 줄이 셋 이상이면 둘로
 *
 * 하지 않는 일 — **줄을 붙이거나 자르지 않습니다.**
 *   긴 줄을 접어 주지 않고, 짧은 줄을 합치지도 않습니다. 줄을 나누는 자리는
 *   사람이 뜻을 담아 고른 것이라, 기계가 다시 고르면 대개 더 나빠집니다.
 *
 * ★ 이 포매터가 옳다는 것을 어떻게 아는가 ★
 *   examples/ 와 libraries/ 의 모든 파일에 돌렸을 때 **한 글자도 안 바뀌어야** 합니다.
 *   tests\run.bat 이 그것을 지킵니다.  규칙을 새로 넣었는데 멀쩡한 코드가
 *   바뀌기 시작하면 거기서 바로 잡힙니다.
 */
#include "lumi.h"
#include "lumiwords.h"
#include "platform.h"

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *read_text_file(const char *path);

/* ============================================================
 * 글자 모으개
 * ============================================================ */

typedef struct { char *p; size_t len, cap; } Out;

static void o_add(Out *o, const char *s, size_t n)
{
    if (o->len + n + 1 > o->cap) {
        while (o->len + n + 1 > o->cap) o->cap = o->cap ? o->cap * 2 : 1024;
        o->p = (char *)xrealloc(o->p, o->cap);
    }
    memcpy(o->p + o->len, s, n);
    o->len += n;
    o->p[o->len] = 0;
}

static void o_str(Out *o, const char *s) { o_add(o, s, strlen(s)); }

static void o_spaces(Out *o, int n)
{
    while (n-- > 0) o_add(o, " ", 1);
}

/* 줄 끝의 빈칸을 걷어냅니다 (줄바꿈을 넣기 직전에 부릅니다).
 * '\r' 도 함께 걷어냅니다 — 주석 원문은 줄 끝까지 잘라 오므로 CRLF 파일에서는
 * 끝에 '\r' 이 딸려 옵니다.  그대로 두면 마지막에 CRLF 로 되돌릴 때 겹칩니다. */
static void o_trim_line(Out *o)
{
    while (o->len > 0 && (o->p[o->len - 1] == ' ' || o->p[o->len - 1] == '\t'
                          || o->p[o->len - 1] == '\r'))
        o->p[--o->len] = 0;
}

/* ============================================================
 * 토큰 하나를 글자로
 * ============================================================ */

/* 원문을 그대로 씁니다.  tokenize_keep_source 가 담아 준 것입니다. */
static const char *tok_text(const Token *t)
{
    return t->text ? t->text : "";
}

static bool is_op(const Token *t, const char *s)
{
    return t->kind == T_OP && t->text && strcmp(t->text, s) == 0;
}

static bool is_kw(const Token *t, const char *s)
{
    return t->kind == T_KEYWORD && t->text && strcmp(t->text, s) == 0;
}

/* 부르는 낱말입니까?  print(...) 와 int(...) 처럼 바로 뒤에 ( 가 붙는 것들.
 * if / while / return 같은 문법 낱말과 갈라 놓아야 'if (x)' 의 빈칸이 삽니다.
 * 갈라 주는 표는 lumiwords.h 하나뿐입니다 — 여기에 목록을 또 적지 않습니다. */
static bool is_call_word(const Token *t)
{
    if (t->kind != T_KEYWORD || !t->text) return false;
    /* error("종류", "말") 은 문법 낱말이면서 부르는 모양입니다 (색칠은 낱말로 해야
     * 맞아서 lumiwords.h 에서는 LW_KEYWORD 입니다 — 여기서만 예외로 둡니다). */
    if (strcmp(t->text, "error") == 0) return true;
    for (size_t i = 0; i < LUMI_WORD_COUNT; i++)
        if (strcmp(LUMI_WORDS[i].name, t->text) == 0)
            return LUMI_WORDS[i].kind != LW_KEYWORD;
    return false;
}

/* 값이 끝나는 자리입니까?  바로 뒤의 -, ( , [ 가 어떤 뜻인지 여기서 갈립니다.
 *   값 뒤   : a - 1 (빼기),  f(x) (부르기),  xs[0] (꺼내기)
 *   값이 아닌 뒤: (-1) (음수), = [1,2] (새 리스트) */
static bool ends_a_value(const Token *t)
{
    switch (t->kind) {
    case T_NUMBER: case T_STRING: case T_FSTRING: case T_IDENT:
        return true;
    case T_KEYWORD:
        /* this / super / none / true / false 는 값입니다. if, for, return 은 아닙니다. */
        return is_kw(t, "this") || is_kw(t, "super") || is_kw(t, "none")
            || is_kw(t, "true") || is_kw(t, "false");
    case T_OP:
        return is_op(t, ")") || is_op(t, "]") || is_op(t, "}");
    default:
        return false;
    }
}

/* prev 와 cur 사이에 빈칸을 하나 둘까요?
 *   prev_unary  prev 가 부호로 쓰인 -, +, ~, ! 였는가 (-1 의 - 이지 a - 1 의 - 가 아닌)
 *   brace       지금 감싸고 있는 가장 안쪽 괄호가 { 인가 ( : 의 뜻이 여기서 갈립니다) */
static bool needs_space(const Token *prev, const Token *cur, bool prev_unary, bool brace)
{
    if (!prev) return false;

    /* --- 붙이는 것들 --- */
    if (is_op(prev, "(") || is_op(prev, "[") || is_op(prev, "{")) return false;
    if (is_op(cur, ")") || is_op(cur, "]") || is_op(cur, "}")) return false;
    if (is_op(cur, ",") || is_op(cur, ":")) return false;
    if (is_op(prev, ".") || is_op(cur, ".")) return false;   /* a.b.c */
    if (is_op(prev, "?.") || is_op(cur, "?.")) return false; /* a?.b?.c */

    /* ':' 뒤 — 딕셔너리와 이름 없는 기능은 띄우고({"a": 1}, func x: x * 2),
     * 자르기는 붙입니다(xs[0:2], upper(0:5, 말)).  brace 가 그 판정을 들고 옵니다. */
    if (is_op(prev, ":")) return brace;

    /* 부르기와 꺼내기: f(x) / print(x) / xs[0] / 표["가"]  — 값 바로 뒤면 붙입니다 */
    if ((is_op(cur, "(") || is_op(cur, "["))
        && (ends_a_value(prev) || is_call_word(prev))) return false;

    /* 부호 뒤는 붙입니다: (-1), [-1], = -x.
     * a + (b) 처럼 두 값을 잇는 + 뒤는 띄웁니다 — 그래서 prev_unary 를 봅니다. */
    if (prev_unary) return false;

    /* ++ 와 -- 는 붙여 씁니다 */
    if (is_op(prev, "++") || is_op(prev, "--")) return false;
    if (is_op(cur, "++") || is_op(cur, "--")) return false;

    /* --- 그 밖에는 한 칸 --- */
    return true;
}

/* ============================================================
 * 다시 찍기
 * ============================================================ */

/* 한 단계 4칸 */
#define STEP 4

/* 소스를 다듬어 새 글자를 돌려줍니다 (malloc).  문법 오류면 NULL. */
char *fmt_source(const char *text)
{
    Out o;
    memset(&o, 0, sizeof o);

    jmp_buf saved;
    memcpy(saved, lumi_jmp, sizeof saved);
    if (setjmp(lumi_jmp) != 0) {
        memcpy(lumi_jmp, saved, sizeof saved);
        free(o.p);
        return NULL;
    }
    TokenList *toks = tokenize_keep_source(text);
    memcpy(lumi_jmp, saved, sizeof saved);

    int depth = 0;                  /* 지금 들여쓰기 단계 */
    bool at_line_start = true;      /* 이 줄에 아직 아무것도 안 찍었는가 */
    int prev_line = 0;              /* 마지막으로 찍은 토큰의 원래 줄 */
    const Token *prev = NULL;       /* 이 줄에서 바로 앞에 찍은 토큰 */
    bool prev_unary = false;        /* prev 가 부호로 쓰인 -, +, ~, ! 였는가 */
    bool force_space = false;       /* 줄 가운데 낀 주석 바로 뒤인가 */

    /* 열려 있는 괄호들.  ':' 이 딕셔너리인지 자르기인지 여기서 가립니다. */
    char brackets[256];
    int nbr = 0;

    /* 이 괄호 깊이에서 'func' 를 보았는가 — 바로 뒤의 ':' 은 자르기가 아니라
     * 이름 없는 기능의 ':' 이라 뒤를 띄워야 합니다 (each(func x: x + 1, ...)).
     * 깊이별로 두어서 func f(g = func x: x) 같은 겹침도 제대로 갈립니다. */
    bool func_here[257];
    memset(func_here, 0, sizeof func_here);

    for (size_t i = 0; i < toks->len; i++) {
        Token *t = &toks->items[i];

        switch (t->kind) {
        case T_INDENT: depth++; continue;
        case T_DEDENT: if (depth > 0) depth--; continue;

        case T_NEWLINE:
            if (!at_line_start) {
                o_trim_line(&o);
                o_str(&o, "\n");
                at_line_start = true;
                prev = NULL;
            }
            continue;

        case T_EOF:
            continue;

        default: break;
        }

        /* --- 원래 있던 빈 줄 살리기 (셋 이상은 둘로) --- */
        if (at_line_start && prev_line > 0) {
            int gap = t->line - prev_line - 1;
            if (gap > 2) gap = 2;
            for (int k = 0; k < gap; k++) o_str(&o, "\n");
        }

        /* --- 코드 뒤에 붙는 주석 ---
         * 원래 있던 칸으로 되돌려 놓습니다.  사람이 여러 줄에 걸쳐 주석을
         * 세로로 맞춰 놓은 것을 포매터가 무너뜨리면 안 되기 때문입니다.
         * 앞의 코드가 그 자리를 넘어섰으면 두 칸만 띄웁니다. */
        if (t->kind == T_COMMENT && !t->own_line) {
            if (!at_line_start) {
                o_trim_line(&o);
                size_t bol = o.len;
                while (bol > 0 && o.p[bol - 1] != '\n') bol--;
                size_t col = o.len - bol;
                if ((long long)col < t->inum) o_spaces(&o, (int)(t->inum - (long long)col));
                else o_str(&o, "  ");
            } else {
                o_spaces(&o, depth * STEP);
            }
            o_str(&o, tok_text(t));
            prev_line = t->line;
            /* 여러 줄 주석이면 마지막 줄로 옮겨 둡니다 */
            for (const char *q = tok_text(t); *q; q++) if (*q == '\n') prev_line++;
            at_line_start = false;
            /* prev 는 그대로 둡니다 — print(1 /. 가운데 ./ + 2) 처럼 줄 가운데 낀
             * 주석 너머로 '+ 가 두 값을 잇는다'는 것을 이어서 알아야 합니다.
             * 대신 주석과 다음 토큰 사이는 무조건 한 칸 띄웁니다. */
            force_space = true;
            continue;
        }

        /* --- 혼자 있는 줄의 주석: 원래 칸 수를 그대로 둡니다 ---
         * 블록 끝에 붙은 주석을 다음 줄 쪽으로 끌어당기지 않으려는 것입니다. */
        if (t->kind == T_COMMENT) {
            if (!at_line_start) { o_trim_line(&o); o_str(&o, "\n"); }
            o_spaces(&o, (int)t->inum);
            o_str(&o, tok_text(t));
            o_trim_line(&o);
            o_str(&o, "\n");
            prev_line = t->line;
            for (const char *q = tok_text(t); *q; q++) if (*q == '\n') prev_line++;
            at_line_start = true;
            prev = NULL;
            continue;
        }

        /* --- 보통 토큰 --- */
        /* '{' 안이거나, 이 깊이에서 func 를 본 뒤라면 ':' 뒤를 띄웁니다.
         * 깃발은 ':' 다음 토큰까지 살려 두었다가 그때 내립니다 — 빈칸을 넣을지
         * 정하는 것이 바로 그 토큰 차례이기 때문입니다. */
        bool in_brace = (nbr > 0 && brackets[nbr - 1] == '{') || func_here[nbr];
        bool after_colon = prev && is_op(prev, ":");
        if (is_kw(t, "func")) func_here[nbr] = true;

        /* 괄호 안에서 원래 줄이 바뀌었으면 그 줄바꿈을 지킵니다.
         * 렉서는 괄호 안의 줄바꿈을 삼키므로 (그래야 식이 이어집니다) 여기서
         * 원래 줄 번호를 보고 되살립니다 — 이것이 없으면 여러 줄 딕셔너리가
         * 한 줄로 뭉개집니다. 그건 '줄을 붙이거나 자르지 않는다'는 약속을 깨는 짓입니다.
         *
         * 들여쓰기는 계산하지 않고 **원래 칸을 그대로** 씁니다. 괄호 안에서 줄을
         * 어디에 맞출지는 사람이 뜻을 담아 고른 것이라, 기계가 다시 고르면 대개
         * 더 나빠집니다 (한 단계씩 더 밀어 넣어 봤더니 실제로 그랬습니다). */
        if (!at_line_start && nbr > 0 && t->line > prev_line) {
            o_trim_line(&o);
            o_str(&o, "\n");
            o_spaces(&o, t->col);
            at_line_start = false;
            force_space = false;
        } else if (at_line_start) {
            o_spaces(&o, depth * STEP);
            at_line_start = false;
        } else if (force_space || needs_space(prev, t, prev_unary, in_brace)) {
            o_str(&o, " ");
        }
        force_space = false;
        if (after_colon) func_here[nbr] = false;
        o_str(&o, tok_text(t));

        /* 이 토큰이 부호였는지 다음 바퀴를 위해 적어 둡니다 —
         * 앞에 값이 없었다면 -1 의 - 이고, 있었다면 a - 1 의 - 입니다. */
        prev_unary = (is_op(t, "-") || is_op(t, "+") || is_op(t, "~") || is_op(t, "!"))
                     && !(prev && ends_a_value(prev));

        if (t->kind == T_OP && t->text && t->text[1] == 0) {
            char c = t->text[0];
            if ((c == '(' || c == '[' || c == '{') && nbr < (int)sizeof brackets)
                brackets[nbr++] = c;
            else if ((c == ')' || c == ']' || c == '}') && nbr > 0)
                nbr--;
        }

        prev = t;
        prev_line = t->line;
        /* 여러 줄에 걸친 글자값이면 줄 수를 따라갑니다 */
        for (const char *q = tok_text(t); *q; q++) if (*q == '\n') prev_line++;
    }

    tokenlist_free(toks);

    /* 파일 끝: 줄바꿈 하나로 (빈 파일은 빈 파일 그대로) */
    o_trim_line(&o);
    while (o.len > 0 && o.p[o.len - 1] == '\n') o.p[--o.len] = 0;
    if (o.len > 0) o_str(&o, "\n");
    if (!o.p) o.p = xstrdup("");

    /* 원래 CRLF 로 적힌 파일이면 CRLF 로 돌려 놓습니다 — 줄 끝을 통째로 바꿔
     * 버리면 고친 곳이 몇 줄이든 파일 전체가 바뀐 것으로 보입니다. */
    if (strstr(text, "\r\n")) {
        Out c;
        memset(&c, 0, sizeof c);
        for (size_t k = 0; k < o.len; k++) {
            if (o.p[k] == '\n') o_add(&c, "\r\n", 2);
            else o_add(&c, o.p + k, 1);
        }
        if (!c.p) c.p = xstrdup("");
        free(o.p);
        return c.p;
    }
    return o.p;
}

/* ============================================================
 * lumi fmt
 * ============================================================ */

typedef struct { int changed, looked, failed; bool check_only; } FmtRun;

static void fmt_file(const char *path, FmtRun *r)
{
    char *text = read_text_file(path);
    if (!text) return;
    r->looked++;

    char *neat = fmt_source(text);
    if (!neat) {
        printf("%s: (skipped - it does not parse yet)\n", path);
        r->failed++;
        free(text);
        return;
    }

    if (strcmp(text, neat) != 0) {
        r->changed++;
        if (r->check_only) {
            printf("%s: would change\n", path);
        } else {
            FILE *f = plat_fopen(path, "wb");
            if (f) {
                fwrite(neat, 1, strlen(neat), f);
                fclose(f);
                printf("%s: tidied\n", path);
            } else {
                printf("%s: could not write to it\n", path);
                r->failed++;
            }
        }
    }
    free(neat);
    free(text);
}

static void fmt_dir(const char *dir, FmtRun *r)
{
    char **names = plat_listdir(dir);
    if (names) {
        for (size_t i = 0; names[i]; i++) {
            size_t n = strlen(names[i]);
            if (n < 5 || strcmp(names[i] + n - 5, ".lumi") != 0) continue;
            char *full = xsprintf("%s%s%s", dir, plat_dir_sep(), names[i]);
            fmt_file(full, r);
            free(full);
        }
        plat_free_names(names);
    }
    char **subs = plat_subdirs(dir);
    if (subs) {
        for (size_t i = 0; subs[i]; i++) {
            char *full = xsprintf("%s%s%s", dir, plat_dir_sep(), subs[i]);
            fmt_dir(full, r);
            free(full);
        }
        plat_free_names(subs);
    }
}

int run_fmt(const char *where, bool check_only)
{
    if (!where || !*where) where = ".";
    FmtRun r;
    memset(&r, 0, sizeof r);
    r.check_only = check_only;

    if (plat_dir_exists(where))       fmt_dir(where, &r);
    else if (plat_file_exists(where)) fmt_file(where, &r);
    else {
        printf("Nothing to tidy at '%s'\n", where);
        return 1;
    }

    printf("\n----------------------------------------\n");
    if (r.changed == 0)   printf("  %d file(s), already tidy\n", r.looked);
    else if (check_only)  printf("  %d file(s), %d would change\n", r.looked, r.changed);
    else                  printf("  %d file(s), %d tidied\n", r.looked, r.changed);
    printf("----------------------------------------\n");

    if (r.failed) return 1;
    return (check_only && r.changed) ? 1 : 0;
}
