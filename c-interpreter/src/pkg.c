/* pkg.c — Lumi 외부 패키지 매니저 (lumipm) 구현 */
#include "pkg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "platform.h"

static bool path_exists(const char *path)
{
    return plat_file_exists(path) || plat_dir_exists(path);
}

static bool dir_exists(const char *path)
{
    return plat_dir_exists(path);
}

static bool make_dir_recursive(const char *path)
{
    if (dir_exists(path)) return true;
    char tmp[LUMI_PATH_MAX * 2];
    snprintf(tmp, sizeof(tmp), "%s", path);
    /* 위쪽 폴더부터 하나씩 만들어 내려갑니다 */
    for (char *p = tmp + 1; *p; p++) {
        if (plat_is_sep(*p)) {
            char c = *p;
            *p = 0;
            if (!dir_exists(tmp) && !plat_mkdir(tmp) && !dir_exists(tmp)) return false;
            *p = c;
        }
    }
    return plat_mkdir(tmp) || dir_exists(tmp);
}

static char *get_user_home(void)
{
    const char *env = getenv("USERPROFILE");
    if (!env) env = getenv("HOME");
    if (!env) env = ".";
    return xsprintf("%s", env);
}

static char *get_global_pkg_dir(void)
{
    char *home = get_user_home();
    char *dir = xsprintf("%s%s.lumi%spackages", home, plat_dir_sep(), plat_dir_sep());
    free(home);
    return dir;
}

static char *get_current_dir_name(void)
{
    char *cwd = plat_cwd();
    if (cwd) {
        /* 마지막 폴더 구분자를 찾습니다.  무엇이 구분자인지는 운영체제마다 다릅니다
         * (리눅스에서 \ 는 그냥 이름에 들어갈 수 있는 글자입니다). */
        char *p = NULL;
        for (char *q = cwd; *q; q++) if (plat_is_sep(*q)) p = q;
        if (p && *(p + 1)) { char *out = xsprintf("%s", p + 1); free(cwd); return out; }
        free(cwd);
    }
    return xsprintf("lumi-package");
}

char *get_package_entry(const char *parent_dir, const char *pkg_name)
{
    if (!parent_dir || !pkg_name) return NULL;

    char target_dir[LUMI_PATH_MAX * 2];
    snprintf(target_dir, sizeof(target_dir), "%s%s%s", parent_dir, plat_dir_sep(), pkg_name);

    if (!dir_exists(target_dir)) return NULL;

    /* 1. lumi.json 확인 및 "main" 필드 추출 */
    char json_path[LUMI_PATH_MAX * 2];
    snprintf(json_path, sizeof(json_path), "%s%slumi.json", target_dir, plat_dir_sep());

    FILE *f = plat_fopen(json_path, "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz > 0 && sz < 1048576) {
            char *buf = (char *)malloc((size_t)sz + 1);
            if (buf) {
                size_t read_bytes = fread(buf, 1, (size_t)sz, f);
                buf[read_bytes] = 0;
                char *main_prop = strstr(buf, "\"main\"");
                if (main_prop) {
                    char *colon = strchr(main_prop + 6, ':');
                    if (colon) {
                        char *q1 = strchr(colon, '"');
                        if (q1) {
                            q1++;
                            char *q2 = strchr(q1, '"');
                            if (q2 && q2 > q1) {
                                size_t mlen = q2 - q1;
                                char main_rel[LUMI_PATH_MAX];
                                if (mlen < sizeof(main_rel)) {
                                    memcpy(main_rel, q1, mlen);
                                    main_rel[mlen] = 0;
                                    fclose(f);
                                    free(buf);
                                    char *full_entry = xsprintf("%s%s%s", target_dir, plat_dir_sep(), main_rel);
                                    if (path_exists(full_entry)) return full_entry;
                                    free(full_entry);
                                    f = NULL;
                                }
                            }
                        }
                    }
                }
                free(buf);
            }
        }
        if (f) fclose(f);
    }

    /* 2. 기본 규약 (main.lumi -> index.lumi -> pkg_name.lumi) */
    char cand1[LUMI_PATH_MAX * 2], cand2[LUMI_PATH_MAX * 2], cand3[LUMI_PATH_MAX * 2];
    snprintf(cand1, sizeof(cand1), "%s%smain.lumi", target_dir, plat_dir_sep());
    if (path_exists(cand1)) return xsprintf("%s", cand1);

    snprintf(cand2, sizeof(cand2), "%s%sindex.lumi", target_dir, plat_dir_sep());
    if (path_exists(cand2)) return xsprintf("%s", cand2);

    snprintf(cand3, sizeof(cand3), "%s%s%s.lumi", target_dir, plat_dir_sep(), pkg_name);
    if (path_exists(cand3)) return xsprintf("%s", cand3);

    return NULL;
}

