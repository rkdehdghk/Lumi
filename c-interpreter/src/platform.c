/* platform.c — 운영체제마다 다른 것들의 실제 구현.
 * ==========================================================
 * 위쪽 절반은 윈도우(넓은 문자 API), 아래쪽 절반은 리눅스/맥(POSIX)입니다.
 * 어느 쪽이든 밖에서 보이는 모습(platform.h)은 똑같습니다.
 */
/* -std=c17 처럼 엄격한 ISO C 로 컴파일하면 realpath·readlink 같은
 * POSIX 함수의 선언이 헤더에서 숨겨집니다.  아무 헤더보다 먼저 이걸 켜서
 * 어떤 빌드 옵션에서도 선언이 보이게 합니다. */
#if !defined(_WIN32)
#  ifndef _POSIX_C_SOURCE
#    define _POSIX_C_SOURCE 200809L
#  endif
#  ifndef _DEFAULT_SOURCE
#    define _DEFAULT_SOURCE 1
#  endif
#  ifndef _DARWIN_C_SOURCE
#    define _DARWIN_C_SOURCE 1
#  endif
#endif

#include "platform.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

/* value.c 의 작은 도우미들.  lumi.h 를 통째로 끌어오지 않으려고 여기서만 알려 둡니다.
 * (선언을 빠뜨리면 64비트에서 포인터가 잘려 조용히 깨집니다 - build 에서 C4013 을 오류로 잡습니다) */
void *xmalloc(size_t n);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);
char *xsprintf(const char *fmt, ...);

/* ============================================================
 * 윈도우
 * ============================================================ */
#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <direct.h>
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <stdlib.h>

/* UTF-8 -> UTF-16.  malloc 합니다. */
static wchar_t *to_wide(const char *s)
{
    if (!s) return NULL;
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (n <= 0) return NULL;
    wchar_t *w = (wchar_t *)xmalloc((size_t)n * sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    return w;
}

/* UTF-16 -> UTF-8.  malloc 합니다. */
static char *to_utf8(const wchar_t *w)
{
    if (!w) return NULL;
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    if (n <= 0) return NULL;
    char *s = (char *)xmalloc((size_t)n);
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, NULL, NULL);
    return s;
}

const char *plat_dir_sep(void) { return "\\"; }
bool plat_is_sep(char c)       { return c == '\\' || c == '/'; }

FILE *plat_fopen(const char *path, const char *mode)
{
    wchar_t *w = to_wide(path);
    if (!w) return NULL;
    wchar_t wmode[8];
    size_t i = 0;
    for (; mode[i] && i < 7; i++) wmode[i] = (wchar_t)mode[i];
    wmode[i] = 0;
    FILE *f = _wfopen(w, wmode);
    free(w);
    return f;
}

static DWORD attrs_of(const char *path)
{
    wchar_t *w = to_wide(path);
    if (!w) return INVALID_FILE_ATTRIBUTES;
    DWORD a = GetFileAttributesW(w);
    free(w);
    return a;
}

