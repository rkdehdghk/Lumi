/* 내장 함수들 — interp.c 안에 그대로 이어 붙는 부분입니다.
 * 부르는 차례는 interpreter.py 의 eval_call 과 똑같이 맞췄습니다. */

/* ---------------- 이름 모음 ---------------- */

/* 대소문자를 가리지 않는 앞부분 비교 (머리말 이름 찾을 때).  운영체제마다
 * strncasecmp / _strnicmp 로 이름이 달라서 직접 적었습니다. */
static int hex_digit_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* %EC%95%88 처럼 적힌 것을 원래 바이트로 되돌립니다 (웹 주소 규칙).
 * plus 가 true 면 '+' 도 빈칸으로 봅니다 (물음표 뒤에서 쓰는 규칙). */
static char *url_undo(const char *s, size_t n, bool plus)
{
    char *out = (char *)xmalloc(n + 1);
    size_t j = 0;
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '%' && i + 2 < n) {
            int h1 = hex_digit_value(s[i + 1]), h2 = hex_digit_value(s[i + 2]);
            if (h1 >= 0 && h2 >= 0) { out[j++] = (char)(h1 * 16 + h2); i += 2; continue; }
        }
        out[j++] = (plus && s[i] == '+') ? ' ' : s[i];
    }
    out[j] = 0;
    return out;
}

static int _strnicmp_lumi(const char *a, const char *b, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return x - y;
        if (!x) return 0;
    }
    return 0;
}

static bool name_in(const char *name, const char *const *list)
{
    for (size_t i = 0; list[i]; i++) if (strcmp(name, list[i]) == 0) return true;
    return false;
}

static const char *BASE_NAMES[]       = { "bin", "hex", NULL };
static const char *TEXT_CHECK_NAMES[] = { "isalpha", "isdigit", "isalgit",
                                          "hasalpha", "hasdigit", "hasalgit", NULL };
static const char *SEARCH_NAMES[]     = { "incount", "infind", NULL };
static const char *PAIR_NAMES[]       = { "inpair", NULL };
static const char *PAIR_SHAPES[]      = { "list", "tuple", "dict", NULL };
static const char *PLACE_ARG_NAMES[]  = { "upper", "lower", "strip", NULL };
static const char *PARTS_NAMES[]      = { "split", "join", "replace", NULL };
static const char *FORMAT_NAMES[]     = { "format", NULL };
static const char *SORT_NAMES[]       = { "sort", "insor", NULL };
static const char *SHUFFLE_NAMES[]    = { "shuffle", NULL };
static const char *BYTES_NAMES[]      = { "encode", "decode", NULL };
static const char *LIST_EDIT_NAMES[]  = { "insert", "erase", NULL };
static const char *DICT_EDIT_NAMES[]  = { "del", NULL };
static const char *FILE_NAMES[]       = { "read", "write", "append", "writeline",
                                          "isfile", "isdir", "newfile", "newdir", "delfile", "deldir", "renamefile",
                                          "movefile",
                                          "open", "close", "readline", "isend",
                                          "move", "where", NULL };
/* 파일/폴더 이름 하나만 받는 것들 (인코딩도, 내용도 없습니다) */
static const char *FILE_ASK_NAMES[]   = { "isfile", "isdir", "newfile", "newdir", "delfile", "deldir", NULL };
/* 폴더 훑기와 경로 다루기 */
static const char *PATH_NAMES[]       = { "listdir", "filename", "foldername",
                                          "pathjoin", "fullpath", NULL };
/* listdir 의 '무엇을' — 따옴표 없이 적습니다 (open 의 모양처럼) */
static const char *LIST_KINDS[]       = { "file", "dir", NULL };
/* 흩뜨리기(해시)와 옮겨 적기(인코딩) */
static const char *DIGEST_NAMES[]     = { "sha256", "md5", NULL };
static const char *B64_NAMES[]        = { "base64", "unbase64", NULL };
/* 다른 프로그램 부르기 */
static const char *SHELL_NAMES[]      = { "shell", "shellcode", NULL };
/* 작은 웹 서버.  사람들은 libraries/http.lumi 의 http.serve 로 부릅니다 —
 * 이 이름은 그 라이브러리가 쓰는 낮은 층입니다. */
static const char *SERVE_NAMES[]      = { "httpserve", NULL };
/* 남의 C 라이브러리 불러 쓰기 — 자세한 것은 src/ffi.c 맨 위 */
static const char *FFI_NAMES[]        = { "cload", "cclose", "chas", "ccall",
                                          "cbuf", "cfree", "cget", "cput",
                                          "ctext", "cbytes", NULL };
/* 무늬 찾기 (정규표현식).  is/has 는 기존 isalpha/hasalpha 가족과 같은 뜻입니다. */
static const char *MATCH_NAMES[]      = { "ismatch", "hasmatch", "findmatch",
                                          "findmatches", "findgroups",
                                          "replacematch", "splitmatch", NULL };
/* 열어 둔 파일 하나만 받는 것들 */
static const char *FILE_USE_NAMES[]   = { "close", "readline", "isend", "where", NULL };
/* open 의 '어떻게 열까' — 따옴표 없이 적습니다 (inpair 의 모양처럼) */
static const char *FILE_MODES[]       = { "read", "write", "append", NULL };
static const char *TIME_NAMES[]       = { "time", "now", "sleep", "clock", "date",
                                          "timeformat", "timeparse", "timezone",
                                          "dateadd", "datediff", "isleap", "timed",
                                          "uptime", "runtime", NULL };
static const char *UTIL_NAMES[]       = { "rand", "filter", "sum", "clamp", "env", "getenv",
                                          "args", NULL };
static const char *ASSERT_NAMES[]     = { "assert", "assertsame", NULL };
static const char *VEC_MATH_NAMES[]   = { "add", "sub", "mul", "div", "mod", "pow", "neg", NULL };
static const char *TASK_NAMES[]       = { "start", "wait", NULL };




/* ---------------- 작은 도우미 ---------------- */

#define ARGS  (n->v.call.args)
#define NARG  (n->v.call.args.len)
#define ARG(i) eval(in, n->v.call.args.items[i], env)

static Str *require_text(Interp *in, const char *what, Value v, const char *role, int line)
{
    if (v.kind != V_STR) {
        char *r = R(in, v);
        lumi_error(line, "%s needs text (a string) %s but got %s", what, role, r);
    }
    return AS_STR(v);
}

/* upper/lower/strip/erase 의 '어디를' 을 읽습니다 */
static Place read_place(Interp *in, Node *arg, Env *env)
{
    Place p;
    memset(&p, 0, sizeof(p));
    if (arg->kind == N_RANGESPEC) {
        p.kind = PLACE_RANGE;
        if (arg->v.rangespec.start) {
            Value s = eval(in, arg->v.rangespec.start, env);
            p.has_start = true;
            p.start = slice_bound(in, s, arg->line);
            release(s);
        }
        if (arg->v.rangespec.end) {
            Value e = eval(in, arg->v.rangespec.end, env);
            p.has_end = true;
            p.end = slice_bound(in, e, arg->line);
            release(e);
        }
        return p;
    }
    p.kind = PLACE_VALUE;
    p.value = eval(in, arg, env);
    return p;
}

static void free_place(Place *p)
{
    if (p->kind == PLACE_VALUE) release(p->value);
}

/* ---------------- bin / hex ---------------- */

