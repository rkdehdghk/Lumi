/* 렉서 — 소스코드를 토큰으로 쪼갭니다.
 * 파이썬처럼 줄바꿈(NEWLINE)과 들여쓰기(INDENT/DEDENT)도 토큰으로 만듭니다. */
#include "lumi.h"

#include <errno.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>


static const char *KEYWORDS[] = {
    "val", "if", "elif", "else", "while", "func", "return",
    "true", "false", "none", "and", "or", "not", "print",
    "bring", "up", "for", "in", "break", "continue",
    "global", "local", "switch", "case", "default",
    "class", "from", "this", "super", "use", "abstract",
    "try", "catch", "safe", "always", "error", "test", NULL
};

/* 자료형 이름 — 예약어가 아니라, 뒤에 <이름> 이 오면 선언이 됩니다 */
static const char *TYPE_NAMES[] = {
    "int", "float", "num", "char", "str", "text", "seq", "bytes",
    /* 아래 넷은 **바꾸지 않고 보기만** 합니다 (coerce_to_type 참고) */
    "bool", "list", "tuple", "dict", NULL
};

/* 자료형 이름이 아닌데 형변환 함수인 이름 (지금은 없습니다) */
static const char *EXTRA_CONVERT[] = { NULL };

static bool in_list(const char *const *list, const char *w)
{
    for (size_t i = 0; list[i]; i++) if (strcmp(list[i], w) == 0) return true;
    return false;
}

bool is_keyword(const char *w)     { return in_list(KEYWORDS, w); }
bool is_type_name(const char *w)   { return in_list(TYPE_NAMES, w); }
bool is_convert_name(const char *w)
{
    return in_list(TYPE_NAMES, w) || in_list(EXTRA_CONVERT, w);
}

/* 한글처럼 ASCII 가 아닌 글자도 이름에 쓸 수 있습니다 (val 이름 = ...).
 * ponytail: 0x80 이상 바이트는 모두 '글자'로 봅니다. 유니코드 분류표를
 * 들고 오지 않으려는 것이며, 필요해지면 그때 표를 넣으세요. */
