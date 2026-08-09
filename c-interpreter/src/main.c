/* lumi.exe — 터미널에서 Lumi 파일을 실행하고, IDE 를 위한 대화형(REPL) 모드도 제공합니다.
 *
 *   lumi file.lumi          파일 실행
 *   lumi --ide file.lumi    파일 실행 + IDE 용 표시(마커) 붙이기
 *   lumi --repl [폴더]      대화형 셸 (IDE 의 REPL 패널이 씁니다)
 *
 * IDE 와 주고받는 표시 (한 줄을 통째로 차지합니다):
 *   \x1eINPUT   input() 이 한 줄을 기다립니다 -> IDE 가 한 줄을 보내 줍니다
 *   \x1eDONE    이번 실행이 끝났습니다
 *   \x1eEOT     (IDE -> lumi) 여기까지가 실행할 코드입니다
 */
#include "lumi.h"
#include "platform.h"
#include "pkg.h"


#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

char *read_text_file(const char *path);

static bool emit_markers = false;

/* 프로그램에 넘어온 인자 — Lumi 쪽에서 sys.args 로 읽습니다.
 * lumi app.lumi --port 8080  ->  ["app.lumi", "--port", "8080"] */
static char **g_args  = NULL;
static size_t g_nargs = 0;

static void out_write(const char *text)
{
    fwrite(text, 1, strlen(text), stdout);
    fflush(stdout);
}

/* stdin 에서 한 줄 (UTF-8).  끝이면 NULL. */
static char *read_line(void)
{
    size_t cap = 256, len = 0;
    char *buf = (char *)xmalloc(cap);
    int c;
    while ((c = fgetc(stdin)) != EOF) {
        if (c == '\n') break;
        if (len + 1 >= cap) { cap *= 2; buf = (char *)xrealloc(buf, cap); }
        buf[len++] = (char)c;
    }
    if (c == EOF && len == 0) { free(buf); return NULL; }
    if (len > 0 && buf[len - 1] == '\r') len--;
    buf[len] = 0;
    return buf;
}

static char *ask_input(const char *prompt)
{
    if (prompt && *prompt) out_write(prompt);
    if (emit_markers) out_write("\x1e" "INPUT\n");
    return read_line();
}

#if defined(_WIN32)
static char *utf8_of(const wchar_t *w)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    char *s = (char *)xmalloc((size_t)n + 1);
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, NULL, NULL);
    return s;
}
#endif

static char *dir_of(const char *path)
{
    char *dir = xstrdup(path);
    char *a = strrchr(dir, '\\');
    char *b = strrchr(dir, '/');
    char *slash = a > b ? a : b;
    if (slash) *slash = 0;
    else { free(dir); dir = xstrdup("."); }
    return dir;
}

static const char *filename_of(const char *path)
{
    if (!path) return "";
    const char *a = strrchr(path, '\\');
    const char *b = strrchr(path, '/');
    const char *slash = a > b ? a : b;
    return slash ? slash + 1 : path;
}

/* ---------------- 대화형 셸 (REPL) ---------------- */

static int run_repl(const char *base_dir)
{
    Interp *interp = interp_new(out_write, ask_input, base_dir);
    for (;;) {
        /* \x1eEOT 줄이 나올 때까지 모읍니다 */
        size_t cap = 1024, len = 0;
        char *code = (char *)xmalloc(cap);
        code[0] = 0;
        bool eof = false;
        for (;;) {
            char *line = read_line();
            if (!line) { eof = true; break; }
            if (strcmp(line, "\x1e" "EOT") == 0) { free(line); break; }
            size_t n = strlen(line);
            if (len + n + 2 > cap) {
                while (len + n + 2 > cap) cap *= 2;
                code = (char *)xrealloc(code, cap);
            }
            memcpy(code + len, line, n);
            len += n;
            code[len++] = '\n';
            code[len] = 0;
            free(line);
        }
        if (eof && len == 0) { free(code); break; }

        if (setjmp(lumi_jmp) == 0) {
            interp_run(interp, code);
        } else {
            char *msg = xsprintf("Error: %s\n", lumi_err_msg);
            out_write(msg);
            free(msg);
            if (lumi_err_trace[0]) out_write(lumi_err_trace);
        }
        free(code);
        out_write("\x1e" "DONE\n");
        if (eof) break;
    }
    return 0;
}