static Value to_base_text(Interp *in, const char *what, Value v, int line)
{
    int base = strcmp(what, "bin") == 0 ? 2 : 16;
    const char *prefix = base == 2 ? "0b" : "0x";
    Value num = to_number_for(in, what, v, line);
    long long number;
    if (num.kind == V_FLOAT) {
        if (num.as.f != floor(num.as.f)) {
            char *s = S(in, v);
            release(num);
            lumi_error(line, "%s needs a whole number but got '%s'", what, s);
        }
        number = (long long)num.as.f;
    } else {
        number = num.as.i;
    }
    release(num);

    unsigned long long mag = number < 0 ? (unsigned long long)(-(number + 1)) + 1
                                        : (unsigned long long)number;
    char digits[80];
    int k = 0;
    if (mag == 0) digits[k++] = '0';
    while (mag) {
        int d = (int)(mag % (unsigned)base);
        digits[k++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
        mag /= (unsigned)base;
    }
    Buf b = {0};
    if (number < 0) buf_str(&b, "-");
    buf_str(&b, prefix);
    for (int i = k - 1; i >= 0; i--) buf_add(&b, &digits[i], 1);
    char *text = buf_done(&b);
    Value out = str_value(text);
    free(text);
    return out;
}

/* ---------------- 문자열 검사 ---------------- */

static bool char_is(const char *kind, uint32_t c)
{
    bool alpha = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    bool digit = (c >= '0' && c <= '9');
    if (strcmp(kind, "alpha") == 0) return alpha;
    if (strcmp(kind, "digit") == 0) return digit;
    return alpha || digit;              /* algit */
}

static Value text_check(Interp *in, const char *what, Value v, int line)
{
    bool has_mode = strncmp(what, "has", 3) == 0;
    const char *kind = what + (has_mode ? 3 : 2);
    if (v.kind != V_STR) {
        char *r = R(in, v);
        lumi_error(line, "%s needs text (a string) but got %s", what, r);
    }
    Str *s = AS_STR(v);
    if (has_mode) {
        for (size_t i = 0; i < s->len; i++)
            if (char_is(kind, s->cp[i])) return bool_val(true);
        return bool_val(false);
    }
    if (s->len == 0) return bool_val(false);
    for (size_t i = 0; i < s->len; i++)
        if (!char_is(kind, s->cp[i])) return bool_val(false);
    return bool_val(true);
}

/* ---------------- incount / infind ---------------- */

static Value search_sequence(Interp *in, const char *what, Value item, Value seq, int line)
{
    Seq *positions = seq_new(V_LIST);

    if (seq.kind == V_STR) {
        if (item.kind != V_STR) {
            char *r = R(in, item);
            release(obj_val(positions));
            lumi_error(line, "%s in text (a string) looks for a smaller piece of text, "
                             "so its first value must be a string, but got %s", what, r);
        }
        Str *h = AS_STR(seq), *nd = AS_STR(item);
        if (nd->len == 0) {
            release(obj_val(positions));
            lumi_error(line, "%s needs something to look for, but the first value is "
                             "empty text", what);
        }
        for (size_t i = 0; nd->len <= h->len && i + nd->len <= h->len; ) {
            if (memcmp(h->cp + i, nd->cp, nd->len * sizeof(uint32_t)) == 0) {
                seq_push(positions, int_val((long long)i));
                i += nd->len;                    /* 겹치지 않게 */
            } else {
                i++;
            }
        }
    } else if (seq.kind == V_BYTES) {
        BytesObj *h = AS_BYTES(seq);
        uint8_t one[1];
        const uint8_t *needle;
        size_t nlen;
        if (item.kind == V_BYTES) {
            needle = AS_BYTES(item)->data;
            nlen = AS_BYTES(item)->len;
            if (nlen == 0) {
                release(obj_val(positions));
                lumi_error(line, "%s needs something to look for, but the first value "
                                 "is empty bytes", what);
            }
        } else if (item.kind != V_BOOL && IS_NUM(item)) {
            char opname[32];
            snprintf(opname, sizeof opname, "%s on bytes", what);
            long long v = require_int(in, item, line, opname);
            if (v < 0 || v > 255) {
                release(obj_val(positions));
                lumi_error(line, "%s in bytes looks for a number from 0 to 255 but got %lld",
                           what, v);
            }
            one[0] = (uint8_t)v;
            needle = one;
            nlen = 1;
        } else {
            char *r = R(in, item);
            release(obj_val(positions));
            lumi_error(line, "%s in bytes looks for a number from 0 to 255 or for smaller "
                             "bytes, but got %s", what, r);
            return NONE_VAL;
        }
        for (size_t i = 0; nlen <= h->len && i + nlen <= h->len; ) {
            if (memcmp(h->data + i, needle, nlen) == 0) {
                seq_push(positions, int_val((long long)i));
                i += nlen;
            } else {
                i++;
            }
        }
    } else if (seq.kind == V_LIST || seq.kind == V_TUPLE) {
        Seq *s = AS_SEQ(seq);
        for (size_t i = 0; i < s->len; i++)
            if (values_equal(in, item, s->items[i], line))
                seq_push(positions, int_val((long long)i));
    } else if (seq.kind == V_DICT) {
        release(obj_val(positions));
        lumi_error(line, "%s needs a sequence (text, list, tuple, or bytes); a dictionary "
                         "has no positions, so use 'has(dictionary, key)' instead", what);
    } else {
        char *r = R(in, seq);
        release(obj_val(positions));
        lumi_error(line, "%s needs a sequence (text, list, tuple, or bytes) to look in, "
                         "but got %s", what, r);
    }

    if (strcmp(what, "incount") == 0) {
        long long count = (long long)positions->len;
        release(obj_val(positions));
        return int_val(count);
    }
    return obj_val(positions);
}

/* ---------------- inpair ---------------- */

/* 시퀀스는 [자리, 값], 딕셔너리는 [키, 값] 짝.
 * 모양(list/tuple/dict)은 바깥과 짝 둘 다에 함께 걸립니다. */
static Value pair_sequence(Interp *in, const char *shape, Value seq, int line)
{
    bool want_dict  = strcmp(shape, "dict") == 0;
    bool want_tuple = strcmp(shape, "tuple") == 0;

    Dict *src = NULL;                 /* 딕셔너리를 받았을 때 */
    Seq *items = NULL;                /* 시퀀스를 받았을 때 */
    if (seq.kind == V_DICT) {
        src = AS_DICT(seq);
    } else if (seq.kind == V_LIST || seq.kind == V_TUPLE || seq.kind == V_STR
               || seq.kind == V_BYTES) {
        items = items_of(in, seq, "inpair", line);
    } else {
        char *r = R(in, seq);
        lumi_error(line, "inpair needs a sequence (text, list, tuple, or bytes) or a "
                         "dictionary but got %s", r);
        return NONE_VAL;
    }

    size_t count = src ? src->len : items->len;
    Dict *outd = want_dict ? dict_new() : NULL;
    Seq *outs = want_dict ? NULL : seq_new(want_tuple ? V_TUPLE : V_LIST);

    for (size_t i = 0; i < count; i++) {
        Value key = src ? retain(src->e[i].key) : int_val((long long)i);
        Value val = retain(src ? src->e[i].val : items->items[i]);
        if (want_dict) {
            dict_set(outd, key, val);
        } else {
            Seq *pair = seq_new(want_tuple ? V_TUPLE : V_LIST);
            seq_push(pair, key);
            seq_push(pair, val);
            seq_push(outs, obj_val(pair));
        }
    }
    if (items) release(obj_val(items));
    return want_dict ? obj_val(outd) : obj_val(outs);
}

/* ---------------- upper / lower / strip ---------------- */

static Value change_case(Interp *in, const char *what, Place *where, Value text, int line)
{
    Str *s = place_text(in, what, text, line);
    bool up = strcmp(what, "upper") == 0;
    uint32_t *buf = (uint32_t *)xmalloc((s->len + 1) * sizeof(uint32_t));
    memcpy(buf, s->cp, s->len * sizeof(uint32_t));

    if (where->kind == PLACE_ALL) {
        for (size_t i = 0; i < s->len; i++)
            if (buf[i] < 0x10000)
                buf[i] = up ? (uint32_t)towupper((wint_t)buf[i])
                            : (uint32_t)towlower((wint_t)buf[i]);
    } else {
        size_t np = 0;
        size_t *places = places_of(in, what, where, s->len, line, "text", &np);
        for (size_t k = 0; k < np; k++) {
            size_t i = places[k];
            if (buf[i] < 0x10000)
                buf[i] = up ? (uint32_t)towupper((wint_t)buf[i])
                            : (uint32_t)towlower((wint_t)buf[i]);
        }
        free(places);
    }
    Value out = obj_val(str_new(buf, s->len));
    free(buf);
    return out;
}

static Value strip_spaces(Interp *in, Place *where, Value text, int line)
{
    Str *s = place_text(in, "strip", text, line);
    size_t np = 0;
    size_t *places = places_of(in, "strip", where, s->len, line, "text", &np);
    bool *marked = (bool *)xmalloc((s->len ? s->len : 1) * sizeof(bool));
    memset(marked, 0, (s->len ? s->len : 1) * sizeof(bool));
    for (size_t k = 0; k < np; k++) marked[places[k]] = true;
    free(places);

    uint32_t *buf = (uint32_t *)xmalloc((s->len + 1) * sizeof(uint32_t));
    size_t len = 0;
    for (size_t i = 0; i < s->len; i++)
        if (!(marked[i] && is_strip_space(s->cp[i]))) buf[len++] = s->cp[i];
    free(marked);
    Value out = obj_val(str_new(buf, len));
    free(buf);
    return out;
}

/* ---------------- split / join / replace ---------------- */

static Value split_text(Interp *in, bool has_sep, Value sep, Value text, int line)
{
    Str *s = place_text(in, "split", text, line);
    Seq *out = seq_new(V_LIST);

    if (!has_sep) {                               /* 공백 기준 (여러 칸도 한 번으로) */
        size_t i = 0;
        while (i < s->len) {
            while (i < s->len && is_split_space(s->cp[i])) i++;
            if (i >= s->len) break;
            size_t start = i;
            while (i < s->len && !is_split_space(s->cp[i])) i++;
            seq_push(out, obj_val(str_new(s->cp + start, i - start)));
        }
        return obj_val(out);
    }

    Str *d = require_text(in, "split", sep, "to cut with", line);
    if (d->len == 0) {
        release(obj_val(out));
        lumi_error(line, "split needs something to cut with, but the separator is empty "
                         "text; to get the characters one by one use list(text) instead");
    }
    size_t start = 0, i = 0;
    while (i + d->len <= s->len) {
        if (memcmp(s->cp + i, d->cp, d->len * sizeof(uint32_t)) == 0) {
            seq_push(out, obj_val(str_new(s->cp + start, i - start)));
            i += d->len;
            start = i;
        } else {
            i++;
        }
    }
    seq_push(out, obj_val(str_new(s->cp + start, s->len - start)));
    return obj_val(out);
}

static Value join_seq(Interp *in, bool has_glue, Value glue, Value seq, int line)
{
    Str *g = NULL;
    if (has_glue) g = require_text(in, "join", glue, "to put between the pieces", line);
    if (seq.kind == V_DICT)
        lumi_error(line, "join needs a sequence (list, tuple, or text) to join; a "
                         "dictionary has two halves, so use keys(...) or values(...) first");
    if (seq.kind != V_LIST && seq.kind != V_TUPLE && seq.kind != V_STR) {
        char *r = R(in, seq);
        lumi_error(line, "join needs a list, tuple, or text to join but got %s", r);
    }

    Buf b = {0};
    size_t count = seq.kind == V_STR ? AS_STR(seq)->len : AS_SEQ(seq)->len;
    char *glue_u = g ? str_to_utf8(g) : xstrdup("");
    for (size_t i = 0; i < count; i++) {
        if (i) buf_str(&b, glue_u);
        if (seq.kind == V_STR) {
            Str *one = str_new(AS_STR(seq)->cp + i, 1);
            buf_take(&b, str_to_utf8(one));
            release(obj_val(one));
        } else {
            to_str_into(in, AS_SEQ(seq)->items[i], &b);
        }
    }
    free(glue_u);
    char *text = buf_done(&b);
    Value out = str_value(text);
    free(text);
    return out;
}

static Value replace_text(Interp *in, Value oldv, Value newv, Value text, int line)
{
    Str *s = place_text(in, "replace", text, line);
    Str *o = require_text(in, "replace", oldv, "to look for", line);
    Str *w = require_text(in, "replace", newv, "to put there instead", line);
    if (o->len == 0)
        lumi_error(line, "replace needs something to look for, but the first value is "
                         "empty text");

    size_t cap = s->len + 16, len = 0;
    uint32_t *buf = (uint32_t *)xmalloc(cap * sizeof(uint32_t));
    size_t i = 0;
    while (i < s->len) {
        if (i + o->len <= s->len
            && memcmp(s->cp + i, o->cp, o->len * sizeof(uint32_t)) == 0) {
            if (len + w->len + 1 > cap) {
                cap = (len + w->len + 1) * 2;
                buf = (uint32_t *)xrealloc(buf, cap * sizeof(uint32_t));
            }
            memcpy(buf + len, w->cp, w->len * sizeof(uint32_t));
            len += w->len;
            i += o->len;
        } else {
            if (len + 2 > cap) { cap *= 2; buf = (uint32_t *)xrealloc(buf, cap * sizeof(uint32_t)); }
            buf[len++] = s->cp[i++];
        }
    }
    Value out = obj_val(str_new(buf, len));
    free(buf);
    return out;
}

/* ---------------- format (서식 맞추기) ----------------
 * format(값들..., 서식문자열) — 집안 규칙대로 다룰 글자가 맨 뒤입니다.
 * 서식 안의 {} 자리에 값을 채웁니다:
 *   {}      적은 차례대로            {0} {1}   번호로 골라서
 *   {name}  딕셔너리를 넘겼을 때 키 이름으로
 *   {:...}  채우는 방법 (아래 format_spec_into 참고)
 *   {{ }}   중괄호를 글자 그대로 쓰고 싶을 때
 */

static size_t cp_to_utf8_one(uint32_t c, char *out)
{
    if (c < 0x80) {
        out[0] = (char)c; return 1;
    } else if (c < 0x800) {
        out[0] = (char)(0xC0 | (c >> 6));
        out[1] = (char)(0x80 | (c & 0x3F)); return 2;
    } else if (c < 0x10000) {
        out[0] = (char)(0xE0 | (c >> 12));
        out[1] = (char)(0x80 | ((c >> 6) & 0x3F));
        out[2] = (char)(0x80 | (c & 0x3F)); return 3;
    } else {
        out[0] = (char)(0xF0 | (c >> 18));
        out[1] = (char)(0x80 | ((c >> 12) & 0x3F));
        out[2] = (char)(0x80 | ((c >> 6) & 0x3F));
        out[3] = (char)(0x80 | (c & 0x3F)); return 4;
    }
}

/* {: 뒤에 적는 '채우는 방법' 하나를 읽어 값을 글자로 만들어 붙입니다.
 *   [< > ^] 정렬(왼쪽/오른쪽/가운데)   [0] 빈자리를 0으로   [숫자] 최소 너비
 *   [.숫자] 소수점 아래 자릿수        [f d x X b] 실수/정수/16진/2진 */
static void format_spec_into(Interp *in, Value v, const char *spec, Buf *b, int line)
{
    char align = 0;
    int width = 0;
    int prec = -1;
    char type = 0;
    bool zero_pad = false;

    const char *p = spec;
    if (*p == '<' || *p == '>' || *p == '^') { align = *p++; }
    if (*p == '0') { zero_pad = true; p++; }
    if (*p >= '0' && *p <= '9') {
        width = 0;
        while (*p >= '0' && *p <= '9') { width = width * 10 + (*p - '0'); p++; }
    }
    if (!align && (*p == '<' || *p == '>' || *p == '^')) { align = *p++; }
    if (zero_pad && !align) align = '>';
    if (*p == '.') {
        p++;
        prec = 0;
        while (*p >= '0' && *p <= '9') { prec = prec * 10 + (*p - '0'); p++; }
    }
    if (*p) type = *p;

    char formatted[256];
    formatted[0] = 0;

    if (type == 'f' || (prec >= 0 && type == 0)) {
        double d = 0;
        if (v.kind == V_FLOAT) d = v.as.f;
        else if (v.kind == V_INT) d = (double)v.as.i;
        else {
            char *r = R(in, v);
            lumi_error(line, "format specifier '.%df' needs a number but got %s", prec >= 0 ? prec : 2, r);
        }
        int p_val = prec >= 0 ? prec : 2;
        snprintf(formatted, sizeof(formatted), "%.*f", p_val, d);
    } else if (type == 'x' || type == 'X' || type == 'b' || type == 'd') {
        long long n = 0;
        if (v.kind == V_INT) n = v.as.i;
        else if (v.kind == V_FLOAT && v.as.f == floor(v.as.f)) n = (long long)v.as.f;
        else {
            char *r = R(in, v);
            lumi_error(line, "format specifier '%c' needs an integer but got %s", type, r);
        }
        if (type == 'x') {
            snprintf(formatted, sizeof(formatted), "%llx", n);
        } else if (type == 'X') {
            snprintf(formatted, sizeof(formatted), "%llX", n);
        } else if (type == 'b') {
            unsigned long long mag = n < 0 ? (unsigned long long)(-(n + 1)) + 1 : (unsigned long long)n;
            char digits[70];
            int k = 0;
            if (mag == 0) digits[k++] = '0';
            while (mag) { digits[k++] = (char)('0' + (mag % 2)); mag /= 2; }
            int pos = 0;
            if (n < 0) formatted[pos++] = '-';
            for (int i = k - 1; i >= 0; i--) formatted[pos++] = digits[i];
            formatted[pos] = 0;
        } else {
            snprintf(formatted, sizeof(formatted), "%lld", n);
        }
    } else {
        Buf tmp = {0};
        to_str_into(in, v, &tmp);
        char *str_val = buf_done(&tmp);
        snprintf(formatted, sizeof(formatted), "%s", str_val);
        free(str_val);
    }

    size_t len = strlen(formatted);
    if (width > 0 && (size_t)width > len) {
        size_t pad = (size_t)width - len;
        char fill = zero_pad ? '0' : ' ';
        if (align == '>') {
            for (size_t i = 0; i < pad; i++) buf_add(b, &fill, 1);
            buf_str(b, formatted);
        } else if (align == '^') {
            size_t left_pad = pad / 2;
            size_t right_pad = pad - left_pad;
            for (size_t i = 0; i < left_pad; i++) buf_add(b, &fill, 1);
            buf_str(b, formatted);
            for (size_t i = 0; i < right_pad; i++) buf_add(b, &fill, 1);
        } else {
            buf_str(b, formatted);
            for (size_t i = 0; i < pad; i++) buf_add(b, &fill, 1);
        }
    } else {
        buf_str(b, formatted);
    }
}

static Value format_text(Interp *in, Node *n, Env *env, int line)
{
    if (NARG == 0)
        lumi_error(line, "format needs text (a string) as its last value, like format(\"Alex\", 20, \"name: {}, age: {}\")");

    Value *evaled_args = (Value *)xmalloc(NARG * sizeof(Value));
    for (size_t i = 0; i < NARG; i++) evaled_args[i] = ARG(i);

    Value fmt_val = evaled_args[NARG - 1];
    if (fmt_val.kind != V_STR) {
        char *r = R(in, fmt_val);
        for (size_t i = 0; i < NARG; i++) release(evaled_args[i]);
        free(evaled_args);
        lumi_error(line, "format needs text (a string) as its last value holding format placeholders, but got %s", r);
    }
    Str *fmt = AS_STR(fmt_val);

    size_t n_vals = 0;
    Value *vals = NULL;
    Dict *dict_val = NULL;

    if (NARG == 1) {
        /* No values passed, just format string */
    } else if (NARG == 2 && (evaled_args[0].kind == V_LIST || evaled_args[0].kind == V_TUPLE)) {
        Seq *s = AS_SEQ(evaled_args[0]);
        n_vals = s->len;
        vals = s->items;
    } else if (NARG == 2 && evaled_args[0].kind == V_DICT) {
        dict_val = AS_DICT(evaled_args[0]);
    } else {
        n_vals = NARG - 1;
        vals = evaled_args;
    }

    Buf b = {0};
    size_t auto_idx = 0;
    size_t i = 0;

    while (i < fmt->len) {
        uint32_t c = fmt->cp[i];
        if (c == '{') {
            if (i + 1 < fmt->len && fmt->cp[i + 1] == '{') {
                buf_str(&b, "{");
                i += 2;
                continue;
            }
            size_t j = i + 1;
            while (j < fmt->len && fmt->cp[j] != '}') j++;
            if (j >= fmt->len) {
                for (size_t k = 0; k < NARG; k++) release(evaled_args[k]);
                free(evaled_args);
                lumi_error(line, "format string has unclosed '{' without matching '}'");
            }

            size_t tag_len = j - (i + 1);
            Str *tag_str = str_new(fmt->cp + i + 1, tag_len);
            char *tag_u = str_to_utf8(tag_str);
            release(obj_val(tag_str));

            char *colon = strchr(tag_u, ':');
            char *key_part = tag_u;
            char *spec_part = NULL;
            if (colon) {
                *colon = 0;
                spec_part = colon + 1;
            }

            Value target;
            bool target_found = false;

            if (key_part[0] == 0) {
                if (auto_idx >= n_vals) {
                    free(tag_u);
                    for (size_t k = 0; k < NARG; k++) release(evaled_args[k]);
                    free(evaled_args);
                    lumi_error(line, "format positional placeholder '{}' is out of bounds (only %zu values were given)", n_vals);
                }
                target = vals[auto_idx++];
                target_found = true;
            } else {
                bool is_digit = true;
                for (char *p = key_part; *p; p++) {
                    if (*p < '0' || *p > '9') { is_digit = false; break; }
                }
                if (is_digit) {
                    int idx = atoi(key_part);
                    if (idx < 0 || (size_t)idx >= n_vals) {
                        free(tag_u);
                        for (size_t k = 0; k < NARG; k++) release(evaled_args[k]);
                        free(evaled_args);
                        lumi_error(line, "format index %d is out of bounds (only %zu values were given)", idx, n_vals);
                    }
                    target = vals[idx];
                    target_found = true;
                } else if (dict_val) {
                    Value kv = str_value(key_part);
                    Value *val_ptr = dict_find(dict_val, kv);
                    release(kv);
                    if (!val_ptr) {
                        /* key_part 는 tag_u 안을 가리키므로, 놓아 주기 전에 베껴 둡니다 */
                        char keybuf[128];
                        snprintf(keybuf, sizeof keybuf, "%s", key_part);
                        free(tag_u);
                        for (size_t k = 0; k < NARG; k++) release(evaled_args[k]);
                        free(evaled_args);
                        lumi_error_kind(line, "KeyError", "format key '%s' was not found in dictionary", keybuf);
                    }
                    target = *val_ptr;
                    target_found = true;
                } else {
                    char keybuf[128];
                    snprintf(keybuf, sizeof keybuf, "%s", key_part);
                    free(tag_u);
                    for (size_t k = 0; k < NARG; k++) release(evaled_args[k]);
                    free(evaled_args);
                    lumi_error(line, "format placeholder '{%s}' needs a dictionary as the first value", keybuf);
                }
            }

            if (target_found) {
                if (spec_part && spec_part[0] != 0) {
                    format_spec_into(in, target, spec_part, &b, line);
                } else {
                    if (target.kind == V_STR) {
                        char *u = str_to_utf8(AS_STR(target));
                        buf_str(&b, u);
                        free(u);
                    } else {
                        to_str_into(in, target, &b);
                    }
                }
            }
            free(tag_u);
            i = j + 1;
        } else if (c == '}') {
            if (i + 1 < fmt->len && fmt->cp[i + 1] == '}') {
                buf_str(&b, "}");
                i += 2;
                continue;
            }
            for (size_t k = 0; k < NARG; k++) release(evaled_args[k]);
            free(evaled_args);
            lumi_error(line, "format string has single unmatched '}'");
        } else {
            char ubuf[8];
            size_t ulen = cp_to_utf8_one(c, ubuf);
            buf_add(&b, ubuf, ulen);
            i++;
        }
    }

    for (size_t k = 0; k < NARG; k++) release(evaled_args[k]);
    free(evaled_args);

    char *result_text = buf_done(&b);
    Value out = str_value(result_text);
    free(result_text);
    return out;
}

/* ---------------- sort / insor / shuffle ---------------- */

static Value sort_sequence(Interp *in, bool has_dir, Value dir, Value seq, int line)
{
    bool reverse = sort_direction(in, has_dir, dir, line);

    if (seq.kind == V_DICT)
        lumi_error(line, "sort needs a sequence (text, list, tuple, or bytes); a "
                         "dictionary has two halves, so sort keys(...) or values(...) "
                         "instead");
    if (seq.kind == V_BYTES) {
        BytesObj *b = AS_BYTES(seq);
        uint8_t *buf = (uint8_t *)xmalloc(b->len ? b->len : 1);
        memcpy(buf, b->data, b->len);
        for (size_t i = 1; i < b->len; i++) {      /* 0~255 뿐이라 간단히 */
            uint8_t v = buf[i];
            size_t j = i;
            while (j > 0 && (reverse ? buf[j - 1] < v : buf[j - 1] > v)) { buf[j] = buf[j - 1]; j--; }
            buf[j] = v;
        }
        Value out = obj_val(bytes_new(buf, b->len));
        free(buf);
        return out;
    }
    if (seq.kind != V_LIST && seq.kind != V_TUPLE && seq.kind != V_STR) {
        char *r = R(in, seq);
        lumi_error(line, "sort needs a sequence (text, list, tuple, or bytes) to put in "
                         "order but got %s", r);
    }

    Seq *items = seq.kind == V_STR ? NULL : AS_SEQ(seq);
    size_t count = seq.kind == V_STR ? AS_STR(seq)->len : items->len;
    Value *arr = (Value *)xmalloc((count ? count : 1) * sizeof(Value));
    for (size_t i = 0; i < count; i++)
        arr[i] = seq.kind == V_STR
               ? obj_val(str_new(AS_STR(seq)->cp + i, 1))
               : retain(items->items[i]);

    check_sort_kinds(in, "sort", arr, count, line);
    merge_sort(in, arr, NULL, count, reverse, "sort", line);

    Value out;
    if (seq.kind == V_STR) {
        uint32_t *buf = (uint32_t *)xmalloc((count + 1) * sizeof(uint32_t));
        for (size_t i = 0; i < count; i++) buf[i] = AS_STR(arr[i])->cp[0];
        out = obj_val(str_new(buf, count));
        free(buf);
        for (size_t i = 0; i < count; i++) release(arr[i]);
    } else {
        Seq *o = seq_new(seq.kind);
        for (size_t i = 0; i < count; i++) seq_push(o, arr[i]);
        out = obj_val(o);
    }
    free(arr);
    return out;
}

static Value insor_key(Interp *in, long long where, Value item, int line)
{
    if (item.kind != V_LIST && item.kind != V_TUPLE
        && item.kind != V_STR && item.kind != V_BYTES) {
        char *r = R(in, item);
        lumi_error(line, "insor puts values in order by one place inside them, so every "
                         "item must be a sequence (list, tuple, text, or bytes) but got %s", r);
    }
    long long length = (long long)seq_length(item);
    long long place = where < 0 ? where + length : where;
    if (place < 0 || place >= length) {
        char *r = R(in, item);
        lumi_error(line, "insor: place %lld is outside %s (its length is %lld)",
                   where, r, length);
    }
    if (item.kind == V_STR) return obj_val(str_new(AS_STR(item)->cp + place, 1));
    if (item.kind == V_BYTES) return int_val(AS_BYTES(item)->data[place]);
    return retain(AS_SEQ(item)->items[place]);
}

static Value insor_sequence(Interp *in, bool has_dir, Value dir, Value where,
                            Value seq, int line)
{
    bool reverse = sort_direction(in, has_dir, dir, line);

    if (where.kind == V_BOOL || !IS_NUM(where)) {
        char *r = R(in, where);
        lumi_error(line, "insor needs a whole number for the place to sort by, like "
                         "insor(1, rows), but got %s", r);
    }
    if (where.kind == V_FLOAT && where.as.f != floor(where.as.f)) {
        char *r = S(in, where);
        lumi_error(line, "insor needs a whole number for the place to sort by but got %s", r);
    }
    long long at = where.kind == V_FLOAT ? (long long)where.as.f : where.as.i;

    if (seq.kind == V_DICT)
        lumi_error(line, "insor needs a list or tuple of sequences; a dictionary has two "
                         "halves, so use keys(...) or values(...) first");
    if (seq.kind != V_LIST && seq.kind != V_TUPLE) {
        char *r = R(in, seq);
        lumi_error(line, "insor needs a list or tuple whose items are sequences "
                         "(like [[\"a\", 2], [\"b\", 1]]) but got %s", r);
    }

    Seq *s = AS_SEQ(seq);
    size_t count = s->len;
    Value *items = (Value *)xmalloc((count ? count : 1) * sizeof(Value));
    Value *keys = (Value *)xmalloc((count ? count : 1) * sizeof(Value));
    for (size_t i = 0; i < count; i++) {
        items[i] = retain(s->items[i]);
        keys[i] = insor_key(in, at, s->items[i], line);
    }
    check_sort_kinds(in, "insor", keys, count, line);
    merge_sort(in, items, keys, count, reverse, "insor", line);

    Seq *o = seq_new(seq.kind);
    for (size_t i = 0; i < count; i++) { seq_push(o, items[i]); release(keys[i]); }
    free(items);
    free(keys);
    return obj_val(o);
}

static Value shuffle_sequence(Interp *in, Value seq, int line)
{
    if (seq.kind == V_DICT)
        lumi_error(line, "shuffle needs a sequence (text, list, tuple, or bytes); a "
                         "dictionary has two halves, so shuffle keys(...) or values(...) "
                         "instead");
    if (seq.kind == V_BYTES) {
        BytesObj *b = AS_BYTES(seq);
        uint8_t *buf = (uint8_t *)xmalloc(b->len ? b->len : 1);
        memcpy(buf, b->data, b->len);
        for (size_t i = b->len; i > 1; i--) {
            size_t j = rng_below(i);
            uint8_t t = buf[i - 1]; buf[i - 1] = buf[j]; buf[j] = t;
        }
        Value out = obj_val(bytes_new(buf, b->len));
        free(buf);
        return out;
    }
    if (seq.kind != V_LIST && seq.kind != V_TUPLE && seq.kind != V_STR) {
        char *r = R(in, seq);
        lumi_error(line, "shuffle needs a sequence (text, list, tuple, or bytes) to mix "
                         "up but got %s", r);
    }

    size_t count = seq.kind == V_STR ? AS_STR(seq)->len : AS_SEQ(seq)->len;
    Value *arr = (Value *)xmalloc((count ? count : 1) * sizeof(Value));
    for (size_t i = 0; i < count; i++)
        arr[i] = seq.kind == V_STR
               ? obj_val(str_new(AS_STR(seq)->cp + i, 1))
               : retain(AS_SEQ(seq)->items[i]);
    for (size_t i = count; i > 1; i--) {
        size_t j = rng_below(i);
        Value t = arr[i - 1]; arr[i - 1] = arr[j]; arr[j] = t;
    }

    Value out;
    if (seq.kind == V_STR) {
        uint32_t *buf = (uint32_t *)xmalloc((count + 1) * sizeof(uint32_t));
        for (size_t i = 0; i < count; i++) buf[i] = AS_STR(arr[i])->cp[0];
        out = obj_val(str_new(buf, count));
        free(buf);
        for (size_t i = 0; i < count; i++) release(arr[i]);
    } else {
        Seq *o = seq_new(seq.kind);
        for (size_t i = 0; i < count; i++) seq_push(o, arr[i]);
        out = obj_val(o);
    }
    free(arr);
    return out;
}

/* ---------------- each ---------------- */

/* 시퀀스의 값마다 함수를 한 번씩 불러, 그 결과를 리스트로 모읍니다 (파이썬의 map).
 * sort/shuffle 과 달리 받은 모양을 지키지 않고 늘 리스트를 돌려줍니다 —
 * 함수가 돌려주는 값은 글자도 바이트도 아닐 수 있기 때문입니다. */
static Value each_sequence(Interp *in, Value fn, const char *fname, Value seq, int line)
{
    if (fn.kind != V_FUNC && fn.kind != V_BOUND && fn.kind != V_CLASS) {
        char *r = R(in, fn);
        lumi_error(line, "each needs a function to call as its first value, like "
                         "each(double, xs) or each(func x: x * 2, xs), but got %s", r);
    }
    if (seq.kind == V_DICT)
        lumi_error(line, "each needs a sequence (text, list, tuple, or bytes); a "
                         "dictionary has two halves, so walk keys(...) or values(...) "
                         "instead");
    if (seq.kind != V_LIST && seq.kind != V_TUPLE && seq.kind != V_STR
        && seq.kind != V_BYTES) {
        char *r = R(in, seq);
        lumi_error(line, "each needs a sequence (text, list, tuple, or bytes) to walk "
                         "through but got %s", r);
    }

    Seq *items = items_of(in, seq, "each", line);
    Seq *out = seq_new(V_LIST);
    for (size_t i = 0; i < items->len; i++) {
        Value arg = retain(items->items[i]);       /* 소유권은 call_value 로 넘어갑니다 */
        seq_push(out, call_value(in, fn, &arg, 1, fname, line));
    }
    release(obj_val(items));
    return obj_val(out);
}

/* ---------------- encode / decode ---------------- */

static const char *encoding_of(Interp *in, const char *what, bool given, Value v, int line)
{
    if (!given) return "utf-8";
    if (v.kind != V_STR) {
        char *r = R(in, v);
        lumi_error(line, "%s needs the encoding name as text (a string) but got %s", what, r);
    }
    char *u = str_to_utf8(AS_STR(v));
    /* 앞뒤 공백을 털고 소문자로 */
    char *p = u;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    size_t n = strlen(p);
    while (n && (p[n-1] == ' ' || p[n-1] == '\t' || p[n-1] == '\n' || p[n-1] == '\r')) n--;
    char low[64];
    size_t k = 0;
    for (size_t i = 0; i < n && k < sizeof(low) - 1; i++)
        low[k++] = (char)(p[i] >= 'A' && p[i] <= 'Z' ? p[i] + 32 : p[i]);
    low[k] = 0;
    const char *canon = encoding_canonical(low);
    if (!canon) {
        char *r = R(in, v);
        free(u);
        lumi_error(line, "%s does not know the encoding %s; the ones it knows are: "
                         "ascii, cp949, euc-kr, latin-1, utf-16, utf-8", what, r);
    }
    free(u);
    return canon;
}

/* ---------------- 리스트 · 딕셔너리 고치기 ---------------- */

static Seq *edit_list(Interp *in, const char *what, Value v, int line)
{
    if (v.kind == V_LIST) return AS_SEQ(v);
    if (v.kind == V_DICT)
        lumi_error(line, "%s needs a list but got a dictionary, which has keys instead "
                         "of places; put a value in with d[key] = value, check for one "
                         "with has(d, key), and delete one with del(d, key)", what);
    char *r = R(in, v);
    lumi_error(line, "%s needs a list to work on but got %s; strings and tuples cannot "
                     "be changed, so build a new one with slices instead", what, r);
    return NULL;
}

static Value insert_into_list(Interp *in, Value where, Value value, Value lst, int line)
{
    Seq *s = edit_list(in, "insert", lst, line);
    if (where.kind == V_BOOL || !IS_NUM(where)) {
        char *r = R(in, where);
        lumi_error(line, "insert needs a whole number for the place to put the value in "
                         "but got %s", r);
    }
    if (where.kind == V_FLOAT && where.as.f != floor(where.as.f)) {
        char *r = S(in, where);
        lumi_error(line, "insert needs a whole number for the place but got %s", r);
    }
    long long place = where.kind == V_FLOAT ? (long long)where.as.f : where.as.i;
    long long length = (long long)s->len;
    if (place < 0) place += length;
    if (place < 0 || place > length) {
        char *r = S(in, where);
        lumi_error(line, "insert: place %s is outside the list (its length is %lld, so "
                         "places 0 to %lld can be used)", r, length, length);
    }
    seq_insert(s, (size_t)place, retain(value));
    return int_val((long long)s->len);
}

static Value erase_from_list(Interp *in, Place *where, Value lst, int line)
{
    Seq *s = edit_list(in, "erase", lst, line);
    size_t np = 0;
    size_t *places = places_of(in, "erase", where, s->len, line, "list", &np);
    if (np == 0) { free(places); return int_val(0); }

    bool *marked = (bool *)xmalloc((s->len ? s->len : 1) * sizeof(bool));
    memset(marked, 0, (s->len ? s->len : 1) * sizeof(bool));
    size_t unique = 0;
    for (size_t k = 0; k < np; k++)
        if (!marked[places[k]]) { marked[places[k]] = true; unique++; }
    free(places);

    size_t w = 0;
    for (size_t i = 0; i < s->len; i++) {
        if (marked[i]) release(s->items[i]);
        else s->items[w++] = s->items[i];
    }
    s->len = w;
    free(marked);
    return int_val((long long)unique);
}

static Dict *edit_dict(Interp *in, const char *what, Value v, int line)
{
    if (v.kind == V_DICT) return AS_DICT(v);
    if (v.kind == V_LIST || v.kind == V_TUPLE || v.kind == V_STR || v.kind == V_BYTES) {
        char *r = R(in, v);
        lumi_error(line, "%s needs a dictionary but got %s, which has places instead of "
                         "keys; erase a place from a list with erase(0, xs)", what, r);
    }
    char *r = R(in, v);
    lumi_error(line, "%s needs a dictionary to work on but got %s", what, r);
    return NULL;
}

static Value delete_from_dict(Interp *in, bool has_which, Value which, Value target, int line)
{
    Dict *d = edit_dict(in, "del", target, line);

    Seq *keys = seq_new(V_LIST);
    if (!has_which) {
        for (size_t i = 0; i < d->len; i++) seq_push(keys, retain(d->e[i].key));
    } else if (which.kind == V_LIST || which.kind == V_TUPLE) {
        Seq *s = AS_SEQ(which);
        for (size_t i = 0; i < s->len; i++) seq_push(keys, retain(s->items[i]));
    } else {
        seq_push(keys, retain(which));
    }
    for (size_t i = 0; i < keys->len; i++) {
        check_dict_key(in, keys->items[i], line);
        if (!dict_find(d, keys->items[i])) {
            char *r = R(in, keys->items[i]);
            release(obj_val(keys));
            lumi_error_kind(line, "KeyError", "del: key %s is not in the dictionary; check with "
                             "has(d, key) before deleting", r);
        }
    }
    long long removed = 0;
    for (size_t i = 0; i < keys->len; i++)
        if (dict_remove(d, keys->items[i])) removed++;
    release(obj_val(keys));
    return int_val(removed);
}

/* ---------------- 텍스트 파일 ---------------- */

/* 파일 이름을 실제 경로로 바꿉니다.  "C:\..." 나 "\..." 처럼 통째로 적은 이름은
 * 그대로 쓰고, 그 밖에는 지금 실행 중인 파일이 있는 폴더 기준입니다 (bring 과 같은 규칙).
 * 폴더를 낀 상대 경로("자료/오늘.txt", "../옆폴더/a.txt")도 그대로 이어 붙습니다. */
static char *file_path_of(Interp *in, const char *what, Value v, int line)
{
    Str *s = require_text(in, what, v, "for the file name", line);
    char *name = str_to_utf8(s);
    if (!*name) {
        free(name);
        lumi_error(line, "%s needs a file name but got empty text", what);
    }
    /* 경로에는 보이지 않는 글자가 들어갈 수 없습니다.  들어왔다면 "C:\notes\tab.txt"
     * 처럼 \n \t \b ... 가 이스케이프로 읽힌 것이니, 조용히 엉뚱한 파일을 만들지 않고
     * 알려 줍니다. */
    for (char *p = name; *p; p++) {
        if ((unsigned char)*p < 0x20) {
            free(name);
            lumi_error(line, "%s got a file name with an invisible letter in it, which no path "
                             "can have; a \\ in the name was read as an escape, so write \\\\ or "
                             "/ instead of \\, or write the path as '...' text",
                       what);
        }
    }
    bool whole = name[0] == '\\' || name[0] == '/'
              || (name[1] == ':' && (name[2] == '\\' || name[2] == '/'));
    if (whole || !in->base_dir) return name;
    char *full = xsprintf("%s%s%s", in->base_dir, plat_dir_sep(), name);
    free(name);
    return full;
}

/* 윈도우 줄바꿈(\r\n)을 \n 하나로 맞춥니다 — 읽을 때만 합니다.
 * 그래서 split("\n", ...) 이 어느 컴퓨터에서 만든 파일이든 똑같이 됩니다. */
static size_t drop_cr(char *p, size_t n)
{
    size_t w = 0;
    for (size_t i = 0; i < n; i++) {
        if (p[i] == '\r' && i + 1 < n && p[i + 1] == '\n') continue;
        p[w++] = p[i];
    }
    return w;
}

/* 읽어 온 바이트를 글자로.  실패하면 인코딩이 다르다고 알려 줍니다. */
static Value bytes_to_text(Interp *in, const char *what, const char *codec,
                           const char *path, char *raw, size_t n, int line)
{
    n = drop_cr(raw, n);
    BytesObj *b = bytes_new((const uint8_t *)raw, n);
    Str *s = decode_bytes(b, codec);
    release(obj_val(b));
    if (!s)
        lumi_error(line, "%s cannot read '%s' as %s; it was probably written with a "
                         "different encoding, so read it with that one, like "
                         "read(\"cp949\", name)", what, path, codec);
    return obj_val(s);
}

static Value read_file(Interp *in, const char *codec, const char *path, int line)
{
    /* ponytail: 통째로 읽어 한 문자열로 만듭니다.  아주 큰 파일이라면
     * open(read, ...) 으로 열어 readline 으로 한 줄씩 읽으세요. */
    char *raw = read_text_file(path);
    if (!raw)
        lumi_error_kind(line, "FileNotFound", "read cannot open the file '%s'; check the name, or ask "
                         "isfile(name) first", path);
    Value out = bytes_to_text(in, "read", codec, path, raw, strlen(raw), line);
    free(raw);
    return out;
}

/* 빈 파일을 새로 만듭니다 (이미 있을 때는 부르지 않습니다) */
static void make_file(const char *path, int line)
{
    FILE *f = plat_fopen(path, "wb");
    if (!f)
        lumi_error(line, "newfile cannot make the file '%s'; check that the folder exists "
                         "and the name has no characters like * ? < > |", path);
    fclose(f);
}

static void delete_file(const char *path, int line)
{
    if (!plat_remove_file(path))
        lumi_error(line, "delfile cannot delete the file '%s'; it may be open in another "
                         "program, or read-only", path);
}

/* 폴더가 있는지 봅니다 (file_exists 는 폴더를 뺍니다) */
static bool folder_exists(const char *path)
{
    return plat_dir_exists(path);
}

/* 폴더를 새로 만듭니다.  중간 폴더가 없으면 앞에서부터 차례로 만듭니다. */
static void make_folder(const char *path, int line)
{
    if (plat_mkdir(path)) return;

    /* 한 번에 안 됐으면 위쪽 폴더부터 하나씩 만들어 봅니다 */
    char *work = xstrdup(path);
    for (size_t i = 0; work[i]; i++) {
        if (plat_is_sep(work[i]) && i > 0) {
            char saved = work[i];
            work[i] = 0;
            if (!plat_dir_exists(work)) plat_mkdir(work);
            work[i] = saved;
        }
    }
    free(work);

    if (!plat_mkdir(path) && !folder_exists(path)) {
        lumi_error(line, "newdir cannot make the folder '%s'; check that the path is valid "
                         "and the name has no characters like * ? < > |", path);
    }
}

static void delete_folder(const char *path, int line)
{
    if (!plat_rmdir_recursive(path)) {
        lumi_error(line, "deldir cannot delete the folder '%s'; it may be in use by another "
                         "program, or read-only", path);
    }
}

/* 파일을 새 자리로 옮깁니다 — 이름 바꾸기(renamefile)와 폴더 옮기기(movefile)가 같이 씁니다.
 * 다른 드라이브/파티션(C: -> D:) 으로도 옮겨집니다.
 * (있는 파일 -> 없는 이름, 둘 다 미리 확인해 두고 부릅니다) */
static void rename_file(const char *what, const char *from, const char *to, int line)
{
    if (!plat_move(from, to))
        lumi_error(line, "%s cannot %s '%s' to '%s'; it may be open in another "
                         "program, or the new name has characters like * ? < > |",
                   what, strcmp(what, "movefile") == 0 ? "move" : "rename", from, to);
}

static Value write_file(Interp *in, const char *what, const char *codec,
                        Value text, const char *path, int line)
{
    Str *s = require_text(in, what, text, "to write", line);
    BytesObj *b = encode_str(s, codec);
    if (!b)
        lumi_error(line, "%s cannot write this text in %s: it has characters that %s has "
                         "no number for; try %s(\"utf-8\", ...)", what, codec, codec, what);

    FILE *f = plat_fopen(path, strcmp(what, "append") == 0 ? "ab" : "wb");
    if (!f) {
        release(obj_val(b));
        lumi_error_kind(line, "FileError", "%s cannot open the file '%s' to write; check that the folder "
                         "exists and the file is not read-only", what, path);
    }
    bool ok = fwrite(b->data, 1, b->len, f) == b->len;
    if (fclose(f) != 0) ok = false;
    release(obj_val(b));
    if (!ok)
        lumi_error(line, "%s could not write all of the text to '%s'; the disk may be "
                         "full", what, path);
    return int_val((long long)s->len);
}

/* ---------------- 열어 두고 쓰는 파일 (open / close) ---------------- */

/* 값이 '열려 있는 파일' 인지 확인하고 꺼냅니다 */
static FileObj *open_file_of(Interp *in, const char *what, Value v, int line)
{
    if (v.kind == V_STR)
        lumi_error(line, "%s needs a file opened with open(...), not a file name; a name "
                         "does not remember how far you have read, so open it with "
                         "open(read, name) -- or read the whole file at once with "
                         "split(\"\\n\", read(name))", what);
    if (v.kind != V_FILE) {
        char *r = R(in, v);
        lumi_error(line, "%s needs a file that was opened with open(...) but got %s", what, r);
    }
    FileObj *f = AS_FILE(v);
    if (!f->fp)
        lumi_error(line, "%s cannot use the file '%s' because it is already closed; open "
                         "it again with open(...)", what, f->path);
    return f;
}

static void need_reading(FileObj *f, const char *what, int line)
{
    if (f->writing)
        lumi_error(line, "%s cannot read '%s' because it was opened for writing; open it "
                         "with open(read, name)", what, f->path);
}

static Value open_file(Interp *in, const char *mode, const char *codec,
                       const char *path, int line)
{
    bool writing = strcmp(mode, "read") != 0;
    const char *how = strcmp(mode, "write") == 0 ? "wb"
                    : strcmp(mode, "append") == 0 ? "ab" : "rb";
    FILE *fp = plat_fopen(path, how);
    if (!fp) {
        if (!writing)
            lumi_error_kind(line, "FileNotFound", "open cannot find the file '%s' to read; check the name, or "
                             "ask isfile(name) first", path);
        lumi_error_kind(line, "FileError", "open cannot open the file '%s' to write; check that the folder "
                         "exists and the file is not read-only", path);
    }
    return obj_val(file_new(fp, path, codec, writing, strcmp(mode, "append") == 0));
}

/* 파일 전체가 몇 바이트인지.  지금 자리는 그대로 되돌려 놓습니다. */
static long long file_size_of(FileObj *f)
{
    long here = ftell(f->fp);
    fseek(f->fp, 0, SEEK_END);
    long end = ftell(f->fp);
    fseek(f->fp, here, SEEK_SET);
    return (long long)end;
}

/* move(자리, 파일) — 읽고 쓰는 자리를 옮깁니다.  자리는 처음부터 센 '바이트'
 * 수이고, 음수는 리스트처럼 끝에서부터 셉니다 (-1 = 마지막 바이트).
 * 파일 크기와 같은 자리(= 맨 끝)까지 쓸 수 있습니다. */
static Value move_in_file(Interp *in, Value where, FileObj *f, int line)
{
    if (f->appending)
        lumi_error(line, "move cannot choose a place in '%s' because it was opened with "
                         "open(append, name), which always writes at the end; open it "
                         "with open(write, name) to choose the place", f->path);
    if (where.kind == V_BOOL || !IS_NUM(where)) {
        char *r = R(in, where);
        lumi_error(line, "move needs a whole number for the place to go to but got %s", r);
    }
    if (where.kind == V_FLOAT && where.as.f != floor(where.as.f)) {
        char *r = S(in, where);
        lumi_error(line, "move needs a whole number for the place but got %s", r);
    }
    long long place = where.kind == V_FLOAT ? (long long)where.as.f : where.as.i;
    long long size = file_size_of(f);
    if (place < 0) place += size;
    if (place < 0 || place > size) {
        char *r = S(in, where);
        lumi_error(line, "move: place %s is outside the file '%s' (it is %lld bytes "
                         "long, so places 0 to %lld can be used)", r, f->path, size, size);
    }
    if (fseek(f->fp, (long)place, SEEK_SET) != 0)
        lumi_error(line, "move could not go to place %lld in '%s'", place, f->path);
    return int_val(place);
}

/* 다음 한 줄.  줄 끝의 줄바꿈은 떼고 '내용만' 줍니다 (writeline 이 다시 붙입니다).
 * 끝까지 읽었는지는 isend 로 물어봅니다. */
static Value read_line_of(Interp *in, FileObj *f, int line)
{
    Buf b = {0};
    int c;
    while ((c = fgetc(f->fp)) != EOF) {
        char ch = (char)c;
        buf_add(&b, &ch, 1);
        if (c == '\n') break;
    }
    size_t n = b.len;
    char *raw = buf_done(&b);
    n = drop_cr(raw, n);                       /* \r\n -> \n */
    if (n && raw[n - 1] == '\n') n--;          /* 줄 끝의 줄바꿈은 뗍니다 */
    Value out = bytes_to_text(in, "readline", f->codec, f->path, raw, n, line);
    free(raw);
    return out;
}

/* 남은 것을 전부 */
static Value read_rest_of(Interp *in, FileObj *f, int line)
{
    Buf b = {0};
    char chunk[4096];
    size_t got;
    while ((got = fread(chunk, 1, sizeof chunk, f->fp)) > 0) buf_add(&b, chunk, got);
    size_t n = b.len;
    char *raw = buf_done(&b);
    Value out = bytes_to_text(in, "read", f->codec, f->path, raw, n, line);
    free(raw);
    return out;
}

/* 한 글자 미리 읽어 보고 도로 넣습니다 (feof 는 읽어 본 뒤에야 참이 됩니다) */
static bool file_at_end(FileObj *f)
{
    int c = fgetc(f->fp);
    if (c == EOF) return true;
    ungetc(c, f->fp);
    return false;
}

static Value write_to_open_file(Interp *in, const char *what, FileObj *f, Value text,
                                int line)
{
    if (!f->writing)
        lumi_error(line, "%s cannot write to '%s' because it was opened for reading; "
                         "open it with open(write, name) or open(append, name)",
                   what, f->path);
    Str *s = require_text(in, what, text, "to write", line);
    BytesObj *b = encode_str(s, f->codec);
    if (!b)
        lumi_error(line, "%s cannot write this text in %s: it has characters that %s "
                         "has no number for; open the file with open(\"utf-8\", write, "
                         "name)", what, f->codec, f->codec);
    bool ok = fwrite(b->data, 1, b->len, f->fp) == b->len;
    release(obj_val(b));
    if (!ok)
        lumi_error(line, "%s could not put all of the text into '%s'; the disk may be "
                         "full", what, f->path);
    return int_val((long long)s->len);
}

/* ---------------- 흩뜨리기(해시)와 옮겨 적기(인코딩) ----------------
 * 바깥 라이브러리 없이 직접 만들었습니다.  캐시 이름표, 파일이 그대로인지 보기,
 * 웹으로 주고받을 때 쓰는 것들입니다.
 * (sha256/md5 는 '되돌릴 수 없는' 지문이고, base64 는 '되돌릴 수 있는' 옮겨 적기입니다.
 *  md5 는 지문이 겹치도록 일부러 만들 수 있어 안전이 걸린 곳에는 쓰면 안 됩니다.) */

static const char B64_ALPHA[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/* 바이트 -> base64 글자.  malloc. */
static char *b64_encode(const uint8_t *d, size_t n)
{
    size_t outn = ((n + 2) / 3) * 4;
    char *out = (char *)xmalloc(outn + 1);
    size_t j = 0;
    for (size_t i = 0; i < n; i += 3) {
        unsigned v = (unsigned)d[i] << 16;
        if (i + 1 < n) v |= (unsigned)d[i + 1] << 8;
        if (i + 2 < n) v |= (unsigned)d[i + 2];
        out[j++] = B64_ALPHA[(v >> 18) & 0x3F];
        out[j++] = B64_ALPHA[(v >> 12) & 0x3F];
        out[j++] = (i + 1 < n) ? B64_ALPHA[(v >> 6) & 0x3F] : '=';
        out[j++] = (i + 2 < n) ? B64_ALPHA[v & 0x3F]        : '=';
    }
    out[j] = 0;
    return out;
}

static int b64_value(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

/* base64 글자 -> 바이트.  잘못된 글자가 있으면 NULL. */
static uint8_t *b64_decode(const char *t, size_t tn, size_t *outn)
{
    uint8_t *out = (uint8_t *)xmalloc(tn / 4 * 3 + 4);
    size_t j = 0;
    unsigned acc = 0;
    int bits = 0;
    for (size_t i = 0; i < tn; i++) {
        char c = t[i];
        if (c == '=' ) break;
        if (c == '\n' || c == '\r' || c == ' ' || c == '\t') continue;
        int v = b64_value(c);
        if (v < 0) { free(out); return NULL; }
        acc = (acc << 6) | (unsigned)v;
        bits += 6;
        if (bits >= 8) { bits -= 8; out[j++] = (uint8_t)((acc >> bits) & 0xFF); }
    }
    *outn = j;
    return out;
}

/* --- sha256 --- */
static uint32_t sha_rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

static void sha256_bytes(const uint8_t *msg, size_t len, uint8_t out[32])
{
    static const uint32_t K[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2 };
    uint32_t h[8] = { 0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
                      0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19 };

    size_t total = ((len + 8) / 64 + 1) * 64;
    uint8_t *buf = (uint8_t *)xmalloc(total);
    memset(buf, 0, total);
    memcpy(buf, msg, len);
    buf[len] = 0x80;
    uint64_t bits = (uint64_t)len * 8;
    for (int i = 0; i < 8; i++) buf[total - 1 - i] = (uint8_t)(bits >> (8 * i));

    for (size_t off = 0; off < total; off += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; i++)
            w[i] = ((uint32_t)buf[off + i * 4] << 24) | ((uint32_t)buf[off + i * 4 + 1] << 16)
                 | ((uint32_t)buf[off + i * 4 + 2] << 8) | (uint32_t)buf[off + i * 4 + 3];
        for (int i = 16; i < 64; i++) {
            uint32_t s0 = sha_rotr(w[i-15],7) ^ sha_rotr(w[i-15],18) ^ (w[i-15] >> 3);
            uint32_t s1 = sha_rotr(w[i-2],17) ^ sha_rotr(w[i-2],19) ^ (w[i-2] >> 10);
            w[i] = w[i-16] + s0 + w[i-7] + s1;
        }
        uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
        for (int i = 0; i < 64; i++) {
            uint32_t S1 = sha_rotr(e,6) ^ sha_rotr(e,11) ^ sha_rotr(e,25);
            uint32_t ch = (e & f) ^ ((~e) & g);
            uint32_t t1 = hh + S1 + ch + K[i] + w[i];
            uint32_t S0 = sha_rotr(a,2) ^ sha_rotr(a,13) ^ sha_rotr(a,22);
            uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t t2 = S0 + mj;
            hh=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
        }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
    }
    free(buf);
    for (int i = 0; i < 8; i++) {
        out[i*4]   = (uint8_t)(h[i] >> 24);
        out[i*4+1] = (uint8_t)(h[i] >> 16);
        out[i*4+2] = (uint8_t)(h[i] >> 8);
        out[i*4+3] = (uint8_t)h[i];
    }
}

/* --- md5 (옛 파일 목록과 맞춰 볼 때만 쓰세요) --- */
static uint32_t md5_rotl(uint32_t x, int c) { return (x << c) | (x >> (32 - c)); }

static void md5_bytes(const uint8_t *msg, size_t len, uint8_t out[16])
{
    static const uint32_t K[64] = {
        0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,
        0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
        0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,
        0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
        0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
        0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
        0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,
        0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391 };
    static const int R[64] = {
        7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22,
        5,9,14,20,5,9,14,20,5,9,14,20,5,9,14,20,
        4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23,
        6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21 };
    uint32_t h[4] = { 0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476 };

    size_t total = ((len + 8) / 64 + 1) * 64;
    uint8_t *buf = (uint8_t *)xmalloc(total);
    memset(buf, 0, total);
    memcpy(buf, msg, len);
    buf[len] = 0x80;
    uint64_t bits = (uint64_t)len * 8;
    for (int i = 0; i < 8; i++) buf[total - 8 + i] = (uint8_t)(bits >> (8 * i));

    for (size_t off = 0; off < total; off += 64) {
        uint32_t w[16];
        for (int i = 0; i < 16; i++)
            w[i] = (uint32_t)buf[off+i*4] | ((uint32_t)buf[off+i*4+1] << 8)
                 | ((uint32_t)buf[off+i*4+2] << 16) | ((uint32_t)buf[off+i*4+3] << 24);
        uint32_t a=h[0],b=h[1],c=h[2],d=h[3];
        for (int i = 0; i < 64; i++) {
            uint32_t f; int g;
            if (i < 16)      { f = (b & c) | ((~b) & d);        g = i; }
            else if (i < 32) { f = (d & b) | ((~d) & c);        g = (5*i + 1) % 16; }
            else if (i < 48) { f = b ^ c ^ d;                   g = (3*i + 5) % 16; }
            else             { f = c ^ (b | (~d));              g = (7*i) % 16; }
            uint32_t tmp = d; d = c; c = b;
            b = b + md5_rotl(a + f + K[i] + w[g], R[i]);
            a = tmp;
        }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d;
    }
    free(buf);
    for (int i = 0; i < 4; i++) {
        out[i*4]   = (uint8_t)h[i];
        out[i*4+1] = (uint8_t)(h[i] >> 8);
        out[i*4+2] = (uint8_t)(h[i] >> 16);
        out[i*4+3] = (uint8_t)(h[i] >> 24);
    }
}

/* 바이트를 16진수 소문자 글자로 */
static Value hex_text_of(const uint8_t *d, size_t n)
{
    static const char *HEXD = "0123456789abcdef";
    char *out = (char *)xmalloc(n * 2 + 1);
    for (size_t i = 0; i < n; i++) {
        out[i*2]     = HEXD[d[i] >> 4];
        out[i*2 + 1] = HEXD[d[i] & 0xF];
    }
    out[n * 2] = 0;
    Value v = str_value(out);
    free(out);
    return v;
}

/* 글자면 utf-8 바이트로, 바이트면 그대로.  free 는 안 해도 되게 owner 로 알려 줍니다. */
static const uint8_t *bytes_of_arg(Interp *in, const char *what, Value v,
                                   size_t *n, char **owner, int line)
{
    *owner = NULL;
    if (v.kind == V_BYTES) { *n = AS_BYTES(v)->len; return AS_BYTES(v)->data; }
    if (v.kind == V_STR) {
        size_t nb = 0;
        char *u = cp_to_utf8(AS_STR(v)->cp, AS_STR(v)->len, &nb);
        *owner = u; *n = nb;
        return (const uint8_t *)u;
    }
    char *r = R(in, v);
    lumi_error_kind(line, "TypeError",
                    "%s needs text or bytes but got %s", what, r);
    return NULL;
}

/* ---------------- 시간 관련 내장 함수 ----------------
 * 이 가족이 주고받는 기본 값은 '타임스탬프'입니다 — 1970-01-01 부터 몇 초가
 * 흘렀는지를 담은 실수 하나(V_FLOAT).  글자로 보여 줄 때만 now/timeformat 이
 * 서식을 입히고, 되돌릴 때는 timeparse 를 씁니다.
 * 시계는 두 가지를 씁니다:
 *   lumi_time_now   — 달력 시계 (사람이 아는 그 시각. 시간대·보정에 따라 움직일 수 있음)
 *   lumi_time_clock — 뒤로 가지 않는 시계 (걸린 시간을 잴 때만 씀)
 */

/* 지금의 달력 시각을 초 단위 실수로 */
static double lumi_time_now(void)
{
#if defined(_WIN32)
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return (double)(u.QuadPart - 116444736000000000ULL) / 10000000.0;
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) == 0)
        return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
    return (double)time(NULL);
#endif
}