bool plat_file_exists(const char *path)
{
    DWORD a = attrs_of(path);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

bool plat_dir_exists(const char *path)
{
    DWORD a = attrs_of(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

bool plat_remove_file(const char *path)
{
    wchar_t *w = to_wide(path);
    if (!w) return false;
    bool ok = _wremove(w) == 0;
    free(w);
    return ok;
}

bool plat_move(const char *from, const char *to)
{
    wchar_t *a = to_wide(from), *b = to_wide(to);
    bool ok = a && b && MoveFileExW(a, b, MOVEFILE_COPY_ALLOWED);
    free(a); free(b);
    return ok;
}

bool plat_mkdir(const char *path)
{
    wchar_t *w = to_wide(path);
    if (!w) return false;
    bool ok = CreateDirectoryW(w, NULL) != 0;
    free(w);
    return ok;
}

static bool rmdir_wide(const wchar_t *wpath)
{
    if (RemoveDirectoryW(wpath)) return true;

    size_t n = wcslen(wpath) + 8;
    wchar_t *search = (wchar_t *)xmalloc(n * sizeof(wchar_t));
    swprintf(search, n, L"%s\\*", wpath);

    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(search, &fd);
    free(search);
    if (h == INVALID_HANDLE_VALUE) return false;

    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
            continue;
        size_t cn = wcslen(wpath) + wcslen(fd.cFileName) + 4;
        wchar_t *child = (wchar_t *)xmalloc(cn * sizeof(wchar_t));
        swprintf(child, cn, L"%s\\%s", wpath, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            rmdir_wide(child);
        } else {
            SetFileAttributesW(child, FILE_ATTRIBUTE_NORMAL);
            DeleteFileW(child);
        }
        free(child);
    } while (FindNextFileW(h, &fd));

    FindClose(h);
    return RemoveDirectoryW(wpath) != 0;
}

bool plat_rmdir_recursive(const char *path)
{
    wchar_t *w = to_wide(path);
    if (!w) return false;
    bool ok = rmdir_wide(w);
    free(w);
    return ok;
}

/* 폴더 안을 훑어 이름을 모읍니다.  want_dirs 면 폴더만, 아니면 파일만. */
static char **list_children(const char *path, bool want_dirs)
{
    size_t sn = strlen(path) + 8;
    char *pattern = (char *)xmalloc(sn);
    snprintf(pattern, sn, "%s\\*", path);
    wchar_t *w = to_wide(pattern);
    free(pattern);
    if (!w) return NULL;

    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(w, &fd);
    free(w);
    if (h == INVALID_HANDLE_VALUE) return NULL;

    size_t n = 0, cap = 8;
    char **out = (char **)xmalloc(cap * sizeof(char *));
    do {
        bool isdir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (isdir != want_dirs) continue;
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        char *name = to_utf8(fd.cFileName);
        if (!name) continue;
        if (n + 1 >= cap) { cap *= 2; out = (char **)xrealloc(out, cap * sizeof(char *)); }
        out[n++] = name;
    } while (FindNextFileW(h, &fd));
    FindClose(h);

    out[n] = NULL;
    return out;
}

char **plat_subdirs(const char *path)  { return list_children(path, true); }
char **plat_listdir(const char *path)  { return list_children(path, false); }

void plat_free_names(char **names)
{
    if (!names) return;
    for (size_t i = 0; names[i]; i++) free(names[i]);
    free(names);
}

char *plat_fullpath(const char *path)
{
    wchar_t *w = to_wide(path);
    if (!w) return xstrdup(path);
    wchar_t wfull[LUMI_PATH_MAX * 2];
    if (!_wfullpath(wfull, w, LUMI_PATH_MAX * 2)) { free(w); return xstrdup(path); }
    free(w);
    char *out = to_utf8(wfull);
    return out ? out : xstrdup(path);
}

bool plat_path_equal(const char *a, const char *b)
{
    return _stricmp(a, b) == 0;      /* 윈도우 파일 이름은 대소문자를 가리지 않습니다 */
}

char *plat_exe_dir(void)
{
    wchar_t w[LUMI_PATH_MAX * 2];
    DWORD n = GetModuleFileNameW(NULL, w, LUMI_PATH_MAX * 2);
    if (n == 0) return NULL;
    char *full = to_utf8(w);
    if (!full) return NULL;
    char *a = strrchr(full, '\\');
    char *b = strrchr(full, '/');
    char *slash = a > b ? a : b;
    if (slash) *slash = 0;
    return full;
}

char *plat_cwd(void)
{
    wchar_t w[LUMI_PATH_MAX * 2];
    if (!_wgetcwd(w, LUMI_PATH_MAX * 2)) return NULL;
    return to_utf8(w);
}

/* 셸에 넘겨 돌리고 찍힌 것을 모읍니다.  윈도우는 콘솔 코드페이지를 UTF-8 로
 * 맞춰 둔 뒤 부르므로, 받아 오는 글자도 UTF-8 로 봅니다. */
char *plat_run(const char *command, int *status)
{
    /* 2>&1 로 오류 쪽도 같이 받습니다.  괄호로 감싸는 까닭: cmd 는
     * `echo hi 2>&1` 에서 리다이렉트 앞의 공백까지 echo 의 인자로 봐서
     * 끝에 공백이 딸려 나옵니다.  (...) 로 묶으면 POSIX 와 결과가 같아집니다. */
    char *full = xsprintf("(%s) 2>&1", command);
    wchar_t *w = to_wide(full);
    free(full);
    if (!w) return NULL;
    FILE *fp = _wpopen(w, L"rb");
    free(w);
    if (!fp) return NULL;

    size_t cap = 4096, len = 0;
    char *out = (char *)xmalloc(cap);
    size_t n;
    while ((n = fread(out + len, 1, cap - len - 1, fp)) > 0) {
        len += n;
        if (len + 1 >= cap) { cap *= 2; out = (char *)xrealloc(out, cap); }
    }
    out[len] = 0;
    int rc = _pclose(fp);
    if (status) *status = rc;
    return out;
}

/* --- 그물 (winsock) --- */

static bool g_net_started = false;

static bool net_start(void)
{
    if (g_net_started) return true;
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
    g_net_started = true;
    return true;
}

int plat_listen(int port)
{
    if (!net_start()) return -1;
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return -1;
    /* 껐다 바로 켤 때 "이미 쓰는 중" 이 나지 않도록 */
    BOOL yes = TRUE;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof yes);

    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons((unsigned short)port);
    if (bind(s, (struct sockaddr *)&a, sizeof a) != 0 || listen(s, 16) != 0) {
        closesocket(s);
        return -1;
    }
    return (int)s;
}

int plat_accept(int server)
{
    SOCKET c = accept((SOCKET)server, NULL, NULL);
    return c == INVALID_SOCKET ? -1 : (int)c;
}

long plat_recv(int sock, char *buf, size_t n)
{
    int got = recv((SOCKET)sock, buf, (int)n, 0);
    return (long)got;
}

bool plat_send(int sock, const char *buf, size_t n)
{
    size_t sent = 0;
    while (sent < n) {
        int k = send((SOCKET)sock, buf + sent, (int)(n - sent), 0);
        if (k <= 0) return false;
        sent += (size_t)k;
    }
    return true;
}

void plat_closesock(int sock) { closesocket((SOCKET)sock); }

/* --- 큰 자물쇠와 실 --- */

/* SRWLOCK 은 정적 초기값이 있어 따로 켤 것이 없습니다 */
static SRWLOCK g_gil = SRWLOCK_INIT;
void plat_gil_lock(void)   { AcquireSRWLockExclusive(&g_gil); }
void plat_gil_unlock(void) { ReleaseSRWLockExclusive(&g_gil); }

typedef struct { PlatThreadFn fn; void *arg; } ThreadStart;

static unsigned __stdcall thread_shim(void *p)
{
    ThreadStart *ts = (ThreadStart *)p;
    PlatThreadFn fn = ts->fn;
    void *arg = ts->arg;
    free(ts);
    fn(arg);
    return 0;
}

void *plat_thread_start(PlatThreadFn fn, void *arg)
{
    ThreadStart *ts = (ThreadStart *)xmalloc(sizeof *ts);
    ts->fn = fn; ts->arg = arg;
    /* 셋째 칸이 자리 크기입니다 - 본 실과 같은 8MB 로 잡습니다 */
    uintptr_t h = _beginthreadex(NULL, 8u * 1024u * 1024u, thread_shim, ts, 0, NULL);
    if (!h) { free(ts); return NULL; }
    return (void *)h;
}

void plat_thread_join(void *handle)
{
    if (!handle) return;
    /* 일감이 마지막 자기 참조를 놓아 여기까지 왔습니다. 자기 자신을 기다릴
     * 수는 없으므로 손잡이만 닫고 운영체제에 정리를 맡깁니다. */
    if (GetThreadId((HANDLE)handle) == GetCurrentThreadId()) {
        CloseHandle((HANDLE)handle);
        return;
    }
    WaitForSingleObject((HANDLE)handle, INFINITE);
    CloseHandle((HANDLE)handle);
}

void plat_console_init(void)
{
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    /* 내보내는 글자를 손대지 않도록 (\n 을 \r\n 으로 바꾸지 않도록) 합니다 */
    _setmode(_fileno(stdout), _O_BINARY);
    setvbuf(stdout, NULL, _IONBF, 0);
}

/* --- 코드페이지 --- */

static UINT codepage_for(const char *codec)
{
    if (strcmp(codec, "cp949") == 0)  return 949;
    if (strcmp(codec, "euc-kr") == 0) return 51949;
    return 0;
}

char *plat_encode_utf8(const char *utf8, size_t nbytes, const char *codec, size_t *out_len)
{
    UINT cp = codepage_for(codec);
    if (!cp) return NULL;

    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8, (int)nbytes, NULL, 0);
    if (wlen < 0) return NULL;
    wchar_t *w = (wchar_t *)xmalloc(((size_t)wlen + 1) * sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8, 0, utf8, (int)nbytes, w, wlen);

    int n = WideCharToMultiByte(cp, 0, w, wlen, NULL, 0, NULL, NULL);
    if (n < 0 || (wlen > 0 && n == 0)) { free(w); return NULL; }
    char *buf = (char *)xmalloc((size_t)n + 1);
    n = WideCharToMultiByte(cp, 0, w, wlen, buf, n, NULL, NULL);
    if (n < 0 || (wlen > 0 && n == 0)) { free(w); free(buf); return NULL; }

    /* 되읽어 원본과 같은지 봅니다 — 담지 못한 글자를 이렇게 잡아냅니다 */
    int back = MultiByteToWideChar(cp, 0, buf, n, NULL, 0);
    bool same = false;
    if (back == wlen) {
        wchar_t *w2 = (wchar_t *)xmalloc(((size_t)back + 1) * sizeof(wchar_t));
        MultiByteToWideChar(cp, 0, buf, n, w2, back);
        same = memcmp(w, w2, (size_t)back * sizeof(wchar_t)) == 0;
        free(w2);
    }
    free(w);
    if (!same) { free(buf); return NULL; }

    buf[n] = 0;
    *out_len = (size_t)n;
    return buf;
}