/* ---------------- 파일 실행 ---------------- */

static int run_file(const char *path, const char *base_dir)
{
    char *code = read_text_file(path);
    if (!code) {
        char *msg = xsprintf("File not found: %s\n", path);
        out_write(msg);
        free(msg);
        return 1;
    }
    /* base_dir 은 bring 이 라이브러리를 찾는 기준입니다.  IDE 는 저장하지 않은
     * 편집 내용을 임시 파일로 넘기므로, 진짜 파일이 있는 폴더를 따로 알려 줍니다. */
    char *base = base_dir ? xstrdup(base_dir) : dir_of(path);
    Interp *interp = interp_new(out_write, ask_input, base);
    /* 트레이스에 찍을 이름.  IDE 는 저장 안 한 내용을 임시 파일로 넘기므로,
     * 그럴 때는 base_dir 옆의 진짜 이름 대신 넘어온 경로를 그대로 씁니다. */
    interp_set_script(interp, path);
    interp_set_args(interp, g_args, g_nargs);

    int status = 0;
    if (setjmp(lumi_jmp) == 0) {
        interp_run(interp, code);
    } else {
        char *msg = xsprintf("\nError: %s\n", lumi_err_msg);
        out_write(msg);
        free(msg);
        if (lumi_err_trace[0]) out_write(lumi_err_trace);
        status = 1;
    }
    free(base);
    free(code);
    return status;
}

/* ---------------- lumi test ---------------- */

/* .lumi 파일 하나를 '시험 켠 상태'로 돌립니다.  결과를 합계에 더합니다. */
static void run_test_file(const char *path, size_t *total, size_t *failed)
{
    char *code = read_text_file(path);
    if (!code) return;

    char *base = dir_of(path);
    Interp *interp = interp_new(out_write, ask_input, base);
    interp_set_script(interp, path);
    interp_set_args(interp, g_args, g_nargs);
    interp->testing = true;

    char *head = xsprintf("%s\n", path);
    out_write(head);
    free(head);

    if (setjmp(lumi_jmp) == 0) {
        interp_run(interp, code);
    } else {
        /* 시험 밖에서 난 오류 — 그 파일은 통째로 실패로 셉니다 */
        char *msg = xsprintf("  FAIL (could not finish the file)\n         %s\n", lumi_err_msg);
        out_write(msg);
        free(msg);
        if (lumi_err_trace[0]) out_write(lumi_err_trace);
        interp->tests_run++;
        interp->tests_failed++;
    }
    *total  += interp->tests_run;
    *failed += interp->tests_failed;
    if (interp->tests_run == 0) out_write("  (no tests in this file)\n");

    free(base);
    free(code);
}

/* 폴더 안의 .lumi 를 모두 (하위 폴더까지) 시험합니다 */
static void run_test_dir(const char *dir, size_t *total, size_t *failed)
{
    char **names = plat_subdirs(dir);
    char **files = plat_listdir(dir);
    if (files) {
        for (size_t i = 0; files[i]; i++) {
            size_t n = strlen(files[i]);
            if (n < 5 || strcmp(files[i] + n - 5, ".lumi") != 0) continue;
            char *full = xsprintf("%s%s%s", dir, plat_dir_sep(), files[i]);
            run_test_file(full, total, failed);
            free(full);
        }
        plat_free_names(files);
    }
    if (names) {
        for (size_t i = 0; names[i]; i++) {
            char *full = xsprintf("%s%s%s", dir, plat_dir_sep(), names[i]);
            run_test_dir(full, total, failed);
            free(full);
        }
        plat_free_names(names);
    }
}

static int run_tests(const char *where)
{
    if (!where || !*where) where = ".";
    size_t total = 0, failed = 0;

    if (plat_dir_exists(where))      run_test_dir(where, &total, &failed);
    else if (plat_file_exists(where)) run_test_file(where, &total, &failed);
    else {
        char *msg = xsprintf("Nothing to test at '%s'\n", where);
        out_write(msg);
        free(msg);
        return 1;
    }

    char *sum = xsprintf("\n----------------------------------------\n"
                         "  %zu test(s), %zu passed, %zu failed\n"
                         "----------------------------------------\n",
                         total, total - failed, failed);
    out_write(sum);
    free(sum);
    return failed ? 1 : 0;
}