/* ============================================================
 * lumi.lock — 어제와 똑같은 코드를 다시 받기 위한 잠금 파일
 * ============================================================
 * git URL 만 적어 두면 `install` 을 돌릴 때마다 그 저장소의 '지금 맨 끝'이
 * 들어옵니다.  그러면 어제 되던 것이 오늘 안 될 수 있습니다.
 * 그래서 실제로 받아 온 **커밋 번호**를 적어 두고, 다음부터는 그 번호로
 * 되돌려 놓습니다.  모양은 이렇습니다:
 *
 *   {
 *     "packages": {
 *       "math_utils": { "source": "https://...", "commit": "a1b2c3..." }
 *     }
 *   }
 */

/* 그 폴더가 지금 어느 커밋인지.  못 알아내면 NULL. (malloc) */
static char *git_commit_of(const char *dir)
{
    char cmd[LUMI_PATH_MAX * 3];
    snprintf(cmd, sizeof(cmd), "git -C \"%s\" rev-parse HEAD", dir);
    int rc = -1;
    char *out = plat_run(cmd, &rc);
    if (!out) return NULL;
    if (rc != 0) { free(out); return NULL; }
    /* 앞의 40 글자만 씁니다 (뒤에 줄바꿈이 붙어 옵니다) */
    size_t n = 0;
    while (out[n] && out[n] != '\n' && out[n] != '\r') n++;
    if (n < 7) { free(out); return NULL; }
    out[n] = 0;
    return out;
}

/* 락파일에서 이름에 해당하는 칸을 찾아 source/commit 을 떼어 옵니다.
 * 아주 단순한 훑기입니다 - 이 파일은 우리가 적고 우리가 읽습니다. */
static bool lock_lookup(const char *text, const char *name,
                        char *src, size_t srcn, char *commit, size_t cn)
{
    char needle[LUMI_PATH_MAX];
    snprintf(needle, sizeof(needle), "\"%s\"", name);
    const char *at = strstr(text, needle);
    if (!at) return false;
    const char *s1 = strstr(at, "\"source\"");
    const char *c1 = strstr(at, "\"commit\"");
    if (!s1 || !c1) return false;

    const char *q = strchr(s1 + 8, '"');
    if (!q) return false;
    const char *e = strchr(q + 1, '"');
    if (!e || (size_t)(e - q - 1) >= srcn) return false;
    memcpy(src, q + 1, (size_t)(e - q - 1)); src[e - q - 1] = 0;

    q = strchr(c1 + 8, '"');
    if (!q) return false;
    e = strchr(q + 1, '"');
    if (!e || (size_t)(e - q - 1) >= cn) return false;
    memcpy(commit, q + 1, (size_t)(e - q - 1)); commit[e - q - 1] = 0;
    return true;
}

static char *read_whole_file(const char *path)
{
    FILE *f = plat_fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0 || sz > 4 * 1024 * 1024) { fclose(f); return NULL; }
    char *buf = (char *)xmalloc((size_t)sz + 1);
    size_t got = fread(buf, 1, (size_t)sz, f);
    buf[got] = 0;
    fclose(f);
    return buf;
}

