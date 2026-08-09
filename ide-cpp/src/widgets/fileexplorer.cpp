#include "fileexplorer.h"
#include <QTreeView>
#include <QFileSystemModel>
#include <QSortFilterProxyModel>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QInputDialog>
#include <QMessageBox>
#include <QDirIterator>
#include <QMenu>
#include <QShortcut>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QApplication>
#include <QMouseEvent>
#include <QStyle>
#include "collapsedbar.h"
#include "headerbits.h"

// .git .svn 등 VCS 폴더만 숨기고 .gitignore/.env 등은 표시.
// build/dist/bin/node_modules 등도 제외. (기존엔 "." 시작 전부 숨김)
static const QStringList HIDDEN_ENTRIES = {".git", ".svn", ".hg", "node_modules",
    "build", "dist", "bin", "__pycache__", "release", ".vs", ".cache"};

// QFileSystemModel 의 nameFilters 는 파일에만 걸리고 폴더는 못 거른다 — 그래서 이 목록이
// 빠른 열기에서만 쓰이고 트리에는 .git/node_modules/build 가 그대로 다 보였다.
class HideJunk : public QSortFilterProxyModel {
public:
    HideJunk(QFileSystemModel *fs, QObject *parent) : QSortFilterProxyModel(parent), m_fs(fs) {
        setSourceModel(fs);
    }
    // 열어 둔 폴더 자신과 거기로 내려가는 길목은 이름이 bin/build 여도 감추면 안 된다.
    // (이 프로젝트의 ide-cpp\bin 처럼) 하나라도 걸러지면 프록시 인덱스 사슬이 끊겨
    // 트리가 통째로 비어 버린다.
    void setRoot(const QString &root) { m_root = root; invalidateFilter(); }

protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override {
        const QModelIndex idx = m_fs->index(row, 0, parent);
        const QString path = m_fs->filePath(idx);
        if (!m_root.isEmpty() && (m_root == path || m_root.startsWith(path + '/'))) return true;
        return !HIDDEN_ENTRIES.contains(idx.data().toString(), Qt::CaseInsensitive);
    }

private:
    QFileSystemModel *m_fs;
    QString m_root;
};

