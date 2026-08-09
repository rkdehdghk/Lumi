#include "tabbar.h"
#include "../theme/theme.h"
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QToolTip>
#include <QFontMetrics>

TabBar::TabBar(TabModel *model, QWidget *parent)
    : QWidget(parent), m_model(model) {
    setMouseTracking(true);
    setMinimumHeight(34);
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    connect(model, &TabModel::tabsChanged, this, [this]{ m_followActive = true; update(); });
}

// 탭이 그려질 수 있는 가로 범위 (INER 라벨 뒤 ~ Detach 버튼 앞).
int TabBar::stripLeft() const {
    QFont f = font();
    f.setBold(true);
    return 14 + QFontMetrics(f).horizontalAdvance("INER") + 22;
}
// 오른쪽 버튼 묶음(⧉ Detach / ▾)은 늘 오른쪽 끝에 붙어 있어야 한다 — 예전엔 폭을
// width()-104 처럼 못박아 놔서 패널이 좁아지면 탭이 버튼 위로 올라탔다.
// 좁아지면 Detach 는 글자를 떼고 아이콘만 남긴다.
bool TabBar::compactButtons() const { return width() < 340; }
int TabBar::detachWidth() const { return compactButtons() ? 26 : 66; }
int TabBar::buttonsLeft() const { return width() - 8 - 24 - 4 - detachWidth(); }
// 창을 아주 좁게 줄여도 폭이 뒤집히지 않도록 최소 40px 는 남긴다.
int TabBar::stripRight() const { return qMax(buttonsLeft() - 8, stripLeft() + 40); }

void TabBar::recompute() {
    m_rects.clear();
    const auto &tabs = m_model->tabs();
    QFontMetrics fm(font());
    m_detachRect = QRect(buttonsLeft(), 5, detachWidth(), height() - 10);
    m_panelCloseRect = QRect(width() - 32, 5, 24, height() - 10);

    const int left = stripLeft(), right = stripRight();
    std::vector<int> w;
    int total = 0;
    for (size_t i = 0; i < tabs.size(); ++i) {
        int tw = qMin(220, fm.horizontalAdvance(tabs[i].name) + 18 /*dirty*/ + 36 /*close*/);
        w.push_back(tw);
        total += tw;
    }
    const int overflow = qMax(0, total - (right - left));

    if (m_followActive) {
        int act = m_model->activeIndex();
        int x = 0;
        for (int i = 0; i < (int)w.size(); ++i) {
            if (i == act) {
                if (x < m_scroll) m_scroll = x;
                if (x + w[i] > m_scroll + (right - left)) m_scroll = x + w[i] - (right - left);
                break;
            }
            x += w[i];
        }
        m_followActive = false;
    }
    m_scroll = qBound(0, m_scroll, overflow);
    m_maxScroll = overflow;

    int x = left - m_scroll;
    for (size_t i = 0; i < w.size(); ++i) {
        m_rects.push_back(QRect(x, 0, w[i], height()));
        x += w[i];
    }
}

QRect TabBar::sliderHandleRect() const {
    if (m_maxScroll <= 0) return QRect();
    const int left = stripLeft(), right = stripRight(), visibleW = right - left;
    int handleW = qMax(30, visibleW * visibleW / (visibleW + m_maxScroll));
    int scrollableTrackW = qMax(1, visibleW - handleW);
    int handleX = left + (m_scroll * scrollableTrackW) / m_maxScroll;
    return QRect(handleX, height() - 5, handleW, 4);
}

QRect TabBar::closeRect(int i) const {
    const QRect &r = m_rects[i];
    return QRect(r.right() - 24, r.center().y() - 9, 18, 18);
}

