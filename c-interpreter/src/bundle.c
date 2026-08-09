/* bundle.c — Lumi 프로그램을 실행 파일 하나로 묶기 (lumi build)
 * ==========================================================
 *   lumi build main.lumi            -> main.exe (윈도우) / main (리눅스·맥)
 *   lumi build main.lumi -o app.exe
 *
 * 하는 일은 단순합니다: lumi 실행 파일을 그대로 복사한 뒤, 그 **뒤에**
 * .lumi 파일들을 붙이고 맨 끝에 표시를 남깁니다.  실행 파일은 켜질 때
 * 자기 뒤를 들여다보고, 표시가 있으면 인자 대신 그 안의 프로그램을 돌립니다.
 *
 * 담는 것: 시작 파일과 **그 파일이 있는 폴더 아래의 모든 .lumi**.
 * 그래서 bring "models/user" 같은 폴더 구조가 그대로 살아 있습니다.
 *
 * 붙이는 모양 (모두 뒤에 이어 붙임):
 *   [파일 1] 이름길이(u32) 이름 내용길이(u32) 내용
 *   [파일 2] ...
 *   [꼬리]   시작파일이름길이(u32) 시작파일이름 개수(u32) 묶음시작자리(u64) "LUMIPACK"
 */
#include "lumi.h"
#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PACK_MAGIC "LUMIPACK"
#define PACK_MAGIC_LEN 8

/* ============================================================
 * 1. 담긴 파일들 (켜질 때 한 번 읽어 둡니다)
 * ============================================================ */

typedef struct { char *name; char *text; } Packed;

static Packed *g_packed;
static size_t  g_npacked;
static char   *g_pack_main;      /* 시작 파일 이름 (없으면 NULL) */
static bool    g_pack_tried;

static uint32_t rd32(FILE *f)
{
    unsigned char b[4];
    if (fread(b, 1, 4, f) != 4) return 0;
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}
static uint64_t rd64(FILE *f)
{
    unsigned char b[8];
    if (fread(b, 1, 8, f) != 8) return 0;
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--) v = (v << 8) | b[i];
    return v;
}
static void wr32(FILE *f, uint32_t v)
{
    unsigned char b[4] = { (unsigned char)v, (unsigned char)(v >> 8),
                           (unsigned char)(v >> 16), (unsigned char)(v >> 24) };
    fwrite(b, 1, 4, f);
}
static void wr64(FILE *f, uint64_t v)
{
    unsigned char b[8];
    for (int i = 0; i < 8; i++) b[i] = (unsigned char)(v >> (8 * i));
    fwrite(b, 1, 8, f);
}

/* 경로를 견주기 좋게 다듬습니다 ( \ -> /, 앞의 ./ 떼기 ) */
static char *norm_path(const char *p)
{
    while (p[0] == '.' && (p[1] == '/' || p[1] == '\\')) p += 2;
    char *out = xstrdup(p);
    for (char *q = out; *q; q++) if (*q == '\\') *q = '/';
    return out;
}