FileExplorer::FileExplorer(QWidget *parent) : QWidget(parent) {
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    // 도크 타이틀바를 숨겨 놨더니 이 패널만 이름표도, 분리 버튼도 없었다.
    auto *hdr = new QFrame(this);
    m_hdr = hdr;
    hdr->setObjectName("HeaderFrame");
    hdr->setFixedHeight(30);
    auto *hl = new QHBoxLayout(hdr);
    hl->setContentsMargins(10, 0, 8, 0);
    hl->setSpacing(2);
    // 두 라벨 모두 좁아지면 0 까지 양보한다 — 그래야 오른쪽 버튼이 밖으로 밀려나지 않는다.
    auto *title = new ElideLabel(hdr);
    title->setObjectName("PanelTitle");
    title->setFullText("EXPLORER");
    m_titleLbl = title;
    m_folderLbl = new ElideLabel(hdr);
    m_folderLbl->setFullText("No Folder");
    m_folderLbl->setEnabled(false);
    m_detachBtn = new QPushButton(QString(QChar(0x29C9)) + " Detach", hdr);
    m_detachBtn->setObjectName("HeaderToolBtn");
    m_detachBtn->setCursor(Qt::PointingHandCursor);
    m_collapseBtn = new QPushButton(QString(QChar(0x25C2)), hdr);   // ◂ 접기
    m_collapseBtn->setObjectName("HeaderChevronBtn");
    m_collapseBtn->setCursor(Qt::PointingHandCursor);
    m_collapseBtn->setToolTip("Collapse / Expand");
    m_collapseBtn->setFixedHeight(24);
    hl->addWidget(title, 0);
    hl->addSpacing(8);
    hl->addWidget(m_folderLbl, 1);
    hl->addWidget(m_detachBtn, 0);
    hl->addWidget(m_collapseBtn, 0);
    connect(m_detachBtn, &QPushButton::clicked, this, &FileExplorer::detachRequested);
    connect(m_collapseBtn, &QPushButton::clicked, this, &FileExplorer::collapseRequested);
    lay->addWidget(hdr);

    m_tree = new QTreeView(this);
    m_model = new QFileSystemModel(this);
    m_model->setRootPath("");
    m_proxy = new HideJunk(m_model, this);
    m_tree->setModel(m_proxy);
    m_tree->setHeaderHidden(true);
    m_tree->setUniformRowHeights(true);
    m_tree->setAnimated(false);
    m_tree->setExpandsOnDoubleClick(true);
    for (int i = 1; i < m_model->columnCount(); ++i) m_tree->hideColumn(i);
    m_model->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);

    // No Folder 오버레이 뷰 (VS Code 스타일)
    m_noFolderWidget = new QWidget(this);
    auto *nfl = new QVBoxLayout(m_noFolderWidget);
    nfl->setContentsMargins(16, 24, 16, 24);
    nfl->setSpacing(12);
    nfl->setAlignment(Qt::AlignTop | Qt::AlignHCenter);

    auto *msgLbl = new QLabel("NO FOLDER OPEN", m_noFolderWidget);
    msgLbl->setAlignment(Qt::AlignCenter);
    msgLbl->setStyleSheet("font-size: 11px; font-weight: bold; color: #888888; letter-spacing: 1px;");

    auto *btnOpen = new QPushButton("Open Folder", m_noFolderWidget);
    btnOpen->setCursor(Qt::PointingHandCursor);
    btnOpen->setStyleSheet(
        "QPushButton {"
        "  background-color: #007acc;"
        "  color: white;"
        "  font-weight: bold;"
        "  border: none;"
        "  border-radius: 3px;"
        "  padding: 6px 14px;"
        "}"
        "QPushButton:hover { background-color: #0098ff; }"
    );
    connect(btnOpen, &QPushButton::clicked, this, &FileExplorer::openFolderRequested);

    nfl->addWidget(msgLbl);
    nfl->addWidget(btnOpen);
    nfl->addStretch(1);

    m_stack = new QStackedWidget(this);
    m_stack->addWidget(m_tree);
    m_stack->addWidget(m_noFolderWidget);
    m_stack->setCurrentWidget(m_noFolderWidget);

    lay->addWidget(m_stack, 1);

    // 세로 띠로 접혔을 때의 모습은 세 패널이 공유한다.
    m_bar = new CollapsedBar("EXPLORER", this);
    m_bar->hide();
    lay->addWidget(m_bar, 1);
    connect(m_bar, &CollapsedBar::clicked, this, &FileExplorer::collapseRequested);

    // activated = 더블클릭 + Enter. 예전엔 Enter 로 열 수 없었다.
    connect(m_tree, &QTreeView::activated, this, [this](const QModelIndex &idx) {
        QString p = pathAt(idx);
        if (QFileInfo(p).isFile()) emit fileOpened(p);
    });

    // 오른쪽 클릭 메뉴 + F2 / Delete
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tree, &QTreeView::customContextMenuRequested, this, [this](const QPoint &pos) {
        QMenu menu(this);
        QAction *nf = menu.addAction("New File...");
        QAction *nd = menu.addAction("New Folder...");
        menu.addSeparator();
        QAction *rn = menu.addAction("Rename\tF2");
        QAction *dl = menu.addAction("Delete\tDel");
        menu.addSeparator();
        QAction *rf = menu.addAction("Refresh");
        QAction *sel = menu.exec(m_tree->viewport()->mapToGlobal(pos));
        if (sel == nf) newFile();
        else if (sel == nd) newFolder();
        else if (sel == rn) renameSelected();
        else if (sel == dl) deleteSelected();
        else if (sel == rf) setFolder(m_folder);
    });

    auto *f2 = new QShortcut(QKeySequence(Qt::Key_F2), m_tree);
    f2->setContext(Qt::WidgetWithChildrenShortcut);
    connect(f2, &QShortcut::activated, this, &FileExplorer::renameSelected);

    auto *del = new QShortcut(QKeySequence(Qt::Key_Delete), m_tree);
    del->setContext(Qt::WidgetWithChildrenShortcut);
    connect(del, &QShortcut::activated, this, &FileExplorer::deleteSelected);

    // 탐색기 빈 곳 클릭 시 선택 해제
    m_tree->viewport()->installEventFilter(this);

    // INER/OUTER 등 외부 영역으로 포커스가 이동하면 하이라이트(선택) 해제
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget *, QWidget *newW) {
        if (newW && !this->isAncestorOf(newW)) {
            if (qobject_cast<QMenu*>(newW) || qobject_cast<QInputDialog*>(newW) ||
                newW->inherits("QMenu")) {
                return;
            }
            m_tree->clearSelection();
            m_tree->setCurrentIndex(QModelIndex());
        }
    });
}

