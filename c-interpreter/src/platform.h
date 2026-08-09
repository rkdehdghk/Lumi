/* platform.h — 운영체제마다 다른 것들을 여기 한 곳에 모았습니다.
 * ==========================================================
 * 나머지 소스는 이 파일의 plat_* 만 부르고 windows.h 를 직접 쓰지 않습니다.
 *
 * 경로는 **어디서나 UTF-8** 입니다.  윈도우에서만 안에서 UTF-16 으로 바꿔
 * 넓은 문자(wide) API 를 부르고, 리눅스/맥에서는 UTF-8 그대로 씁니다.
 * 그래서 한글 파일 이름이 세 운영체제에서 똑같이 동작합니다.
 */
#ifndef LUMI_PLATFORM_H
#define LUMI_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

/* 경로 하나가 들어갈 넉넉한 크기.  윈도우의 MAX_PATH(260)보다 크게 잡습니다. */
#define LUMI_PATH_MAX 1024

/* 이 운영체제가 폴더를 나눌 때 쓰는 글자 ("\\" 또는 "/") */
const char *plat_dir_sep(void);
/* 경로를 나누는 글자입니까? (윈도우는 \ 와 / 둘 다) */
bool plat_is_sep(char c);

/* ---------------- 파일 ---------------- */

/* mode 는 "rb" / "wb" / "ab" / "r+b" 처럼 ASCII 로 적습니다. 실패하면 NULL. */
FILE *plat_fopen(const char *path, const char *mode);
/* 있고, 폴더가 아닐 때만 true */
bool  plat_file_exists(const char *path);
/* 있고, 폴더일 때만 true */
bool  plat_dir_exists(const char *path);
/* 파일 지우기.  성공하면 true */
bool  plat_remove_file(const char *path);
/* 옮기기 / 이름 바꾸기.  다른 드라이브·파티션으로도 옮겨집니다. */
bool  plat_move(const char *from, const char *to);

/* ---------------- 폴더 ---------------- */

/* 폴더 하나 만들기 (중간 폴더는 만들지 않습니다).  이미 있어도 false */
bool  plat_mkdir(const char *path);
/* 안에 든 것까지 몽땅 지웁니다.  성공하면 true */
bool  plat_rmdir_recursive(const char *path);
/* 폴더 안의 '바로 아래 폴더' 이름들 (. 과 .. 은 뺍니다).
 * 마지막 칸이 NULL 인 배열을 malloc 해서 돌려줍니다.  못 열면 NULL.
 * 다 쓴 뒤에는 plat_free_names 로 놓아 주세요. */
char **plat_subdirs(const char *path);
/* 폴더 안의 '바로 아래 파일' 이름들 (폴더는 뺍니다).  쓰는 법은 plat_subdirs 와 같습니다. */
char **plat_listdir(const char *path);
void   plat_free_names(char **names);

/* ---------------- 경로 ---------------- */

/* 절대 경로로 펴기.  malloc 한 UTF-8 을 돌려줍니다 (실패하면 원본 복사본). */
char *plat_fullpath(const char *path);
/* 경로 두 개가 같은 곳을 가리킵니까? (윈도우는 대소문자를 가리지 않습니다) */
bool  plat_path_equal(const char *a, const char *b);
/* lumi 실행 파일이 있는 폴더.  malloc.  못 찾으면 NULL. */
char *plat_exe_dir(void);
/* 지금 있는 폴더.  malloc.  못 찾으면 NULL. */
char *plat_cwd(void);

/* ---------------- 다른 프로그램 부르기 ---------------- */

/* 명령을 셸에 넘겨 돌리고, 그 프로그램이 찍은 것(표준출력+표준오류)을 모아 옵니다.
 * 돌려주는 글자는 malloc 한 UTF-8 (실패하면 NULL).
 * 그 프로그램이 남긴 종료 코드는 *status 에 담아 줍니다. */
char *plat_run(const char *command, int *status);

/* ---------------- 그물 (작은 웹 서버용) ----------------
 * 바깥 라이브러리 없이 운영체제가 주는 소켓만 씁니다.
 * 자리(socket)는 그냥 정수로 주고받고, 실패하면 -1 입니다. */

/* 포트를 열고 손님을 기다립니다.  못 열면 -1. */
int  plat_listen(int port);
/* 손님 하나를 받습니다.  실패하면 -1. */
int  plat_accept(int server);
/* 받아 오기.  0 이면 손님이 끊은 것, -1 이면 잘못된 것. */
long plat_recv(int sock, char *buf, size_t n);
/* 보내기.  다 보내면 true. */
bool plat_send(int sock, const char *buf, size_t n);
void plat_closesock(int sock);

/* ---------------- 따로 도는 일감 (start / wait) ----------------
 * 실 하나에 하나씩 태우되, **큰 자물쇠 하나**로 한 번에 한 실만 Lumi 코드를
 * 돌게 합니다 (파이썬의 GIL 과 같은 생각).  참조 세기와 순환 수집기가
 * 그래야 안전합니다.  기다리는 일(shell·sleep·wait)에서만 자물쇠를 놓습니다.  */

void  plat_gil_lock(void);
void  plat_gil_unlock(void);

typedef void *(*PlatThreadFn)(void *arg);
/* 실 하나를 띄웁니다.  손잡이를 돌려주고, 실패하면 NULL.
 * 되돌이가 깊어질 수 있으므로 자리(스택)를 8MB 로 잡습니다 (본 실과 같게). */
void *plat_thread_start(PlatThreadFn fn, void *arg);
/* 끝날 때까지 기다린 뒤 손잡이를 놓아 줍니다.  **자물쇠를 놓고 부르세요.** */
void  plat_thread_join(void *handle);

/* ---------------- 터미널 ---------------- */

/* 글자를 UTF-8 로 주고받도록 콘솔을 맞춥니다 (윈도우에서만 할 일이 있습니다) */
void plat_console_init(void);

/* ---------------- 코드페이지 (cp949, euc-kr, ...) ----------------
 * utf-8 / utf-16 / ascii / latin-1 은 unicode.c 가 직접 다루고,
 * 나머지(cp949, euc-kr)만 운영체제에 맡깁니다.
 * 둘 다 성공하면 malloc 한 버퍼와 길이를, 실패하면 NULL 을 돌려줍니다. */
char     *plat_encode_utf8(const char *utf8, size_t nbytes, const char *codec, size_t *out_len);
char     *plat_decode_to_utf8(const char *bytes, size_t nbytes, const char *codec, size_t *out_len);

/* ---------------- 남의 라이브러리 불러 쓰기 (.dll / .so / .dylib) ----------------
 * 윈도우는 LoadLibrary, 그 밖은 dlopen 입니다.  ffi.c 만 씁니다. */

/* 라이브러리를 엽니다.  못 열면 NULL 을 주고 why 에 까닭을 적습니다.
 * name 은 "sqlite3" 처럼 맨이름으로 주어도 되고 (앞뒤 장식은 알아서 붙입니다),
 * "C:\...\a.dll" 처럼 온 경로로 주어도 됩니다. */
void *plat_dlopen(const char *name, char *why, size_t whyn);
/* 그 안의 함수 자리를 찾습니다.  없으면 NULL. */
void *plat_dlsym(void *handle, const char *symbol);
void  plat_dlclose(void *handle);
/* 이 운영체제에서 라이브러리 파일에 붙는 꼬리 (".dll" / ".so" / ".dylib") */
const char *plat_dl_suffix(void);

#endif /* LUMI_PLATFORM_H */