void TabBar::paintEvent(QPaintEvent *) {
    recompute();
    const Palette &p = currentPalette();
    QPainter g(this);
    g.setRenderHint(QPainter::Antialiasing, true);

    g.fillRect(rect(), p.tabbarBg);

    QFontMetrics fm(font());

    // INER 라벨 (볼드 + 연한 텍스트 톤 p.textDim)
    g.save();
    QFont titleFont = font();
    titleFont.setBold(true);
    g.setFont(titleFont);
    g.setPen(p.textDim);
    QFontMetrics titleFm(titleFont);
    g.drawText(QRect(14, 0, titleFm.horizontalAdvance("INER") + 10, height()),
               Qt::AlignVCenter | Qt::AlignLeft, "INER");
    g.restore();

    if (m_model->count() == 0) {
        g.save();
        g.setPen(p.textDim);
        g.drawText(QRect(stripLeft(), 0, 100, height()),
                   Qt::AlignVCenter | Qt::AlignLeft, "No file");
        g.restore();
    }

    // 탭들 — Detach 버튼 자리를 침범하지 않도록 잘라 그린다.
    g.save();
    g.setClipRect(QRect(stripLeft(), 0, stripRight() - stripLeft(), height()));
    int active = m_model->activeIndex();
    const auto &tabs = m_model->tabs();
    for (int i = 0; i < (int)m_rects.size() && i < (int)tabs.size(); ++i) {
        const QRect &r = m_rects[i];
        if (r.right() < stripLeft() || r.left() > stripRight()) continue;
        bool act = (i == active);
        g.fillRect(r, act ? p.tabActive : p.tabInactive);

        // hover
        if (m_hoverTab == i && !act) g.fillRect(r, p.surfaceAlt);

        // 활성 탭 상단 액센트 선
        if (act) g.fillRect(QRect(r.left(), 0, r.width(), 2), p.accent);

        // 텍스트
        g.setPen(act ? p.tabFg : p.tabFgDim);
        QRect textRect = r.adjusted(10, 0, -30, 0);
        QString disp = fm.elidedText(tabs[i].name, Qt::ElideRight, textRect.width());
        g.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, disp);

        // 더러운 표시 ●
        int dx = textRect.left() + fm.horizontalAdvance(disp) + 4;
        if (tabs[i].modified) {
            g.setPen(p.accent);
            g.drawText(QRect(dx, 0, 14, height()), Qt::AlignVCenter, QString(QChar(0x25CF)));
        }

        // 닫기 × (호버 시 빨간색 배경 + 하얀색 글자로 명확하게 연출)
        QRect cr = closeRect(i);
        if (m_hoverClose == i) {
            g.setPen(Qt::NoPen);
            g.setBrush(QColor("#e53935"));
            g.drawRoundedRect(cr, 4, 4);
            g.setPen(QColor("#ffffff"));
        } else {
            g.setPen(p.tabFgDim);
        }
        g.drawText(cr, Qt::AlignCenter, QString(QChar(0x00D7)));
    }
    g.restore();

    // 탭 밑 슬라이더 (스크롤바) — 은은하고 연한 반투명 색상
    if (m_maxScroll > 0) {
        const int left = stripLeft(), right = stripRight();
        QRect trackRect(left, height() - 3, right - left, 2);
        QColor trackCol = p.textDim;
        trackCol.setAlpha(30);
        g.fillRect(trackRect, trackCol);

        QRect hRect = sliderHandleRect();
        hRect.setTop(height() - 4);
        hRect.setHeight(3);
        QColor handleCol = p.accent;
        handleCol.setAlpha((m_sliderDragging || m_hoverSlider) ? 160 : 80);
        g.fillRect(hRect, handleCol);
    }

    // detach 버튼 (HeaderToolBtn 과 통일된 라운드 4px 호버 + accent 하이라이트 연출)
    if (m_hoverDetach) {
        g.setPen(Qt::NoPen);
        g.setBrush(p.btnHover);
        g.drawRoundedRect(m_detachRect, 4, 4);
    }
    g.setPen(m_hoverDetach ? p.accent : p.tabFgDim);
    QFont f = font();
    f.setPointSize(f.pointSize() - 1);
    g.setFont(f);
    g.drawText(m_detachRect, Qt::AlignCenter,
               compactButtons() ? QString(QChar(0x29C9))
                                : QString(QChar(0x29C9)) + (m_detached ? " Dock" : " Detach"));

    // 패널 접기 ▾ / ▴ (Detach 버튼과 같은 호버 연출)
    if (m_hoverPanelClose) {
        g.setPen(Qt::NoPen);
        g.setBrush(p.btnHover);
        g.drawRoundedRect(m_panelCloseRect, 4, 4);
    }
    g.setBrush(Qt::NoBrush);
    g.setPen(m_hoverPanelClose ? p.accent : p.tabFgDim);
    // 화살표만 키운다 — 탭바 높이(34px)는 그대로.
    QFont cf = font();
    cf.setPointSize(cf.pointSize() + 6);
    cf.setBold(true);
    g.setFont(cf);
    g.drawText(m_panelCloseRect, Qt::AlignCenter,
               QString(QChar(m_collapsed ? 0x25B4 : 0x25BE)));   // ▴ / ▾
}

int TabBar::tabAtPos(const QPoint &pt) const {
    if (pt.x() < stripLeft() || pt.x() > stripRight()) return -1;
    for (int i = 0; i < (int)m_rects.size(); ++i)
        if (m_rects[i].contains(pt)) return i;
    return -1;
}

void TabBar::mousePressEvent(QMouseEvent *e) {
    QPoint pt = e->pos();
    if (e->button() == Qt::LeftButton && m_detachRect.contains(pt)) {
        emit detachClicked();
        return;
    }
    if (e->button() == Qt::LeftButton && m_panelCloseRect.contains(pt)) {
        emit panelCollapseClicked();
        return;
    }

    if (e->button() == Qt::LeftButton && m_maxScroll > 0 && pt.y() >= height() - 8) {
        m_sliderDragging = true;
        const int left = stripLeft(), right = stripRight(), visibleW = right - left;
        int handleW = qMax(30, visibleW * visibleW / (visibleW + m_maxScroll));
        int scrollableTrackW = qMax(1, visibleW - handleW);
        int targetX = pt.x() - left - handleW / 2;
        m_scroll = qBound(0, targetX * m_maxScroll / scrollableTrackW, m_maxScroll);
        update();
        return;
    }

    if (e->button() == Qt::LeftButton) {
        m_dragging = false;
        m_dragStartMouseX = pt.x();
        m_dragStartScroll = m_scroll;
    }
}