/* 지금 설치되어 있는 것들을 훑어 lumi.lock 을 새로 적습니다 */
static int write_lock(const char *pkg_dir)
{
    char **names = plat_subdirs(pkg_dir);
    if (!names) {
        printf("[lumipm] 설치된 패키지가 없어 lumi.lock 을 적지 않았습니다.\n");
        return 0;
    }
    char *old = read_whole_file("lumi.lock");     /* source 는 예전 것을 물려받습니다 */

    FILE *f = plat_fopen("lumi.lock", "wb");
    if (!f) {
        plat_free_names(names);
        free(old);
        printf("[lumipm] lumi.lock 을 적을 수 없습니다.\n");
        return 1;
    }
    fprintf(f, "{\n  \"packages\": {\n");
    int n = 0;
    for (size_t i = 0; names[i]; i++) {
        char dir[LUMI_PATH_MAX * 2];
        snprintf(dir, sizeof(dir), "%s%s%s", pkg_dir, plat_dir_sep(), names[i]);
        char *commit = git_commit_of(dir);
        if (!commit) {
            printf("[lumipm] '%s' 는 git 저장소가 아니라 건너뜁니다.\n", names[i]);
            continue;
        }
        char src[LUMI_PATH_MAX * 2] = "", oldc[128] = "";
        if (!old || !lock_lookup(old, names[i], src, sizeof(src), oldc, sizeof(oldc))) {
            /* source 를 모르면 git 에게 물어봅니다 */
            char cmd[LUMI_PATH_MAX * 3];
            snprintf(cmd, sizeof(cmd), "git -C \"%s\" remote get-url origin", dir);
            int rc = -1;
            char *u = plat_run(cmd, &rc);
            if (u && rc == 0) {
                size_t k = 0;
                while (u[k] && u[k] != '\n' && u[k] != '\r') k++;
                u[k] = 0;
                snprintf(src, sizeof(src), "%s", u);
            }
            free(u);
        }
        if (n++) fprintf(f, ",\n");
        fprintf(f, "    \"%s\": { \"source\": \"%s\", \"commit\": \"%s\" }",
                names[i], src, commit);
        free(commit);
    }
    fprintf(f, "\n  }\n}\n");
    fclose(f);
    plat_free_names(names);
    free(old);
    printf("[lumipm] lumi.lock 에 패키지 %d 개를 적었습니다.\n", n);
    return 0;
}

/* 락파일에 적힌 그대로 되돌려 놓습니다 */
static int install_from_lock(const char *pkg_dir)
{
    char *text = read_whole_file("lumi.lock");
    if (!text) return -1;                          /* 락파일 없음 */

    printf("[lumipm] lumi.lock 을 따라 설치합니다 (적힌 커밋 그대로).\n");
    make_dir_recursive(pkg_dir);

    int done = 0, failed = 0;
    const char *at = strstr(text, "\"packages\"");
    if (!at) { free(text); return -1; }
    const char *p2 = strchr(at, '{');
    if (!p2) { free(text); return -1; }
    p2++;

    /* "이름": { "source": "...", "commit": "..." } 을 차례로 훑습니다 */
    while ((p2 = strchr(p2, '"')) != NULL) {
        const char *ne = strchr(p2 + 1, '"');
        if (!ne) break;
        char name[LUMI_PATH_MAX];
        size_t nl = (size_t)(ne - p2 - 1);
        if (nl == 0 || nl >= sizeof(name)) break;
        memcpy(name, p2 + 1, nl); name[nl] = 0;
        p2 = ne + 1;
        if (strcmp(name, "source") == 0 || strcmp(name, "commit") == 0) continue;

        char src[LUMI_PATH_MAX * 2] = "", commit[128] = "";
        if (!lock_lookup(text, name, src, sizeof(src), commit, sizeof(commit))) continue;

        char dir[LUMI_PATH_MAX * 2];
        snprintf(dir, sizeof(dir), "%s%s%s", pkg_dir, plat_dir_sep(), name);
        char cmd[LUMI_PATH_MAX * 4];
        if (!dir_exists(dir)) {
            printf("[lumipm] -> %s 를 받는 중...\n", name);
            snprintf(cmd, sizeof(cmd), "git clone \"%s\" \"%s\"", src, dir);
            if (system(cmd) != 0) { printf("[lumipm]    받기 실패: %s\n", src); failed++; continue; }
        }
        snprintf(cmd, sizeof(cmd), "git -C \"%s\" checkout --quiet %s", dir, commit);
        if (system(cmd) != 0) {
            printf("[lumipm]    적힌 커밋(%s)으로 못 옮겼습니다.\n", commit);
            failed++;
            continue;
        }
        printf("[lumipm]    %s @ %.10s\n", name, commit);
        done++;
    }
    free(text);
    printf("[lumipm] %d 개 맞췄습니다%s.\n", done,
           failed ? " (실패한 것이 있습니다)" : "");
    return failed ? 1 : 0;
}