/* main.c 가 켜질 때 자기 자신의 자리를 알려 줍니다 */
void pack_open(const char *exe_path)
{
    if (g_pack_tried) return;
    g_pack_tried = true;
    if (!exe_path) return;

    FILE *f = plat_fopen(exe_path, "rb");
    if (!f) return;
    if (fseek(f, -(long)PACK_MAGIC_LEN, SEEK_END) != 0) { fclose(f); return; }
    char magic[PACK_MAGIC_LEN + 1] = {0};
    if (fread(magic, 1, PACK_MAGIC_LEN, f) != PACK_MAGIC_LEN
        || memcmp(magic, PACK_MAGIC, PACK_MAGIC_LEN) != 0) { fclose(f); return; }

    /* 꼬리를 거꾸로 읽습니다 */
    if (fseek(f, -(long)(PACK_MAGIC_LEN + 8), SEEK_END) != 0) { fclose(f); return; }
    uint64_t start = rd64(f);
    if (fseek(f, (long)start, SEEK_SET) != 0) { fclose(f); return; }

    uint32_t mainlen = rd32(f);
    if (mainlen == 0 || mainlen > 4096) { fclose(f); return; }
    char *mainname = (char *)xmalloc(mainlen + 1);
    if (fread(mainname, 1, mainlen, f) != mainlen) { free(mainname); fclose(f); return; }
    mainname[mainlen] = 0;

    uint32_t count = rd32(f);
    if (count > 4096) { free(mainname); fclose(f); return; }

    g_packed = (Packed *)xmalloc((count ? count : 1) * sizeof(Packed));
    for (uint32_t i = 0; i < count; i++) {
        uint32_t nl = rd32(f);
        if (nl == 0 || nl > 4096) break;
        char *nm = (char *)xmalloc(nl + 1);
        if (fread(nm, 1, nl, f) != nl) { free(nm); break; }
        nm[nl] = 0;
        uint32_t dl = rd32(f);
        char *tx = (char *)xmalloc((size_t)dl + 1);
        if (fread(tx, 1, dl, f) != dl) { free(nm); free(tx); break; }
        tx[dl] = 0;
        g_packed[g_npacked].name = nm;
        g_packed[g_npacked].text = tx;
        g_npacked++;
    }
    fclose(f);
    g_pack_main = mainname;
}

bool pack_has_program(void) { return g_pack_main != NULL && g_npacked > 0; }
const char *pack_main_name(void) { return g_pack_main; }

/* 담긴 파일을 이름으로 찾습니다.  없으면 NULL (그러면 진짜 파일을 봅니다). */
const char *pack_find(const char *path)
{
    if (!g_npacked || !path) return NULL;
    char *want = norm_path(path);
    const char *out = NULL;
    for (size_t i = 0; i < g_npacked; i++) {
        if (strcmp(g_packed[i].name, want) == 0) { out = g_packed[i].text; break; }
        /* 앞에 폴더가 더 붙어 온 경우도 봐 줍니다 (base_dir 이 붙은 경로) */
        size_t nl = strlen(g_packed[i].name), wl = strlen(want);
        if (wl > nl && strcmp(want + wl - nl, g_packed[i].name) == 0
            && want[wl - nl - 1] == '/') { out = g_packed[i].text; break; }
    }
    free(want);
    return out;
}

/* ============================================================
 * 2. 묶기 (lumi build)
 * ============================================================ */

typedef struct { char **v; size_t n, cap; } Names;

static void names_push(Names *a, const char *s)
{
    if (a->n == a->cap) { a->cap = a->cap ? a->cap * 2 : 16;
                          a->v = (char **)xrealloc(a->v, a->cap * sizeof(char *)); }
    a->v[a->n++] = xstrdup(s);
}

/* dir 아래의 .lumi 를 rel 을 앞에 붙인 이름으로 모읍니다 */
static void collect(const char *dir, const char *rel, Names *out)
{
    char **files = plat_listdir(dir);
    if (files) {
        for (size_t i = 0; files[i]; i++) {
            size_t n = strlen(files[i]);
            if (n < 5 || strcmp(files[i] + n - 5, ".lumi") != 0) continue;
            char *name = rel[0] ? xsprintf("%s/%s", rel, files[i]) : xstrdup(files[i]);
            names_push(out, name);
            free(name);
        }
        plat_free_names(files);
    }
    char **subs = plat_subdirs(dir);
    if (subs) {
        for (size_t i = 0; subs[i]; i++) {
            char *sub = xsprintf("%s%s%s", dir, plat_dir_sep(), subs[i]);
            char *r = rel[0] ? xsprintf("%s/%s", rel, subs[i]) : xstrdup(subs[i]);
            collect(sub, r, out);
            free(sub);
            free(r);
        }
        plat_free_names(subs);
    }
}

