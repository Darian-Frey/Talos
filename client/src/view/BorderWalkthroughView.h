// BorderWalkthroughView — Phase 6: a guided tour of the four ST screen borders.
//
// Pick a border and see, on one 2-D screen diagram (X = cycles across a line,
// Y = lines down the frame): the normal display rectangle, the region the trick
// opens, and where/when the sync/resolution write must land. Every figure is
// sourced from Hatari (BorderCodegen, C-007). The left border is runnable —
// Build & Run opens it live, Verify confirms it via the border-check harness.
// This finishes M1's story: the border mechanism made legible, not just overlaid.

#pragma once

#include <QWidget>

#include "model/BorderCodegen.h"

class QLabel;
class QPushButton;

// The 2-D screen diagram — paints the display rect + the selected border's
// opened region + the switch marker. No Q_OBJECT (it only paints).
class BorderDiagram : public QWidget
{
public:
    explicit BorderDiagram(QWidget *parent = nullptr);
    void setBorder(BorderCodegen::Border b);
    QSize sizeHint() const override { return {520, 300}; }
    QSize minimumSizeHint() const override { return {360, 220}; }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    BorderCodegen::Border m_border = BorderCodegen::Border::Left;
};

class BorderWalkthroughView : public QWidget
{
    Q_OBJECT
public:
    explicit BorderWalkthroughView(QWidget *parent = nullptr);

    BorderCodegen::Border currentBorder() const { return m_border; }
    void setBusy(bool busy);
    void setResult(const QString &text, bool ok);

signals:
    void buildRequested(BorderCodegen::Border border);   // left only (runnable)
    void verifyRequested(BorderCodegen::Border border);

private:
    void selectBorder(BorderCodegen::Border b);

    BorderCodegen::Border m_border = BorderCodegen::Border::Left;
    QPushButton *m_btn[4] = {nullptr, nullptr, nullptr, nullptr};
    BorderDiagram *m_diagram = nullptr;
    QLabel *m_facts = nullptr;
    QLabel *m_result = nullptr;
    QPushButton *m_build = nullptr;
    QPushButton *m_verify = nullptr;
    QWidget *m_actions = nullptr;
};