static bool ident_start(unsigned char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c >= 0x80;
}
static bool ident_char(unsigned char c)
{
    return ident_start(c) || (c >= '0' && c <= '9');
}
static bool ascii_digit(unsigned char c) { return c >= '0' && c <= '9'; }
static bool ascii_alnum(unsigned char c)
{
    return ascii_digit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

/* 여는 주석 `/. ... ./` 를 건너뜁니다. i 는 여는 `/.` 위에 있어야 합니다.
 * 여러 줄에 걸칠 수 있으므로 줄 수도 같이 셉니다. */
static void skip_block_comment(const char *src, size_t n, size_t *i, int *line)
{
    int open_line = *line;
    size_t k = *i + 2;
    while (k + 1 < n && !(src[k] == '.' && src[k + 1] == '/')) {
        if (src[k] == '\n') (*line)++;
        k++;
    }
    if (k + 1 >= n)
        lumi_error(open_line, "this comment is opened with '/.' but never closed. "
                              "Close it with './'");
    *i = k + 2;
}

/* ---------------- 토큰 목록 ---------------- */

/* 지금 읽고 있는 토큰이 시작하는 칸.  tok_push 가 토큰마다 적어 둡니다
 * (fmt 가 괄호 안 여러 줄 식의 들여쓰기를 원래대로 지킬 때 씁니다). */
static int cur_col = 0;

static void tok_push(TokenList *t, Token tok)
{
    tok.col = cur_col;
    if (t->len == t->cap) {
        t->cap = t->cap ? t->cap * 2 : 256;
        t->items = (Token *)xrealloc(t->items, t->cap * sizeof(Token));
    }
    t->items[t->len++] = tok;
}

void tokenlist_free(TokenList *t)
{
    for (size_t i = 0; i < t->len; i++) {
        free(t->items[i].text);
        if (t->items[i].str) release(obj_val(t->items[i].str));
    }
    free(t->items);
    free(t);
}

/* 진법 표기 (0b1011 / 0xff) 안의 글자들을 정수로 */
static long long digits_to_int(const char *digits, size_t n, int base, int line)
{
    const char *prefix = base == 2 ? "0b" : "0x";
    const char *full = base == 2 ? "binary" : "hexadecimal";
    const char *desc = base == 2 ? "digit (0 or 1)" : "digit (0-9, a-f)";
    if (n == 0)
        lumi_error(line, "'%s' needs at least one %s %s after it, for example %s%s",
                   prefix, full, desc, prefix, base == 2 ? "1011" : "1f");
    long long value = 0;
    for (size_t i = 0; i < n; i++) {
        char c = digits[i];
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else d = 99;
        if (d >= base) {
            char *text = (char *)xmalloc(n + 1);
            memcpy(text, digits, n);
            text[n] = 0;
            lumi_error(line, "'%s%s' is not a valid %s number: after '%s' use only a %s",
                       prefix, text, full, prefix, desc);
        }
        if (value > (0x7FFFFFFFFFFFFFFFLL - d) / base)
            lumi_error(line, "this whole number is too big for Lumi "
                             "(the largest is 9223372036854775807)");
        value = value * base + d;
    }
    return value;
}

/* 16진 글자 하나를 0~15 로 (아니면 -1) */
static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* 켜 두면 주석을 토큰으로 내보내고, 숫자·글자의 원문도 함께 담습니다.
 * lumi fmt 만 켭니다 — 평소 경로는 한 바이트도 달라지지 않습니다. */
static bool keep_source = false;

/* 주석 하나를 토큰으로 담습니다.  col 은 주석이 시작하는 칸 (바이트 기준) — fmt 가
 * 코드 뒤에 붙은 주석을 원래 자리에 그대로 두려고 씁니다 (줄 맞춰 놓은 것 보존). */
static void push_comment(TokenList *t, const char *src, size_t from, size_t to,
                         int line, long long col, bool own_line)
{
    Token tok;
    memset(&tok, 0, sizeof(tok));
    tok.kind = T_COMMENT;
    tok.line = line;
    tok.inum = col;
    tok.own_line = own_line;
    tok.text = (char *)xmalloc(to - from + 1);
    memcpy(tok.text, src + from, to - from);
    tok.text[to - from] = 0;
    tok_push(t, tok);
}

/* 원문 조각을 토큰의 text 칸에 담아 둡니다 (fmt 가 그대로 다시 찍으려고) */
static void keep_raw(Token *t, const char *src, size_t from, size_t to)
{
    if (!keep_source) return;
    t->text = (char *)xmalloc(to - from + 1);
    memcpy(t->text, src + from, to - from);
    t->text[to - from] = 0;
}

TokenList *tokenize(const char *source)
{
    TokenList *tokens = (TokenList *)xmalloc(sizeof(TokenList));
    memset(tokens, 0, sizeof(*tokens));

    size_t indent_stack[256];
    size_t indent_top = 0;
    indent_stack[0] = 0;

    size_t i = 0, n = strlen(source);
    int line = 1;
    int paren_depth = 0;
    bool at_line_start = true;
    size_t line_begin = 0;      /* 이 줄이 시작하는 바이트 자리 (fmt 가 칸을 셀 때) */

    static const char *three_ops[] = { "**=", "<<=", ">>=", NULL };
    static const char *two_ops[] = {
        "==", "!=", "<=", ">=", "&&", "||", "**", "<<", ">>",
        "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "++", "--",
        "?.", "??", NULL
    };
    static const char one_ops[] = "+-*/%()<>=,:![]{}&|^~.";

    #define ADD(k) do { Token t; memset(&t, 0, sizeof(t)); \
                        t.kind = (k); t.line = line; tok_push(tokens, t); } while (0)
    #define ADD_TEXT(k, s, len) do { Token t; memset(&t, 0, sizeof(t)); \
                        t.kind = (k); t.line = line; \
                        t.text = (char *)xmalloc((len) + 1); \
                        memcpy(t.text, (s), (len)); t.text[len] = 0; \
                        tok_push(tokens, t); } while (0)

    while (i < n) {
        /* ---------- 줄 시작: 들여쓰기 재기 ---------- */
        if (at_line_start && paren_depth == 0) {
            size_t col = 0;
            while (i < n && (source[i] == ' ' || source[i] == '\t')) {
                col += source[i] == '\t' ? 4 : 1;
                i++;
            }
            /* 줄 앞에 여는 주석이 있으면 먼저 걷어냅니다 (들여쓰기 칸 수는 그대로) */
            while (i + 1 < n && source[i] == '/' && source[i + 1] == '.') {
                size_t cs = i;
                int cline = line;
                skip_block_comment(source, n, &i, &line);
                if (keep_source)
                    push_comment(tokens, source, cs, i, cline, (long long)col, true);
                while (i < n && (source[i] == ' ' || source[i] == '\t')) i++;
            }
            /* 빈 줄이나 주석만 있는 줄은 들여쓰기 계산에서 뺍니다.
             * '\r' 도 줄 끝으로 봅니다 — 이걸 빠뜨리면 CRLF 파일에서 빈 줄이
             * '빈 줄'로 안 보여 칸 수 0 으로 읽히고, 열려 있던 블록이 통째로
             * 닫혀 버립니다 (윈도우에서 적은 .lumi 가 통째로 안 열렸습니다). */
            if (i >= n || source[i] == '\n' || source[i] == '\r'
                || (source[i] == '/' && i + 1 < n && source[i + 1] == '/')) {
                size_t cs = i;
                bool is_comment = i < n && source[i] == '/';
                while (i < n && source[i] != '\n') i++;
                if (keep_source && is_comment)
                    push_comment(tokens, source, cs, i, line, (long long)col, true);
                if (i < n) { i++; line++; line_begin = i; }
                continue;
            }
            if (col > indent_stack[indent_top]) {
                if (indent_top + 1 >= 256)
                    lumi_error(line, "Indentation is nested too deeply");
                indent_stack[++indent_top] = col;
                ADD(T_INDENT);
            } else if (col < indent_stack[indent_top]) {
                while (col < indent_stack[indent_top]) {
                    indent_top--;
                    ADD(T_DEDENT);
                }
                if (col != indent_stack[indent_top])
                    lumi_error(line, "Indentation does not match");
            }
            at_line_start = false;
            continue;
        }

        char c = source[i];
        cur_col = (int)(i - line_begin);

        /* ---------- 세미콜론: Lumi 는 쓰지 않습니다 ---------- */
        if (c == ';')
            lumi_error(line, "Lumi does not use semicolons (;). Just remove the ';' "
                             "and put each statement on its own line.");

        /* ---------- 줄바꿈 ---------- */
        if (c == '\n') {
            line++;
            i++;
            line_begin = i;
            if (paren_depth == 0) {
                if (tokens->len > 0) {
                    TokKind k = tokens->items[tokens->len - 1].kind;
                    if (k != T_NEWLINE && k != T_INDENT && k != T_DEDENT)
                        ADD(T_NEWLINE);
                }
                at_line_start = true;
            }
            continue;
        }

        /* ---------- 공백 ---------- */
        if (c == ' ' || c == '\t' || c == '\r') { i++; continue; }

        /* ---------- 주석 ---------- */
        /* 여기까지 왔다는 것은 이 줄에 이미 코드가 있다는 뜻입니다 (줄 맨 앞의
         * 주석은 위쪽 들여쓰기 자리에서 걷어냅니다) — 그래서 칸 수는 -1 입니다. */
        if (c == '/' && i + 1 < n && source[i + 1] == '/') {
            size_t cs = i;
            while (i < n && source[i] != '\n') i++;
            if (keep_source)
                push_comment(tokens, source, cs, i, line, (long long)(cs - line_begin), false);
            continue;
        }
        /* 열고 닫는 주석 — 여러 줄도 됩니다 */
        if (c == '/' && i + 1 < n && source[i + 1] == '.') {
            size_t cs = i;
            int cline = line;
            skip_block_comment(source, n, &i, &line);
            if (keep_source)
                push_comment(tokens, source, cs, i, cline, (long long)(cs - line_begin), false);
            continue;
        }

        /* ---------- 숫자 ---------- */
        if (ascii_digit((unsigned char)c)) {
            size_t num_start = i;
            if (c == '0' && i + 1 < n
                && (source[i + 1] == 'b' || source[i + 1] == 'B'
                    || source[i + 1] == 'x' || source[i + 1] == 'X')) {
                int base = (source[i + 1] == 'b' || source[i + 1] == 'B') ? 2 : 16;
                i += 2;
                size_t ds = i;
                while (i < n && ascii_alnum((unsigned char)source[i])) i++;
                if (i < n && source[i] == '.')
                    lumi_error(line, "A %s number cannot have a point (.); "
                                     "only whole numbers can be written this way",
                               base == 2 ? "binary" : "hexadecimal");
                Token t;
                memset(&t, 0, sizeof(t));
                t.kind = T_NUMBER;
                t.line = line;
                t.inum = digits_to_int(source + ds, i - ds, base, line);
                keep_raw(&t, source, num_start, i);
                tok_push(tokens, t);
                continue;
            }
            size_t start = i;
            bool dot_seen = false;
            while (i < n && (ascii_digit((unsigned char)source[i])
                             || (source[i] == '.' && !dot_seen))) {
                if (source[i] == '.') dot_seen = true;
                i++;
            }
            char *text = (char *)xmalloc(i - start + 1);
            memcpy(text, source + start, i - start);
            text[i - start] = 0;
            Token t;
            memset(&t, 0, sizeof(t));
            t.kind = T_NUMBER;
            t.line = line;
            if (dot_seen) {
                t.is_float = true;
                t.fnum = strtod(text, NULL);
            } else {
                errno = 0;
                t.inum = strtoll(text, NULL, 10);
                if (errno == ERANGE)
                    lumi_error(line, "this whole number is too big for Lumi "
                                     "(the largest is 9223372036854775807)");
            }
            free(text);
            keep_raw(&t, source, num_start, i);
            tok_push(tokens, t);
            continue;
        }

        /* ---------- 문자열 보간 ($"..." / $'...') ---------- */
        size_t lit_start = i;
        bool is_fstring = false;
        if (c == '$' && i + 1 < n && (source[i + 1] == '"' || source[i + 1] == '\'')) {
            is_fstring = true;
            i++;
            c = source[i];
        }

        if (c == '"' || c == '\'') {
            char quote = c;
            bool raw = c == '\'';
            i++;
            size_t cap = 32, len = 0;
            uint32_t *buf = (uint32_t *)xmalloc(cap * sizeof(uint32_t));
            while (i < n && source[i] != quote) {
                if (!raw && source[i] == '\\' && i + 1 < n) {
                    char nx = source[i + 1];
                    uint32_t out = (unsigned char)nx;
                    bool known = true;
                    int hex_len = 0;    /* \xNN \uXXXX \UXXXXXXXX 의 자릿수 */
                    switch (nx) {
                        case 'n': out = '\n'; break;   /* 줄바꿈 */
                        case 't': out = '\t'; break;   /* 탭 */
                        case 'r': out = '\r'; break;   /* 줄 맨 앞으로 */
                        case 'a': out = 7;    break;   /* 삑 소리 */
                        case 'b': out = 8;    break;   /* 한 칸 뒤로 */
                        case 'f': out = 12;   break;   /* 다음 쪽 */
                        case 'v': out = 11;   break;   /* 세로 탭 */
                        case 'e': out = 27;   break;   /* 이스케이프 (색 넣을 때) */
                        case '"': case '\'': case '\\': break;
                        case 'x': hex_len = 2; break;
                        case 'u': hex_len = 4; break;
                        case 'U': hex_len = 8; break;
                        default: known = false; break;
                    }
                    if (hex_len > 0) {
                        uint32_t code = 0;
                        int k = 0;
                        for (; k < hex_len && i + 2 + (size_t)k < n; k++) {
                            int d = hex_digit(source[i + 2 + k]);
                            if (d < 0) break;
                            code = code * 16 + (uint32_t)d;
                        }
                        if (k < hex_len) {
                            free(buf);
                            lumi_error(line, "'\\%c' needs %d hexadecimal digits (0-9, a-f) right "
                                             "after it, for example \"\\%c%s\"; in a Windows path "
                                             "write \\\\ instead of \\, or use '...' text",
                                       nx, hex_len, nx,
                                       hex_len == 2 ? "41" : hex_len == 4 ? "ac00" : "0001f600");
                        }
                        /* 0 은 막습니다 — 글자 0 이 있으면 print 가 거기서 잘립니다 */
                        if (code == 0 || code > 0x10FFFF) {
                            free(buf);
                            lumi_error(line, "'\\%c%.*s' is not a letter number Lumi can use "
                                             "(letter numbers go from 1 to 10ffff)",
                                       nx, hex_len, source + i + 2);
                        }
                        if (len + 1 >= cap) { cap *= 2; buf = (uint32_t *)xrealloc(buf, cap * sizeof(uint32_t)); }
                        buf[len++] = code;
                        i += 2 + (size_t)hex_len;
                        continue;
                    }
                    /* 모르는 이스케이프는 역슬래시를 그대로 둡니다 — "C:\사진\개.png"
                     * 같은 윈도우 경로가 통째로 살아 있게 (파이썬과 같은 규칙). */
                    if (!known) {
                        if (len + 1 >= cap) { cap *= 2; buf = (uint32_t *)xrealloc(buf, cap * sizeof(uint32_t)); }
                        buf[len++] = '\\';
                    }
                    if (len + 1 >= cap) { cap *= 2; buf = (uint32_t *)xrealloc(buf, cap * sizeof(uint32_t)); }
                    if ((unsigned char)nx >= 0x80) {
                        /* 여러 바이트짜리 글자는 그대로 한 글자로 옮깁니다 */
                        size_t clen = 1;
                        while (i + 1 + clen < n && ((unsigned char)source[i + 1 + clen] & 0xC0) == 0x80)
                            clen++;
                        size_t got = 0;
                        uint32_t *one = utf8_to_cp(source + i + 1, clen, &got);
                        for (size_t k = 0; k < got; k++) {
                            if (len + 1 >= cap) { cap *= 2; buf = (uint32_t *)xrealloc(buf, cap * sizeof(uint32_t)); }
                            buf[len++] = one[k];
                        }
                        free(one);
                        i += 1 + clen;
                        continue;
                    }
                    buf[len++] = out;
                    i += 2;
                    continue;
                }
                if (source[i] == '\n') line++;
                /* 한 글자(코드포인트) 읽기 */
                size_t clen = 1;
                while (i + clen < n && ((unsigned char)source[i + clen] & 0xC0) == 0x80) clen++;
                size_t got = 0;
                uint32_t *one = utf8_to_cp(source + i, clen, &got);
                for (size_t k = 0; k < got; k++) {
                    if (len + 1 >= cap) { cap *= 2; buf = (uint32_t *)xrealloc(buf, cap * sizeof(uint32_t)); }
                    buf[len++] = one[k];
                }
                free(one);
                i += clen;
            }
            if (i >= n) {
                free(buf);
                lumi_error(line, "String is not closed (missing a closing %s)",
                           raw ? "single quote '" : "quote \"");
            }
            i++;
            Token t;
            memset(&t, 0, sizeof(t));
            t.kind = is_fstring ? T_FSTRING : T_STRING;
            t.line = line;
            t.str = str_new(buf, len);
            free(buf);
            keep_raw(&t, source, lit_start, i);
            tok_push(tokens, t);
            continue;
        }

        /* ---------- 이름 / 예약어 ---------- */
        if (ident_start((unsigned char)c)) {
            size_t start = i;
            while (i < n && ident_char((unsigned char)source[i])) i++;
            char *word = (char *)xmalloc(i - start + 1);
            memcpy(word, source + start, i - start);
            word[i - start] = 0;
            Token t;
            memset(&t, 0, sizeof(t));
            t.kind = is_keyword(word) ? T_KEYWORD : T_IDENT;
            t.line = line;
            t.text = word;
            tok_push(tokens, t);
            continue;
        }

        /* ---------- 세 글자 연산자 ---------- */
        if (i + 2 < n) {
            bool matched = false;
            for (size_t k = 0; three_ops[k]; k++) {
                if (strncmp(source + i, three_ops[k], 3) == 0) {
                    ADD_TEXT(T_OP, source + i, 3);
                    i += 3;
                    matched = true;
                    break;
                }
            }
            if (matched) continue;
        }

        /* ---------- 두 글자 연산자 ---------- */
        if (i + 1 < n) {
            bool matched = false;
            for (size_t k = 0; two_ops[k]; k++) {
                if (strncmp(source + i, two_ops[k], 2) == 0) {
                    ADD_TEXT(T_OP, source + i, 2);
                    i += 2;
                    matched = true;
                    break;
                }
            }
            if (matched) continue;
        }

        /* ---------- 한 글자 연산자/기호 ---------- */
        if (strchr(one_ops, c) && c != 0) {
            if (c == '(' || c == '[' || c == '{') paren_depth++;
            else if (c == ')' || c == ']' || c == '}')
                paren_depth = paren_depth > 0 ? paren_depth - 1 : 0;
            ADD_TEXT(T_OP, source + i, 1);
            i++;
            continue;
        }

        lumi_error(line, "Unexpected character: '%c'", c);
    }

    /* ---------- 파일 끝 정리 ---------- */
    if (tokens->len > 0) {
        TokKind k = tokens->items[tokens->len - 1].kind;
        if (k != T_NEWLINE && k != T_INDENT && k != T_DEDENT) ADD(T_NEWLINE);
    }
    while (indent_top > 0) { indent_top--; ADD(T_DEDENT); }
    ADD(T_EOF);
    return tokens;
}

TokenList *tokenize_keep_source(const char *source)
{
    keep_source = true;
    /* longjmp 로 되돌아왔을 때도 값이 살아 있어야 하므로 volatile 입니다 */
    TokenList *volatile t = NULL;
    /* 문법 오류로 longjmp 가 나가더라도 깃발은 내려놓아야 합니다 */
    jmp_buf saved;
    memcpy(saved, lumi_jmp, sizeof saved);
    if (setjmp(lumi_jmp) == 0) t = tokenize(source);
    keep_source = false;
    memcpy(lumi_jmp, saved, sizeof saved);
    if (!t) longjmp(lumi_jmp, 1);
    return t;
}