static char *extract_repo_name(const char *url)
{
    char tmp[LUMI_PATH_MAX];
    snprintf(tmp, sizeof(tmp), "%s", url);
    
    /* .git 확장자 제거 */
    size_t len = strlen(tmp);
    if (len > 4 && strcmp(tmp + len - 4, ".git") == 0) {
        tmp[len - 4] = 0;
    }
    
    char *p = strrchr(tmp, '/');
    char *p2 = strrchr(tmp, '\\');
    char *p3 = strrchr(tmp, ':');
    if (p2 > p) p = p2;
    if (p3 > p) p = p3;
    
    if (p && *(p + 1)) return xsprintf("%s", p + 1);
    return xsprintf("%s", tmp);
}

static void update_lumi_json_dep(const char *json_path, const char *pkg_name, const char *source)
{
    char *code = NULL;
    FILE *f = plat_fopen(json_path, "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz > 0 && sz < 1048576) {
            code = (char *)malloc((size_t)sz + 1);
            if (code) {
                size_t n = fread(code, 1, (size_t)sz, f);
                code[n] = 0;
            }
        }
        fclose(f);
    }

    if (!code) {
        /* lumi.json 이 없으면 새로 작성 */
        char *dir_name = get_current_dir_name();
        FILE *wf = plat_fopen(json_path, "wb");
        if (wf) {
            fprintf(wf, "{\n");
            fprintf(wf, "  \"name\": \"%s\",\n", dir_name);
            fprintf(wf, "  \"version\": \"0.1.0\",\n");
            fprintf(wf, "  \"main\": \"main.lumi\",\n");
            fprintf(wf, "  \"dependencies\": {\n");
            fprintf(wf, "    \"%s\": \"%s\"\n", pkg_name, source);
            fprintf(wf, "  }\n");
            fprintf(wf, "}\n");
            fclose(wf);
        }
        free(dir_name);
        return;
    }

    /* 이미 lumi.json 이 있는 경우 "dependencies" 섹션에 추가 */
    char *deps = strstr(code, "\"dependencies\"");
    if (deps) {
        char *brace = strchr(deps, '{');
        if (brace) {
            size_t insert_pos = (brace + 1) - code;
            FILE *wf = plat_fopen(json_path, "wb");
            if (wf) {
                fwrite(code, 1, insert_pos, wf);
                fprintf(wf, "\n    \"%s\": \"%s\",", pkg_name, source);
                fprintf(wf, "%s", code + insert_pos);
                fclose(wf);
            }
        }
    } else {
        /* dependencies 섹션이 없으면 마지막 } 직전에 추가 */
        char *last_brace = strrchr(code, '}');
        if (last_brace) {
            size_t insert_pos = last_brace - code;
            FILE *wf = plat_fopen(json_path, "wb");
            if (wf) {
                fwrite(code, 1, insert_pos, wf);
                fprintf(wf, ",\n  \"dependencies\": {\n    \"%s\": \"%s\"\n  }\n", pkg_name, source);
                fprintf(wf, "%s", code + insert_pos);
                fclose(wf);
            }
        }
    }
    free(code);
}

