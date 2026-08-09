/* 글자 다루기: UTF-8 <-> 코드포인트, 그리고 인코딩(encode/decode)
 *
 * 문자열은 파이썬처럼 코드포인트의 나열입니다.
 * utf-8 / utf-16 / ascii / latin-1 은 여기서 직접 다루고 (어느 운영체제에서나 같습니다),
 * cp949 / euc-kr 처럼 표가 있어야 하는 것만 platform.h 를 거쳐 운영체제에 맡깁니다.
 */
#include "lumi.h"
#include "platform.h"

#include <stdlib.h>
#include <string.h>

size_t cp_utf8_size(uint32_t c)
{
    if (c < 0x80) return 1;
    if (c < 0x800) return 2;
    if (c < 0x10000) return 3;
    return 4;
}

uint32_t *utf8_to_cp(const char *s, size_t n, size_t *out_len)
{
    const unsigned char *p = (const unsigned char *)s;
    uint32_t *out = (uint32_t *)xmalloc((n + 1) * sizeof(uint32_t));
    size_t len = 0, i = 0;
    while (i < n) {
        unsigned char c = p[i];
        uint32_t cp;
        size_t extra;
        if (c < 0x80)        { cp = c;        extra = 0; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
        else { cp = 0xFFFD; extra = 0; }        /* 깨진 바이트는 물음표 글자로 */
        if (i + extra >= n) {          /* 뒤가 잘렸으면 물음표 글자 하나로 */
            out[len++] = 0xFFFD;
            i++;
            continue;
        }
        for (size_t k = 1; k <= extra; k++) {
            unsigned char cc = p[i + k];
            if ((cc & 0xC0) != 0x80) { cp = 0xFFFD; extra = 0; break; }
            cp = (cp << 6) | (cc & 0x3F);
        }
        out[len++] = cp;
        i += extra + 1;
    }
    out[len] = 0;
    if (out_len) *out_len = len;
    return out;
}

char *cp_to_utf8(const uint32_t *cp, size_t len, size_t *out_bytes)
{
    size_t total = 0;
    for (size_t i = 0; i < len; i++) total += cp_utf8_size(cp[i]);
    char *out = (char *)xmalloc(total + 1);
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        uint32_t c = cp[i];
        if (c < 0x80) {
            out[j++] = (char)c;
        } else if (c < 0x800) {
            out[j++] = (char)(0xC0 | (c >> 6));
            out[j++] = (char)(0x80 | (c & 0x3F));
        } else if (c < 0x10000) {
            out[j++] = (char)(0xE0 | (c >> 12));
            out[j++] = (char)(0x80 | ((c >> 6) & 0x3F));
            out[j++] = (char)(0x80 | (c & 0x3F));
        } else {
            out[j++] = (char)(0xF0 | (c >> 18));
            out[j++] = (char)(0x80 | ((c >> 12) & 0x3F));
            out[j++] = (char)(0x80 | ((c >> 6) & 0x3F));
            out[j++] = (char)(0x80 | (c & 0x3F));
        }
    }
    out[j] = 0;
    if (out_bytes) *out_bytes = j;
    return out;
}

/* ---------------- 인코딩 이름 ---------------- */

static const struct { const char *name, *canon; } ENCODINGS[] = {
    {"utf-8", "utf-8"}, {"utf8", "utf-8"},
    {"utf-16", "utf-16"}, {"utf16", "utf-16"},
    {"ascii", "ascii"},
    {"cp949", "cp949"},
    {"euc-kr", "euc-kr"}, {"euckr", "euc-kr"},
    {"latin-1", "latin-1"},
};

const char *encoding_canonical(const char *name)
{
    for (size_t i = 0; i < sizeof(ENCODINGS) / sizeof(ENCODINGS[0]); i++)
        if (strcmp(ENCODINGS[i].name, name) == 0) return ENCODINGS[i].canon;
    return NULL;
}

/* 한 바이트가 곧 글자 번호인 약속들.  담을 수 있는 가장 큰 번호를 돌려주고,
 * 그런 약속이 아니면 0 을 돌려줍니다 (ascii 는 0..127, latin-1 은 0..255). */
static uint32_t one_byte_top(const char *canon)
{
    if (strcmp(canon, "ascii") == 0)   return 0x7F;
    if (strcmp(canon, "latin-1") == 0) return 0xFF;
    return 0;
}

/* 코드포인트 -> UTF-16 코드유닛 배열.
 * wchar_t 는 운영체제마다 크기가 달라(윈도우 16비트, 리눅스·맥 32비트)
 * 뜻이 흔들리므로, UTF-16 을 다룰 때는 언제나 uint16_t 를 씁니다. */
static uint16_t *cp_to_utf16(const uint32_t *cp, size_t len, int *out_len)
{
    uint16_t *w = (uint16_t *)xmalloc((len * 2 + 1) * sizeof(uint16_t));
    int n = 0;
    for (size_t i = 0; i < len; i++) {
        uint32_t c = cp[i];
        if (c < 0x10000) {
            w[n++] = (uint16_t)c;
        } else {
            c -= 0x10000;
            w[n++] = (uint16_t)(0xD800 + (c >> 10));
            w[n++] = (uint16_t)(0xDC00 + (c & 0x3FF));
        }
    }
    w[n] = 0;
    *out_len = n;
    return w;
}