bool FileExplorer::eventFilter(QObject *obj, QEvent *ev) {
    if (obj == m_tree->viewport() && ev->type() == QEvent::MouseButtonPress) {
        auto *me = static_cast<QMouseEvent*>(ev);
        QModelIndex idx = m_tree->indexAt(me->pos());
        if (!idx.isValid()) {
            m_tree->clearSelection();
            m_tree->setCurrentIndex(QModelIndex());
        }
    }
    return QWidget::eventFilter(obj, ev);
}

void FileExplorer::setFolder(const QString &folder) {
    if (folder.isEmpty() || !QFileInfo(folder).isDir()) {
        m_folder.clear();
        m_proxy->setRoot(QString());
        m_model->setRootPath(QString());
        m_tree->setRootIndex(QModelIndex());
        m_stack->setCurrentWidget(m_noFolderWidget);
        m_folderLbl->setFullText("No Folder");
        m_folderLbl->setToolTip("No Folder");
        emit folderChanged(QString());
        return;
    }
    m_folder = QDir(folder).absolutePath();
    m_proxy->setRoot(m_folder);
    m_model->setRootPath(m_folder);
    m_tree->setRootIndex(m_proxy->mapFromSource(m_model->index(m_folder)));
    m_stack->setCurrentWidget(m_tree);
    m_folderLbl->setFullText(QDir(m_folder).dirName());
    m_folderLbl->setToolTip(QDir::toNativeSeparators(m_folder));
    emit folderChanged(m_folder);
}

// 세로 띠로 접히면 공통 막대(CollapsedBar)만 보이고, 세로로 접히면 헤더 줄만 남는다.
void FileExplorer::setCollapsed(bool on, bool horizontal) {
    const bool strip = on && horizontal;
    m_stack->setVisible(!on);
    m_hdr->setVisible(!strip);
    m_bar->setVisible(strip);
    // 화살표는 "누르면 갈 방향"을 가리킨다 — 옆에 붙은 패널이면 좌우(◂▸), 위아래면 상하(▴▾).
    m_collapseBtn->setText(QString(QChar(horizontal ? (on ? 0x25B8 : 0x25C2)
                                                    : (on ? 0x25B4 : 0x25BE))));
}

void FileExplorer::setDetached(bool on) {
    m_detachBtn->setText(QString(QChar(0x29C9)) + (on ? " Dock" : " Detach"));
}