char *plat_decode_to_utf8(const char *bytes, size_t nbytes, const char *codec, size_t *out_len)
{
    UINT cp = codepage_for(codec);
    if (!cp) return NULL;
    if (nbytes == 0) { *out_len = 0; char *e = (char *)xmalloc(1); e[0] = 0; return e; }

    int wlen = MultiByteToWideChar(cp, MB_ERR_INVALID_CHARS, bytes, (int)nbytes, NULL, 0);
    if (wlen <= 0) return NULL;
    wchar_t *w = (wchar_t *)xmalloc(((size_t)wlen + 1) * sizeof(wchar_t));
    wlen = MultiByteToWideChar(cp, MB_ERR_INVALID_CHARS, bytes, (int)nbytes, w, wlen);
    if (wlen <= 0) { free(w); return NULL; }

    int n = WideCharToMultiByte(CP_UTF8, 0, w, wlen, NULL, 0, NULL, NULL);
    if (n < 0) { free(w); return NULL; }
    char *out = (char *)xmalloc((size_t)n + 1);
    WideCharToMultiByte(CP_UTF8, 0, w, wlen, out, n, NULL, NULL);
    free(w);
    out[n] = 0;
    *out_len = (size_t)n;
    return out;
}

/* ---------------- 남의 라이브러리 불러 쓰기 (윈도우) ---------------- */

