#pragma once
#include <QDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QStringList>
#include <QList>

class CommandPalette : public QDialog {
    Q_OBJECT
public:
    enum Mode { Files, Commands };
    explicit CommandPalette(Mode mode, QWidget *parent = nullptr);

    void setFileList(const QStringList &files, const QString &root = {}) {
        m_files = files; m_root = root; populate(m_edit->text());
    }
    QString selectedText() const { return m_selected; }
    int selectedCommand() const { return m_cmdId; }

protected:
    bool eventFilter(QObject *o, QEvent *e) override;

private:
    void populate(const QString &filter);
    void chooseItem(QListWidgetItem *item);
    Mode m_mode;
    QString m_root;
    QLineEdit *m_edit;
    QListWidget *m_list;
    QString m_selected;
    int m_cmdId = 0;

    QStringList m_files;
    QList<QPair<QString, int>> m_cmds;
};