static int run_terminal_repl(void)
{
    out_write("Lumi " LUMI_VERSION " (C Tree-Walk Engine)\n");
    out_write("Type 'run <filename.lumi>' to execute a file.\n");
    out_write("Type code line by line, or 'exit' to quit.\n");
    out_write("--------------------------------------------------\n\n");
    Interp *interp = interp_new(out_write, ask_input, ".");

    char *cwd = plat_cwd();
    if (cwd) {
        free(interp->base_dir);
        interp->base_dir = cwd;
    }

    for (;;) {
        out_write(">>> ");
        char *line = read_line();
        if (!line) break;

        char *trimmed = line;
        while (*trimmed == ' ' || *trimmed == '\t') trimmed++;

        if (strncmp(trimmed, "run", 3) == 0 || strncmp(trimmed, "->run", 5) == 0) {
            char *target = NULL;
            if (strncmp(trimmed, "->run", 5) == 0) target = trimmed + 5;
            else target = trimmed + 3;

            while (target && (*target == ' ' || *target == '\t')) target++;

            if (!target || strlen(target) == 0) {
                out_write("Usage: run <file.lumi>\n");
                free(line);
                continue;
            }

            out_write(">run ");
            out_write(filename_of(target));
            out_write("\n\n");

            run_file(target, NULL);

            free(line);
            continue;
        }

        if (strcmp(line, "exit()") == 0 || strcmp(line, "quit()") == 0 ||
            strcmp(line, "exit") == 0 || strcmp(line, "quit") == 0) {
            free(line);
            break;
        }
        if (strlen(line) > 0) {
            if (setjmp(lumi_jmp) == 0) {
                interp_run(interp, line);
                out_write("\n");
            } else {
                char *msg = xsprintf("Error: %s\n", lumi_err_msg);
                out_write(msg);
                free(msg);
                if (lumi_err_trace[0]) out_write(lumi_err_trace);
                out_write("\n");
            }
        }
        free(line);
    }
    return 0;
}

static int run_string(const char *code)
{
    Interp *interp = interp_new(out_write, ask_input, ".");
    interp_set_script(interp, "<command line>");
    interp_set_args(interp, g_args, g_nargs);
    int status = 0;
    if (setjmp(lumi_jmp) == 0) {
        interp_run(interp, code);
    } else {
        char *msg = xsprintf("\nError: %s\n", lumi_err_msg);
        out_write(msg);
        free(msg);
        if (lumi_err_trace[0]) out_write(lumi_err_trace);
        status = 1;
    }
    return status;
}

/* 진짜 일은 전부 여기서 합니다.  argv 는 어느 운영체제에서나 UTF-8 입니다.
 * 윈도우는 아래 wmain 이 UTF-16 을 UTF-8 로 바꿔 넘겨 주고,
 * 리눅스/맥은 main 이 받은 것을 그대로 넘깁니다. */
