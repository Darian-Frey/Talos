#include "CollapsibleDock.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>

CollapsibleDock::CollapsibleDock(const QString &title, QWidget *content, QWidget *parent)
    : QDockWidget(title, parent)
    , m_content(content)
{
    setWidget(content);

    auto *bar = new QWidget(this);
    bar->setObjectName(QStringLiteral("dockTitleBar"));
    // A header look with top/bottom borders so adjacent panels are clearly
    // separated (theme-aware via palette roles).
    bar->setStyleSheet(QStringLiteral(
        "#dockTitleBar { background: palette(button);"
        " border-top: 1px solid palette(dark);"
        " border-bottom: 1px solid palette(dark); }"));
    auto *lay = new QHBoxLayout(bar);
    lay->setContentsMargins(4, 3, 4, 3);
    lay->setSpacing(6);

    m_toggle = new QToolButton(bar);
    m_toggle->setArrowType(Qt::DownArrow);
    m_toggle->setAutoRaise(true);
    m_toggle->setToolTip(QStringLiteral("Collapse / expand this panel"));
    connect(m_toggle, &QToolButton::clicked, this,
            [this] { setCollapsed(!m_collapsed); });
    lay->addWidget(m_toggle);

    auto *label = new QLabel(title, bar);
    label->setStyleSheet(QStringLiteral("font-weight:bold;"));
    lay->addWidget(label);
    lay->addStretch();

    setTitleBarWidget(bar);   // dragging the bar still moves/floats the dock
}

void CollapsibleDock::setCollapsed(bool collapsed)
{
    if (collapsed == m_collapsed)
        return;
    m_collapsed = collapsed;
    m_toggle->setArrowType(collapsed ? Qt::RightArrow : Qt::DownArrow);

    if (collapsed) {
        m_content->setVisible(false);
        setFixedHeight(titleBarWidget()->sizeHint().height());
    } else {
        m_content->setVisible(true);
        setMinimumHeight(0);
        setMaximumHeight(QWIDGETSIZE_MAX);
        // Height is restored by the content's own size policy: content-sized
        // panels return to their natural height, the register table refills
        // whatever the others leave. No manual resize() (ignored in a dock).
    }
}
