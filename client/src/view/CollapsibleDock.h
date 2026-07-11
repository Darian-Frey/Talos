// CollapsibleDock — a QDockWidget with a collapse/expand toggle in its title bar.
//
// Clicking the toggle collapses *this* dock (the content directly below the
// button) to just its title bar, giving the neighbouring docks the space —
// rather than the splitter-drag behaviour where resizing one dock also resizes
// its neighbour.

#pragma once

#include <QDockWidget>

class QToolButton;

class CollapsibleDock : public QDockWidget
{
    Q_OBJECT
public:
    CollapsibleDock(const QString &title, QWidget *content, QWidget *parent = nullptr);

private:
    void setCollapsed(bool collapsed);

    QToolButton *m_toggle = nullptr;
    QWidget *m_content = nullptr;
    bool m_collapsed = false;
    int m_expandedHeight = 0;   // remembered so expand restores a sane size
};
