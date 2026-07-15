#include "ScrollerWorkspace.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include "model/ScrollerCodegen.h"

ScrollerWorkspace::ScrollerWorkspace(QWidget *parent)
    : QWidget(parent)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(6, 6, 6, 6);

    auto *form = new QHBoxLayout;
    form->addWidget(new QLabel(QStringLiteral("Message"), this));
    m_message = new QLineEdit(QStringLiteral("TALOS STE HARDWARE SCROLLER... "), this);
    m_message->setToolTip(QStringLiteral(
        "A-Z 0-9 and . , ! ? ' - : + / (lowercase is uppercased; other glyphs -> space). "
        "This is rendered to a plane0 strip and scrolled by the STE shifter."));
    form->addWidget(m_message, 1);
    form->addWidget(new QLabel(QStringLiteral("Speed"), this));
    m_speed = new QSpinBox(this);
    m_speed->setRange(1, 8);
    m_speed->setValue(ScrollerCodegen::kDefaultSpeed);
    m_speed->setSuffix(QStringLiteral(" px/frame"));
    m_speed->setToolTip(QStringLiteral("STE fine-scroll advance per VBL (1 = smoothest)"));
    form->addWidget(m_speed);
    lay->addLayout(form);

    m_hint = new QLabel(this);
    m_hint->setWordWrap(true);
    lay->addWidget(m_hint);

    m_actions = new QWidget(this);
    auto *btns = new QHBoxLayout(m_actions);
    btns->setContentsMargins(0, 0, 0, 0);
    auto *build = new QPushButton(QStringLiteral("Build & Run"), m_actions);
    auto *verify = new QPushButton(QStringLiteral("Verify on Hatari"), m_actions);
    auto *import = new QPushButton(QStringLiteral("Import…"), m_actions);
    auto *xport = new QPushButton(QStringLiteral("Export…"), m_actions);
    build->setToolTip(QStringLiteral("Codegen -> vasm -> run the scroller on STE/PAL (F-212)"));
    verify->setToolTip(QStringLiteral("Run the scroller headless and confirm it scrolls smoothly"));
    import->setToolTip(QStringLiteral("Load a scroller sequence (scroller.json) back into the workspace"));
    xport->setToolTip(QStringLiteral("Write the .s stub, assembled .PRG and sequence to a folder"));
    btns->addWidget(build);
    btns->addWidget(verify);
    btns->addStretch();
    btns->addWidget(import);
    btns->addWidget(xport);
    lay->addWidget(m_actions);

    m_result = new QLabel(this);
    m_result->setWordWrap(true);
    lay->addWidget(m_result);

    connect(m_message, &QLineEdit::textChanged, this, [this] { refreshHint(); });
    connect(build, &QPushButton::clicked, this,
            [this] { emit buildRequested(message(), speed()); });
    connect(verify, &QPushButton::clicked, this,
            [this] { emit verifyRequested(message(), speed()); });
    connect(import, &QPushButton::clicked, this, [this] { emit importRequested(); });
    connect(xport, &QPushButton::clicked, this,
            [this] { emit exportRequested(message(), speed()); });

    refreshHint();
}

QString ScrollerWorkspace::message() const
{
    return m_message->text();
}

int ScrollerWorkspace::speed() const
{
    return m_speed->value();
}

void ScrollerWorkspace::loadFrom(const QString &message, int speed)
{
    m_message->setText(message);
    m_speed->setValue(qBound(1, speed, 8));
    refreshHint();
}

void ScrollerWorkspace::refreshHint()
{
    const QString msg = message();
    int unknown = 0;
    for (const QChar c : msg)
        if (c != ' ' && !ScrollerCodegen::isRenderable(c))
            ++unknown;
    const int cols = ScrollerCodegen::stripColumns(msg);
    QString h = QStringLiteral("%1 chars → %2-column strip (16px each)")
                    .arg(msg.length()).arg(cols);
    if (unknown)
        h += QStringLiteral("  ·  %1 unsupported char(s) render as blanks").arg(unknown);
    m_hint->setText(h);
}

void ScrollerWorkspace::setBusy(bool busy)
{
    m_actions->setEnabled(!busy);
}

void ScrollerWorkspace::setResult(const QString &text, bool ok)
{
    m_result->setStyleSheet(ok ? QStringLiteral("color:#2e7d32;")
                               : QStringLiteral("color:#c62828;"));
    m_result->setText(text);
}