static int lumi_main(int argc, char **argv)
{
    /* 글자는 언제나 UTF-8 로 주고받습니다 */
    plat_console_init();

    /* lumi 실행 파일을 뺀 나머지가 프로그램의 인자입니다 (python 의 sys.argv 와 같은 규칙).
     *   lumi app.lumi --port 8080  ->  args = ["app.lumi", "--port", "8080"] */
    g_args  = argv + 1;
    g_nargs = argc > 1 ? (size_t)(argc - 1) : 0;

    /* lumi build 로 묶어 놓은 프로그램인지 내 뒤를 들여다봅니다.
     * 묶여 있으면 인자를 보지 않고 그 프로그램을 돌립니다 — 그래야 배포한
     * 실행 파일이 자기 일만 합니다.  인자는 전부 그 프로그램에게 넘어갑니다. */
    pack_open(argv[0]);
    if (pack_has_program()) {
        g_args  = argv;                       /* args(0) 은 실행 파일 이름 */
        g_nargs = (size_t)argc;
        return run_file(pack_main_name(), ".");
    }

    if (argc >= 2 && strncmp(argv[1], "->run", 5) == 0) {
        char *target = argv[1] + 5;
        while (*target == ' ' || *target == '\t') target++;
        if (strlen(target) == 0 && argc >= 3) target = argv[2];

        if (strlen(target) == 0) {
            out_write("Usage: lumi ->run <file.lumi>\n");
            return 1;
        }
        out_write("... ");
        char *confirm = read_line();
        if (confirm) free(confirm);
        out_write(">run ");
        out_write(filename_of(target));
        out_write("\n\n");
        return run_file(target, NULL);
    }

    if (argc >= 2 && (strcmp(argv[1], "pkg") == 0 || strcmp(argv[1], "pm") == 0 || strcmp(argv[1], "--pkg") == 0 || strcmp(argv[1], "package") == 0)) {
        return run_pkg_manager(argc - 2, argv + 2);
    }
    if (argc >= 2 && strcmp(argv[1], "lsp") == 0) {
        return run_lsp();
    }
    if (argc >= 2 && strcmp(argv[1], "dap") == 0) {
        return run_dap();
    }
    if (argc >= 2 && strcmp(argv[1], "build") == 0) {
        if (argc < 3) {
            out_write("Usage: lumi build <file.lumi> [-o <output>]\n");
            return 1;
        }
        const char *out_opt = NULL;
        for (int i = 3; i + 1 < argc; i++)
            if (strcmp(argv[i], "-o") == 0) out_opt = argv[i + 1];
        return run_build(argv[2], out_opt, argv[0]);
    }
    if (argc >= 2 && strcmp(argv[1], "test") == 0) {
        return run_tests(argc >= 3 ? argv[2] : ".");
    }
    if (argc >= 2 && strcmp(argv[1], "lint") == 0) {
        return run_lint(argc >= 3 ? argv[2] : ".");
    }
    if (argc >= 2 && strcmp(argv[1], "fmt") == 0) {
        bool check_only = false;
        const char *where = ".";
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--check") == 0) check_only = true;
            else where = argv[i];
        }
        return run_fmt(where, check_only);
    }
    if (argc >= 2 && strcmp(argv[1], "--repl") == 0) {
        emit_markers = true;
        return run_repl(argc >= 3 ? argv[2] : NULL);
    }
    if (argc >= 3 && strcmp(argv[1], "--ide") == 0) {
        emit_markers = true;
        int status = run_file(argv[2], argc >= 4 ? argv[3] : NULL);
        out_write("\x1e" "DONE\n");
        return status;
    }
    if (argc >= 3 && (strcmp(argv[1], "-c") == 0 || strcmp(argv[1], "-e") == 0)) {
        return run_string(argv[2]);
    }
    if (argc >= 2 && (strcmp(argv[1], "-v") == 0 || strcmp(argv[1], "--version") == 0)) {
        out_write("Lumi " LUMI_VERSION " (C Tree-Walk Engine)\n");
        return 0;
    }
    if (argc >= 2 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        out_write("Lumi Programming Language Terminal CLI\n"
                  "Usage: lumi                            Start Lumi shell (type ->run <file.lumi>)\n"
                  "       lumi <file.lumi>               Run a Lumi script file\n"
                  "       lumi ->run <file.lumi>         Run file after double Enter\n"
                  "       lumi -c \"<code>\"             Run Lumi code directly from terminal\n"
                  "       lumi test [file|folder]        Run the test blocks (default: this folder)\n"
                  "       lumi lint [file|folder]        Look for likely mistakes without running\n"
                  "       lumi fmt [file|folder]         Tidy spacing and indentation (--check to only report)\n"
                  "       lumi lsp                       Start the language server (for editors)\n"
                  "       lumi dap                       Start the debugger (breakpoints, stepping)\n"
                  "       lumi build <file.lumi> [-o out] Make a stand-alone program\n"
                  "       lumi pkg <command>             Run Lumi package manager\n"
                  "       lumi --version                 Show Lumi version\n");
        return 0;
    }
    if (argc < 2) {
        return run_terminal_repl();
    }
    return run_file(argv[1], NULL);
}

#if defined(_WIN32)
/* 윈도우에서는 wmain 으로 받아야 한글 파일 이름이 깨지지 않습니다 (UTF-16 -> UTF-8). */
int wmain(int argc, wchar_t **wargv)
{
    char **argv = (char **)xmalloc((size_t)argc * sizeof(char *));
    for (int i = 0; i < argc; i++) argv[i] = utf8_of(wargv[i]);
    return lumi_main(argc, argv);
}
#else
/* 리눅스/맥은 argv 가 이미 UTF-8 바이트열입니다. */
int main(int argc, char **argv)
{
    return lumi_main(argc, argv);
}
#endif