/* 걸린 시간을 재는 시계 — 값 자체는 뜻이 없고, 두 번 재서 뺀 차이만 씁니다 */
static double lumi_time_clock(void)
{
#if defined(_WIN32)
    LARGE_INTEGER freq, count;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&count);
    return (double)count.QuadPart / (double)freq.QuadPart;
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
        return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
    return (double)clock() / CLOCKS_PER_SEC;
#endif
}

static void lumi_time_sleep(double sec)
{
#if defined(_WIN32)
    Sleep((DWORD)(sec * 1000.0));
#else
    struct timespec req;
    req.tv_sec = (time_t)sec;
    req.tv_nsec = (long)((sec - (double)req.tv_sec) * 1e9);
    nanosleep(&req, NULL);
#endif
}

/* 서식 글자에 시각을 채웁니다.  %Y %m %d %H %M %S 는 어느 컴퓨터에서나 같은
 * 결과가 나오도록 직접 만들고(윈도우 strftime 의 차이를 피합니다), 그 밖의
 * 표시는 strftime 에 맡깁니다. */
static char *lumi_format_time(const char *fmt, const struct tm *tm_buf)
{
    Buf b = {0};
    size_t i = 0, len = strlen(fmt);
    while (i < len) {
        if (fmt[i] == '%' && i + 1 < len) {
            char spec = fmt[i + 1];
            char tmp[64];
            tmp[0] = '\0';
            if (spec == 'Y') snprintf(tmp, sizeof(tmp), "%04d", tm_buf->tm_year + 1900);
            else if (spec == 'm') snprintf(tmp, sizeof(tmp), "%02d", tm_buf->tm_mon + 1);
            else if (spec == 'd') snprintf(tmp, sizeof(tmp), "%02d", tm_buf->tm_mday);
            else if (spec == 'H') snprintf(tmp, sizeof(tmp), "%02d", tm_buf->tm_hour);
            else if (spec == 'M') snprintf(tmp, sizeof(tmp), "%02d", tm_buf->tm_min);
            else if (spec == 'S') snprintf(tmp, sizeof(tmp), "%02d", tm_buf->tm_sec);
            else if (spec == '%') snprintf(tmp, sizeof(tmp), "%%");
            else {
                char s_one[3] = { '%', spec, '\0' };
                strftime(tmp, sizeof(tmp), s_one, tm_buf);
            }
            buf_str(&b, tmp);
            i += 2;
        } else {
            buf_add(&b, &fmt[i], 1);
            i++;
        }
    }
    return buf_done(&b);
}

