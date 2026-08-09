#include "mainwindow.h"
#include "core/settings.h"
#include "theme/theme.h"
#include <QApplication>
#include <QSettings>

int main(int argc, char *argv[]) {
    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication app(argc, argv);
    app.setApplicationName("Lumina");
    app.setOrganizationName("Lumina");
    app.setApplicationVersion("1.0");
    QSettings::setDefaultFormat(QSettings::IniFormat);   // %APPDATA%/Lumina/Lumina.ini

    applyTheme(&app, settings::loadDarkTheme());

    MainWindow w;
    // 크기/최대화 여부는 지난번 상태를 따른다 (첫 실행은 restoreWindowState 가 최대화로 둔다).
    // 예전엔 여기서 무조건 showMaximized 라 창 크기를 줄여 놔도 다음에 켜면 되돌아갔다.
    w.show();
    // 창틀이 붙은 뒤 한 번 더 — 복원한 크기가 화면 밖으로 삐져나가지 않게.
    settings::clampToScreen(&w);
    return app.exec();
}
