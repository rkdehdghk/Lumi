#pragma once
#include <QColor>
#include <QApplication>
#include "../core/lumitokenizer.h"

struct Palette {
    QColor bg, surface, surfaceAlt, border;
    QColor text, textDim;
    QColor accent, accentText;
    QColor error, errorBg, success, info;
    QColor editorBg, editorFg;
    QColor termBg, termFg, termInfo, termInput, termPrompt;
    QColor tabbarBg, tabActive, tabInactive, tabFg, tabFgDim;
    QColor sidebarBg, sidebarFg, sidebarSel;
    QColor btnBg, btnHover, btnFg, ghostFg, ghostHover;
    QColor statusBg, statusFg;
    QColor line, focus;
    // 신택스
    QColor synKeyword, synString, synNumber, synComment, synBuiltin;
};

const Palette &currentPalette();
bool isDark();

// dark=true 다크, false 라이트. QPalette + QSS 전역 적용.
void applyTheme(QApplication *app, bool dark);
void toggleTheme(QApplication *app);

// 하이라이트 헬퍼
QColor tokColor(TokKind k);
bool tokBold(TokKind k);
