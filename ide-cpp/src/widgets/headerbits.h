#pragma once
#include <QLabel>
#include <QResizeEvent>

// 패널 헤더 줄(Explorer / OUTER)이 폭에 맞춰 줄어들게 해 주는 두 조각.
//
// 예전엔 헤더 글자를 만들 때 한 번만 elidedText(..., 260) 으로 잘라 놨다. 그래서
// 창을 넓혀도 260px 짜리 그대로였고, 반대로 좁히면 라벨과 버튼의 최소폭 합이 헤더보다
// 커져 오른쪽 버튼(⧉ Detach / ▾)이 패널 밖으로 밀려나 잘렸다.
// 규칙: 헤더 왼쪽 것들은 자리가 모자라면 0 까지 양보하고, 오른쪽 버튼은 늘 자리를 지킨다.

// 폭에 맞춰 스스로 …로 줄이는 라벨. 최소폭은 0 이라 헤더를 넓히지 않는다.
class ElideLabel : public QLabel {
public:
    // 정책은 Preferred 그대로 둔다: 자리가 있으면 sizeHint(글자 전체 폭)를 요구하고,
    // 모자라면 minimumSizeHint(0)까지 줄어든다. Ignored 로 두면 반대로 늘 남는 자리를
    // 다 먹어 버려서 옆 칸이 밀린다.
    explicit ElideLabel(QWidget *parent = nullptr, Qt::TextElideMode mode = Qt::ElideRight)
        : QLabel(parent), m_mode(mode) {}

    void setFullText(const QString &t) { m_full = t; reelide(); }
    QString fullText() const { return m_full; }

    QSize minimumSizeHint() const override {
        return QSize(0, QLabel::minimumSizeHint().height());
    }
    QSize sizeHint() const override {
        return QSize(fontMetrics().horizontalAdvance(m_full) + 2,
                     QLabel::sizeHint().height());
    }

protected:
    void resizeEvent(QResizeEvent *e) override { QLabel::resizeEvent(e); reelide(); }

private:
    void reelide() {
        QLabel::setText(fontMetrics().elidedText(m_full, m_mode, qMax(0, width())));
    }
    QString m_full;
    Qt::TextElideMode m_mode;
};

// 헤더 왼쪽 묶음(제목 + 터미널 탭 + [+]). 자리가 있으면 제 크기를 요구하되,
// 좁아지면 0 까지 줄어들어 오른쪽 버튼을 밖으로 밀어내지 않는다.
class ShrinkBox : public QWidget {
public:
    using QWidget::QWidget;
    QSize minimumSizeHint() const override {
        return QSize(0, QWidget::minimumSizeHint().height());
    }
};
