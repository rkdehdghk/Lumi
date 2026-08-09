#include "paths.h"
#include "hostenv.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

QString interpreterPath() {
    const QString exe = hostExeName("lumi");
    QString dir = QCoreApplication::applicationDirPath();
    const QStringList tries = {
        dir + "/" + exe,
        dir + "/../c-interpreter/bin/" + exe,
        dir + "/../../c-interpreter/bin/" + exe,
        dir + "/../../../c-interpreter/bin/" + exe
    };
    for (const QString &rel : tries) {
        QString full = QDir::cleanPath(rel);
        if (QFileInfo::exists(full)) return full;
    }
    return QDir::cleanPath(dir + "/" + exe);
}

// F5 는 터미널에 `lumi --ide "x.lumi"` 를 쳐 넣는다. PATH 에 lumi 가 없으면 그 줄이
// "'lumi' is not recognized" 로 끝나 버린다 — 그럴 땐 찾아 둔 전체 경로를 쓴다.
QString interpreterCommand() {
    if (!QStandardPaths::findExecutable("lumi").isEmpty()) return "lumi";
    const QString full = interpreterPath();
    if (QFileInfo::exists(full)) return "\"" + QDir::toNativeSeparators(full) + "\"";
    return "lumi";
}