void TabBar::mouseReleaseEvent(QMouseEvent *e) {
    QPoint pt = e->pos();

    if (m_sliderDragging) {
        m_sliderDragging = false;
        update();
        return;
    }

    if (m_dragging) {
        m_dragging = false;
        return;   // 드래그 슬라이드 조작이었으므로 탭 클릭/닫기 동작은 건너뛴다.
    }

    int i = tabAtPos(pt);
    if (i < 0) return;
    if (e->button() == Qt::MiddleButton) { emit tabCloseRequested(i); return; }
    if (e->button() != Qt::LeftButton) return;
    if (closeRect(i).contains(pt)) { emit tabCloseRequested(i); return; }
    emit tabClicked(i);
}

void TabBar::mouseDoubleClickEvent(QMouseEvent *e) {
    if (e->button() == Qt::LeftButton && tabAtPos(e->pos()) < 0
        && !m_detachRect.contains(e->pos()) && !m_panelCloseRect.contains(e->pos()))
        emit newTabRequested();
}

void TabBar::wheelEvent(QWheelEvent *e) {
    recompute();
    if (m_maxScroll <= 0) { e->accept(); return; }
    int delta = e->angleDelta().y();
    if (delta == 0) delta = e->angleDelta().x();
    if (delta != 0) {
        int step = (delta > 0) ? -40 : 40;
        if (qAbs(delta) < 120) step = (delta > 0) ? -qAbs(delta) : qAbs(delta);
        m_scroll = qBound(0, m_scroll + step, m_maxScroll);
        update();
    }
    e->accept();
}

void TabBar::mouseMoveEvent(QMouseEvent *e) {
    QPoint pt = e->pos();

    if (m_sliderDragging && (e->buttons() & Qt::LeftButton) && m_maxScroll > 0) {
        const int left = stripLeft(), right = stripRight(), visibleW = right - left;
        int handleW = qMax(30, visibleW * visibleW / (visibleW + m_maxScroll));
        int scrollableTrackW = qMax(1, visibleW - handleW);
        int targetX = pt.x() - left - handleW / 2;
        m_scroll = qBound(0, targetX * m_maxScroll / scrollableTrackW, m_maxScroll);
        update();
        return;
    }

    // 마우스 왼쪽 버튼을 누른 채 좌우로 끌면 실시간 슬라이드 스크롤
    if ((e->buttons() & Qt::LeftButton) && m_maxScroll > 0) {
        int dx = pt.x() - m_dragStartMouseX;
        if (!m_dragging && qAbs(dx) > 4) {
            m_dragging = true;
        }
        if (m_dragging) {
            m_scroll = qBound(0, m_dragStartScroll - dx, m_maxScroll);
            update();
            return;
        }
    }

    int oldTab = m_hoverTab, oldClose = m_hoverClose;
    bool oldDetach = m_hoverDetach, oldSlider = m_hoverSlider;
    bool oldPanelClose = m_hoverPanelClose;
    m_hoverTab = tabAtPos(pt);
    m_hoverClose = (m_hoverTab >= 0 && closeRect(m_hoverTab).contains(pt)) ? m_hoverTab : -1;
    m_hoverDetach = m_detachRect.contains(pt);
    m_hoverPanelClose = m_panelCloseRect.contains(pt);
    m_hoverSlider = (m_maxScroll > 0 && pt.y() >= height() - 8);

    if (m_hoverTab != oldTab || m_hoverDetach != oldDetach) {
        // 좁아지면 Detach 는 아이콘만 남으니 무슨 버튼인지 툴팁으로 알려 준다.
        if (m_hoverDetach)
            setToolTip(m_detached ? "Dock" : "Detach");
        else if (Tab *t = m_model->tabAt(m_hoverTab))
            setToolTip(t->path.isEmpty() ? t->name : t->path);
        else
            setToolTip(QString());
    }
    if (oldTab != m_hoverTab || oldClose != m_hoverClose || oldDetach != m_hoverDetach
        || oldSlider != m_hoverSlider || oldPanelClose != m_hoverPanelClose)
        update();
}

void TabBar::leaveEvent(QEvent *) {
    m_hoverTab = -1; m_hoverClose = -1; m_hoverDetach = false; m_hoverSlider = false;
    m_hoverPanelClose = false;
    update();
}

void TabBar::contextMenuEvent(QContextMenuEvent *e) {
    int i = tabAtPos(e->pos());
    if (i >= 0) emit contextMenuRequested(i, e->globalPos());
}
