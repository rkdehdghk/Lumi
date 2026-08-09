#include "commandpalette.h"
#include "../core/fuzzy.h"
#include "../core/commands.h"
#include <QVBoxLayout>
#include <QFileInfo>
#include <QKeyEvent>
#include <QApplication>
#include <algorithm>

CommandPalette::CommandPalette(Mode mode, QWidget *parent)
    : QDialog(parent), m_mode(mode) {
    setWindowFlags(Qt::Dialog);
    setModal(true);
    setWindowTitle(mode == Files ? "Quick Open" : "Command Palette");
    resize(520, 340);

    auto *lay = new QVBoxLayout(this);
    m_edit = new QLineEdit(this);
    m_edit->setPlaceholderText(mode == Files ? "Search files..." : "Type a command...");
    m_list = new QListWidget(this);
    m_list->setUniformItemSizes(true);
    lay->addWidget(m_edit);
    lay->addWidget(m_list);

    if (m_mode == Commands) {
        m_cmds = {
            {"File: New Tab", Cmd_New}, {"File: Open", Cmd_Open}, {"File: Open Folder", Cmd_OpenFolder},
            {"File: Close Folder", Cmd_CloseFolder}, {"File: Save", Cmd_Save},
            {"File: Save As", Cmd_SaveAs}, {"File: Close", Cmd_CloseTab},
            {"File: Close Others", Cmd_CloseOthers}, {"File: Close All", Cmd_CloseAll},
            {"Run: Run", Cmd_Run}, {"Run: Stop", Cmd_Stop},
            {"View: Toggle Theme", Cmd_ThemeToggle}, {"View: Explore", Cmd_ToggleSidebar},
            {"View: Toggle INER", Cmd_ToggleIner}, {"View: Toggle OUTER", Cmd_TogglePanel},
            {"View: Detach INER", Cmd_DetachIner},
            {"View: Detach OUTER", Cmd_DetachOuter},
            {"View: Detach Explorer", Cmd_DetachTree},
            {"Edit: Find", Cmd_Find}, {"Edit: Replace", Cmd_Replace},
            {"Edit: Go to Line", Cmd_GoToLine}, {"Edit: Toggle Comment", Cmd_Comment},
            {"View: Zoom In", Cmd_ZoomIn}, {"View: Zoom Out", Cmd_ZoomOut},
            {"View: Reset Zoom", Cmd_ZoomReset},
            {"File: New File", Cmd_NewFile}, {"File: New Folder", Cmd_NewFolder},
            {"Explorer: Refresh", Cmd_Refresh}
        };
    }

    connect(m_edit, &QLineEdit::textChanged, this, &CommandPalette::populate);
    connect(m_list, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        chooseItem(item);
    });
    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        chooseItem(item);
    });
    // Enter 로 고를 수 없었고 ↑↓ 도 안 먹었다 — 입력칸에서 목록을 몰아 준다.
    connect(m_edit, &QLineEdit::returnPressed, this, [this] {
        chooseItem(m_list->currentItem());
    });
    m_edit->installEventFilter(this);

    populate({});
    m_edit->setFocus();
}

void CommandPalette::chooseItem(QListWidgetItem *item) {
    if (!item) return;
    if (m_mode == Files) m_selected = item->data(Qt::UserRole).toString();
    else m_cmdId = item->data(Qt::UserRole).toInt();
    accept();
}

bool CommandPalette::eventFilter(QObject *o, QEvent *e) {
    if (o == m_edit && e->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(e);
        switch (ke->key()) {
            case Qt::Key_Up: case Qt::Key_Down:
            case Qt::Key_PageUp: case Qt::Key_PageDown:
                QApplication::sendEvent(m_list, ke);
                return true;
            default: break;
        }
    }
    return QDialog::eventFilter(o, e);
}

void CommandPalette::populate(const QString &filter) {
    m_list->clear();
    if (m_mode == Files) {
        QList<QPair<int, QString>> ranked;
        for (const QString &f : m_files) {
            int r = fuzzyRank(filter, QFileInfo(f).fileName());
            if (r >= 0) ranked.append({r, f});
        }
        std::sort(ranked.begin(), ranked.end());
        for (int i = 0; i < qMin(200, ranked.size()); ++i) {
            const QString &f = ranked[i].second;
            // 이름만 보이면 같은 이름의 파일을 구별할 수 없다 — 뒤에 폴더를 붙인다.
            QString dir = QFileInfo(f).absolutePath();
            if (!m_root.isEmpty() && dir.startsWith(m_root)) dir = dir.mid(m_root.size() + 1);
            auto *item = new QListWidgetItem(
                dir.isEmpty() ? QFileInfo(f).fileName()
                              : QFileInfo(f).fileName() + "   " + dir);
            item->setToolTip(f);
            item->setData(Qt::UserRole, f);
            m_list->addItem(item);
        }
    } else {
        QList<QPair<int, int>> ranked;
        for (int i = 0; i < m_cmds.size(); ++i) {
            int r = fuzzyRank(filter, m_cmds[i].first);
            if (r >= 0) ranked.append({r, i});
        }
        std::sort(ranked.begin(), ranked.end());
        for (int i = 0; i < qMin(200, ranked.size()); ++i) {
            int ci = ranked[i].second;
            auto *item = new QListWidgetItem(m_cmds[ci].first);
            item->setData(Qt::UserRole, m_cmds[ci].second);
            m_list->addItem(item);
        }
    }
    if (m_list->count() > 0) m_list->setCurrentRow(0);
}
