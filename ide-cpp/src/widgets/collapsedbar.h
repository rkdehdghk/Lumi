#pragma once
#include <QWidget>
#include <QPainter>
#include <QMouseEvent>
#include "../theme/theme.h"

// 세 패널(Explorer / INER / OUTER)이 세로 띠로 접혔을 때 쓰는 공통 막대.
// 패널마다 헤더를 흉내내 그리면 두께·색·화살표 크기가 계속 어긋난다 — 모양은 여기 한 곳에서만 정한다.
class CollapsedBar : public QWidget {
    Q_OBJECT
public:
    explicit CollapsedBar(const QString &label, QWidget *parent = nullptr)
        : QWidget(parent), m_label(label) {
        setCursor(Qt::PointingHandCursor);
        setToolTip(label + " — 클릭하면 펼칩니다");
    }

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent *) override {
        const Palette &p = currentPalette();
        QPainter g(this);
        g.setRenderHint(QPainter::Antialiasing, true);
        g.fillRect(rect(), p.tabbarBg);

        const QRect box = rect().adjusted(4, 5, -4, -5);
        if (m_hover) {
            g.setPen(Qt::NoPen);
            g.setBrush(p.btnHover);
            g.drawRoundedRect(box, 4, 4);
        }
        // 접힌 채로도 어느 창인지 알아보게 이름을 90도 눕혀 쓴다 (위 → 아래로 읽힌다).
        g.setBrush(Qt::NoBrush);
        g.setPen(m_hover ? p.accent : p.tabFgDim);
        QFont f = font();
        f.setBold(true);
        g.setFont(f);
        g.setClipRect(box);
        g.translate(width() / 2.0, height() / 2.0);
        g.rotate(90);
        g.drawText(QRect(-height() / 2, -width() / 2, height(), width()),
                   Qt::AlignCenter, m_label);
    }

    void mousePressEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton) emit clicked();
    }
    void enterEvent(QEnterEvent *) override { m_hover = true; update(); }
    void leaveEvent(QEvent *) override { m_hover = false; update(); }

private:
    QString m_label;
    bool m_hover = false;
};
