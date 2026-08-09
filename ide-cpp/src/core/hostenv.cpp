/* -std=c++17 처럼 엄격한 ISO 로 컴파일하면 kill·setsid 같은 POSIX 함수의 선언이
 * 헤더에서 숨겨집니다.  아무 헤더보다 먼저 이걸 켜서 어떤 빌드 옵션에서도 보이게 합니다.
 * (인터프리터 쪽 c-interpreter/src/platform.c 가 같은 것에 걸린 적이 있습니다) */
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

#include "hostenv.h"

#include <QDir>
#include <QProcess>

#if !defined(Q_OS_WIN)
#  include <sys/types.h>
#  include <signal.h>
#  include <unistd.h>
#endif

QString hostExeName(const QString &base)
{
#if defined(Q_OS_WIN)
    return base + ".exe";
#else
    return base;
#endif
}

QString hostShellProgram()
{
#if defined(Q_OS_WIN)
    return "cmd.exe";
#else
    // 일부러 $SHELL 이 아니라 sh 다. 윈도우가 사람이 고른 셸이 아니라 cmd.exe 를
    // 쓰는 것과 같은 이유이고, 더 중요하게는 **프롬프트를 못박을 수 있어서**다 —
    // 대화형 bash 는 ~/.bashrc 를 읽어 우리가 넣어 준 PS1 을 도로 덮어쓴다.
    return QStringLiteral("/bin/sh");
#endif
}

QStringList hostShellArgs()
{
#if defined(Q_OS_WIN)
    // /q = 명령을 되찍지 않음, /k = 계속 살아 있음, chcp 65001 = 글자를 UTF-8 로
    return {"/q", "/k", "chcp 65001 >nul"};
#else
    // -i 라야 프롬프트를 찍는다 (파이프에 물려 있어도).
    return {"-i"};
#endif
}

QStringList hostShellEnv()
{
#if defined(Q_OS_WIN)
    return {};
#else
    // 프롬프트를 **우리가 아는 모양**으로 못박는다. 사람마다 제각각인 PS1
    // (색, 깃 브랜치, 여러 줄)을 무늬로 알아맞히려 들면 반드시 틀린다.
    //   \w  = 지금 폴더,  \$ = 보통 사용자면 $ / 루트면 #
    return {"PS1=\\w\\$ ", "PROMPT_COMMAND="};
#endif
}

QString hostLineEnd()
{
#if defined(Q_OS_WIN)
    return "\r\n";
#else
    return "\n";
#endif
}

QString hostCdCommand(const QString &dir)
{
    const QString quoted = "\"" + QDir::toNativeSeparators(dir) + "\"";
#if defined(Q_OS_WIN)
    return "cd /d " + quoted;      // /d 라야 드라이브까지 함께 옮긴다
#else
    return "cd " + quoted;
#endif
}

void hostKillTree(QProcess *p)
{
    const qint64 pid = p->processId();
#if defined(Q_OS_WIN)
    if (pid > 0)
        QProcess::execute("taskkill", {"/T", "/F", "/PID", QString::number(pid)});
#else
    // spawn 이 자식을 제 무리(process group)의 우두머리로 세워 두었으므로,
    // 음수 pid 로 그 무리를 통째로 죽인다 — 손자까지 함께 간다.
    if (pid > 0) {
        ::kill(static_cast<pid_t>(-pid), SIGTERM);
        ::kill(static_cast<pid_t>(-pid), SIGKILL);
    }
#endif
    p->kill();
}

QString hostDefaultTermDir()
{
#if defined(Q_OS_WIN)
    return "C:\\";
#else
    return QDir::homePath();
#endif
}

QString hostPromptInitPattern()
{
#if defined(Q_OS_WIN)
    return R"((?:[A-Za-z]:\\|\\\\)[^>]*>\s*(?:\x1b\[[0-9;]*[a-zA-Z]\s*)*$)";
#else
    return R"([^\r\n]*[$#] ?\s*(?:\x1b\[[0-9;]*[a-zA-Z]\s*)*$)";
#endif
}

QString hostPromptTailPattern()
{
#if defined(Q_OS_WIN)
    return R"((?:[A-Za-z]:\\|\\\\)[^>\r\n]*> ?$)";
#else
    // 끝에 색 코드가 붙어 있어도 프롬프트로 알아본다
    return R"([^\r\n]*[$#] ?(?:\x1b\[[0-9;]*[a-zA-Z])*$)";
#endif
}
