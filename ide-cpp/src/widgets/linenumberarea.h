#pragma once
#include <QWidget>

class CodeEditor;

class LineNumberArea : public QWidget {
    Q_OBJECT
public:
    explicit LineNumberArea(CodeEditor *editor);
    QSize sizeHint() const override;
protected:
    void paintEvent(QPaintEvent *) override;
private:
    CodeEditor *m_editor;
};