static Str *utf16_to_str(const uint16_t *w, int wlen)
{
    uint32_t *cp = (uint32_t *)xmalloc(((size_t)wlen + 1) * sizeof(uint32_t));
    size_t len = 0;
    for (int i = 0; i < wlen; i++) {
        unsigned c = w[i];
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < wlen) {
            unsigned lo = w[i + 1];
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                cp[len++] = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
                i++;
                continue;
            }
        }
        cp[len++] = c;
    }
    Str *s = str_new(cp, len);
    free(cp);
    return s;
}

BytesObj *encode_str(const Str *s, const char *canon)
{
    if (strcmp(canon, "utf-8") == 0) {
        size_t nb = 0;
        char *u = cp_to_utf8(s->cp, s->len, &nb);
        BytesObj *b = bytes_new((const uint8_t *)u, nb);
        free(u);
        return b;
    }

    int wlen = 0;
    uint16_t *w = cp_to_utf16(s->cp, s->len, &wlen);

    if (strcmp(canon, "utf-16") == 0) {
        /* 파이썬의 utf-16 은 맨 앞에 표시(BOM) 2바이트를 붙입니다 */
        size_t nb = (size_t)wlen * 2 + 2;
        uint8_t *buf = (uint8_t *)xmalloc(nb);
        buf[0] = 0xFF; buf[1] = 0xFE;
        for (int i = 0; i < wlen; i++) {
            buf[2 + i * 2]     = (uint8_t)(w[i] & 0xFF);
            buf[2 + i * 2 + 1] = (uint8_t)(w[i] >> 8);
        }
        free(w);
        BytesObj *b = bytes_new(buf, nb);
        free(buf);
        return b;
    }

    free(w);

    /* ascii / latin-1 은 글자 번호가 곧 바이트 번호입니다 (표가 따로 필요 없습니다) */
    uint32_t top = one_byte_top(canon);
    if (top) {
        uint8_t *buf = (uint8_t *)xmalloc(s->len + 1);
        for (size_t i = 0; i < s->len; i++) {
            if (s->cp[i] > top) { free(buf); return NULL; }   /* 담을 수 없는 글자 */
            buf[i] = (uint8_t)s->cp[i];
        }
        BytesObj *b = bytes_new(buf, s->len);
        free(buf);
        return b;
    }

    /* cp949 / euc-kr 처럼 표가 있어야 하는 것은 운영체제에 맡깁니다 */
    size_t nb = 0;
    char *u = cp_to_utf8(s->cp, s->len, &nb);
    size_t outn = 0;
    char *enc = plat_encode_utf8(u, nb, canon, &outn);
    free(u);
    if (!enc) return NULL;
    BytesObj *b = bytes_new((const uint8_t *)enc, outn);
    free(enc);
    return b;
}

Str *decode_bytes(const BytesObj *b, const char *canon)
{
    if (strcmp(canon, "utf-16") == 0) {
        if (b->len % 2 != 0) return NULL;
        const uint8_t *p = b->data;
        size_t n = b->len;
        bool big = false;
        if (n >= 2 && p[0] == 0xFF && p[1] == 0xFE) { p += 2; n -= 2; }
        else if (n >= 2 && p[0] == 0xFE && p[1] == 0xFF) { p += 2; n -= 2; big = true; }
        int wlen = (int)(n / 2);
        uint16_t *w = (uint16_t *)xmalloc(((size_t)wlen + 1) * sizeof(uint16_t));
        for (int i = 0; i < wlen; i++)
            w[i] = big ? (uint16_t)((p[i * 2] << 8) | p[i * 2 + 1])
                       : (uint16_t)((p[i * 2 + 1] << 8) | p[i * 2]);
        Str *s = utf16_to_str(w, wlen);
        free(w);
        return s;
    }

    if (b->len == 0) return str_new(NULL, 0);

    if (strcmp(canon, "utf-8") == 0) {
        size_t len = 0;
        uint32_t *cp = utf8_to_cp((const char *)b->data, b->len, &len);
        /* 깨진 바이트가 있었으면 utf8_to_cp 가 U+FFFD 를 넣습니다 — 그건 오류로 봅니다 */
        for (size_t i = 0; i < len; i++)
            if (cp[i] == 0xFFFD) { free(cp); return NULL; }
        Str *s = str_new(cp, len);
        free(cp);
        return s;
    }

    /* ascii / latin-1 은 바이트 번호가 곧 글자 번호입니다 */
    uint32_t top = one_byte_top(canon);
    if (top) {
        uint32_t *cp = (uint32_t *)xmalloc((b->len + 1) * sizeof(uint32_t));
        for (size_t i = 0; i < b->len; i++) {
            if (b->data[i] > top) { free(cp); return NULL; }   /* ascii 에 없는 바이트 */
            cp[i] = b->data[i];
        }
        Str *s = str_new(cp, b->len);
        free(cp);
        return s;
    }

    /* cp949 / euc-kr 은 운영체제의 표를 빌립니다 */
    size_t outn = 0;
    char *u = plat_decode_to_utf8((const char *)b->data, b->len, canon, &outn);
    if (!u) return NULL;
    size_t len = 0;
    uint32_t *cp = utf8_to_cp(u, outn, &len);
    free(u);
    Str *s = str_new(cp, len);
    free(cp);
    return s;
}
