#pragma once
#include <QWidget>
#include <QRect>
#include <vector>
#include "../models/tabmodel.h"

class TabBar : public QWidget {
    Q_OBJECT
public:
    explicit TabBar(TabModel *model, QWidget *parent = nullptr);

    // 분리 상태면 버튼 글자가 Detach → Dock 으로 바뀐다.
    void setDetached(bool on) { m_detached = on; update(); }
    void setCollapsed(bool on) { m_collapsed = on; update(); }

signals:
    void tabClicked(int index);
    void tabCloseRequested(int index);
    void contextMenuRequested(int index, QPoint pos);
    void detachClicked();    // detach 버튼 (idx == -2 의미)
    void panelCollapseClicked(); // 패널을 접는 ▾ 버튼
    void newTabRequested();  // 빈 자리 더블클릭

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void leaveEvent(QEvent *) override;
    void contextMenuEvent(QContextMenuEvent *) override;
    QSize sizeHint() const override { return {400, 34}; }
    QSize minimumSizeHint() const override { return {100, 34}; }

private:
    void recompute();
    int stripLeft() const;
    int stripRight() const;
    bool compactButtons() const;   // 좁으면 Detach 는 아이콘만
    int detachWidth() const;
    int buttonsLeft() const;       // 오른쪽 버튼 묶음이 시작하는 x
    QRect closeRect(int i) const;
    int tabAtPos(const QPoint &pt) const;
    QRect sliderHandleRect() const;

    TabModel *m_model;
    std::vector<QRect> m_rects;
    QRect m_detachRect;
    QRect m_panelCloseRect;
    int m_hoverTab = -1;
    int m_hoverClose = -1;
    bool m_hoverDetach = false;
    bool m_hoverPanelClose = false;
    bool m_collapsed = false;
    bool m_hoverSlider = false;
    bool m_detached = false;
    int m_scroll = 0;          // 탭이 넘칠 때 가로 스크롤 오프셋
    int m_maxScroll = 0;
    bool m_followActive = true;   // 다음 그리기에서 활성 탭을 보이게 맞춘다

    bool m_dragging = false;       // 마우스 드래그 슬라이드 스크롤 중
    int m_dragStartMouseX = 0;     // 드래그 시작 마우스 X 좌표
    int m_dragStartScroll = 0;     // 드래그 시작 시점 스크롤 값
    bool m_sliderDragging = false; // 슬라이더 노브 드래그 중
};