const char *plat_dl_suffix(void) { return ".dll"; }

void *plat_dlopen(const char *name, char *why, size_t whyn)
{
    /* 맨이름이면 ".dll" 을 붙여 한 번 더 봅니다 ("sqlite3" -> "sqlite3.dll") */
    const char *tries[2];
    char *withsuf = xsprintf("%s.dll", name);
    tries[0] = name;
    tries[1] = withsuf;

    for (int i = 0; i < 2; i++) {
        wchar_t *w = to_wide(tries[i]);
        if (!w) continue;
        HMODULE h = LoadLibraryW(w);
        free(w);
        if (h) { free(withsuf); return (void *)h; }
    }
    free(withsuf);
    snprintf(why, whyn, "could not load '%s' (error %lu)", name, (unsigned long)GetLastError());
    return NULL;
}

void *plat_dlsym(void *handle, const char *symbol)
{
    return (void *)(uintptr_t)GetProcAddress((HMODULE)handle, symbol);
}

void plat_dlclose(void *handle) { if (handle) FreeLibrary((HMODULE)handle); }

/* ============================================================
 * 리눅스 / 맥 (POSIX)
 * ============================================================ */
#else

#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <iconv.h>
#include <limits.h>
#include <stdint.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <pthread.h>
#if defined(__APPLE__)
#  include <mach-o/dyld.h>          /* _NSGetExecutablePath */
#endif
#ifndef PATH_MAX
#  define PATH_MAX 4096
#endif