static void remove_lumi_json_dep(const char *json_path, const char *pkg_name)
{
    FILE *f = plat_fopen(json_path, "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz >= 1048576) { fclose(f); return; }

    char *code = (char *)malloc((size_t)sz + 1);
    if (!code) { fclose(f); return; }
    size_t n = fread(code, 1, (size_t)sz, f);
    code[n] = 0;
    fclose(f);

    char search_pattern[256];
    snprintf(search_pattern, sizeof(search_pattern), "\"%s\"", pkg_name);
    char *pos = strstr(code, search_pattern);
    if (pos) {
        char *line_start = pos;
        while (line_start > code && *(line_start - 1) != '\n') line_start--;
        char *line_end = strchr(pos, '\n');
        if (!line_end) line_end = code + strlen(code);
        else line_end++;

        FILE *wf = plat_fopen(json_path, "wb");
        if (wf) {
            fwrite(code, 1, line_start - code, wf);
            fprintf(wf, "%s", line_end);
            fclose(wf);
        }
    }
    free(code);
}

static int cmd_pkg_init(void)
{
    if (path_exists("lumi.json")) {
        printf("[lumipm] lumi.json이 이미 존재합니다.\n");
        return 0;
    }
    char *name = get_current_dir_name();
    FILE *f = plat_fopen("lumi.json", "wb");
    if (!f) {
        printf("[lumipm] lumi.json 파일을 생성할 수 없습니다.\n");
        free(name);
        return 1;
    }
    fprintf(f, "{\n");
    fprintf(f, "  \"name\": \"%s\",\n", name);
    fprintf(f, "  \"version\": \"0.1.0\",\n");
    fprintf(f, "  \"description\": \"Lumi 언어 패키지\",\n");
    fprintf(f, "  \"main\": \"main.lumi\",\n");
    fprintf(f, "  \"dependencies\": {}\n");
    fprintf(f, "}\n");
    fclose(f);
    printf("[lumipm] 현재 디렉터리에 lumi.json 패키지 매니페스트가 생성되었습니다 (%s).\n", name);
    free(name);
    return 0;
}

static int install_single_target(const char *target, const char *pkg_dir, bool is_global)
{
    make_dir_recursive(pkg_dir);

    char git_url[LUMI_PATH_MAX * 2];
    if (strncmp(target, "github:", 7) == 0) {
        snprintf(git_url, sizeof(git_url), "https://github.com/%s.git", target + 7);
    } else {
        snprintf(git_url, sizeof(git_url), "%s", target);
    }

    /* URL 또는 Git 저장소인 경우 */
    if (strstr(git_url, "://") || strstr(git_url, "git@") || strstr(git_url, ".git")) {
        char *repo_name = extract_repo_name(git_url);
        char dest_path[LUMI_PATH_MAX * 2];
        snprintf(dest_path, sizeof(dest_path), "%s%s%s", pkg_dir, plat_dir_sep(), repo_name);

        printf("[lumipm] 패키지 다운로드 중: %s -> %s...\n", git_url, dest_path);

        if (dir_exists(dest_path)) {
            printf("[lumipm] 이미 설치되어 있습니다. 업데이트를 수행합니다...\n");
            char cmd[LUMI_PATH_MAX * 3];
            snprintf(cmd, sizeof(cmd), "git -C \"%s\" pull", dest_path);
            system(cmd);
        } else {
            char cmd[LUMI_PATH_MAX * 3];
            snprintf(cmd, sizeof(cmd), "git clone --depth 1 \"%s\" \"%s\"", git_url, dest_path);
            int res = system(cmd);
            if (res != 0) {
                printf("[lumipm] 오류: git clone 실패 (%s). git이 설치되어 있는지 확인하세요.\n", git_url);
                free(repo_name);
                return 1;
            }
        }

        if (!is_global && path_exists("lumi.json")) {
            update_lumi_json_dep("lumi.json", repo_name, git_url);
        }

        printf("[lumipm] 패키지 '%s' 설치가 완료되었습니다!\n", repo_name);
        free(repo_name);
        /* 무엇을 받았는지 적어 두어야 다음에 똑같이 받을 수 있습니다 */
        if (!is_global) write_lock(pkg_dir);
        return 0;
    }

    /* 로컬 디렉터리 경로인 경우 */
    if (dir_exists(target)) {
        char *pkg_name = extract_repo_name(target);
        char dest_path[LUMI_PATH_MAX * 2];
        snprintf(dest_path, sizeof(dest_path), "%s%s%s", pkg_dir, plat_dir_sep(), pkg_name);

        printf("[lumipm] 로컬 패키지 복사 중: %s -> %s...\n", target, dest_path);
        char cmd[LUMI_PATH_MAX * 3];
        snprintf(cmd, sizeof(cmd), "xcopy /E /I /Y /Q \"%s\" \"%s\"", target, dest_path);
        system(cmd);

        if (!is_global && path_exists("lumi.json")) {
            update_lumi_json_dep("lumi.json", pkg_name, target);
        }
        printf("[lumipm] 로컬 패키지 '%s' 설치 완료!\n", pkg_name);
        free(pkg_name);
        return 0;
    }

    printf("[lumipm] 오류: '%s' 은(는) 올바른 Git URL 또는 로컬 경로가 아닙니다.\n", target);
    return 1;
}

static int cmd_pkg_install(const char *target, bool is_global)
{
    char *pkg_dir = is_global ? get_global_pkg_dir() : xsprintf("lumi_packages");

    if (!target || strlen(target) == 0) {
        /* target이 없으면 lumi.json dependencies 전체 설치 */
        if (!path_exists("lumi.json")) {
            printf("[lumipm] lumi.json 파일이 없습니다. 설치할 패키지 URL을 입력하거나 'lumipm init'을 실행하세요.\n");
            free(pkg_dir);
            return 1;
        }

        FILE *f = plat_fopen("lumi.json", "rb");
        if (!f) { free(pkg_dir); return 1; }
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz <= 0 || sz >= 1048576) { fclose(f); free(pkg_dir); return 1; }

        char *code = (char *)malloc((size_t)sz + 1);
        if (!code) { fclose(f); free(pkg_dir); return 1; }
        size_t n = fread(code, 1, (size_t)sz, f);
        code[n] = 0;
        fclose(f);

        char *deps = strstr(code, "\"dependencies\"");
        if (!deps) {
            printf("[lumipm] lumi.json 에 의존성(dependencies)이 정의되어 있지 않습니다.\n");
            free(code);
            free(pkg_dir);
            return 0;
        }

        char *brace = strchr(deps, '{');
        char *end_brace = brace ? strchr(brace, '}') : NULL;
        if (brace && end_brace && end_brace > brace) {
            char *p = brace + 1;
            int count = 0;
            while (p < end_brace) {
                char *q1 = strchr(p, '"');
                if (!q1 || q1 >= end_brace) break;
                char *q2 = strchr(q1 + 1, '"');
                if (!q2 || q2 >= end_brace) break;

                char *colon = strchr(q2 + 1, ':');
                if (!colon || colon >= end_brace) break;

                char *v1 = strchr(colon + 1, '"');
                if (!v1 || v1 >= end_brace) break;
                char *v2 = strchr(v1 + 1, '"');
                if (!v2 || v2 >= end_brace) break;

                size_t key_len = q2 - (q1 + 1);
                size_t val_len = v2 - (v1 + 1);
                char pkg_name[128], pkg_url[LUMI_PATH_MAX];
                if (key_len < sizeof(pkg_name) && val_len < sizeof(pkg_url)) {
                    memcpy(pkg_name, q1 + 1, key_len); pkg_name[key_len] = 0;
                    memcpy(pkg_url, v1 + 1, val_len); pkg_url[val_len] = 0;

                    printf("[lumipm] 의존성 설치 (%d): %s (%s)\n", ++count, pkg_name, pkg_url);
                    install_single_target(pkg_url, pkg_dir, is_global);
                }
                p = v2 + 1;
            }
            if (count == 0) {
                printf("[lumipm] 의존성 목록이 비어있습니다.\n");
            }
        }
        free(code);
        free(pkg_dir);
        return 0;
    }

    int res = install_single_target(target, pkg_dir, is_global);
    free(pkg_dir);
    return res;
}

