// LedToolButton — a checkable toolbar button with a retro rectangular LED that
// lights red when checked. Used for the "Fast boot" toggle so its state stands
// out at a glance.
//
// QToolButton centres its text in the whole button, which would collide with a
// right-hand LED, so we draw the frame via the style but render the text
// left-aligned ourselves and place the LED to its right with a fixed gap.
// No Q_OBJECT: it only overrides painting/sizing, adds no signals.

#pragma once

#include <QLinearGradient>
#include <QStyleOptionToolButton>
#include <QStylePainter>
#include <QToolButton>

class LedToolButton : public QToolButton
{
public:
    explicit LedToolButton(QWidget *parent = nullptr)
        : QToolButton(parent)
    {
        setCheckable(true);
        setAutoRaise(true);
        setToolButtonStyle(Qt::ToolButtonTextOnly);
    }

protected:
    QSize sizeHint() const override
    {
        const int tw = fontMetrics().horizontalAdvance(text());
        return QSize(kPad + tw + kGap + kLedW + kPad,
                     QToolButton::sizeHint().height());
    }

    void paintEvent(QPaintEvent *) override
    {
        QStylePainter sp(this);

        // 1. Button frame/background only (raised / sunken-when-checked), no text.
        QStyleOptionToolButton opt;
        initStyleOption(&opt);
        const QString label = opt.text;
        opt.text.clear();
        opt.icon = QIcon();
        sp.drawComplexControl(QStyle::CC_ToolButton, opt);

        // 2. Left-aligned text.
        const QRect tr = rect().adjusted(kPad, 0, -(kGap + kLedW + kPad), 0);
        sp.setPen(palette().color(isEnabled() ? QPalette::Active : QPalette::Disabled,
                                  QPalette::ButtonText));
        sp.drawText(tr, Qt::AlignVCenter | Qt::AlignLeft, label);

        // 3. The LED, to the right of the text.
        sp.setRenderHint(QPainter::Antialiasing, true);
        sp.setPen(Qt::NoPen);
        const double h = 7.0;
        const QRectF led(width() - kLedW - kPad, (height() - h) / 2.0, kLedW, h);

        sp.setBrush(QColor(10, 10, 12));                     // recessed bezel
        sp.drawRoundedRect(led.adjusted(-1.5, -1.5, 1.5, 1.5), 2.5, 2.5);

        if (isChecked()) {
            sp.setBrush(QColor(255, 40, 30, 70));            // soft glow
            sp.drawRoundedRect(led.adjusted(-2, -2, 2, 2), 2.5, 2.5);
            QLinearGradient g(led.topLeft(), led.bottomLeft());
            g.setColorAt(0.0, QColor(255, 150, 140));
            g.setColorAt(0.45, QColor(255, 45, 32));
            g.setColorAt(1.0, QColor(150, 0, 0));
            sp.setBrush(g);
            sp.drawRoundedRect(led, 1.5, 1.5);
            sp.setBrush(QColor(255, 225, 215, 200));         // specular highlight
            sp.drawRoundedRect(QRectF(led.left() + 1.5, led.top() + 1.2,
                                      kLedW * 0.4, h * 0.35), 1.0, 1.0);
        } else {
            sp.setBrush(QColor(70, 16, 16));                 // unlit: dim dark red
            sp.drawRoundedRect(led, 1.5, 1.5);
        }
    }

private:
    static constexpr int kPad = 10;    // left/right margin
    static constexpr int kGap = 12;    // gap between text and LED
    static constexpr int kLedW = 13;   // LED width
};