/* TIME_NAMES 에 든 이름을 여기서 갈래 나눠 처리합니다 */
static Value eval_time_builtin(Interp *in, const char *name, Node *n, Env *env, int line)
{
    /* --- time() : 지금의 타임스탬프 --- */
    if (strcmp(name, "time") == 0) {
        if (NARG != 0) lumi_error(line, "time takes no arguments");
        return float_val(lumi_time_now());
    }

    /* --- now([서식]) : 지금을 사람이 읽는 글자로 (기본 "%Y-%m-%d %H:%M:%S") --- */

    if (strcmp(name, "now") == 0) {
        if (NARG > 1) lumi_error(line, "now takes at most one format text argument");
        double sec = lumi_time_now();
        time_t tsec = (time_t)sec;
        struct tm tm_buf;
#if defined(_WIN32)
        localtime_s(&tm_buf, &tsec);
#else
        localtime_r(&tsec, &tm_buf);
#endif
        const char *fmt = "%Y-%m-%d %H:%M:%S";
        char *utf8_fmt = NULL;
        if (NARG == 1) {
            Value v_fmt = ARG(0);
            Str *s_fmt = require_text(in, "now", v_fmt, "as format text", line);
            utf8_fmt = str_to_utf8(s_fmt);
            release(v_fmt);
            fmt = utf8_fmt;
        }
        char *formatted = lumi_format_time(fmt, &tm_buf);
        if (utf8_fmt) free(utf8_fmt);
        Value out = str_value(formatted);
        free(formatted);
        return out;
    }



    /* --- clock() : 걸린 시간 재기용 시계 --- */
    if (strcmp(name, "clock") == 0) {
        if (NARG != 0) lumi_error(line, "clock takes no arguments");
        return float_val(lumi_time_clock());
    }

    /* --- sleep(초) : 그만큼 잠깐 멈추기 --- */
    if (strcmp(name, "sleep") == 0) {
        if (NARG != 1) lumi_error(line, "sleep needs exactly one number (seconds)");
        Value v = ARG(0);
        if (!IS_NUM(v)) {
            char *s = S(in, v);
            lumi_error(line, "sleep needs a number but got '%s'", s);
        }
        double sec = v.kind == V_INT ? (double)v.as.i : v.as.f;
        if (sec < 0) lumi_error(line, "sleep time cannot be negative");
        /* 잠든 동안에는 다른 일감이 Lumi 코드를 실행할 수 있습니다. */
        g_execution_locked = false;
        plat_gil_unlock();
        lumi_time_sleep(sec);
        plat_gil_lock();
        g_execution_locked = true;
        return NONE_VAL;
    }

    /* --- date([타임스탬프]) : 연·월·일·시·분·초를 딕셔너리로 --- */
    if (strcmp(name, "date") == 0) {
        if (NARG > 1) lumi_error(line, "date takes at most one timestamp argument");
        double sec = lumi_time_now();
        if (NARG == 1) {
            Value v = ARG(0);
            if (!IS_NUM(v)) {
                char *s = S(in, v);
                lumi_error(line, "date timestamp must be a number but got '%s'", s);
            }
            sec = v.kind == V_INT ? (double)v.as.i : v.as.f;
        }
        time_t tsec = (time_t)sec;
        int ms = (int)((sec - (double)tsec) * 1000.0);
        if (ms < 0) ms = 0;
        if (ms > 999) ms = 999;
        struct tm tm_buf;
#if defined(_WIN32)
        localtime_s(&tm_buf, &tsec);
#else
        localtime_r(&tsec, &tm_buf);
#endif
        Dict *d = dict_new();
        dict_set(d, str_value("year"), int_val(tm_buf.tm_year + 1900));
        dict_set(d, str_value("month"), int_val(tm_buf.tm_mon + 1));
        dict_set(d, str_value("day"), int_val(tm_buf.tm_mday));
        dict_set(d, str_value("hour"), int_val(tm_buf.tm_hour));
        dict_set(d, str_value("minute"), int_val(tm_buf.tm_min));
        dict_set(d, str_value("second"), int_val(tm_buf.tm_sec));
        dict_set(d, str_value("millisecond"), int_val(ms));
        int wday = tm_buf.tm_wday == 0 ? 7 : tm_buf.tm_wday;
        dict_set(d, str_value("weekday"), int_val(wday));
        dict_set(d, str_value("yearday"), int_val(tm_buf.tm_yday + 1));
        dict_set(d, str_value("timestamp"), float_val(sec));
        char fmt_buf[64];
        strftime(fmt_buf, sizeof(fmt_buf), "%Y-%m-%d %H:%M:%S", &tm_buf);
        dict_set(d, str_value("formatted"), str_value(fmt_buf));
        return obj_val(d);
    }

    /* --- timeformat(서식[, 타임스탬프]) : 타임스탬프 -> 글자.
     *     둘을 바꿔 적어도(timeformat(타임스탬프, 서식)) 알아서 받습니다 --- */
    if (strcmp(name, "timeformat") == 0) {
        if (NARG < 1 || NARG > 2)
            lumi_error(line, "timeformat needs a format text and an optional timestamp");
        double sec = lumi_time_now();
        Str *fmt_str = NULL;
        Value arg0 = ARG(0);
        if (NARG == 1) {
            fmt_str = require_text(in, "timeformat", arg0, "as the format string", line);
            release(arg0);
        } else {
            Value arg1 = ARG(1);
            if (arg0.kind == V_STR) {
                fmt_str = AS_STR(arg0);
                if (!IS_NUM(arg1))
                    lumi_error(line, "timeformat timestamp must be a number");
                sec = arg1.kind == V_INT ? (double)arg1.as.i : arg1.as.f;
                release(arg1);
                release(arg0);
            } else if (IS_NUM(arg0)) {
                sec = arg0.kind == V_INT ? (double)arg0.as.i : arg0.as.f;
                fmt_str = require_text(in, "timeformat", arg1, "as format string", line);
                release(arg1);
            } else {
                lumi_error(line, "timeformat arguments must include a format string");
            }
        }
        char *utf8_fmt = str_to_utf8(fmt_str);
        time_t tsec = (time_t)sec;
        struct tm tm_buf;
#if defined(_WIN32)
        localtime_s(&tm_buf, &tsec);
#else
        localtime_r(&tsec, &tm_buf);
#endif
        char *formatted = lumi_format_time(utf8_fmt, &tm_buf);
        free(utf8_fmt);
        Value out = str_value(formatted);
        free(formatted);
        return out;
    }


    /* --- timeparse(날짜글자, 서식) : 글자 -> 타임스탬프 (timeformat 의 반대)
     *     흔한 서식 세 가지를 알아보고, 그 밖은 "연-월-일 [시:분:초]" 로 읽어 봅니다 --- */
    if (strcmp(name, "timeparse") == 0) {
        if (NARG != 2)
            lumi_error(line, "timeparse needs two text values: date string and format string");
        Value v_str = ARG(0), v_fmt = ARG(1);
        Str *s_str = require_text(in, "timeparse", v_str, "for date text", line);
        Str *s_fmt = require_text(in, "timeparse", v_fmt, "for format text", line);
        char *u_str = str_to_utf8(s_str);
        char *u_fmt = str_to_utf8(s_fmt);
        release(v_str); release(v_fmt);

        struct tm tm_buf;
        memset(&tm_buf, 0, sizeof(tm_buf));
        tm_buf.tm_mday = 1;
        bool ok = false;
        
        int y = 0, m = 0, d = 0, H = 0, M = 0, S = 0;
        if (strcmp(u_fmt, "%Y-%m-%d %H:%M:%S") == 0) {
            if (sscanf(u_str, "%d-%d-%d %d:%d:%d", &y, &m, &d, &H, &M, &S) == 6) ok = true;
        } else if (strcmp(u_fmt, "%Y-%m-%d") == 0) {
            if (sscanf(u_str, "%d-%d-%d", &y, &m, &d) == 3) ok = true;
        } else if (strcmp(u_fmt, "%H:%M:%S") == 0) {
            time_t now_sec = time(NULL);
            struct tm now_tm;
#if defined(_WIN32)
            localtime_s(&now_tm, &now_sec);
#else
            localtime_r(&now_sec, &now_tm);
#endif
            y = now_tm.tm_year + 1900; m = now_tm.tm_mon + 1; d = now_tm.tm_mday;
            if (sscanf(u_str, "%d:%d:%d", &H, &M, &S) == 3) ok = true;
        } else {
            if (sscanf(u_str, "%d-%d-%d %d:%d:%d", &y, &m, &d, &H, &M, &S) == 6) ok = true;
            else if (sscanf(u_str, "%d-%d-%d", &y, &m, &d) == 3) ok = true;
        }

        if (!ok || y < 1900 || m < 1 || m > 12 || d < 1 || d > 31) {
            free(u_str); free(u_fmt);
            lumi_error(line, "timeparse failed to parse date text with given format");
        }
        tm_buf.tm_year = y - 1900;
        tm_buf.tm_mon = m - 1;
        tm_buf.tm_mday = d;
        tm_buf.tm_hour = H;
        tm_buf.tm_min = M;
        tm_buf.tm_sec = S;
        tm_buf.tm_isdst = -1;

        free(u_str); free(u_fmt);
        time_t tsec = mktime(&tm_buf);
        if (tsec == (time_t)-1)
            lumi_error(line, "timeparse generated invalid timestamp");
        return float_val((double)tsec);
    }

    /* --- timezone() : UTC 와 몇 초 차이인지 + 시간대 이름 --- */
    if (strcmp(name, "timezone") == 0) {
        if (NARG != 0) lumi_error(line, "timezone takes no arguments");
        time_t now_sec = time(NULL);
        struct tm gmt_tm, loc_tm;
#if defined(_WIN32)
        gmtime_s(&gmt_tm, &now_sec);
        localtime_s(&loc_tm, &now_sec);
#else
        gmtime_r(&now_sec, &gmt_tm);
        localtime_r(&now_sec, &loc_tm);
#endif
        time_t gmt_t = mktime(&gmt_tm);
        time_t loc_t = mktime(&loc_tm);
        long offset = (long)difftime(loc_t, gmt_t);
        char tzname_buf[64] = "UTC";
#if defined(_WIN32)
        TIME_ZONE_INFORMATION tzi;
        if (GetTimeZoneInformation(&tzi) != TIME_ZONE_ID_INVALID) {
            WideCharToMultiByte(CP_UTF8, 0, tzi.StandardName, -1, tzname_buf, sizeof(tzname_buf), NULL, NULL);
        }
#endif
        Dict *d = dict_new();
        dict_set(d, str_value("offset"), int_val(offset));
        dict_set(d, str_value("name"), str_value(tzname_buf));
        return obj_val(d);
    }

    /* --- dateadd(타임스탬프, 얼마, 단위) : 날짜 더하기·빼기.
     *     달·해는 날짜 수가 들쭉날쭉해서 달력(struct tm)으로 옮겨 셉니다 --- */
    if (strcmp(name, "dateadd") == 0) {
        if (NARG != 3) lumi_error(line, "dateadd needs three values: timestamp, amount, and unit text");
        Value v_t = ARG(0), v_amt = ARG(1), v_unit = ARG(2);
        if (!IS_NUM(v_t) || !IS_NUM(v_amt)) {
            release(v_t); release(v_amt); release(v_unit);
            lumi_error(line, "dateadd timestamp and amount must be numbers");
        }
        double tsec = v_t.kind == V_INT ? (double)v_t.as.i : v_t.as.f;
        double amt = v_amt.kind == V_INT ? (double)v_amt.as.i : v_amt.as.f;
        Str *s_u = require_text(in, "dateadd", v_unit, "for unit", line);
        char *unit = str_to_utf8(s_u);
        release(v_t); release(v_amt); release(v_unit);

        double out_t = tsec;
        if (strcmp(unit, "second") == 0 || strcmp(unit, "seconds") == 0 || strcmp(unit, "sec") == 0) {
            out_t += amt;
        } else if (strcmp(unit, "minute") == 0 || strcmp(unit, "minutes") == 0 || strcmp(unit, "min") == 0) {
            out_t += amt * 60.0;
        } else if (strcmp(unit, "hour") == 0 || strcmp(unit, "hours") == 0) {
            out_t += amt * 3600.0;
        } else if (strcmp(unit, "day") == 0 || strcmp(unit, "days") == 0) {
            out_t += amt * 86400.0;
        } else if (strcmp(unit, "week") == 0 || strcmp(unit, "weeks") == 0) {
            out_t += amt * 604800.0;
        } else if (strcmp(unit, "month") == 0 || strcmp(unit, "months") == 0 || strcmp(unit, "year") == 0 || strcmp(unit, "years") == 0) {
            time_t base_t = (time_t)tsec;
            struct tm tm_buf;
#if defined(_WIN32)
            localtime_s(&tm_buf, &base_t);
#else
            localtime_r(&base_t, &tm_buf);
#endif
            int iamt = (int)amt;
            if (strcmp(unit, "month") == 0 || strcmp(unit, "months") == 0) {
                tm_buf.tm_mon += iamt;
            } else {
                tm_buf.tm_year += iamt;
            }
            time_t new_t = mktime(&tm_buf);
            if (new_t != (time_t)-1) out_t = (double)new_t + (tsec - floor(tsec));
        } else {
            free(unit);
            lumi_error(line, "dateadd unit must be seconds, minutes, hours, days, weeks, months, or years");
        }
        free(unit);
        return float_val(out_t);
    }

    /* --- datediff(시작, 끝, 단위) : 두 시각의 차이 (끝 - 시작) --- */
    if (strcmp(name, "datediff") == 0) {
        if (NARG != 3) lumi_error(line, "datediff needs three values: start timestamp, end timestamp, and unit text");
        Value v_t1 = ARG(0), v_t2 = ARG(1), v_unit = ARG(2);
        if (!IS_NUM(v_t1) || !IS_NUM(v_t2)) {
            release(v_t1); release(v_t2); release(v_unit);
            lumi_error(line, "datediff timestamps must be numbers");
        }
        double t1 = v_t1.kind == V_INT ? (double)v_t1.as.i : v_t1.as.f;
        double t2 = v_t2.kind == V_INT ? (double)v_t2.as.i : v_t2.as.f;
        Str *s_u = require_text(in, "datediff", v_unit, "for unit", line);
        char *unit = str_to_utf8(s_u);
        release(v_t1); release(v_t2); release(v_unit);

        double diff = t2 - t1;
        double divisor = 1.0;
        if (strcmp(unit, "second") == 0 || strcmp(unit, "seconds") == 0 || strcmp(unit, "sec") == 0) divisor = 1.0;
        else if (strcmp(unit, "minute") == 0 || strcmp(unit, "minutes") == 0 || strcmp(unit, "min") == 0) divisor = 60.0;
        else if (strcmp(unit, "hour") == 0 || strcmp(unit, "hours") == 0) divisor = 3600.0;
        else if (strcmp(unit, "day") == 0 || strcmp(unit, "days") == 0) divisor = 86400.0;
        else if (strcmp(unit, "week") == 0 || strcmp(unit, "weeks") == 0) divisor = 604800.0;
        else if (strcmp(unit, "year") == 0 || strcmp(unit, "years") == 0) divisor = 31536000.0;
        else {
            free(unit);
            lumi_error(line, "datediff unit must be seconds, minutes, hours, days, weeks, or years");
        }
        free(unit);
        return float_val(diff / divisor);
    }

    /* --- isleap(연도) : 윤년인가? 아주 큰 수를 주면 타임스탬프로 보고 연도를 뽑습니다 --- */
    if (strcmp(name, "isleap") == 0) {
        if (NARG != 1) lumi_error(line, "isleap needs exactly one year number or timestamp");
        Value v = ARG(0);
        if (!IS_NUM(v)) {
            char *s = S(in, v);
            release(v);
            lumi_error(line, "isleap needs a year number but got '%s'", s);
        }
        long long y = v.kind == V_INT ? v.as.i : (long long)v.as.f;
        release(v);
        if (y > 100000000LL) {
            time_t tsec = (time_t)y;
            struct tm tm_buf;
#if defined(_WIN32)
            localtime_s(&tm_buf, &tsec);
#else
            localtime_r(&tsec, &tm_buf);
#endif
            y = tm_buf.tm_year + 1900;
        }
        bool leap = (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
        return bool_val(leap);
    }

    /* --- timed(함수, 인자...) : 불러 보고 [결과, 걸린 시간] 으로 돌려줍니다 --- */
    if (strcmp(name, "timed") == 0) {
        if (NARG < 1) lumi_error(line, "timed needs at least a function to call");
        Value callee = ARG(0);
        size_t fn_nargs = NARG - 1;
        Value *fn_args = fn_nargs ? (Value *)xmalloc(fn_nargs * sizeof(Value)) : NULL;
        for (size_t i = 0; i < fn_nargs; i++) fn_args[i] = ARG(i + 1);

        double t0 = lumi_time_clock();
        Value res = call_value(in, callee, fn_args, fn_nargs, "timed", line);
        double t1 = lumi_time_clock();

        free(fn_args);
        release(callee);

        Seq *seq = seq_new(V_LIST);
        seq_push(seq, res);
        seq_push(seq, float_val(t1 - t0));
        return obj_val(seq);
    }

    /* --- uptime() / runtime() : 프로그램이 시작된 뒤 흐른 시간 (같은 함수, 이름만 둘) --- */
    if (strcmp(name, "uptime") == 0 || strcmp(name, "runtime") == 0) {
        if (NARG != 0) lumi_error(line, "%s takes no arguments", name);
        return float_val(lumi_time_clock() - in->start_clock);
    }



    return NONE_VAL;
}

/* ---------------- 알짜 유틸 (rand / filter / sum / clamp / env) ----------------
 * 자주 쓰는데 직접 짜기는 번거로운 것들만 모았습니다. */
static Value eval_util_builtin(Interp *in, const char *name, Node *n, Env *env, int line)
{
    /* --- rand([최소, 최대]) : 인자 모양에 따라 네 가지로 갈립니다
     *     없음 -> 0.0~1.0 실수 / 정수 n -> 1~n / 두 수 -> 그 사이 / 시퀀스 -> 하나 뽑기 --- */
    if (strcmp(name, "rand") == 0) {
        if (NARG > 2) lumi_error(line, "rand takes at most two arguments");
        if (NARG == 0) {
            double r = (double)rng_below(1000000000) / 1000000000.0;
            return float_val(r);
        }
        if (NARG == 1) {
            Value arg0 = ARG(0);
            if (arg0.kind == V_INT) {
                long long max_val = arg0.as.i;
                if (max_val <= 0) return int_val(0);
                long long r = 1 + (long long)rng_below((size_t)max_val);
                return int_val(r);
            }
            if (arg0.kind == V_FLOAT) {
                double r = ((double)rng_below(1000000000) / 1000000000.0) * arg0.as.f;
                return float_val(r);
            }
            if (arg0.kind == V_LIST || arg0.kind == V_TUPLE) {
                Seq *seq = AS_SEQ(arg0);
                if (seq->len == 0) { release(arg0); return NONE_VAL; }
                size_t idx = rng_below(seq->len);
                Value out = retain(seq->items[idx]);
                release(arg0);
                return out;
            }
            char *s = S(in, arg0);
            lumi_error(line, "rand argument must be a number or sequence but got '%s'", s);
        }
        Value a = ARG(0), b = ARG(1);
        if (!IS_NUM(a) || !IS_NUM(b)) {
            release(a); release(b);
            lumi_error(line, "rand(a, b) needs two numbers");
        }
        if (a.kind == V_INT && b.kind == V_INT) {
            long long min_val = a.as.i < b.as.i ? a.as.i : b.as.i;
            long long max_val = a.as.i > b.as.i ? a.as.i : b.as.i;
            long long range = max_val - min_val + 1;
            long long r = min_val + (long long)rng_below((size_t)range);
            return int_val(r);
        }
        double min_val = a.kind == V_INT ? (double)a.as.i : a.as.f;
        double max_val = b.kind == V_INT ? (double)b.as.i : b.as.f;
        double r = min_val + ((double)rng_below(1000000000) / 1000000000.0) * (max_val - min_val);
        return float_val(r);
    }


    /* --- filter(함수, 시퀀스) : 참이라고 답한 것만 남깁니다 (담은 모양 그대로) --- */
    if (strcmp(name, "filter") == 0) {
        if (NARG != 2) lumi_error(line, "filter needs two arguments: function and sequence");
        Value callee = ARG(0), seq_v = ARG(1);
        if (seq_v.kind == V_LIST || seq_v.kind == V_TUPLE) {
            Seq *src = AS_SEQ(seq_v);
            Seq *out = seq_new(seq_v.kind);
            for (size_t i = 0; i < src->len; i++) {
                Value arg = retain(src->items[i]);   /* 소유권은 call_value 로 넘어갑니다 */
                Value cond = call_value(in, callee, &arg, 1, "filter", line);
                if (value_truthy(cond)) seq_push(out, retain(src->items[i]));
                release(cond);
            }
            release(callee); release(seq_v);
            return obj_val(out);
        }
        if (seq_v.kind == V_STR) {
            Str *src = AS_STR(seq_v);
            Buf b = {0};
            for (size_t i = 0; i < src->len; i++) {
                Value item = obj_val(str_new(&src->cp[i], 1));  /* 글자 하나짜리 문자열 */
                Value arg = retain(item);            /* 소유권은 call_value 로 넘어갑니다 */
                Value cond = call_value(in, callee, &arg, 1, "filter", line);
                if (value_truthy(cond)) {
                    char *utf8 = str_to_utf8(AS_STR(item));
                    buf_str(&b, utf8);
                    free(utf8);
                }
                release(item); release(cond);
            }
            release(callee); release(seq_v);
            char *res_str = buf_done(&b);
            Value out = str_value(res_str);
            free(res_str);
            return out;
        }
        char *s = S(in, seq_v);
        release(callee); release(seq_v);
        lumi_error(line, "filter needs a sequence as second argument but got '%s'", s);
    }

    /* --- sum(시퀀스) : 다 더하기. 실수가 하나라도 섞이면 결과도 실수 --- */
    if (strcmp(name, "sum") == 0) {
        if (NARG != 1) lumi_error(line, "sum needs exactly one sequence of numbers");
        Value seq_v = ARG(0);
        if (seq_v.kind != V_LIST && seq_v.kind != V_TUPLE) {
            char *s = S(in, seq_v);
            release(seq_v);
            lumi_error(line, "sum needs a list or tuple but got '%s'", s);
        }
        Seq *seq = AS_SEQ(seq_v);
        bool has_float = false;
        long long sum_i = 0;
        double sum_f = 0.0;
        for (size_t i = 0; i < seq->len; i++) {
            Value item = seq->items[i];
            if (!IS_NUM(item)) {
                char *s = S(in, item);
                release(seq_v);
                lumi_error(line, "sum sequence items must be numbers but got '%s'", s);
            }
            if (item.kind == V_FLOAT) {
                if (!has_float) { sum_f = (double)sum_i; has_float = true; }
                sum_f += item.as.f;
            } else {
                if (has_float) sum_f += (double)item.as.i;
                else sum_i = add_ll(sum_i, item.as.i, line);   /* 넘치면 알려 줍니다 */
            }
        }
        release(seq_v);
        return has_float ? float_val(sum_f) : int_val(sum_i);
    }

    /* --- clamp(값, 최소, 최대) : 범위를 벗어나면 가까운 끝값으로 --- */
    if (strcmp(name, "clamp") == 0) {
        if (NARG != 3) lumi_error(line, "clamp needs three numbers: value, min, and max");
        Value v = ARG(0), v_min = ARG(1), v_max = ARG(2);
        if (!IS_NUM(v) || !IS_NUM(v_min) || !IS_NUM(v_max)) {
            release(v); release(v_min); release(v_max);
            lumi_error(line, "clamp arguments must be numbers");
        }
        double val = v.kind == V_INT ? (double)v.as.i : v.as.f;
        double min_val = v_min.kind == V_INT ? (double)v_min.as.i : v_min.as.f;
        double max_val = v_max.kind == V_INT ? (double)v_max.as.i : v_max.as.f;

        Value out = v;
        if (val < min_val) out = v_min;
        else if (val > max_val) out = v_max;
        else retain(out);

        release(v); release(v_min); release(v_max);
        return out;
    }

    /* --- assert(조건, [설명]) / assertsame(기대한 것, 나온 것) ---
     * 시험에서 '이러해야 한다'를 적는 데 씁니다.  틀리면 AssertError 를 냅니다.
     * test 블록 밖에서도 쓸 수 있습니다 (그냥 오류가 납니다). */
    if (name_in(name, ASSERT_NAMES)) {
        if (strcmp(name, "assert") == 0) {
            if (NARG != 1 && NARG != 2)
                lumi_error(line, "assert needs a condition, and may take a note: "
                                 "assert(a == b) or assert(a == b, \"why\")");
            Value cond = ARG(0);
            bool ok = value_truthy(cond);
            release(cond);
            if (ok) return NONE_VAL;
            if (NARG == 2) {
                Value note = ARG(1);
                char *t = S(in, note);
                release(note);
                lumi_error_kind(line, "AssertError", "%s", t);
            }
            lumi_error_kind(line, "AssertError", "this was supposed to be true, but was not");
        }
        /* assertsame: 다를 때 둘 다 보여 줍니다 */
        if (NARG != 2)
            lumi_error(line, "assertsame needs two values: assertsame(what you expect, "
                             "what you got)");
        Value want = ARG(0), got = ARG(1);
        bool same = values_equal(in, want, got, line);
        if (!same) {
            char *w = R(in, want), *g = R(in, got);
            release(want); release(got);
            lumi_error_kind(line, "AssertError", "expected %s but got %s", w, g);
        }
        release(want); release(got);
        return NONE_VAL;
    }

    /* --- args() / args(자리) : 터미널에서 넘어온 인자 ---
     *   lumi app.lumi --port 8080   ->  args() 는 ["app.lumi", "--port", "8080"]
     * 자리를 대면 그 하나만 (뒤에서부터 세는 음수도 됩니다).  없는 자리는 오류 —
     * 몇 개인지는 len(args()) 로 미리 세어 보세요. */
    if (strcmp(name, "args") == 0) {
        if (NARG > 1) lumi_error(line, "args takes no argument, or one place");
        if (NARG == 0) {
            Seq *s = seq_new(V_LIST);
            for (size_t i = 0; i < in->argc; i++)
                seq_push(s, str_value(in->argv[i]));
            return obj_val(s);
        }
        Value v_at = ARG(0);
        long long at = require_int(in, v_at, line, "args");
        release(v_at);
        if (at < 0) at += (long long)in->argc;
        if (at < 0 || (size_t)at >= in->argc)
            lumi_error(line, "args has no place %lld; there are %zu (count them with len(args()))",
                       at < 0 ? at - (long long)in->argc : at, in->argc);
        return str_value(in->argv[at]);
    }

    /* --- env(이름) / getenv(이름) : 운영체제 환경 변수 (없으면 none) --- */
    if (strcmp(name, "env") == 0 || strcmp(name, "getenv") == 0) {
        if (NARG != 1) lumi_error(line, "%s needs an environment variable name", name);
        Value v_name = ARG(0);
        Str *s_name = require_text(in, name, v_name, "for variable name", line);
        char *utf8 = str_to_utf8(s_name);
        release(v_name);
        const char *env_val = getenv(utf8);
        free(utf8);
        if (!env_val) return NONE_VAL;
        return str_value(env_val);
    }

    return NONE_VAL;
}

/* ---------------- 벡터 산술 (add / sub / mul / div / mod / pow / neg) ----------------
 * +, -, * 는 리스트끼리 쓰면 '이어 붙이기·되풀이'라서, 리스트 속 숫자를 자리마다
 * 짝지어 계산하려면 이 가족을 씁니다.
 *   숫자 와 숫자   -> 그냥 계산
 *   시퀀스 와 숫자 -> 모든 원소에 적용 (숫자가 앞이어도 같습니다)
 *   시퀀스 와 시퀀스 -> 같은 자리끼리, 짧은 쪽 길이에 맞춰서
 * 아래 함수는 원소가 또 시퀀스면 자기 자신을 다시 불러 파고듭니다. */
static Value eval_vec_math_op(Interp *in, const char *op, Value a, Value b, int line)
{
    if (IS_NUM(a) && IS_NUM(b)) {
        bool is_float = (a.kind == V_FLOAT || b.kind == V_FLOAT);
        double da = a.kind == V_INT ? (double)a.as.i : a.as.f;
        double db = b.kind == V_INT ? (double)b.as.i : b.as.f;

        if (strcmp(op, "add") == 0) {
            return is_float ? float_val(da + db) : int_val(add_ll(a.as.i, b.as.i, line));
        }
        if (strcmp(op, "sub") == 0) {
            return is_float ? float_val(da - db) : int_val(sub_ll(a.as.i, b.as.i, line));
        }
        if (strcmp(op, "mul") == 0) {
            return is_float ? float_val(da * db) : int_val(mul_ll(a.as.i, b.as.i, line));
        }
        if (strcmp(op, "div") == 0) {
            if (db == 0.0) lumi_error_kind(line, "MathError", "division by zero in %s", op);
            return float_val(da / db);
        }
        if (strcmp(op, "mod") == 0) {
            if (b.kind == V_INT && b.as.i == 0) lumi_error_kind(line, "MathError", "modulo by zero in %s", op);
            if (db == 0.0) lumi_error_kind(line, "MathError", "modulo by zero in %s", op);
            if (!is_float) return int_val(a.as.i % b.as.i);
            return float_val(fmod(da, db));
        }
        if (strcmp(op, "pow") == 0) {
            if (!is_float && b.as.i >= 0) {
                long long base = a.as.i, exp = b.as.i, res = 1;
                while (exp > 0) {
                    if (exp & 1) res = mul_ll(res, base, line);
                    exp >>= 1;
                    if (exp) base = mul_ll(base, base, line);
                }
                return int_val(res);
            }
            return float_val(pow(da, db));
        }
    }
    if ((a.kind == V_LIST || a.kind == V_TUPLE) && IS_NUM(b)) {
        Seq *src = AS_SEQ(a);
        Seq *out = seq_new(a.kind);
        for (size_t i = 0; i < src->len; i++) {
            Value v = eval_vec_math_op(in, op, src->items[i], b, line);
            seq_push(out, v);
        }
        return obj_val(out);
    }
    if (IS_NUM(a) && (b.kind == V_LIST || b.kind == V_TUPLE)) {
        Seq *src = AS_SEQ(b);
        Seq *out = seq_new(b.kind);
        for (size_t i = 0; i < src->len; i++) {
            Value v = eval_vec_math_op(in, op, a, src->items[i], line);
            seq_push(out, v);
        }
        return obj_val(out);
    }
    if ((a.kind == V_LIST || a.kind == V_TUPLE) && (b.kind == V_LIST || b.kind == V_TUPLE)) {
        Seq *sa = AS_SEQ(a), *sb = AS_SEQ(b);
        size_t min_len = sa->len < sb->len ? sa->len : sb->len;
        Seq *out = seq_new(a.kind);
        for (size_t i = 0; i < min_len; i++) {
            Value v = eval_vec_math_op(in, op, sa->items[i], sb->items[i], line);
            seq_push(out, v);
        }
        return obj_val(out);
    }
    char *sa = S(in, a), *sb = S(in, b);
    lumi_error(line, "%s operates on numbers and sequences but got '%s' and '%s'", op, sa, sb);
    return NONE_VAL;
}

static Value eval_vec_math_builtin(Interp *in, const char *name, Node *n, Env *env, int line)
{
    /* neg 만 인자가 하나입니다 (부호 뒤집기) */
    if (strcmp(name, "neg") == 0) {
        if (NARG != 1) lumi_error(line, "neg takes exactly one argument");
        Value v = ARG(0);
        if (IS_NUM(v)) {
            Value out = v.kind == V_INT ? int_val(-v.as.i) : float_val(-v.as.f);
            release(v);
            return out;
        }
        if (v.kind == V_LIST || v.kind == V_TUPLE) {
            Seq *src = AS_SEQ(v);
            Seq *out = seq_new(v.kind);
            for (size_t i = 0; i < src->len; i++) {
                Value item = src->items[i];
                Value item_neg = IS_NUM(item)
                    ? (item.kind == V_INT ? int_val(-item.as.i) : float_val(-item.as.f))
                    : retain(item);
                seq_push(out, item_neg);
            }
            release(v);
            return obj_val(out);
        }
        char *s = S(in, v);
        release(v);
        lumi_error(line, "neg needs a number or sequence of numbers but got '%s'", s);
    }

    if (NARG != 2) lumi_error(line, "%s needs exactly two arguments", name);
    Value a = ARG(0), b = ARG(1);
    Value res = eval_vec_math_op(in, name, a, b, line);
    release(a); release(b);
    return res;
}

/* ============================================================
 * 남의 C 라이브러리 불러 쓰기 (FFI)
 * ============================================================
 * 배관입니다.  사람이 쓰는 물건은 이것을 감싼 Lumi 라이브러리입니다.
 * 서명 적는 법과 위험한 까닭은 src/ffi.c 맨 위에 있습니다. */

/* 기억 장치에서 값 하나를 읽고 쓸 때 쓰는 종류 글자의 크기 (모르는 글자면 0) */
static size_t cmem_size(char kind)
{
    switch (kind) {
    case 'c': return 1;                     /* 바이트 하나 */
    case 'h': return 2;                     /* 16비트 */
    case 'i': return 4;                     /* 32비트 */
    case 'f': return 4;                     /* 실수 (32비트) */
    case 'l': case 'p': case 'd': return 8; /* 64비트 / 주소 / 실수 */
    default:  return 0;
    }
}

static char kind_letter(Interp *in, const char *who, Value v, int line)
{
    Str *s = require_text(in, who, v, "as the kind of value", line);
    char *u = str_to_utf8(s);
    if (strlen(u) != 1) {
        char *keep = xstrdup(u);
        free(u);
        lumi_error_kind(line, "ValueError",
                        "%s needs a one-letter kind (c, h, i, l, p, f, or d) but got \"%s\"",
                        who, keep);
    }
    char k = u[0];
    free(u);
    if (cmem_size(k) == 0)
        lumi_error_kind(line, "ValueError",
                        "'%c' is not a kind I know - use c (1 byte), h (2), i (4), "
                        "l (8), p (address), f (float), or d (double)", k);
    return k;
}

static Value eval_ffi_builtin(Interp *in, const char *name, Node *n, Env *env, int line)
{
    char why[256];

    /* ---------- cload(이름) -> 손잡이 ---------- */
    if (strcmp(name, "cload") == 0) {
        if (NARG != 1)
            lumi_error(line, "cload needs one library name, like cload(\"sqlite3\")");
        Value v = ARG(0);
        Str *s = require_text(in, "cload", v, "as the library name", line);
        char *u = str_to_utf8(s);
        release(v);
        long long id = ffi_load(u, why, sizeof why);
        if (!id) {
            char *keep = xstrdup(why);
            free(u);
            lumi_error_kind(line, "FileNotFound",
                            "%s. Looked for it the usual ways for this system "
                            "(suffix %s). Give a full path if it is somewhere else.",
                            keep, plat_dl_suffix());
        }
        free(u);
        return int_val(id);
    }

    /* ---------- cclose(손잡이) ---------- */
    if (strcmp(name, "cclose") == 0) {
        if (NARG != 1) lumi_error(line, "cclose needs one library handle");
        Value v = ARG(0);
        long long id = require_int(in, v, line, "cclose");
        release(v);
        if (!ffi_close(id))
            lumi_error_kind(line, "ValueError",
                            "this is not an open library handle (cload gives you one)");
        return NONE_VAL;
    }

    /* ---------- chas(손잡이, 함수이름) -> 참/거짓 ---------- */
    if (strcmp(name, "chas") == 0) {
        if (NARG != 2)
            lumi_error(line, "chas needs a library handle and a function name, "
                             "like chas(lib, \"sqlite3_open\")");
        Value hv = ARG(0);
        long long id = require_int(in, hv, line, "chas");
        release(hv);
        Value nv = ARG(1);
        Str *s = require_text(in, "chas", nv, "as the function name", line);
        char *u = str_to_utf8(s);
        release(nv);
        int got = ffi_has(id, u);
        free(u);
        if (got < 0)
            lumi_error_kind(line, "ValueError", "this is not an open library handle");
        return bool_val(got == 1);
    }

    /* ---------- cbuf(바이트수) -> 주소 ---------- */
    if (strcmp(name, "cbuf") == 0) {
        if (NARG != 1) lumi_error(line, "cbuf needs how many bytes, like cbuf(8)");
        Value v = ARG(0);
        long long nb = require_int(in, v, line, "cbuf");
        release(v);
        long long addr = ffi_buf(nb);
        if (!addr)
            lumi_error_kind(line, "ValueError",
                            "could not set aside %lld byte(s) (must be 1 .. 2147483648)", nb);
        return int_val(addr);
    }

    /* ---------- cfree(주소) ---------- */
    if (strcmp(name, "cfree") == 0) {
        if (NARG != 1) lumi_error(line, "cfree needs one address that came from cbuf");
        Value v = ARG(0);
        long long addr = require_int(in, v, line, "cfree");
        release(v);
        if (!ffi_free(addr))
            lumi_error_kind(line, "ValueError",
                            "cfree only takes addresses that cbuf gave you "
                            "(this one did not, or it was already freed)");
        return NONE_VAL;
    }

    /* ---------- cget(종류, 주소) ---------- */
    if (strcmp(name, "cget") == 0) {
        if (NARG != 2)
            lumi_error(line, "cget needs a kind and an address, like cget(\"l\", addr)");
        Value kv = ARG(0);
        char k = kind_letter(in, "cget", kv, line);
        release(kv);
        Value av = ARG(1);
        long long addr = require_int(in, av, line, "cget");
        release(av);
        if (!addr) lumi_error_kind(line, "ValueError", "cget was given address 0");
        const void *p = (const void *)(uintptr_t)addr;
        /* memcpy 로 읽습니다 — 자리가 안 맞아도(정렬) 안전합니다 */
        switch (k) {
        case 'c': { int8_t  x; memcpy(&x, p, 1); return int_val(x); }
        case 'h': { int16_t x; memcpy(&x, p, 2); return int_val(x); }
        case 'i': { int32_t x; memcpy(&x, p, 4); return int_val(x); }
        case 'l': case 'p': { int64_t x; memcpy(&x, p, 8); return int_val(x); }
        case 'f': { float  x; memcpy(&x, p, 4); return float_val(x); }
        default:  { double x; memcpy(&x, p, 8); return float_val(x); }
        }
    }

    /* ---------- cput(값, 종류, 주소) — '무엇을 어디에' 차례 ---------- */
    if (strcmp(name, "cput") == 0) {
        if (NARG != 3)
            lumi_error(line, "cput needs a value, a kind, and an address, "
                             "like cput(5, \"i\", addr)");
        Value vv = ARG(0);
        Value kv = ARG(1);
        char k = kind_letter(in, "cput", kv, line);
        release(kv);
        Value av = ARG(2);
        long long addr = require_int(in, av, line, "cput");
        release(av);
        if (!addr) { release(vv); lumi_error_kind(line, "ValueError", "cput was given address 0"); }
        void *p = (void *)(uintptr_t)addr;
        if (k == 'f' || k == 'd') {
            double d = vv.kind == V_FLOAT ? vv.as.f
                     : vv.kind == V_INT   ? (double)vv.as.i : 0;
            if (!IS_NUM(vv)) { release(vv); lumi_error_kind(line, "TypeError",
                               "cput with kind '%c' needs a number", k); }
            release(vv);
            if (k == 'f') { float f = (float)d; memcpy(p, &f, 4); }
            else memcpy(p, &d, 8);
            return NONE_VAL;
        }
        long long x = require_int(in, vv, line, "cput");
        release(vv);
        switch (k) {
        case 'c': { int8_t  y = (int8_t)x;  memcpy(p, &y, 1); break; }
        case 'h': { int16_t y = (int16_t)x; memcpy(p, &y, 2); break; }
        case 'i': { int32_t y = (int32_t)x; memcpy(p, &y, 4); break; }
        default:  { int64_t y = x;          memcpy(p, &y, 8); break; }
        }
        return NONE_VAL;
    }

    /* ---------- ctext(주소) -> 글자 ---------- */
    if (strcmp(name, "ctext") == 0) {
        if (NARG != 1) lumi_error(line, "ctext needs one address");
        Value v = ARG(0);
        long long addr = require_int(in, v, line, "ctext");
        release(v);
        if (!addr) return NONE_VAL;          /* NULL 은 none 으로 (흔한 일입니다) */
        return obj_val(str_from_utf8((const char *)(uintptr_t)addr));
    }

    /* ---------- cbytes(길이, 주소) -> 바이트 ---------- */
    if (strcmp(name, "cbytes") == 0) {
        if (NARG != 2)
            lumi_error(line, "cbytes needs how many bytes and an address, "
                             "like cbytes(16, addr)");
        Value lv = ARG(0);
        long long nb = require_int(in, lv, line, "cbytes");
        release(lv);
        Value av = ARG(1);
        long long addr = require_int(in, av, line, "cbytes");
        release(av);
        if (nb < 0) lumi_error_kind(line, "ValueError", "cbytes cannot read %lld bytes", nb);
        if (!addr) return NONE_VAL;
        return obj_val(bytes_new((const uint8_t *)(uintptr_t)addr, (size_t)nb));
    }

    /* ---------- ccall(손잡이, 함수이름, 서명, [인자들]) ---------- */
    if (NARG != 4)
        lumi_error(line, "ccall needs a library handle, a function name, a signature, "
                         "and a list of arguments, like "
                         "ccall(lib, \"sqlite3_open\", \"sp>i\", [path, out])");

    Value hv = ARG(0);
    long long id = require_int(in, hv, line, "ccall");
    release(hv);

    Value nv = ARG(1);
    char *fname = str_to_utf8(require_text(in, "ccall", nv, "as the function name", line));
    release(nv);

    Value sv = ARG(2);
    char *sig = NULL;
    {
        Str *s = require_text(in, "ccall", sv, "as the signature", line);
        sig = str_to_utf8(s);
        release(sv);
    }

    Value lv = ARG(3);
    if (lv.kind != V_LIST && lv.kind != V_TUPLE) {
        char *got = S(in, lv);
        release(lv);
        free(fname); free(sig);
        lumi_error_kind(line, "TypeError",
                        "ccall wants the arguments in a list but got '%s'", got);
    }
    Seq *argseq = AS_SEQ(lv);

    /* 넘기는 동안만 살아 있으면 되는 임시 글자들 */
    char *temps[8];
    size_t ntemp = 0;
    long long raw[8];
    size_t nargs = argseq->len;

    /* 메시지에 sig 나 fname 이 들어가므로 **놓아주기 전에** 글을 다 지어 둡니다.
     * (이걸 반대로 했다가 GCC 의 -Wuse-after-free 에 잡혔습니다.) */
    #define CCALL_FAIL(...) do { \
            char _m[512]; \
            snprintf(_m, sizeof _m, __VA_ARGS__); \
            for (size_t t = 0; t < ntemp; t++) free(temps[t]); \
            release(lv); free(fname); free(sig); \
            lumi_error_kind(line, "ValueError", "%s", _m); \
        } while (0)

    if (nargs > 8) CCALL_FAIL("ccall takes at most 8 arguments (got %zu)", nargs);

    const char *arrow = strchr(sig, '>');
    if (!arrow) CCALL_FAIL("the signature must have '>' in it, like \"sp>i\" (got \"%s\")", sig);

    for (size_t i = 0; i < nargs && sig + i < arrow; i++) {
        char k = sig[i];
        Value a = argseq->items[i];
        switch (k) {
        case 'i': case 'l': case 'p':
            if (!IS_NUM(a) && a.kind != V_BOOL)
                CCALL_FAIL("argument %zu is '%c' in the signature, so it needs a whole number",
                           i + 1, k);
            raw[i] = a.kind == V_BOOL ? (a.as.b ? 1 : 0)
                   : a.kind == V_INT  ? a.as.i : (long long)a.as.f;
            break;
        case 's':
            if (a.kind == V_NONE) { raw[i] = 0; break; }   /* none -> NULL */
            if (a.kind != V_STR)
                CCALL_FAIL("argument %zu is 's' in the signature, so it needs text", i + 1);
            temps[ntemp] = str_to_utf8(AS_STR(a));
            raw[i] = (long long)(uintptr_t)temps[ntemp];
            ntemp++;
            break;
        case 'b':
            if (a.kind == V_NONE) { raw[i] = 0; break; }
            if (a.kind != V_BYTES)
                CCALL_FAIL("argument %zu is 'b' in the signature, so it needs bytes", i + 1);
            raw[i] = (long long)(uintptr_t)AS_BYTES(a)->data;
            break;
        case 'd':
            /* 실수는 **마지막 인자일 때만** 넘길 수 있습니다 (까닭은 src/ffi.c).
             * 비트를 그대로 long long 에 담아 보내고 저쪽에서 되돌려 꺼냅니다. */
            if (sig + i != arrow - 1)
                CCALL_FAIL("a float can only be the last argument, but the signature "
                           "\"%s\" puts one at position %zu. Move it last, or put it in "
                           "a cbuf with cput and pass the address ('p') instead.",
                           sig, i + 1);
            if (!IS_NUM(a))
                CCALL_FAIL("argument %zu is 'd' in the signature, so it needs a number",
                           i + 1);
            {
                double d = a.kind == V_FLOAT ? a.as.f : (double)a.as.i;
                memcpy(&raw[i], &d, sizeof d);
            }
            break;
        default:
            CCALL_FAIL("'%c' is not an argument kind I know (use i, l, p, s, or b)", k);
        }
    }

    FfiRet r;
    bool ok = ffi_call(id, fname, sig, raw, nargs, &r, why, sizeof why);

    for (size_t t = 0; t < ntemp; t++) free(temps[t]);
    release(lv);
    free(fname);
    free(sig);
    #undef CCALL_FAIL

    if (!ok) lumi_error_kind(line, "ValueError", "%s", why);

    switch (r.kind) {
    case 'v': return NONE_VAL;
    case 'd': return float_val(r.d);
    case 's': return r.i ? obj_val(str_from_utf8((const char *)(uintptr_t)r.i)) : NONE_VAL;
    default:  return int_val(r.i);
    }
}

