/* ffi.c — 남이 만든 C 라이브러리 불러 쓰기 (Foreign Function Interface)
 * ==========================================================
 *
 * ★ 먼저 읽어 주세요 ★
 *
 * 여기 있는 것들은 **Lumi 에서 가장 위험한 부분**입니다. 서명을 한 글자만 틀리게
 * 적어도 프로그램이 그냥 죽습니다 (Lumi 오류가 아니라 운영체제가 내리는 죽음).
 * 잘못된 주소를 넘기면 남의 기억 장치를 밟습니다. 이것은 FFI 의 본성이고,
 * 어떤 언어에서도 마찬가지입니다.
 *
 * **그래서 쓰는 방식이 정해져 있습니다: 이것들은 배관이고, 물건은 라이브러리입니다.**
 * 보통 쓰는 사람은 `ccall` 을 직접 부르지 않습니다. `libraries/` 안에 Lumi 로 적은
 * 안전한 감싸개를 두고, 그것을 `bring` 합니다.
 *
 *     bring sqlite            <- 사람이 쓰는 것 (Lumi 로 적혀 있고 안전합니다)
 *       └ ccall(...)          <- 그 안에서만 쓰는 배관
 *
 * ---------------------------------------------------------------
 * 서명 적기
 * ---------------------------------------------------------------
 *   "인자들>돌려주는것"      예: "sp>i"  =  (글자, 주소) -> 32비트 정수
 *
 *   i  32비트 정수          l  64비트 정수         p  주소 (Lumi 에서는 정수)
 *   s  글자 (UTF-8 로 바꿔 넘깁니다)              b  바이트
 *   d  실수 (돌려받는 데만 씁니다 — 아래 참고)    v  없음 (돌려주는 자리에만)
 *
 * **인자로 실수(d)는 못 넘깁니다.** x86-64 는 정수와 실수를 서로 다른 레지스터로
 * 넘기는데, libffi 없이 그것을 자리마다 골라 맞추려면 경우의 수가 폭발합니다.
 * 실수를 넘겨야 하면 `cbuf` 로 자리를 잡아 `cput` 으로 넣고 주소를 넘기세요.
 * 돌려받는 것은 됩니다 (돌려주는 자료형은 인자 넘기는 방식을 안 바꿉니다).
 *
 * 인자는 여덟 개까지입니다.
 */
#include "lumi.h"
#include "platform.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================
 * 열어 둔 라이브러리들
 * ============================================================ */

/* 손잡이는 Lumi 쪽에 정수로 건네줍니다.  주소를 그대로 주지 않고 번호를 주는
 * 까닭은, 아무 정수나 cclose 에 넣었을 때 죽지 않고 오류를 내게 하려는 것입니다. */
typedef struct { void *h; char *name; bool open; } Lib;
static Lib   *g_libs;
static size_t g_nlibs, g_clibs;

static Lib *lib_at(long long id)
{
    if (id < 1 || (size_t)id > g_nlibs) return NULL;
    Lib *l = &g_libs[id - 1];
    return l->open ? l : NULL;
}

static long long lib_add(void *h, const char *name)
{
    if (g_nlibs == g_clibs) {
        g_clibs = g_clibs ? g_clibs * 2 : 8;
        g_libs = (Lib *)xrealloc(g_libs, g_clibs * sizeof(Lib));
    }
    g_libs[g_nlibs].h = h;
    g_libs[g_nlibs].name = xstrdup(name);
    g_libs[g_nlibs].open = true;
    g_nlibs++;
    return (long long)g_nlibs;          /* 1 부터 셉니다 (0 은 '없음') */
}

long long ffi_load(const char *name, char *why, size_t whyn)
{
    void *h = plat_dlopen(name, why, whyn);
    if (!h) return 0;
    return lib_add(h, name);
}

bool ffi_close(long long id)
{
    Lib *l = lib_at(id);
    if (!l) return false;
    plat_dlclose(l->h);
    l->open = false;
    return true;
}

/* 그 이름의 함수가 있습니까?  라이브러리가 닫혀 있으면 -1. */
int ffi_has(long long id, const char *symbol)
{
    Lib *l = lib_at(id);
    if (!l) return -1;
    return plat_dlsym(l->h, symbol) ? 1 : 0;
}

/* ============================================================
 * 빌린 자리 (cbuf / cfree)
 * ============================================================ */

/* cbuf 로 잡아 준 자리를 적어 둡니다.  cfree 가 **우리가 준 주소인지** 확인하고
 * 놓아 주게 하려는 것입니다 — 아무 정수나 free 하면 그 자리에서 죽어 버립니다.
 * FFI 가 원래 위험하다고 해서, 값싸게 막을 수 있는 것까지 내버려 둘 까닭은 없습니다. */
