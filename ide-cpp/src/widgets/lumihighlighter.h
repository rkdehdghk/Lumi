#pragma once
#include <QSyntaxHighlighter>

class LumiHighlighter : public QSyntaxHighlighter {
    Q_OBJECT
public:
    explicit LumiHighlighter(QTextDocument *doc);
    void refreshTheme();   // 테마 변경 시 rehighlight
protected:
    void highlightBlock(const QString &text) override;
};