const char *plat_dir_sep(void) { return "/"; }
bool plat_is_sep(char c)       { return c == '/'; }

FILE *plat_fopen(const char *path, const char *mode)
{
    return fopen(path, mode);         /* POSIX 는 경로가 이미 UTF-8 바이트열입니다 */
}

bool plat_file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

bool plat_dir_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

bool plat_remove_file(const char *path)
{
    return unlink(path) == 0;
}

bool plat_move(const char *from, const char *to)
{
    if (rename(from, to) == 0) return true;
    if (errno != EXDEV) return false;

    /* 다른 파티션이면 rename 이 안 됩니다 — 옮겨 적고 원본을 지웁니다 */
    FILE *src = fopen(from, "rb");
    if (!src) return false;
    FILE *dst = fopen(to, "wb");
    if (!dst) { fclose(src); return false; }

    char buf[65536];
    size_t n;
    bool ok = true;
    while ((n = fread(buf, 1, sizeof buf, src)) > 0) {
        if (fwrite(buf, 1, n, dst) != n) { ok = false; break; }
    }
    if (ferror(src)) ok = false;
    fclose(src);
    if (fclose(dst) != 0) ok = false;
    if (!ok) { unlink(to); return false; }
    return unlink(from) == 0;
}

bool plat_mkdir(const char *path)
{
    return mkdir(path, 0777) == 0;
}

bool plat_rmdir_recursive(const char *path)
{
    if (rmdir(path) == 0) return true;

    DIR *d = opendir(path);
    if (!d) return false;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        size_t n = strlen(path) + strlen(e->d_name) + 2;
        char *child = (char *)xmalloc(n);
        snprintf(child, n, "%s/%s", path, e->d_name);
        struct stat st;
        if (lstat(child, &st) == 0 && S_ISDIR(st.st_mode)) plat_rmdir_recursive(child);
        else unlink(child);
        free(child);
    }
    closedir(d);
    return rmdir(path) == 0;
}

/* 폴더 안을 훑어 이름을 모읍니다.  want_dirs 면 폴더만, 아니면 파일만. */
static char **list_children(const char *path, bool want_dirs)
{
    DIR *d = opendir(path);
    if (!d) return NULL;

    size_t n = 0, cap = 8;
    char **out = (char **)xmalloc(cap * sizeof(char *));
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        size_t cn = strlen(path) + strlen(e->d_name) + 2;
        char *child = (char *)xmalloc(cn);
        snprintf(child, cn, "%s/%s", path, e->d_name);
        struct stat st;
        bool isdir = stat(child, &st) == 0 && S_ISDIR(st.st_mode);
        free(child);
        if (isdir != want_dirs) continue;
        if (n + 1 >= cap) { cap *= 2; out = (char **)xrealloc(out, cap * sizeof(char *)); }
        out[n++] = xstrdup(e->d_name);
    }
    closedir(d);

    out[n] = NULL;
    return out;
}

char **plat_subdirs(const char *path)  { return list_children(path, true); }
char **plat_listdir(const char *path)  { return list_children(path, false); }

void plat_free_names(char **names)
{
    if (!names) return;
    for (size_t i = 0; names[i]; i++) free(names[i]);
    free(names);
}