static void  **g_bufs;
static size_t  g_nbufs, g_cbufs;

long long ffi_buf(long long nbytes)
{
    if (nbytes <= 0 || nbytes > (1LL << 31)) return 0;
    void *p = calloc(1, (size_t)nbytes);      /* 0 으로 채워 줍니다 */
    if (!p) return 0;
    if (g_nbufs == g_cbufs) {
        g_cbufs = g_cbufs ? g_cbufs * 2 : 16;
        g_bufs = (void **)xrealloc(g_bufs, g_cbufs * sizeof(void *));
    }
    g_bufs[g_nbufs++] = p;
    return (long long)(uintptr_t)p;
}

bool ffi_free(long long addr)
{
    void *p = (void *)(uintptr_t)addr;
    for (size_t i = 0; i < g_nbufs; i++) {
        if (g_bufs[i] != p) continue;
        free(p);
        g_bufs[i] = g_bufs[--g_nbufs];        /* 자리 메우기 */
        return true;
    }
    return false;                             /* 우리가 준 자리가 아닙니다 */
}

/* ============================================================
 * 부르기
 * ============================================================ */

/* 정수 자리로 넘기는 인자들 — 여덟 개까지.
 * 돌려받는 것만 갈래를 나눕니다 (돌려주는 자료형은 인자 넘기는 방식을 안 바꿉니다). */
typedef long long (*FnI)(long long, long long, long long, long long,
                         long long, long long, long long, long long);
typedef double    (*FnD)(long long, long long, long long, long long,
                         long long, long long, long long, long long);

/* 실수를 **마지막 인자로** 넘기는 경우만 따로 다룹니다.
 *
 * 왜 마지막만인가: x86-64 는 실수를 정수와 다른 레지스터로 넘기고, 윈도우 ABI 는
 * 그 자리를 **몇 번째 인자인지**로 정합니다. 그래서 자리마다 정수/실수를 골라
 * 맞추려면 경우의 수가 2^n 으로 늘어납니다 (libffi 가 하는 일이 그것입니다).
 * 실제로 필요한 모양은 거의 다 `f(..., 실수)` 하나라서 — sqlite3_bind_double,
 * sqlite3_result_double, 대개의 set_xxx(대상, 값) — 그것만 열어 둡니다.
 * 인자 개수마다 **정확한 원형**이 있어야 하므로 여섯 개를 적어 둡니다. */
typedef long long (*L1D)(double);
typedef long long (*L2D)(long long, double);
typedef long long (*L3D)(long long, long long, double);
typedef long long (*L4D)(long long, long long, long long, double);
typedef long long (*L5D)(long long, long long, long long, long long, double);
typedef long long (*L6D)(long long, long long, long long, long long, long long, double);
typedef double    (*D1D)(double);
typedef double    (*D2D)(long long, double);
typedef double    (*D3D)(long long, long long, double);
typedef double    (*D4D)(long long, long long, long long, double);
typedef double    (*D5D)(long long, long long, long long, long long, double);
typedef double    (*D6D)(long long, long long, long long, long long, long long, double);

/* 인자를 여덟 칸으로 채워 부릅니다.  남는 칸은 0 입니다.
 *
 * 인자를 세 개만 받는 함수에 여덟 개를 넘겨도 되는가?  요즘 쓰는 곳에서는 됩니다.
 *   x86-64 (SysV·Win64) : 앞의 것들은 레지스터로 가고, 스택에 놓인 것은
 *                         **부르는 쪽이 치웁니다.** 남는 인자는 그냥 무시됩니다.
 *   arm64               : 여덟 개가 정확히 x0~x7 레지스터에 들어갑니다.
 *
 * 안 되는 곳: **32비트 윈도우의 stdcall**. 거기서는 불린 쪽이 스택을 치우기 때문에
 * 개수가 어긋나면 스택이 망가집니다. Lumi 는 64비트만 지원하므로 문제되지 않습니다.
 *
 * 가변 인자 함수(printf 같은 것)는 이 방식으로 부르면 안 됩니다 — 그런 함수는
 * 애초에 서명을 하나로 정할 수 없습니다. */
/* fn 을 괄호로 감싸야 합니다 — 안 그러면 형변환이 호출보다 늦게 붙습니다 */
#define CALL8(fn, a) (fn)(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7])