static int cmd_pkg_remove(const char *pkg_name, bool is_global)
{
    if (!pkg_name || strlen(pkg_name) == 0) {
        printf("[lumipm] 삭제할 패키지 이름을 지정해 주세요. 예: lumipm remove my-lib\n");
        return 1;
    }

    char *pkg_dir = is_global ? get_global_pkg_dir() : xsprintf("lumi_packages");
    char target_path[LUMI_PATH_MAX * 2];
    snprintf(target_path, sizeof(target_path), "%s%s%s", pkg_dir, plat_dir_sep(), pkg_name);

    if (!dir_exists(target_path)) {
        printf("[lumipm] 패키지 '%s' 이(가) %s 에 존재하지 않습니다.\n", pkg_name, pkg_dir);
        free(pkg_dir);
        return 1;
    }

    char cmd[LUMI_PATH_MAX * 3];
    snprintf(cmd, sizeof(cmd), "rmdir /s /q \"%s\"", target_path);
    system(cmd);

    if (!is_global && path_exists("lumi.json")) {
        remove_lumi_json_dep("lumi.json", pkg_name);
    }

    printf("[lumipm] 패키지 '%s' 삭제가 완료되었습니다.\n", pkg_name);
    free(pkg_dir);
    return 0;
}

static int cmd_pkg_list(bool is_global)
{
    char *pkg_dir = is_global ? get_global_pkg_dir() : xsprintf("lumi_packages");
    printf("[lumipm] 설치된 패키지 목록 (%s):\n", is_global ? "전역 (~/.lumi/packages)" : "프로젝트 (lumi_packages)");

    if (!dir_exists(pkg_dir)) {
        printf("  (설치된 패키지가 없습니다)\n");
        free(pkg_dir);
        return 0;
    }

    char **names = plat_subdirs(pkg_dir);
    if (!names) {
        printf("  (설치된 패키지가 없습니다)\n");
        free(pkg_dir);
        return 0;
    }

    int count = 0;
    for (size_t i = 0; names[i]; i++) {
        count++;
        char *main_file = get_package_entry(pkg_dir, names[i]);
        printf("  - %-20s (진입점: %s)\n", names[i], main_file ? main_file : "알 수 없음");
        if (main_file) free(main_file);
    }
    plat_free_names(names);

    if (count == 0) {
        printf("  (설치된 패키지가 없습니다)\n");
    }

    free(pkg_dir);
    return 0;
}