/* ============================================================
 * 함수 부르기 (내장 함수 갈래 나누기)
 * ============================================================ */

static Value eval_call(Interp *in, Node *n, Env *env)
{
    const char *name = n->v.call.name;
    int line = n->line;

    /* 내장 함수 이름이 먼저입니다 — 같은 이름으로 만든 내 함수는 불리지 않습니다.
     * (메서드는 여기를 거치지 않으니 class 안의 func add(...) 는 그대로 동작합니다) */
    if (name_in(name, TIME_NAMES)) {
        return eval_time_builtin(in, name, n, env, line);
    }
    if (name_in(name, UTIL_NAMES) || name_in(name, ASSERT_NAMES)) {
        return eval_util_builtin(in, name, n, env, line);
    }
    if (name_in(name, VEC_MATH_NAMES)) {
        return eval_vec_math_builtin(in, name, n, env, line);
    }

    /* --- start(함수, 값...) / wait(일감) --- */
    if (name_in(name, TASK_NAMES)) {
        if (strcmp(name, "start") == 0) {
            if (NARG < 1)
                lumi_error(line, "start needs a function first, like start(work, value)");
            if (ARGS.items[0]->kind == N_KWARG)
                lumi_error(line, "start's first value must be a function, not a named argument");
            Value fn = eval(in, ARGS.items[0], env);
            size_t na = NARG - 1;
            Value *args = na ? (Value *)xmalloc(na * sizeof(Value)) : NULL;
            for (size_t i = 0; i < na; i++) {
                if (ARGS.items[i + 1]->kind == N_KWARG) {
                    for (size_t k = 0; k < i; k++) release(args[k]);
                    free(args); release(fn);
                    lumi_error(line, "start passes values by order, so 'name = value' cannot go here");
                }
                args[i] = eval(in, ARGS.items[i + 1], env);
            }
            return task_start(in, fn, args, na, line); /* fn/args 소유권 이동 */
        }
        if (NARG != 1) lumi_error(line, "wait needs exactly one task made by start()");
        Value v = ARG(0);
        if (v.kind != V_TASK) {
            char *got = S(in, v);
            release(v);
            lumi_error(line, "wait needs a task made by start() but got %s", got);
        }
        Value out = task_wait(AS_TASK(v), line);
        release(v);
        return out;
    }



    /* --- print([값...] [, keep([끝말])]) --- */
    if (strcmp(name, "print") == 0) {
        size_t nargs = NARG;
        Node *keep = NULL;
        if (nargs && ARGS.items[nargs - 1]->kind == N_CALL
            && strcmp(ARGS.items[nargs - 1]->v.call.name, "keep") == 0) {
            keep = ARGS.items[--nargs];
            if (keep->v.call.args.len > 1)
                lumi_error(keep->line, "keep takes at most one ending text");
        }
        Buf b = {0};
        for (size_t i = 0; i < nargs; i++) {
            if (i) buf_str(&b, " ");
            Value v = ARG(i);
            to_str_into(in, v, &b);
            release(v);
        }
        if (!keep) buf_str(&b, "\n");
        else if (keep->v.call.args.len == 1) {
            Value v = eval(in, keep->v.call.args.items[0], env);
            to_str_into(in, v, &b);
            release(v);
        }
        char *text = buf_done(&b);
        in->output(text);
        free(text);
        return NONE_VAL;
    }

    /* --- keep: print 안에서만 --- */
    if (strcmp(name, "keep") == 0)
        lumi_error(line, "keep can only be used as the last argument of print, "
                         "like print(\"hi\", keep(\"\"))");

    /* --- input([안내문구]) --- */
    if (strcmp(name, "input") == 0) {
        if (NARG > 1) lumi_error(line, "input takes at most one prompt argument");
        char *prompt = xstrdup("");
        if (NARG == 1) {
            Value v = ARG(0);
            free(prompt);
            prompt = value_to_utf8(in, v);
            release(v);
        }
        char *reply = in->input(prompt);
        free(prompt);
        if (!reply)
            lumi_error(line, "input has no more lines to read: the input was closed, so "
                             "there is nothing left to type");
        Value out = str_value(reply);
        free(reply);
        return out;
    }

    /* --- 형변환 함수 --- */
    if (is_convert_name(name)) {
        if (NARG != 1)
            lumi_error(line, "%s needs exactly one value to convert", name);
        Value v = ARG(0);
        Value out = convert_value(in, name, v, line);
        release(v);
        return out;
    }

    /* --- 진법 표기 (bin / hex) --- */
    if (name_in(name, BASE_NAMES)) {
        if (NARG != 1) lumi_error(line, "%s needs exactly one whole number", name);
        Value v = ARG(0);
        Value out = to_base_text(in, name, v, line);
        release(v);
        return out;
    }

    /* --- 문자열 검사 --- */
    if (name_in(name, TEXT_CHECK_NAMES)) {
        if (NARG != 1) lumi_error(line, "%s needs exactly one text value", name);
        Value v = ARG(0);
        Value out = text_check(in, name, v, line);
        release(v);
        return out;
    }

    /* --- 찾기 (incount / infind) --- */
    if (name_in(name, SEARCH_NAMES)) {
        if (NARG != 2)
            lumi_error(line, "%s needs two values: what to look for, and the sequence to "
                             "look in, like %s(\"l\", \"hello\")", name, name);
        Value item = ARG(0), seq = ARG(1);
        Value out = search_sequence(in, name, item, seq, line);
        release(item); release(seq);
        return out;
    }

    /* --- 짝짓기 (inpair) --- */
    if (name_in(name, PAIR_NAMES)) {
        /* 모양은 따옴표 없는 이름이라 값이 아니라 '적힌 낱말'로 읽습니다 (print 의 keep 처럼) */
        const char *shape = "list";
        size_t at = 0;
        if (NARG == 2) {
            Node *s = ARGS.items[0];
            if (s->kind != N_VAR || !name_in(s->v.var.name, PAIR_SHAPES))
                lumi_error(line, "inpair's shape must be written as list, tuple, or dict "
                                 "without quotes, like inpair(tuple, xs)");
            shape = s->v.var.name;
            at = 1;
        } else if (NARG != 1) {
            lumi_error(line, "inpair needs the sequence or dictionary to pair up, and in "
                             "front of it you may put the shape, like inpair(xs), "
                             "inpair(tuple, xs), or inpair(dict, xs)");
        }
        Value v = eval(in, ARGS.items[at], env);
        Value out = pair_sequence(in, shape, v, line);
        release(v);
        return out;
    }

    /* --- upper / lower / strip --- */
    if (name_in(name, PLACE_ARG_NAMES)) {
        Place where;
        memset(&where, 0, sizeof(where));
        Value text;
        if (NARG == 1 && ARGS.items[0]->kind != N_RANGESPEC) {
            where.kind = PLACE_ALL;
            text = ARG(0);
        } else if (NARG == 2) {
            where = read_place(in, ARGS.items[0], env);
            text = ARG(1);
        } else {
            lumi_error(line, "%s needs the text to work on, and in front of it you may "
                             "say which places to work on, like %s(\"hi\"), %s(0, \"hi\"), "
                             "%s(0:2, \"hi\"), or %s([0, 2], \"hi\")",
                       name, name, name, name, name);
            return NONE_VAL;
        }
        Value out = strcmp(name, "strip") == 0
                  ? strip_spaces(in, &where, text, line)
                  : change_case(in, name, &where, text, line);
        free_place(&where);
        release(text);
        return out;
    }

    /* --- format --- */
    if (name_in(name, FORMAT_NAMES)) {
        return format_text(in, n, env, line);
    }

    /* --- split / join / replace --- */
    if (name_in(name, PARTS_NAMES)) {
        if (strcmp(name, "replace") == 0) {
            if (NARG != 3)
                lumi_error(line, "replace needs three values: what to look for, what to "
                                 "put there instead, and the text to work on, like "
                                 "replace(\" \", \"-\", \"a b\")");
            Value o = ARG(0), w = ARG(1), t = ARG(2);
            Value out = replace_text(in, o, w, t, line);
            release(o); release(w); release(t);
            return out;
        }
        bool has_first;
        Value first = NONE_VAL, second;
        if (NARG == 1) {
            has_first = false;
            second = ARG(0);
        } else if (NARG == 2) {
            has_first = true;
            first = ARG(0);
            second = ARG(1);
        } else if (strcmp(name, "split") == 0) {
            lumi_error(line, "split needs the text to cut, and in front of it you may put "
                             "what to cut with, like split(\"a b\") or split(\",\", \"a,b\")");
            return NONE_VAL;
        } else {
            lumi_error(line, "join needs the pieces to join, and in front of them you may "
                             "put what to put between, like join([\"a\", \"b\"]) or "
                             "join(\"-\", [\"a\", \"b\"])");
            return NONE_VAL;
        }
        Value out = strcmp(name, "split") == 0
                  ? split_text(in, has_first, first, second, line)
                  : join_seq(in, has_first, first, second, line);
        if (has_first) release(first);
        release(second);
        return out;
    }

    /* --- insor --- */
    if (strcmp(name, "insor") == 0) {
        bool has_dir;
        Value dir = NONE_VAL, where, seq;
        if (NARG == 2) {
            has_dir = false;
            where = ARG(0);
            seq = ARG(1);
        } else if (NARG == 3) {
            has_dir = true;
            dir = ARG(0);
            where = ARG(1);
            seq = ARG(2);
        } else {
            lumi_error(line, "insor needs the place to sort by and the sequence to put in "
                             "order, and in front of them you may put the direction, like "
                             "insor(1, rows) or insor(false, 1, rows)");
            return NONE_VAL;
        }
        Value out = insor_sequence(in, has_dir, dir, where, seq, line);
        if (has_dir) release(dir);
        release(where); release(seq);
        return out;
    }

    /* --- sort --- */
    if (name_in(name, SORT_NAMES)) {
        bool has_dir;
        Value dir = NONE_VAL, seq;
        if (NARG == 1) {
            has_dir = false;
            seq = ARG(0);
        } else if (NARG == 2) {
            has_dir = true;
            dir = ARG(0);
            seq = ARG(1);
        } else {
            lumi_error(line, "sort needs the sequence to put in order, and in front of it "
                             "you may put the direction, like sort(xs), sort(true, xs), "
                             "or sort(false, xs)");
            return NONE_VAL;
        }
        Value out = sort_sequence(in, has_dir, dir, seq, line);
        if (has_dir) release(dir);
        release(seq);
        return out;
    }

    /* --- shuffle --- */
    if (name_in(name, SHUFFLE_NAMES)) {
        if (NARG != 1)
            lumi_error(line, "shuffle needs exactly one sequence to mix up, like shuffle(xs)");
        Value v = ARG(0);
        Value out = shuffle_sequence(in, v, line);
        release(v);
        return out;
    }

    /* --- each --- */
    if (strcmp(name, "each") == 0) {
        if (NARG != 2)
            lumi_error(line, "each needs two values: the function to call, and the "
                             "sequence to walk through, like each(double, xs)");
        Value fn = ARG(0), seq = ARG(1);
        /* 오류 글에 적힌 이름을 그대로 보여 주려고 씁니다 */
        const char *fname = ARGS.items[0]->kind == N_VAR
                          ? ARGS.items[0]->v.var.name : "each's function";
        Value out = each_sequence(in, fn, fname, seq, line);
        release(fn); release(seq);
        return out;
    }

    /* --- encode / decode --- */
    if (name_in(name, BYTES_NAMES)) {
        bool has_enc;
        Value enc = NONE_VAL, value;
        if (NARG == 1) {
            has_enc = false;
            value = ARG(0);
        } else if (NARG == 2) {
            has_enc = true;
            enc = ARG(0);
            value = ARG(1);
        } else {
            const char *what = strcmp(name, "encode") == 0
                ? "the text to turn into bytes" : "the bytes to turn back into text";
            lumi_error(line, "%s needs %s, and in front of it you may put the encoding "
                             "name, like %s(value) or %s(\"utf-8\", value)",
                       name, what, name, name);
            return NONE_VAL;
        }
        const char *codec = encoding_of(in, name, has_enc, enc, line);
        Value out;
        if (strcmp(name, "encode") == 0) {
            if (value.kind == V_BYTES)
                lumi_error(line, "encode turns text into bytes, but this value is already "
                                 "bytes; to get the text back use decode(...)");
            if (value.kind != V_STR) {
                char *r = R(in, value);
                lumi_error(line, "encode needs text (a string) to turn into bytes but got %s", r);
            }
            BytesObj *b = encode_str(AS_STR(value), codec);
            if (!b)
                lumi_error(line, "encode cannot write this text in %s: it has characters "
                                 "that %s has no number for; try encode(\"utf-8\", ...)",
                           codec, codec);
            out = obj_val(b);
        } else {
            if (value.kind == V_STR)
                lumi_error(line, "decode turns bytes back into text, but this value is "
                                 "already text; to turn text into bytes use encode(...)");
            if (value.kind != V_BYTES) {
                char *r = R(in, value);
                lumi_error(line, "decode needs a bytes object but got %s", r);
            }
            Str *s = decode_bytes(AS_BYTES(value), codec);
            if (!s)
                lumi_error(line, "decode cannot read these bytes as %s; they were probably "
                                 "written with a different encoding, so read them with "
                                 "that one", codec);
            out = obj_val(s);
        }
        if (has_enc) release(enc);
        release(value);
        return out;
    }

    /* --- 무늬 찾기 (정규표현식) ---
     *   ismatch(무늬, 글자)          글자 '전체'가 무늬와 같은가
     *   hasmatch(무늬, 글자)         어딘가에 무늬가 있는가
     *   findmatch(무늬, 글자)        처음 맞은 조각 (없으면 none)
     *   findmatches(무늬, 글자)      맞은 조각 전부 (없으면 [])
     *   findgroups(무늬, 글자)       괄호로 묶은 부분들 (없으면 none)
     *   replacematch(무늬, 새것, 글자)  맞은 곳을 모두 바꾼 글자
     *   splitmatch(무늬, 글자)       무늬가 나오는 자리마다 자르기
     * 집 규칙대로 '무늬가 앞, 글자가 뒤' 입니다 (replace 와 같은 차례). */
    if (name_in(name, MATCH_NAMES)) {
        bool is_replace = strcmp(name, "replacematch") == 0;
        size_t want = is_replace ? 3u : 2u;
        if (NARG != want) {
            if (is_replace)
                lumi_error(line, "replacematch needs the pattern, the new text, and then the "
                                 "text to change, like replacematch(\"\\\\d+\", \"N\", s)");
            lumi_error(line, "%s needs the pattern and then the text, like %s(\"\\\\d+\", s)",
                       name, name);
        }

        Value pv = ARG(0);
        Str *ps = require_text(in, name, pv, "for the pattern", line);
        char errbuf[256];
        Regex *re = regex_compile(ps->cp, ps->len, errbuf, sizeof errbuf);
        release(pv);
        if (!re)
            lumi_error_kind(line, "ValueError", "%s cannot read that pattern: %s", name, errbuf);

        Value newv = NONE_VAL;
        Str *news = NULL;
        if (is_replace) {
            newv = ARG(1);
            news = require_text(in, name, newv, "for the new text", line);
        }
        Value tv = ARG(is_replace ? 2 : 1);
        if (tv.kind != V_STR) {
            regex_free(re);
            char *r = R(in, tv);
            release(tv);
            if (is_replace) release(newv);
            lumi_error_kind(line, "TypeError", "%s needs text to look in but got %s", name, r);
        }
        Str *ts = AS_STR(tv);

        int ng = regex_group_count(re);
        long *gs = (long *)xmalloc((size_t)(ng + 1) * sizeof(long));
        long *ge = (long *)xmalloc((size_t)(ng + 1) * sizeof(long));
        bool gaveup = false;

        Value out = NONE_VAL;

        if (strcmp(name, "hasmatch") == 0) {
            out = bool_val(regex_search(re, ts->cp, ts->len, 0, gs, ge, &gaveup));

        } else if (strcmp(name, "ismatch") == 0) {
            /* 전체가 맞아야 하므로, 0 에서 시작해 끝까지 닿았는지 봅니다 */
            bool ok = false;
            size_t at = 0;
            while (regex_search(re, ts->cp, ts->len, at, gs, ge, &gaveup)) {
                if (gs[0] == 0 && (size_t)ge[0] == ts->len) { ok = true; break; }
                if (gs[0] > 0) break;                 /* 0 에서 시작하는 것만 봅니다 */
                at = (size_t)gs[0] + 1;
                if (at > ts->len) break;
            }
            out = bool_val(ok);

        } else if (strcmp(name, "findmatch") == 0) {
            if (regex_search(re, ts->cp, ts->len, 0, gs, ge, &gaveup))
                out = obj_val(str_slice(ts, (size_t)gs[0], (size_t)ge[0]));

        } else if (strcmp(name, "findgroups") == 0) {
            if (regex_search(re, ts->cp, ts->len, 0, gs, ge, &gaveup)) {
                Seq *seq = seq_new(V_LIST);
                for (int i = 1; i <= ng; i++) {
                    if (gs[i] < 0 || ge[i] < 0) seq_push(seq, str_value(""));
                    else seq_push(seq, obj_val(str_slice(ts, (size_t)gs[i], (size_t)ge[i])));
                }
                out = obj_val(seq);
            }

        } else if (strcmp(name, "findmatches") == 0) {
            Seq *seq = seq_new(V_LIST);
            size_t at = 0;
            while (at <= ts->len && regex_search(re, ts->cp, ts->len, at, gs, ge, &gaveup)) {
                seq_push(seq, obj_val(str_slice(ts, (size_t)gs[0], (size_t)ge[0])));
                /* 빈 것에 걸려 제자리걸음 하지 않도록 한 칸 밀어 줍니다 */
                at = (ge[0] > gs[0]) ? (size_t)ge[0] : (size_t)ge[0] + 1;
            }
            out = obj_val(seq);

        } else if (strcmp(name, "splitmatch") == 0) {
            Seq *seq = seq_new(V_LIST);
            size_t at = 0, piece = 0;
            while (at <= ts->len && regex_search(re, ts->cp, ts->len, at, gs, ge, &gaveup)) {
                if (ge[0] == gs[0]) {                 /* 빈 무늬로는 자르지 않습니다 */
                    at = (size_t)ge[0] + 1;
                    continue;
                }
                seq_push(seq, obj_val(str_slice(ts, piece, (size_t)gs[0])));
                piece = (size_t)ge[0];
                at = piece;
            }
            seq_push(seq, obj_val(str_slice(ts, piece, ts->len)));
            out = obj_val(seq);

        } else {   /* replacematch */
            Buf b = {0};
            size_t at = 0, piece = 0;
            while (at <= ts->len && regex_search(re, ts->cp, ts->len, at, gs, ge, &gaveup)) {
                /* 맞기 전까지를 그대로 옮기고 */
                Str *keep = str_slice(ts, piece, (size_t)gs[0]);
                char *u = str_to_utf8(keep);
                buf_str(&b, u);
                free(u);
                release(obj_val(keep));
                /* 새 글자를 넣습니다.  \1 \2 는 그 묶음으로 바뀝니다. */
                for (size_t k = 0; k < news->len; k++) {
                    uint32_t c = news->cp[k];
                    if (c == '\\' && k + 1 < news->len) {
                        uint32_t d = news->cp[k + 1];
                        if (d >= '1' && d <= '9') {
                            int gi = (int)(d - '0');
                            if (gi <= ng && gs[gi] >= 0) {
                                Str *gv = str_slice(ts, (size_t)gs[gi], (size_t)ge[gi]);
                                char *gu = str_to_utf8(gv);
                                buf_str(&b, gu);
                                free(gu);
                                release(obj_val(gv));
                            }
                            k++;
                            continue;
                        }
                        if (d == '\\') { buf_add(&b, "\\", 1); k++; continue; }
                    }
                    size_t nb = 0;
                    char *one = cp_to_utf8(&c, 1, &nb);
                    buf_add(&b, one, nb);
                    free(one);
                }
                piece = (size_t)ge[0];
                at = (ge[0] > gs[0]) ? (size_t)ge[0] : (size_t)ge[0] + 1;
                if (ge[0] == gs[0] && (size_t)gs[0] < ts->len) {
                    /* 빈 무늬였으면 글자 하나를 그대로 옮기고 넘어갑니다 */
                    uint32_t c = ts->cp[gs[0]];
                    size_t nb = 0;
                    char *one = cp_to_utf8(&c, 1, &nb);
                    buf_add(&b, one, nb);
                    free(one);
                    piece = (size_t)gs[0] + 1;
                }
            }
            Str *rest = str_slice(ts, piece, ts->len);
            char *ru = str_to_utf8(rest);
            buf_str(&b, ru);
            free(ru);
            release(obj_val(rest));
            char *done = buf_done(&b);
            out = str_value(done);
            free(done);
        }

        free(gs);
        free(ge);
        regex_free(re);
        release(tv);
        if (is_replace) release(newv);
        if (gaveup) {
            release(out);
            lumi_error_kind(line, "ValueError",
                            "%s gave up: that pattern needs too much back-and-forth on this "
                            "text. Make it more exact - nested repeats like (a+)+ are the "
                            "usual cause", name);
        }
        return out;
    }

    /* --- httpserve(포트, 처리함수, 몇번) : 작은 웹 서버 ---
     * 한 번에 손님 하나씩 받습니다.  사내 도구·웹훅 받기·만들어 보기에 알맞습니다.
     * 처리함수는 요청 딕셔너리를 받아 답 딕셔너리를 돌려줍니다.
     * 처리함수 안에서 오류가 나도 서버는 죽지 않고 500 을 보내고 다음 손님을 받습니다. */
    /* ---------- 남의 C 라이브러리 불러 쓰기 ----------
     *   cload(이름)                    -> 손잡이
     *   cclose(손잡이) / chas(손잡이, 함수이름)
     *   ccall(손잡이, 함수이름, 서명, [인자들])
     *   cbuf(바이트수) -> 주소 / cfree(주소)
     *   cget(종류, 주소) / cput(값, 종류, 주소)
     *   ctext(주소) / cbytes(길이, 주소)
     * 서명 적는 법과 위험한 까닭은 src/ffi.c 맨 위에 적어 두었습니다. */
    if (name_in(name, FFI_NAMES)) {
        return eval_ffi_builtin(in, name, n, env, line);
    }

    if (name_in(name, SERVE_NAMES)) {
        if (NARG != 2 && NARG != 3)
            lumi_error(line, "httpserve needs a port and a handler, and may take how many "
                             "requests to serve: httpserve(8080, handle) or "
                             "httpserve(8080, handle, 1)");
        Value pv = ARG(0);
        long long port = require_int(in, pv, line, "httpserve");
        release(pv);
        if (port < 1 || port > 65535)
            lumi_error_kind(line, "ValueError",
                            "httpserve needs a port between 1 and 65535 but got %lld", port);

        Value handler = ARG(1);
        long long want = 0;
        if (NARG == 3) {
            Value wv = ARG(2);
            want = require_int(in, wv, line, "httpserve");
            release(wv);
        }

        int server = plat_listen((int)port);
        if (server < 0) {
            release(handler);
            lumi_error_kind(line, "NetworkError",
                            "httpserve cannot open port %lld; another program may already be "
                            "using it, or the port needs administrator rights", port);
        }

        long long served = 0;
        while (want <= 0 || served < want) {
            int c = plat_accept(server);
            if (c < 0) break;

            /* 머리말 끝(빈 줄)까지 읽고, Content-Length 만큼 몸통을 더 읽습니다 */
            size_t cap = 8192, len = 0;
            char *req = (char *)xmalloc(cap);
            long got;
            size_t head_end = 0;
            while ((got = plat_recv(c, req + len, cap - len - 1)) > 0) {
                len += (size_t)got;
                req[len] = 0;
                char *blank = strstr(req, "\r\n\r\n");
                if (blank) { head_end = (size_t)(blank - req) + 4; break; }
                if (len + 1 >= cap) {
                    if (cap > 1u << 20) break;          /* 머리말이 1MB 면 이상한 손님입니다 */
                    cap *= 2;
                    req = (char *)xrealloc(req, cap);
                }
            }
            if (head_end == 0) { free(req); plat_closesock(c); continue; }

            /* Content-Length 를 찾아 몸통을 마저 받습니다 */
            size_t want_body = 0;
            for (char *q = req; q < req + head_end; q++) {
                if ((*q == 'c' || *q == 'C') &&
                    _strnicmp_lumi(q, "content-length:", 15) == 0) {
                    want_body = (size_t)strtoul(q + 15, NULL, 10);
                    break;
                }
            }
            while (len - head_end < want_body) {
                if (len + 1 >= cap) { cap = cap * 2 + want_body; req = (char *)xrealloc(req, cap); }
                got = plat_recv(c, req + len, cap - len - 1);
                if (got <= 0) break;
                len += (size_t)got;
                req[len] = 0;
            }

            /* 첫 줄: 방법 길 버전 */
            char *sp1 = strchr(req, ' ');
            char *sp2 = sp1 ? strchr(sp1 + 1, ' ') : NULL;
            Dict *rq = dict_new();
            if (sp1 && sp2) {
                *sp1 = 0; *sp2 = 0;
                char *path = sp1 + 1;
                char *qmark = strchr(path, '?');
                if (qmark) *qmark = 0;
                dict_set(rq, str_value("방법"), str_value(req));
                /* 길은 %EC%95%88 같은 적기를 풀어서 줍니다 */
                char *dec = url_undo(path, strlen(path), false);
                dict_set(rq, str_value("길"), str_value(dec));
                free(dec);
                /* 물음표 뒤는 이름=값 짝으로 갈라서 딕셔너리로 줍니다.
                 * 가르고 나서 풀어야 이름이나 값에 든 & = 가 안 헷갈립니다. */
                Dict *qd = dict_new();
                if (qmark) {
                    const char *qp = qmark + 1;
                    while (*qp) {
                        const char *amp = strchr(qp, '&');
                        size_t seg = amp ? (size_t)(amp - qp) : strlen(qp);
                        const char *eq = memchr(qp, '=', seg);
                        size_t kl = eq ? (size_t)(eq - qp) : seg;
                        char *k2 = url_undo(qp, kl, true);
                        char *v2 = eq ? url_undo(eq + 1, seg - kl - 1, true) : xstrdup("");
                        if (*k2) dict_set(qd, str_value(k2), str_value(v2));
                        free(k2); free(v2);
                        if (!amp) break;
                        qp = amp + 1;
                    }
                }
                dict_set(rq, str_value("물음"), obj_val(qd));
                *sp1 = ' '; *sp2 = ' ';
                if (qmark) *qmark = '?';
            } else {
                dict_set(rq, str_value("방법"), str_value("GET"));
                dict_set(rq, str_value("길"), str_value("/"));
                dict_set(rq, str_value("물음"), obj_val(dict_new()));
            }

            /* 머리말들 (이름은 소문자로 맞춰 담습니다) */
            Dict *hd = dict_new();
            char *linep = strstr(req, "\r\n");
            if (linep) linep += 2;
            while (linep && (size_t)(linep - req) < head_end - 2) {
                char *eol = strstr(linep, "\r\n");
                if (!eol || eol == linep) break;
                char *colon = memchr(linep, ':', (size_t)(eol - linep));
                if (colon) {
                    size_t nl = (size_t)(colon - linep);
                    char *nm = (char *)xmalloc(nl + 1);
                    for (size_t i = 0; i < nl; i++)
                        nm[i] = (linep[i] >= 'A' && linep[i] <= 'Z') ? linep[i] + 32 : linep[i];
                    nm[nl] = 0;
                    char *vs = colon + 1;
                    while (vs < eol && (*vs == ' ' || *vs == '\t')) vs++;
                    char *vv = (char *)xmalloc((size_t)(eol - vs) + 1);
                    memcpy(vv, vs, (size_t)(eol - vs));
                    vv[eol - vs] = 0;
                    dict_set(hd, str_value(nm), str_value(vv));
                    free(nm); free(vv);
                }
                linep = eol + 2;
            }
            dict_set(rq, str_value("머리말"), obj_val(hd));

            size_t have_body = len > head_end ? len - head_end : 0;
            if (have_body > want_body && want_body) have_body = want_body;
            Value bodyv = obj_val(str_from_utf8_n(req + head_end, have_body));
            dict_set(rq, str_value("본문"), bodyv);
            free(req);

            /* 처리 함수를 부릅니다.  터져도 서버는 살아 있어야 합니다. */
            long code = 500;
            char *body_out = NULL;
            Buf extra = {0};

            TryHandler h;
            h.prev = in->top_handler;
            in->top_handler = &h;
            Value args1 = obj_val(rq);
            if (setjmp(h.jmp) == 0) {
                Value arg = retain(args1);
                Value res = call_value(in, handler, &arg, 1, "handler", line);
                in->top_handler = h.prev;
                if (res.kind == V_DICT) {
                    Value *cv = dict_find(AS_DICT(res), str_value("코드"));
                    Value ck = str_value("코드");
                    cv = dict_find(AS_DICT(res), ck);
                    release(ck);
                    code = cv && cv->kind == V_INT ? (long)cv->as.i : 200;
                    Value bk = str_value("본문");
                    Value *bv = dict_find(AS_DICT(res), bk);
                    release(bk);
                    body_out = bv ? S(in, *bv) : xstrdup("");
                    Value hk = str_value("머리말");
                    Value *hv = dict_find(AS_DICT(res), hk);
                    release(hk);
                    if (hv && hv->kind == V_DICT) {
                        Dict *od = AS_DICT(*hv);
                        for (size_t i = 0; i < od->len; i++) {
                            char *k2 = S(in, od->e[i].key), *v2 = S(in, od->e[i].val);
                            buf_str(&extra, k2); buf_str(&extra, ": ");
                            buf_str(&extra, v2); buf_str(&extra, "\r\n");
                            free(k2); free(v2);
                        }
                    }
                } else {
                    code = 200;
                    body_out = S(in, res);
                }
                release(res);
            } else {
                in->top_handler = h.prev;
                code = 500;
                body_out = xsprintf("500 Internal Server Error\n%s\n", lumi_err_msg);
                char *msg = xsprintf("httpserve: handler failed - %s\n", lumi_err_msg);
                in->output(msg);
                free(msg);
            }
            release(args1);

            if (!body_out) body_out = xstrdup("");
            const char *extra_h = extra.p ? extra.p : "";
            /* 길이는 바이트로 세어야 합니다 (한글은 글자 수와 다릅니다) */
            char *head = xsprintf("HTTP/1.1 %ld %s\r\n%s"
                                  "Content-Length: %zu\r\n"
                                  "Connection: close\r\n"
                                  "%s\r\n",
                                  code, code >= 200 && code < 300 ? "OK" : "Error",
                                  strstr(extra_h, "Content-Type") || strstr(extra_h, "content-type")
                                      ? "" : "Content-Type: text/plain; charset=utf-8\r\n",
                                  strlen(body_out), extra_h);
            plat_send(c, head, strlen(head));
            plat_send(c, body_out, strlen(body_out));
            free(head);
            free(body_out);
            free(extra.p);
            plat_closesock(c);
            served++;
        }
        plat_closesock(server);
        release(handler);
        return int_val(served);
    }

    /* --- 다른 프로그램 부르기 ---
     *   shell(명령)      돌리고, 그 프로그램이 찍은 글자를 돌려줍니다
     *   shellcode(명령)  돌리고, 종료 코드(성공이면 0)만 돌려줍니다
     * 둘 다 표준오류까지 함께 받습니다.  명령은 셸에 그대로 넘어갑니다.
     * (이름을 run 이 아니라 shell 로 둔 까닭: run 은 사람들이 자기 함수 이름으로
     *  아주 흔히 쓰는 낱말이라, 내장으로 삼으면 남의 run() 을 조용히 가립니다.) */
    if (name_in(name, SHELL_NAMES)) {
        if (NARG != 1)
            lumi_error(line, "%s needs one command, like %s(\"git status\")", name, name);
        Value v = ARG(0);
        Str *cs = require_text(in, name, v, "for the command", line);
        char *cmd = str_to_utf8(cs);
        release(v);
        if (!*cmd) { free(cmd); lumi_error(line, "%s got an empty command", name); }

        int rc = -1;
        /* 셸 프로그램을 기다리는 시간도 다른 일감에게 넘깁니다. */
        g_execution_locked = false;
        plat_gil_unlock();
        char *out = plat_run(cmd, &rc);
        plat_gil_lock();
        g_execution_locked = true;
        if (!out) {
            char *bad = xstrdup(cmd);
            free(cmd);
            lumi_error(line, "%s could not start '%s'; is the program installed and on PATH?",
                       name, bad);
        }
        free(cmd);
        if (strcmp(name, "shellcode") == 0) { free(out); return int_val(rc); }
        /* 윈도우 프로그램은 줄 끝을 

 으로 냅니다.  read() 와 같은 규칙으로
         * 
 하나로 맞춰 주어야 같은 명령이 어느 운영체제에서나 같게 보입니다. */
        size_t on = drop_cr(out, strlen(out));
        out[on] = 0;
        Value res = str_value(out);
        free(out);
        return res;
    }

    /* --- 지문(해시)과 옮겨 적기 ---
     *   sha256(글자나 바이트)   -> 64 글자 16진수 지문
     *   md5(글자나 바이트)      -> 32 글자 16진수 지문 (옛 자료 맞춰 볼 때만)
     *   base64(글자나 바이트)   -> base64 글자
     *   unbase64(글자)          -> 바이트 (되돌리기)
     */
    if (name_in(name, DIGEST_NAMES) || name_in(name, B64_NAMES)) {
        if (NARG != 1)
            lumi_error(line, "%s needs exactly one value, like %s(\"hello\")", name, name);
        Value v = ARG(0);

        if (strcmp(name, "unbase64") == 0) {
            Str *t = require_text(in, "unbase64", v, "to turn back into bytes", line);
            char *u = str_to_utf8(t);
            release(v);
            size_t outn = 0;
            uint8_t *raw = b64_decode(u, strlen(u), &outn);
            free(u);
            if (!raw)
                lumi_error_kind(line, "ValueError",
                                "unbase64 got a letter that base64 never uses; only "
                                "A-Z a-z 0-9 + / and = are allowed");
            BytesObj *b = bytes_new(raw, outn);
            free(raw);
            return obj_val(b);
        }

        char *owner = NULL;
        size_t n = 0;
        const uint8_t *d = bytes_of_arg(in, name, v, &n, &owner, line);

        Value out;
        if (strcmp(name, "base64") == 0) {
            char *t = b64_encode(d, n);
            out = str_value(t);
            free(t);
        } else if (strcmp(name, "sha256") == 0) {
            uint8_t dig[32];
            sha256_bytes(d, n, dig);
            out = hex_text_of(dig, 32);
        } else {
            uint8_t dig[16];
            md5_bytes(d, n, dig);
            out = hex_text_of(dig, 16);
        }
        free(owner);
        release(v);
        return out;
    }

    /* --- 폴더 훑기와 경로 다루기 ---
     *   listdir([무엇,] 폴더)  폴더 안 이름들.  무엇 = file / dir (따옴표 없이)
     *   filename(경로)         마지막 조각 ("자료/a.txt" -> "a.txt")
     *   foldername(경로)       앞의 폴더 부분 ("자료/a.txt" -> "자료")
     *   pathjoin(...)          조각들을 이 운영체제의 구분자로 잇기
     *   fullpath(경로)         절대 경로로 펴기
     */
    if (name_in(name, PATH_NAMES)) {
        if (strcmp(name, "listdir") == 0) {
            /* 무엇을 볼지는 따옴표 없는 낱말입니다 (open 의 read/write 처럼) */
            const char *kind = "all";
            size_t at = 0;
            if (NARG == 2) {
                Node *k = ARGS.items[0];
                if (k->kind != N_VAR || !name_in(k->v.var.name, LIST_KINDS))
                    lumi_error(line, "listdir's what must be written as file or dir without "
                                     "quotes, like listdir(file, \"folder\")");
                kind = k->v.var.name;
                at = 1;
            } else if (NARG != 1) {
                lumi_error(line, "listdir needs the folder to look in, and in front of it you "
                                 "may put what to look for: listdir(f), listdir(file, f), "
                                 "or listdir(dir, f)");
            }
            Value pv = eval(in, ARGS.items[at], env);
            char *path = file_path_of(in, "listdir", pv, line);
            release(pv);
            if (!plat_dir_exists(path)) {
                char *bad = xstrdup(path);
                free(path);
                lumi_error_kind(line, "FileNotFound",
                                "listdir cannot find the folder '%s'; check the name, or ask "
                                "isdir(name) first", bad);
            }
            Seq *out = seq_new(V_LIST);
            /* 폴더를 먼저, 그 다음 파일 — 어느 운영체제에서나 차례가 같도록 */
            if (strcmp(kind, "file") != 0) {
                char **d = plat_subdirs(path);
                if (d) { for (size_t i = 0; d[i]; i++) seq_push(out, str_value(d[i]));
                         plat_free_names(d); }
            }
            if (strcmp(kind, "dir") != 0) {
                char **f = plat_listdir(path);
                if (f) { for (size_t i = 0; f[i]; i++) seq_push(out, str_value(f[i]));
                         plat_free_names(f); }
            }
            free(path);
            /* 이름 차례는 운영체제마다 다르므로 가나다순으로 맞춰 돌려줍니다 */
            Value raw = obj_val(out);
            Value sorted = sort_sequence(in, false, NONE_VAL, raw, line);
            release(raw);
            return sorted;
        }

        if (strcmp(name, "filename") == 0 || strcmp(name, "foldername") == 0) {
            if (NARG != 1)
                lumi_error(line, "%s needs one path, like %s(\"folder/note.txt\")", name, name);
            Value v = ARG(0);
            Str *sv = require_text(in, name, v, "for the path", line);
            char *u = str_to_utf8(sv);
            release(v);
            /* 마지막 구분자를 찾습니다 (윈도우는 \ 와 / 둘 다) */
            char *cut = NULL;
            for (char *q = u; *q; q++) if (plat_is_sep(*q)) cut = q;
            Value out;
            if (strcmp(name, "filename") == 0) {
                out = str_value(cut ? cut + 1 : u);
            } else if (!cut) {
                out = str_value("");            /* 폴더가 없으면 빈 글자 */
            } else {
                *cut = 0;
                out = str_value(u[0] ? u : plat_dir_sep());   /* "/a.txt" 의 폴더는 뿌리 */
            }
            free(u);
            return out;
        }

        if (strcmp(name, "pathjoin") == 0) {
            /* 조각을 여럿 주거나, 리스트 하나를 줘도 됩니다 */
            Seq *pieces = NULL;
            Value listv = NONE_VAL;
            if (NARG == 1) {
                listv = ARG(0);
                if (listv.kind == V_LIST || listv.kind == V_TUPLE) pieces = AS_SEQ(listv);
            }
            if (NARG == 0)
                lumi_error(line, "pathjoin needs the pieces to join, like "
                                 "pathjoin(\"folder\", \"note.txt\")");
            Buf b = {0};
            size_t count = pieces ? pieces->len : NARG;
            for (size_t i = 0; i < count; i++) {
                Value piece = pieces ? retain(pieces->items[i])
                            : (i == 0 && NARG == 1 ? retain(listv) : eval(in, ARGS.items[i], env));
                Str *ps = require_text(in, "pathjoin", piece, "for each piece", line);
                char *u = str_to_utf8(ps);
                release(piece);
                if (!*u) { free(u); continue; }             /* 빈 조각은 건너뜁니다 */
                /* 앞 조각이 이미 구분자로 끝났으면 또 붙이지 않습니다 */
                if (b.len && !plat_is_sep(b.p[b.len - 1])) buf_str(&b, plat_dir_sep());
                buf_str(&b, u);
                free(u);
            }
            if (NARG == 1) release(listv);
            char *joined = buf_done(&b);
            Value out = str_value(joined);
            free(joined);
            return out;
        }

        /* fullpath(경로) */
        if (NARG != 1)
            lumi_error(line, "fullpath needs one path, like fullpath(\"note.txt\")");
        Value v = ARG(0);
        char *path = file_path_of(in, "fullpath", v, line);
        release(v);
        char *full = plat_fullpath(path);
        free(path);
        Value out = str_value(full);
        free(full);
        return out;
    }

    /* --- 텍스트 파일 (read / write / append / isfile) --- */
    if (name_in(name, FILE_NAMES)) {
        /* --- renamefile(새 이름, 파일이름) — 집 규칙대로 바꿀 파일이 뒤입니다 --- */
        if (strcmp(name, "renamefile") == 0) {
            if (NARG != 2)
                lumi_error(line, "renamefile needs the new name and then the file to rename, "
                                 "like renamefile(\"new.txt\", \"old.txt\")");
            Value nv = ARG(0), ov = ARG(1);
            char *to   = file_path_of(in, name, nv, line);
            char *from = file_path_of(in, name, ov, line);
            release(nv);
            release(ov);
            if (!file_exists(from))
                lumi_error_kind(line, "FileNotFound", "renamefile cannot find the file '%s' to rename; check the "
                                 "name, or ask isfile(name) first", from);
            if (file_exists(to))
                lumi_error(line, "renamefile will not rename '%s' to '%s' because a file with "
                                 "that name is already there; delete it first with "
                                 "delfile(name)", from, to);
            rename_file(name, from, to, line);
            free(to);
            free(from);
            return bool_val(true);
        }

        /* --- movefile(폴더, 파일이름) — 집 규칙대로 옮길 파일이 뒤입니다 ---
         * 이름은 그대로 두고 자리만 옮깁니다 (이름까지 바꾸려면 옮긴 뒤 renamefile). */
        if (strcmp(name, "movefile") == 0) {
            if (NARG != 2)
                lumi_error(line, "movefile needs the folder and then the file to move, "
                                 "like movefile(\"backup\", \"note.txt\")");
            Value dv = ARG(0), fv = ARG(1);
            char *dir  = file_path_of(in, name, dv, line);
            char *from = file_path_of(in, name, fv, line);
            release(dv);
            release(fv);
            if (!file_exists(from))
                lumi_error_kind(line, "FileNotFound", "movefile cannot find the file '%s' to move; check the "
                                 "name, or ask isfile(name) first", from);
            if (!folder_exists(dir))
                lumi_error(line, "movefile cannot find the folder '%s' to move '%s' into; "
                                 "make the folder first", dir, from);
            /* 파일 이름만 떼어 폴더 뒤에 붙입니다 */
            const char *base = from + strlen(from);
            while (base > from && !plat_is_sep(base[-1])) base--;
            char *to = xsprintf("%s%s%s", dir, plat_dir_sep(), base);
            if (file_exists(to))
                lumi_error(line, "movefile will not move '%s' to '%s' because a file with "
                                 "that name is already there; delete it first with "
                                 "delfile(name)", from, to);
            rename_file(name, from, to, line);
            free(to);
            free(dir);
            free(from);
            return bool_val(true);
        }

        if (name_in(name, FILE_ASK_NAMES)) {
            bool is_dir_func = (strcmp(name, "newdir") == 0 || strcmp(name, "deldir") == 0 || strcmp(name, "isdir") == 0);
            if (NARG != 1)
                lumi_error(line, "%s needs exactly one %s name, like %s(\"%s\")",
                           name, is_dir_func ? "folder" : "file", name, is_dir_func ? "backup" : "note.txt");
            Value v = ARG(0);
            char *path = file_path_of(in, name, v, line);
            release(v);
            bool there = is_dir_func ? folder_exists(path) : file_exists(path);
            if (strcmp(name, "newfile") == 0) {
                if (!there) make_file(path, line);          /* 있으면 그대로 둡니다 */
            } else if (strcmp(name, "newdir") == 0) {
                if (!there) make_folder(path, line);        /* 있으면 그대로 둡니다 */
            } else if (strcmp(name, "delfile") == 0) {
                if (!there)
                    lumi_error_kind(line, "FileNotFound", "delfile cannot find the file '%s' to delete; check "
                                     "the name, or ask isfile(name) first", path);
                delete_file(path, line);
            } else if (strcmp(name, "deldir") == 0) {
                if (!there)
                    lumi_error(line, "deldir cannot find the folder '%s' to delete; check "
                                     "the name", path);
                delete_folder(path, line);
            }
            free(path);
            /* isfile: 있는지 / newfile, newdir: 새로 만들었는지 / delfile, deldir: 지웠는지 */
            return bool_val((strcmp(name, "newfile") == 0 || strcmp(name, "newdir") == 0) ? !there : there);
        }

        /* --- open([인코딩,] [어떻게,] 파일이름) --- */
        if (strcmp(name, "open") == 0) {
            if (NARG < 1 || NARG > 3)
                lumi_error(line, "open needs the file name, and in front of it you may put "
                                 "the encoding and how to open it, like open(\"note.txt\"), "
                                 "open(write, \"note.txt\"), or open(\"cp949\", append, "
                                 "\"note.txt\")");
            /* 여는 방법은 따옴표 없는 낱말이라 '적힌 말' 로 읽습니다 (inpair 의 모양처럼) */
            const char *mode = "read";
            bool has_enc = false;
            Value enc = NONE_VAL;
            for (size_t i = 0; i + 1 < NARG; i++) {
                Node *a = ARGS.items[i];
                if (a->kind == N_VAR && name_in(a->v.var.name, FILE_MODES)) {
                    mode = a->v.var.name;
                } else if (a->kind == N_VAR && !env_lookup(env, a->v.var.name, NULL)) {
                    lumi_error(line, "open does not know '%s'; write how to open it as "
                                     "read, write, or append without quotes, like "
                                     "open(write, \"note.txt\")", a->v.var.name);
                } else if (has_enc) {
                    lumi_error(line, "open takes one encoding and one way to open it, like "
                                     "open(\"cp949\", write, \"note.txt\")");
                } else {
                    enc = eval(in, a, env);
                    has_enc = true;
                }
            }
            const char *codec = encoding_of(in, "open", has_enc, enc, line);
            if (has_enc) release(enc);
            if (strcmp(codec, "utf-16") == 0)
                lumi_error(line, "open cannot use utf-16 because it works one line at a "
                                 "time; read or write the whole file at once instead, with "
                                 "read(\"utf-16\", name)");
            Value fname = ARG(NARG - 1);
            char *path = file_path_of(in, "open", fname, line);
            release(fname);
            Value out = open_file(in, mode, codec, path, line);
            free(path);
            return out;
        }

        /* --- move(자리, 파일) --- */
        if (strcmp(name, "move") == 0) {
            if (NARG != 2)
                lumi_error(line, "move needs the place to go to and the file that was "
                                 "opened with open(...), like move(0, f)");
            Value place = ARG(0);
            Value v = ARG(1);
            FileObj *f = open_file_of(in, "move", v, line);
            Value out = move_in_file(in, place, f, line);
            release(place);
            release(v);
            return out;
        }

        /* --- close / readline / isend / where --- */
        if (name_in(name, FILE_USE_NAMES)) {
            if (NARG != 1)
                lumi_error(line, "%s needs exactly one file that was opened with open(...), "
                                 "like %s(f)", name, name);
            Value v = ARG(0);
            if (strcmp(name, "close") == 0) {
                if (v.kind != V_FILE) {
                    char *r = R(in, v);
                    lumi_error(line, "close needs a file that was opened with open(...) but "
                                     "got %s", r);
                }
                FileObj *f = AS_FILE(v);
                bool was_open = f->fp != NULL;
                if (was_open) { fclose(f->fp); f->fp = NULL; }
                release(v);
                return bool_val(was_open);       /* 이미 닫혀 있었으면 false */
            }
            FileObj *f = open_file_of(in, name, v, line);
            Value out;
            if (strcmp(name, "where") == 0) {
                out = int_val((long long)ftell(f->fp));   /* 읽든 쓰든 물어볼 수 있습니다 */
            } else {
                need_reading(f, name, line);
                out = strcmp(name, "isend") == 0 ? bool_val(file_at_end(f))
                                                 : read_line_of(in, f, line);
            }
            release(v);
            return out;
        }
        bool reading = strcmp(name, "read") == 0;
        bool one_line = strcmp(name, "writeline") == 0;   /* 끝에 줄바꿈을 붙여 씁니다 */
        size_t plain = reading ? 1 : 2;              /* 인코딩을 뺀 값의 개수 */
        bool has_enc = NARG == plain + 1;
        if (NARG != plain && !has_enc) {
            if (reading)
                lumi_error(line, "read needs the file name, and in front of it you may put "
                                 "the encoding, like read(\"note.txt\") or "
                                 "read(\"cp949\", \"note.txt\")");
            lumi_error(line, "%s needs the text to put in and the file name, and in front "
                             "of them you may put the encoding, like %s(\"hi\", "
                             "\"note.txt\") or %s(\"cp949\", \"hi\", \"note.txt\")",
                       name, name, name);
        }
        size_t at = has_enc ? 1 : 0;
        Value enc = has_enc ? ARG(0) : NONE_VAL;
        const char *codec = encoding_of(in, name, has_enc, enc, line);
        if (has_enc) release(enc);

        Value text = reading ? NONE_VAL : ARG(at);
        if (one_line) {                              /* 내용 뒤에 줄바꿈 하나를 붙입니다 */
            Str *s = require_text(in, name, text, "to write", line);
            Str *nl = str_from_utf8("\n");
            Value joined = obj_val(str_concat(s, nl));
            release(obj_val(nl));
            release(text);
            text = joined;
        }
        Value target = ARG(at + (reading ? 0 : 1));

        /* 마지막 자리가 파일 '이름' 이 아니라 open 으로 열어 둔 파일일 수도 있습니다 */
        if (target.kind == V_FILE) {
            if (has_enc)
                lumi_error(line, "%s does not take an encoding for an open file; the "
                                 "encoding is chosen once, in open(...)", name);
            if (strcmp(name, "append") == 0)
                lumi_error(line, "append works on a file name; an open file already writes "
                                 "at its end, so use open(append, name) and then "
                                 "write(text, f)");
            FileObj *f = open_file_of(in, name, target, line);
            Value out;
            if (reading) {
                need_reading(f, name, line);
                out = read_rest_of(in, f, line);
            } else {
                out = write_to_open_file(in, name, f, text, line);
            }
            release(target);
            release(text);
            return out;
        }

        char *path = file_path_of(in, name, target, line);
        release(target);
        Value out = reading ? read_file(in, codec, path, line)
                            : write_file(in, name, codec, text, path, line);
        free(path);
        release(text);
        return out;
    }

    /* --- type(값) --- */
    if (strcmp(name, "type") == 0) {
        if (NARG != 1) lumi_error(line, "type needs exactly one value");
        Value v = ARG(0);
        Value out = str_value(type_name_of(v));
        release(v);
        return out;
    }

    /* --- len(값) --- */
    if (strcmp(name, "len") == 0) {
        if (NARG != 1) lumi_error(line, "len needs exactly one value");
        Value v = ARG(0);
        if (v.kind == V_FILE) {                    /* 열어 둔 파일은 몇 바이트인지 */
            FileObj *f = open_file_of(in, "len", v, line);
            Value out = int_val(file_size_of(f));
            release(v);
            return out;
        }
        if (v.kind == V_INSTANCE) {
            LumiClass *owner = NULL;
            Func *m = class_find_method(AS_INST(v)->cls, "len", &owner);
            if (m) {
                Value res = invoke_function(in, m, NULL, 0, "len", line, AS_INST(v), owner);
                release(v);
                return res;
            }
        }
        if (v.kind != V_LIST && v.kind != V_TUPLE && v.kind != V_STR
            && v.kind != V_DICT && v.kind != V_BYTES) {
            char *s = S(in, v);
            lumi_error(line, "len needs a list, tuple, string, bytes, dictionary, file, or "
                             "class instance with len() method but got '%s'", s);
        }
        Value out = int_val((long long)seq_length(v));
        release(v);
        return out;
    }

    /* --- keys / values / has --- */
    if (strcmp(name, "keys") == 0 || strcmp(name, "values") == 0) {
        if (NARG != 1) lumi_error(line, "%s needs exactly one dictionary", name);
        Value v = ARG(0);
        if (v.kind != V_DICT) {
            char *s = S(in, v);
            lumi_error(line, "%s needs a dictionary but got '%s'", name, s);
        }
        Dict *d = AS_DICT(v);
        Seq *out = seq_new(V_LIST);
        bool want_keys = strcmp(name, "keys") == 0;
        for (size_t i = 0; i < d->len; i++)
            seq_push(out, retain(want_keys ? d->e[i].key : d->e[i].val));
        release(v);
        return obj_val(out);
    }
    if (strcmp(name, "has") == 0) {
        if (NARG != 2) lumi_error(line, "has needs a dictionary and a key");
        Value v = ARG(0);
        if (v.kind != V_DICT) {
            char *s = S(in, v);
            lumi_error(line, "has needs a dictionary but got '%s'", s);
        }
        Value key = ARG(1);
        check_dict_key(in, key, line);
        bool found = dict_find(AS_DICT(v), key) != NULL;
        release(v); release(key);
        return bool_val(found);
    }

    /* --- push / pop --- */
    if (strcmp(name, "push") == 0) {
        if (NARG != 2) lumi_error(line, "push needs a list and a value");
        Value lst = ARG(0);
        if (lst.kind != V_LIST) {
            char *s = S(in, lst);
            lumi_error(line, "push needs a list but got '%s'", s);
        }
        Value v = ARG(1);
        seq_push(AS_SEQ(lst), v);                /* 소유권 이동 */
        Value out = int_val((long long)AS_SEQ(lst)->len);
        release(lst);
        return out;
    }
    if (strcmp(name, "pop") == 0) {
        if (NARG != 1) lumi_error(line, "pop needs exactly one list");
        Value lst = ARG(0);
        if (lst.kind != V_LIST) {
            char *s = S(in, lst);
            lumi_error(line, "pop needs a list but got '%s'", s);
        }
        if (AS_SEQ(lst)->len == 0) lumi_error(line, "Cannot pop from an empty list");
        Value out = seq_pop(AS_SEQ(lst));
        release(lst);
        return out;
    }

    /* --- insert / erase --- */
    if (name_in(name, LIST_EDIT_NAMES)) {
        if (strcmp(name, "insert") == 0) {
            if (NARG != 3)
                lumi_error(line, "insert needs three values: the place to put it in, the "
                                 "value to put there, and the list to put it in, like "
                                 "insert(0, \"a\", xs)");
            Value where = ARG(0), value = ARG(1), lst = ARG(2);
            Value out = insert_into_list(in, where, value, lst, line);
            release(where); release(value); release(lst);
            return out;
        }
        Place where;
        memset(&where, 0, sizeof(where));
        Value lst;
        if (NARG == 1 && ARGS.items[0]->kind != N_RANGESPEC) {
            where.kind = PLACE_ALL;
            lst = ARG(0);
        } else if (NARG == 2) {
            where = read_place(in, ARGS.items[0], env);
            lst = ARG(1);
        } else {
            lumi_error(line, "erase needs the list to work on, and in front of it you may "
                             "say which places to erase, like erase(xs), erase(0, xs), "
                             "erase(0:2, xs), or erase([0, 2], xs)");
            return NONE_VAL;
        }
        Value out = erase_from_list(in, &where, lst, line);
        free_place(&where);
        release(lst);
        return out;
    }

    /* --- del --- */
    if (name_in(name, DICT_EDIT_NAMES)) {
        if (NARG != 1 && NARG != 2)
            lumi_error(line, "del needs the dictionary to work on, and after it you may "
                             "say which key to delete, like del(d), del(d, \"a\"), or "
                             "del(d, [\"a\", \"b\"])");
        Value target = ARG(0);
        bool has_which = NARG == 2;
        Value which = has_which ? ARG(1) : NONE_VAL;
        Value out = delete_from_dict(in, has_which, which, target, line);
        if (has_which) release(which);
        release(target);
        return out;
    }

    /* --- json.parse / json_parse / json.stringify / json_stringify --- */
    if (strcmp(name, "json_parse") == 0 || strcmp(name, "json.parse") == 0 || strcmp(name, "json_stringify") == 0 || strcmp(name, "json.stringify") == 0) {
        if (NARG != 1) lumi_error(line, "%s needs exactly 1 argument", name);
        Value v = ARG(0);
        Value out;
        if (strstr(name, "parse")) {
            out = lumi_json_parse(in, v, line);
        } else {
            out = lumi_json_stringify(in, v, line);
        }
        release(v);
        return out;
    }

    /* --- gc() --- */
    if (strcmp(name, "gc") == 0) {
        size_t collected = gc_collect(in);
        return int_val((long long)collected);
    }

    /* --- isa(값, 클래스) --- */
    if (strcmp(name, "isa") == 0) {
        if (NARG != 2) lumi_error(line, "isa needs a value and a class");
        Value v = ARG(0), c = ARG(1);
        if (c.kind != V_CLASS) {
            char *r = R(in, c);
            lumi_error(line, "isa needs a class as its second value but got %s", r);
        }
        bool yes = v.kind == V_INSTANCE && class_inherits(AS_INST(v)->cls, AS_CLASS(c));
        release(v); release(c);
        return bool_val(yes);
    }

    /* --- eval(문자열) : 글자로 적어 둔 식을 계산합니다 --- */
    if (strcmp(name, "eval") == 0) {
        if (NARG != 1)
            lumi_error(line, "eval needs exactly one text value holding an expression, "
                             "like eval(\"1 + 2\")");
        Value v = ARG(0);
        Str *s = require_text(in, "eval", v, "to run", line);
        char *utf8 = str_to_utf8(s);
        release(v);
        char *code = xsprintf("%s\n", utf8);   /* 렉서는 줄 끝을 봅니다 */
        free(utf8);
        /* ponytail: 만든 AST 는 그대로 둡니다 - 이 인터프리터는 어디서도 AST 를
         * 반납하지 않습니다.  eval 을 오래 도는 반복문에서 쓰면 메모리가 늘어나니,
         * 그때 노드 반납 함수를 만드세요. */
        TokenList *tokens = tokenize(code);
        free(code);
        NodeList prog = parse_program(tokens);
        tokenlist_free(tokens);
        if (prog.len != 1) {
            free(prog.items);
            lumi_error(line, "eval needs exactly one expression, like eval(\"1 + 2\")");
        }
        Node *expr = prog.items[0];
        free(prog.items);
        return eval(in, expr, env);   /* 부르는 자리의 변수를 그대로 씁니다 */
    }

    /* --- 사용자가 만든 함수 / 클래스 --- */
    Value callee = env_get(env, name, line);
    if (callee.kind == V_MODULE) {
        release(callee);
        lumi_error(line, "'%s' is a library; call its functions as '%s.something(...)'",
                   name, name);
    }
    Value *args = NULL; char **anames = NULL;
    eval_call_args(in, &ARGS, env, &args, &anames);
    Value out = call_value_kw(in, callee, args, NARG, anames, name, line);
    free(args);
    free(anames);
    release(callee);
    return out;
}
