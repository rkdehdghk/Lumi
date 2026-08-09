#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <vector>

struct Tab {
    int id = 0;            // id 는 순서 섞임에도 유지
    QString path;          // Untitled 는 빈 문자열
    QString name;
    QString content;       // 비활성 탭의 내용. 활성 탭은 CodeEditor 가 들고,
                           // textChanged 마다 setActiveContent 로 동기화됨.
    bool modified = false;
    int cursor = 0;        // 탭을 떠날 때 커서 자리 (돌아오면 여기로)
};

class TabModel : public QObject {
    Q_OBJECT
public:
    explicit TabModel(QObject *parent = nullptr);

    const std::vector<Tab> &tabs() const { return m_tabs; }
    int activeIndex() const { return m_active; }
    Tab *activeTab();
    Tab *tabAt(int i);
    int count() const { return (int)m_tabs.size(); }

    // 대화상자(저장 확인/Save As)의 부모 창과 Save As 기본 폴더.
    void setOwner(QWidget *w) { m_owner = w; }
    void setDefaultDir(const QString &dir) { m_defaultDir = dir; }

    void openPath(const QString &path);
    int newTab(const QString &content = {});
    void activate(int i);
    void close(int i, bool confirm = true);
    void closeOthers(int i);
    void closeAll();
    bool save(int i, bool saveAs, QWidget *parent = nullptr);
    void setActiveContent(const QString &text);
    void setModified(int i, bool m);
    void pathRenamed(const QString &from, const QString &to);
    void pathRemoved(const QString &path);

    // 더러운 탭이 있으면 사용자에게 저장 확인. true=진행 가능.
    bool confirmDiscardAll(QWidget *parent);

signals:
    void tabsChanged();           // 탭 목록/순서/활성 변경
    void contentChanged();        // 활성 탭 content 변경
    void activeChanged(int index);
    void status(const QString &msg);

private:
    std::vector<Tab> m_tabs;
    int m_active = -1;
    int m_nextId = 1;
    QWidget *m_owner = nullptr;
    QString m_defaultDir;
    QString nextUntitledName() const;
    int findByPath(const QString &path) const;
    bool confirmClose(int i);     // 안 저장한 탭이면 물어본다. false = 닫지 마라.
};