static int cmd_pkg_update(const char *pkg_name, bool is_global)
{
    char *pkg_dir = is_global ? get_global_pkg_dir() : xsprintf("lumi_packages");

    if (pkg_name && strlen(pkg_name) > 0) {
        char target_path[LUMI_PATH_MAX * 2];
        snprintf(target_path, sizeof(target_path), "%s%s%s", pkg_dir, plat_dir_sep(), pkg_name);
        if (!dir_exists(target_path)) {
            printf("[lumipm] 패키지 '%s' 이(가) 존재하지 않습니다.\n", pkg_name);
            free(pkg_dir);
            return 1;
        }
        printf("[lumipm] '%s' 업데이트 진행 중...\n", pkg_name);
        char cmd[LUMI_PATH_MAX * 3];
        snprintf(cmd, sizeof(cmd), "git -C \"%s\" pull", target_path);
        system(cmd);
    } else {
        printf("[lumipm] 모든 설치된 패키지 업데이트 진행 중...\n");
        char **names = plat_subdirs(pkg_dir);
        if (names) {
            for (size_t i = 0; names[i]; i++) {
                char target_path[LUMI_PATH_MAX * 2];
                snprintf(target_path, sizeof(target_path), "%s%s%s",
                         pkg_dir, plat_dir_sep(), names[i]);
                printf("[lumipm] -> %s 업데이트 중...\n", names[i]);
                char cmd[LUMI_PATH_MAX * 3];
                snprintf(cmd, sizeof(cmd), "git -C \"%s\" pull", target_path);
                system(cmd);
            }
            plat_free_names(names);
        }
    }
    free(pkg_dir);
    return 0;
}

