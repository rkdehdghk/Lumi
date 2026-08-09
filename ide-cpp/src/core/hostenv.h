#pragma once
#include <QString>
#include <QStringList>

class QProcess;

// 운영체제마다 다른 것은 **여기 한 곳에만** 적는다.
// 인터프리터 쪽 c-interpreter/src/platform.c 의 plat_* 와 같은 생각이다 —
// 나머지 IDE 코드는 cmd.exe·taskkill·"C:\" 를 직접 알지 못한다.
//
// 지금 갈라지는 것은 여섯 가지뿐이다: 실행 파일 이름, 아래 터미널이 쓰는 셸,
// 줄 끝, 폴더 옮기기 명령, 프로세스 트리 죽이기, 그리고 프롬프트 생김새.

// "lumi" -> 윈도우 "lumi.exe", 그 밖 "lumi"
QString hostExeName(const QString &base);

// 아래 터미널(OUTER)이 띄우는 셸과 그 인자.
// 윈도우: cmd.exe /q /k chcp 65001   (echo off + UTF-8 코드페이지)
// 그 밖 : $SHELL -i  (없으면 /bin/sh -i)
QString     hostShellProgram();
QStringList hostShellArgs();
// 셸에 넣어 줄 환경 변수 ("이름=값").  POSIX 에서 프롬프트를 우리가 아는 모양으로
// 고정하는 데 쓴다.  윈도우는 빈 목록.
QStringList hostShellEnv();

// 셸에 한 줄을 밀어 넣을 때 붙이는 줄 끝 ("\r\n" / "\n")
QString hostLineEnd();

// 그 폴더로 옮기는 명령 한 줄 (줄 끝은 안 붙인다)
QString hostCdCommand(const QString &dir);

// 자식까지 통째로 죽인다.  QProcess::kill 은 셸만 죽이고 그 안에서 돌던
// lumi 는 살아남기 때문에, Stop 이 정말 멈추려면 트리째 죽여야 한다.
void hostKillTree(QProcess *p);

// 터미널이 처음 열릴 때의 폴더 (설정에 아무것도 없을 때)
QString hostDefaultTermDir();

// 프롬프트 알아보기 (QRegularExpression 무늬).
//   Init  셸이 처음 뜬 것을 알아채는 데 — 끝에 ANSI 색 코드가 붙어도 맞아야 한다
//   Tail  출력 꼬리에 붙은 프롬프트를 다른 색으로 칠하는 데
QString hostPromptInitPattern();
QString hostPromptTailPattern();