QStringList FileExplorer::collectFiles() const {
    QStringList out;
    if (m_folder.isEmpty()) return out;
    QDirIterator it(m_folder, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    int count = 0;
    while (it.hasNext() && count < 4000) {
        it.next();
        QString path = it.filePath();
        // 숨김 항목 필터
        bool skip = false;
        for (const QString &h : HIDDEN_ENTRIES) {
            if (path.contains("/" + h + "/") || path.contains("\\" + h + "\\")) { skip = true; break; }
        }
        if (!skip) { out << path; ++count; }
    }
    return out;
}

// 트리는 프록시(HideJunk)를 보고 있으므로, 파일 경로는 늘 원본 모델로 되돌려 묻는다.
QString FileExplorer::pathAt(const QModelIndex &viewIdx) const {
    return m_model->filePath(m_proxy->mapToSource(viewIdx));
}

// 새로 만들 자리: 폴더를 골랐으면 그 안, 파일을 골랐으면 그 파일이 있는 폴더.
QString FileExplorer::targetDir() const {
    QModelIndex idx = m_tree->currentIndex();
    if (!idx.isValid()) return m_folder;
    QString p = pathAt(idx);
    return QFileInfo(p).isDir() ? p : QFileInfo(p).absolutePath();
}

void FileExplorer::reveal(const QString &path) {
    QModelIndex idx = m_proxy->mapFromSource(m_model->index(path));
    if (!idx.isValid()) return;
    m_tree->setCurrentIndex(idx);
    m_tree->scrollTo(idx);
}

void FileExplorer::newFile() {
    if (m_folder.isEmpty() || !QFileInfo(m_folder).isDir()) {
        QMessageBox::warning(this, "Open Folder Required", "No folder is currently open. Please open a folder first before creating a file.");
        return;
    }
    QString dir = targetDir();
    bool ok;
    QString name = QInputDialog::getText(this, "New File", "Name:", QLineEdit::Normal,
                                         "newfile.lumi", &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    QString path = QDir(dir).filePath(name.trimmed());
    // WriteOnly 는 이미 있는 파일을 통째로 비운다 — 있으면 만들지 말고 그냥 연다.
    if (QFileInfo::exists(path)) {
        QMessageBox::information(this, "New File", "'" + name + "' already exists - opening it.");
        emit fileOpened(path);
        reveal(path);
        return;
    }
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, "New File", "Cannot create " + path + "\n" + f.errorString());
        return;
    }
    f.close();
    emit fileOpened(path);
    reveal(path);
}

void FileExplorer::newFolder() {
    if (m_folder.isEmpty() || !QFileInfo(m_folder).isDir()) {
        QMessageBox::warning(this, "Open Folder Required", "No folder is currently open. Please open a folder first before creating a folder.");
        return;
    }
    QString dir = targetDir();
    bool ok;
    QString name = QInputDialog::getText(this, "New Folder", "Name:", QLineEdit::Normal,
                                         "NewFolder", &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    QString path = QDir(dir).filePath(name.trimmed());
    if (QFileInfo::exists(path)) {
        QMessageBox::warning(this, "New Folder", "'" + name + "' already exists.");
        return;
    }
    if (!QDir().mkpath(path)) {
        QMessageBox::warning(this, "New Folder", "Cannot create " + path);
        return;
    }
    reveal(path);
}

void FileExplorer::renameSelected() {
    QModelIndex idx = m_tree->currentIndex();
    if (!idx.isValid()) return;
    QString oldP = pathAt(idx);
    bool ok;
    QString name = QInputDialog::getText(this, "Rename", "New name:", QLineEdit::Normal,
                                         QFileInfo(oldP).fileName(), &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    QString newP = QDir(QFileInfo(oldP).absolutePath()).filePath(name.trimmed());
    if (newP == oldP) return;
    if (QFileInfo::exists(newP)) {
        QMessageBox::warning(this, "Rename", "'" + name + "' already exists.");
        return;
    }
    if (QFile::rename(oldP, newP) || QDir().rename(oldP, newP)) {
        emit pathRenamed(oldP, newP);
        reveal(newP);
    } else {
        // 조용히 실패하면 이름이 안 바뀐 이유를 알 수 없었다.
        QMessageBox::warning(this, "Rename", "Cannot rename to '" + name +
                             "'. The file may be open in another program.");
    }
}

void FileExplorer::deleteSelected() {
    QModelIndex idx = m_tree->currentIndex();
    if (!idx.isValid()) return;
    QString p = pathAt(idx);
    const bool isDir = QFileInfo(p).isDir();
    auto r = QMessageBox::question(this, "Delete",
        QString("Delete %1 '%2'? This cannot be undone.")
            .arg(isDir ? "folder" : "file", QFileInfo(p).fileName()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);   // 실수 방지: 기본은 No
    if (r != QMessageBox::Yes) return;
    bool done = isDir ? QDir(p).removeRecursively() : QFile::remove(p);
    if (!done) {
        QMessageBox::warning(this, "Delete", "Cannot delete " + p);
        return;
    }
    emit pathRemoved(p);
}