static int cmd_pkg_info(const char *pkg_name, bool is_global)
{
    if (!pkg_name || strlen(pkg_name) == 0) {
        printf("[lumipm] 정보를 조회할 패키지 이름을 지정하세요.\n");
        return 1;
    }

    char *pkg_dir = is_global ? get_global_pkg_dir() : xsprintf("lumi_packages");
    char target_path[LUMI_PATH_MAX * 2];
    snprintf(target_path, sizeof(target_path), "%s%s%s", pkg_dir, plat_dir_sep(), pkg_name);

    if (!dir_exists(target_path)) {
        printf("[lumipm] 패키지 '%s' 이(가) 존재하지 않습니다.\n", pkg_name);
        free(pkg_dir);
        return 1;
    }

    char json_path[LUMI_PATH_MAX * 2];
    snprintf(json_path, sizeof(json_path), "%s%slumi.json", target_path, plat_dir_sep());
    char *entry = get_package_entry(pkg_dir, pkg_name);

    printf("========================================\n");
    printf("  패키지 정보: %s\n", pkg_name);
    printf("  경로: %s\n", target_path);
    printf("  진입점 파일: %s\n", entry ? entry : "N/A");
    printf("========================================\n");
    if (entry) free(entry);

    if (path_exists(json_path)) {
        FILE *f = plat_fopen(json_path, "rt");
        if (f) {
            char line[256];
            while (fgets(line, sizeof(line), f)) {
                printf("%s", line);
            }
            fclose(f);
        }
    } else {
        printf("(lumi.json 설명 파일 없음)\n");
    }

    free(pkg_dir);
    return 0;
}

static void print_pkg_help(void)
{
    printf("Lumi 외부 패키지 매니저 (lumipm / lumi pkg)\n");
    printf("사용법: lumi pkg <명령어> [옵션]\n\n");
    printf("명령어 목록:\n");
    printf("  init                      현재 디렉터리에 lumi.json 패키지 매니페스트를 생성합니다.\n");
    printf("  install, add [Git-URL|경로]  패키지를 설치합니다 (지정이 없으면 lumi.json 의존성 전체 설치).\n");
    printf("  remove, uninstall <이름>    설치된 패키지를 삭제합니다.\n");
    printf("  list, ls                  설치된 패키지 목록을 출력합니다.\n");
    printf("  update [이름]              설치된 패키지를 git pull로 업데이트합니다.\n");
    printf("  info <이름>                패키지의 세부 정보를 조회합니다.\n");
    printf("  lock                      지금 설치된 것들의 커밋을 lumi.lock 에 적습니다.\n");
    printf("                            (install 을 이름 없이 돌리면 lumi.lock 대로 맞춥니다)\n");
    printf("\n옵션:\n");
    printf("  --global, -g              전역 패키지 디렉터리 (~/.lumi/packages) 대상 작성을 수행합니다.\n");
}

int run_pkg_manager(int argc, char **argv)
{
    if (argc < 1) {
        print_pkg_help();
        return 0;
    }

    bool is_global = false;

    const char *subcmd = argv[0];

    /* 전역 옵션 (-g / --global) 확인 */
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-g") == 0 || strcmp(argv[i], "--global") == 0) {
            is_global = true;
        }
    }

    const char *target = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-g") != 0 && strcmp(argv[i], "--global") != 0) {
            target = argv[i];
            break;
        }
    }

    if (strcmp(subcmd, "lock") == 0) {
        char *pd = is_global ? get_global_pkg_dir() : xsprintf("lumi_packages");
        int rc = write_lock(pd);
        free(pd);
        return rc;
    }
    if (strcmp(subcmd, "init") == 0) {
        return cmd_pkg_init();
    } else if (strcmp(subcmd, "install") == 0 || strcmp(subcmd, "add") == 0 || strcmp(subcmd, "i") == 0) {
        return cmd_pkg_install(target, is_global);
    } else if (strcmp(subcmd, "remove") == 0 || strcmp(subcmd, "uninstall") == 0 || strcmp(subcmd, "rm") == 0) {
        return cmd_pkg_remove(target, is_global);
    } else if (strcmp(subcmd, "list") == 0 || strcmp(subcmd, "ls") == 0) {
        return cmd_pkg_list(is_global);
    } else if (strcmp(subcmd, "update") == 0 || strcmp(subcmd, "up") == 0) {
        return cmd_pkg_update(target, is_global);
    } else if (strcmp(subcmd, "info") == 0) {
        return cmd_pkg_info(target, is_global);
    } else if (strcmp(subcmd, "help") == 0 || strcmp(subcmd, "--help") == 0 || strcmp(subcmd, "-h") == 0) {
        print_pkg_help();
        return 0;
    } else {
        printf("[lumipm] 알 수 없는 명령어: %s\n\n", subcmd);
        print_pkg_help();
        return 1;
    }
}