/* FfiRet 은 lumi.h 에 있습니다 (kind 는 서명의 돌려주는 글자 그대로).
 * 실패하면 false 를 주고 why 에 까닭을 적습니다 (오류는 부르는 쪽에서 냅니다). */
/* 마지막 인자가 실수인 경우.  참을 주면 out 을 채운 것입니다. */
static bool call_trailing_double(void *f, char ret, const long long *a, size_t n,
                                 double last, FfiRet *out, char *why, size_t whyn)
{
    if (n < 1 || n > 6) {
        snprintf(why, whyn, "a float can only be the last of 1..6 arguments (got %zu)", n);
        return false;
    }
    out->kind = ret;
    if (ret == 'd') {
        switch (n) {
        case 1: out->d = ((D1D)(uintptr_t)f)(last); break;
        case 2: out->d = ((D2D)(uintptr_t)f)(a[0], last); break;
        case 3: out->d = ((D3D)(uintptr_t)f)(a[0], a[1], last); break;
        case 4: out->d = ((D4D)(uintptr_t)f)(a[0], a[1], a[2], last); break;
        case 5: out->d = ((D5D)(uintptr_t)f)(a[0], a[1], a[2], a[3], last); break;
        default: out->d = ((D6D)(uintptr_t)f)(a[0], a[1], a[2], a[3], a[4], last); break;
        }
        return true;
    }
    switch (n) {
    case 1: out->i = ((L1D)(uintptr_t)f)(last); break;
    case 2: out->i = ((L2D)(uintptr_t)f)(a[0], last); break;
    case 3: out->i = ((L3D)(uintptr_t)f)(a[0], a[1], last); break;
    case 4: out->i = ((L4D)(uintptr_t)f)(a[0], a[1], a[2], last); break;
    case 5: out->i = ((L5D)(uintptr_t)f)(a[0], a[1], a[2], a[3], last); break;
    default: out->i = ((L6D)(uintptr_t)f)(a[0], a[1], a[2], a[3], a[4], last); break;
    }
    if (ret == 'i') out->i = (long long)(int32_t)out->i;
    return true;
}

bool ffi_call(long long id, const char *symbol, const char *sig,
              const long long *args, size_t nargs, FfiRet *out,
              char *why, size_t whyn)
{
    Lib *l = lib_at(id);
    if (!l) { snprintf(why, whyn, "this library handle is not open"); return false; }

    void *f = plat_dlsym(l->h, symbol);
    if (!f) {
        snprintf(why, whyn, "'%s' has no function called '%s'", l->name, symbol);
        return false;
    }

    const char *arrow = strchr(sig, '>');
    if (!arrow || arrow[1] == 0 || arrow[2] != 0) {
        snprintf(why, whyn, "the signature must look like \"sp>i\" - "
                            "argument letters, then '>', then one letter for what "
                            "comes back (got \"%s\")", sig);
        return false;
    }
    size_t want = (size_t)(arrow - sig);
    if (want != nargs) {
        snprintf(why, whyn, "the signature \"%s\" says %zu argument(s) but %zu were given",
                 sig, want, nargs);
        return false;
    }
    if (nargs > 8) { snprintf(why, whyn, "at most 8 arguments (got %zu)", nargs); return false; }

    long long a[8] = {0};
    for (size_t i = 0; i < nargs; i++) a[i] = args[i];

    char ret = arrow[1];

    /* 마지막 인자가 실수면 따로 갑니다 (부르는 쪽이 그 값을 last 로 건네줍니다) */
    if (nargs > 0 && sig[nargs - 1] == 'd') {
        double last;
        memcpy(&last, &a[nargs - 1], sizeof last);   /* 비트를 그대로 옮겨 담아 왔습니다 */
        if (ret != 'd' && ret != 'v' && ret != 'i' && ret != 'l'
            && ret != 'p' && ret != 's') {
            snprintf(why, whyn, "'%c' is not a return kind I know "
                                "(use i, l, p, s, d, or v)", ret);
            return false;
        }
        return call_trailing_double(f, ret, a, nargs, last, out, why, whyn);
    }

    switch (ret) {
    case 'd':
        out->kind = 'd';
        out->d = CALL8((FnD)(uintptr_t)f, a);
        return true;
    case 'v': case 'i': case 'l': case 'p': case 's':
        out->kind = ret;
        out->i = CALL8((FnI)(uintptr_t)f, a);
        if (ret == 'i') out->i = (long long)(int32_t)out->i;   /* 32비트로 잘라 부호를 살립니다 */
        return true;
    default:
        snprintf(why, whyn, "'%c' is not a return kind I know "
                            "(use i, l, p, s, d, or v)", ret);
        return false;
    }
}
