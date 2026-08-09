#pragma once
#include <QWidget>
#include "../models/tabmodel.h"

class TabBar;
class CodeEditor;
class CollapsedBar;
class QMenu;

class QStackedWidget;

class EditorPanel : public QWidget {
    Q_OBJECT
public:
    explicit EditorPanel(TabModel *model, QWidget *parent = nullptr);

    CodeEditor *editor() { return m_edit; }
    void setDetached(bool on);
    void setCollapsed(bool on, bool horizontal);   // 에디터만 접고 탭바는 남긴다
    void loadActive();
    void refreshTheme();
    void newTab();
    void triggerFind();
    void findNext(bool backward);
    void triggerReplace();
    void goToLine();
    void commentToggle();

signals:
    void detachRequested();
    void collapseRequested();
    void contentChanged();
    void cursorPos(int line, int col);
    void status(const QString &msg);

private:
    TabModel *m_model;
    TabBar *m_tabs;
    CodeEditor *m_edit;
    QStackedWidget *m_stack;
    CollapsedBar *m_bar;
    QWidget *m_emptyOverlay;
    bool m_loading = false;
    int m_lastIndex = -1;
    QString m_lastFind;

    bool findFrom(const QString &term, bool backward);
};