static bool copy_file(const char *from, const char *to)
{
    FILE *a = plat_fopen(from, "rb");
    if (!a) return false;
    FILE *b = plat_fopen(to, "wb");
    if (!b) { fclose(a); return false; }
    char buf[65536];
    size_t n;
    bool ok = true;
    while ((n = fread(buf, 1, sizeof buf, a)) > 0)
        if (fwrite(buf, 1, n, b) != n) { ok = false; break; }
    fclose(a);
    if (fclose(b) != 0) ok = false;
    return ok;
}

int run_build(const char *script, const char *out_opt, const char *exe_path)
{
    if (!plat_file_exists(script)) {
        printf("lumi build: cannot find '%s'\n", script);
        return 1;
    }
    if (!exe_path || !plat_file_exists(exe_path)) {
        printf("lumi build: cannot find the lumi program itself to copy\n");
        return 1;
    }

    /* 시작 파일이 있는 폴더가 묶음의 뿌리입니다 */
    char *full = plat_fullpath(script);
    char *root = xstrdup(full);
    char *cut = NULL;
    for (char *q = root; *q; q++) if (plat_is_sep(*q)) cut = q;
    char *mainrel;
    if (cut) { *cut = 0; mainrel = xstrdup(cut + 1); }
    else     { free(root); root = xstrdup("."); mainrel = xstrdup(script); }

    /* 내보낼 이름 정하기 */
    char *outname;
    if (out_opt) {
        outname = xstrdup(out_opt);
    } else {
        char *base = xstrdup(mainrel);
        size_t bl = strlen(base);
        if (bl > 5 && strcmp(base + bl - 5, ".lumi") == 0) base[bl - 5] = 0;
#if defined(_WIN32)
        outname = xsprintf("%s.exe", base);
#else
        outname = xstrdup(base);
#endif
        free(base);
    }

    Names files = {0};
    collect(root, "", &files);
    if (files.n == 0) {
        printf("lumi build: no .lumi files found under '%s'\n", root);
        return 1;
    }

    printf("lumi build: %s  ->  %s\n", script, outname);
    if (!copy_file(exe_path, outname)) {
        printf("lumi build: cannot write '%s'\n", outname);
        return 1;
    }

    FILE *o = plat_fopen(outname, "ab");
    if (!o) { printf("lumi build: cannot append to '%s'\n", outname); return 1; }
    /* "ab" 로 연 직후의 ftell 은 0 을 주기도 합니다 — 끝으로 옮겨 놓고 물어봅니다 */
    fseek(o, 0, SEEK_END);
    long start = ftell(o);

    char *mn = norm_path(mainrel);
    wr32(o, (uint32_t)strlen(mn));
    fwrite(mn, 1, strlen(mn), o);
    wr32(o, (uint32_t)files.n);

    size_t total = 0;
    for (size_t i = 0; i < files.n; i++) {
        char *path = xsprintf("%s%s%s", root, plat_dir_sep(), files.v[i]);
        /* 경로 구분자를 이 운영체제 것으로 되돌립니다 */
        for (char *q = path + strlen(root); *q; q++)
            if (*q == '/' && plat_dir_sep()[0] == '\\') *q = '\\';
        char *text = read_text_file(path);
        if (!text) { printf("  (건너뜀) %s\n", files.v[i]); free(path); continue; }
        size_t tl = strlen(text);
        wr32(o, (uint32_t)strlen(files.v[i]));
        fwrite(files.v[i], 1, strlen(files.v[i]), o);
        wr32(o, (uint32_t)tl);
        fwrite(text, 1, tl, o);
        printf("  담음 %s (%zu 글자)\n", files.v[i], tl);
        total += tl;
        free(text);
        free(path);
    }
    wr64(o, (uint64_t)start);
    fwrite(PACK_MAGIC, 1, PACK_MAGIC_LEN, o);
    fclose(o);

    printf("lumi build: 파일 %zu 개, 글자 %zu 개를 담았습니다.\n", files.n, total);
    printf("lumi build: 이제 '%s' 만 있으면 Lumi 없이 돌아갑니다.\n", outname);

    for (size_t i = 0; i < files.n; i++) free(files.v[i]);
    free(files.v);
    free(full); free(root); free(mainrel); free(outname); free(mn);
    return 0;
}
