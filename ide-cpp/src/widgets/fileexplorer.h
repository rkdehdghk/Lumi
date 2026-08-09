#pragma once
#include <QWidget>
#include <QString>

class QTreeView;
class QFileSystemModel;
class QModelIndex;
class HideJunk;   // .cpp 안에 있는 필터 프록시 (.git / node_modules / build ... 감추기)
class QLabel;
class QPushButton;
class QFrame;
class CollapsedBar;
class ElideLabel;

class FileExplorer : public QWidget {
    Q_OBJECT
public:
    explicit FileExplorer(QWidget *parent = nullptr);
    void setFolder(const QString &folder);
    void setDetached(bool on);
    void setCollapsed(bool on, bool horizontal);   // 트리만 접고 얇은 띠로 남긴다
    QString folder() const { return m_folder; }

    // 빠른 열기를 위한 파일 목록 수집 (walkDir 이식)
    QStringList collectFiles() const;

signals:
    void fileOpened(const QString &path);
    void folderChanged(const QString &folder);
    void pathRenamed(const QString &from, const QString &to);
    void pathRemoved(const QString &path);
    void detachRequested();
    void collapseRequested();
    void openFolderRequested();

protected:
    bool eventFilter(QObject *obj, QEvent *ev) override;

public slots:
    void newFile();
    void newFolder();
    void renameSelected();
    void deleteSelected();

private:
    QString targetDir() const;
    void reveal(const QString &path);
    QString pathAt(const QModelIndex &viewIdx) const;   // 트리 인덱스 → 실제 파일 경로

    class QStackedWidget *m_stack;
    QWidget *m_noFolderWidget;
    QTreeView *m_tree;
    QFileSystemModel *m_model;
    HideJunk *m_proxy;   // .git / node_modules / build ... 를 트리에서 감춘다
    ElideLabel *m_folderLbl;
    ElideLabel *m_titleLbl;
    QFrame *m_hdr;
    QPushButton *m_detachBtn;
    QPushButton *m_collapseBtn;
    CollapsedBar *m_bar;
    QString m_folder;
};