char *plat_fullpath(const char *path)
{
    char buf[PATH_MAX];
    if (realpath(path, buf)) return xstrdup(buf);
    /* 아직 없는 파일이면 realpath 가 실패합니다 — 지금 폴더를 앞에 붙여 둡니다 */
    if (path[0] == '/') return xstrdup(path);
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof cwd)) return xstrdup(path);
    size_t n = strlen(cwd) + strlen(path) + 2;
    char *out = (char *)xmalloc(n);
    snprintf(out, n, "%s/%s", cwd, path);
    return out;
}

bool plat_path_equal(const char *a, const char *b)
{
    return strcmp(a, b) == 0;         /* 리눅스 파일 이름은 대소문자를 가립니다 */
}

char *plat_exe_dir(void)
{
    char buf[PATH_MAX];
    ssize_t n = -1;
#if defined(__APPLE__)
    uint32_t sz = (uint32_t)sizeof buf;
    if (_NSGetExecutablePath(buf, &sz) == 0) n = (ssize_t)strlen(buf);
#else
    n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n > 0) buf[n] = 0;
#endif
    if (n <= 0) return NULL;
    char *slash = strrchr(buf, '/');
    if (slash) *slash = 0;
    return xstrdup(buf);
}

char *plat_cwd(void)
{
    char buf[PATH_MAX];
    if (!getcwd(buf, sizeof buf)) return NULL;
    return xstrdup(buf);
}

char *plat_run(const char *command, int *status)
{
    char *full = xsprintf("%s 2>&1", command);
    FILE *fp = popen(full, "r");
    free(full);
    if (!fp) return NULL;

    size_t cap = 4096, len = 0;
    char *out = (char *)xmalloc(cap);
    size_t n;
    while ((n = fread(out + len, 1, cap - len - 1, fp)) > 0) {
        len += n;
        if (len + 1 >= cap) { cap *= 2; out = (char *)xrealloc(out, cap); }
    }
    out[len] = 0;
    int rc = pclose(fp);
    /* POSIX 는 wait 결과라 실제 종료 코드는 위쪽 8비트에 있습니다 */
    if (status) *status = (rc > 0 && (rc & 0x7F) == 0) ? (rc >> 8) : rc;
    return out;
}

/* --- 그물 (BSD 소켓) --- */

int plat_listen(int port)
{
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return -1;
    int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);

    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons((unsigned short)port);
    if (bind(s, (struct sockaddr *)&a, sizeof a) != 0 || listen(s, 16) != 0) {
        close(s);
        return -1;
    }
    return s;
}

int plat_accept(int server)
{
    int c = accept(server, NULL, NULL);
    return c < 0 ? -1 : c;
}

long plat_recv(int sock, char *buf, size_t n)
{
    return (long)recv(sock, buf, n, 0);
}

bool plat_send(int sock, const char *buf, size_t n)
{
    size_t sent = 0;
    while (sent < n) {
        ssize_t k = send(sock, buf + sent, n - sent, 0);
        if (k <= 0) return false;
        sent += (size_t)k;
    }
    return true;
}

void plat_closesock(int sock) { close(sock); }

/* --- 큰 자물쇠와 실 --- */

static pthread_mutex_t g_gil = PTHREAD_MUTEX_INITIALIZER;
void plat_gil_lock(void)   { pthread_mutex_lock(&g_gil); }
void plat_gil_unlock(void) { pthread_mutex_unlock(&g_gil); }

void *plat_thread_start(PlatThreadFn fn, void *arg)
{
    pthread_t *t = (pthread_t *)xmalloc(sizeof *t);
    pthread_attr_t at;
    pthread_attr_init(&at);
    pthread_attr_setstacksize(&at, 8u * 1024u * 1024u);   /* 본 실과 같게 */
    int rc = pthread_create(t, &at, fn, arg);
    pthread_attr_destroy(&at);
    if (rc != 0) { free(t); return NULL; }
    return t;
}

void plat_thread_join(void *handle)
{
    if (!handle) return;
    if (pthread_equal(*(pthread_t *)handle, pthread_self())) {
        pthread_detach(*(pthread_t *)handle);
        free(handle);
        return;
    }
    pthread_join(*(pthread_t *)handle, NULL);
    free(handle);
}

void plat_console_init(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);   /* 터미널 코드페이지는 이미 UTF-8 입니다 */
}

/* --- 코드페이지 (iconv) --- */

static const char *iconv_name_for(const char *codec)
{
    if (strcmp(codec, "cp949") == 0)  return "CP949";
    if (strcmp(codec, "euc-kr") == 0) return "EUC-KR";
    return NULL;
}

/* iconv 로 통째로 옮겨 적습니다.  담을 수 없는 글자가 하나라도 있으면 NULL. */
static char *iconv_all(const char *from, const char *to,
                       const char *src, size_t srclen, size_t *out_len)
{
    iconv_t cd = iconv_open(to, from);
    if (cd == (iconv_t)-1) return NULL;

    size_t cap = srclen * 4 + 8;
    char *out = (char *)xmalloc(cap + 1);

    char *inbuf = (char *)src;
    size_t inleft = srclen;
    char *outbuf = out;
    size_t outleft = cap;

    while (inleft > 0) {
        /* iconv 의 둘째 인자는 리눅스에서 char**, 맥에서 const char** 이라 캐스팅합니다 */
        size_t r = iconv(cd, (void *)&inbuf, &inleft, &outbuf, &outleft);
        if (r == (size_t)-1) {
            if (errno == E2BIG) {
                size_t used = (size_t)(outbuf - out);
                cap *= 2;
                out = (char *)xrealloc(out, cap + 1);
                outbuf = out + used;
                outleft = cap - used;
                continue;
            }
            /* EILSEQ(담을 수 없는 글자) / EINVAL(중간에 끊긴 글자) — 둘 다 실패입니다 */
            free(out);
            iconv_close(cd);
            return NULL;
        }
    }
    iconv_close(cd);

    *out_len = (size_t)(outbuf - out);
    out[*out_len] = 0;
    return out;
}

/* ---------------- 남의 라이브러리 불러 쓰기 (POSIX) ---------------- */

const char *plat_dl_suffix(void)
{
#if defined(__APPLE__)
    return ".dylib";
#else
    return ".so";
#endif
}

void *plat_dlopen(const char *name, char *why, size_t whyn)
{
    /* 맨이름이면 관례대로 장식을 붙여 차례로 봅니다:
     *   sqlite3 -> sqlite3 / libsqlite3.so / libsqlite3.so.0 / libsqlite3.dylib */
    const char *suf = plat_dl_suffix();
    char *t1 = xsprintf("lib%s%s", name, suf);
    char *t2 = xsprintf("lib%s%s.0", name, suf);
    char *t3 = xsprintf("%s%s", name, suf);
    const char *tries[4] = { name, t1, t2, t3 };

    void *h = NULL;
    for (int i = 0; i < 4 && !h; i++) h = dlopen(tries[i], RTLD_LAZY);

    if (!h) {
        const char *e = dlerror();
        snprintf(why, whyn, "could not load '%s' (%s)", name, e ? e : "no reason given");
    }
    free(t1); free(t2); free(t3);
    return h;
}

void *plat_dlsym(void *handle, const char *symbol)
{
    dlerror();                       /* 묵은 오류를 치웁니다 */
    return dlsym(handle, symbol);
}

void plat_dlclose(void *handle) { if (handle) dlclose(handle); }

char *plat_encode_utf8(const char *utf8, size_t nbytes, const char *codec, size_t *out_len)
{
    const char *name = iconv_name_for(codec);
    if (!name) return NULL;
    return iconv_all("UTF-8", name, utf8, nbytes, out_len);
}

char *plat_decode_to_utf8(const char *bytes, size_t nbytes, const char *codec, size_t *out_len)
{
    const char *name = iconv_name_for(codec);
    if (!name) return NULL;
    if (nbytes == 0) { *out_len = 0; char *e = (char *)xmalloc(1); e[0] = 0; return e; }
    return iconv_all(name, "UTF-8", bytes, nbytes, out_len);
}

#endif
