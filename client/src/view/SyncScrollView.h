// SyncScrollView — Phase 6: a guided view of the STF "sync scroll" trick.
//
// The plain ST has no fine-scroll register; ST Connexion's answer is three
// $ffff8260 resolution switches at the start of a line (hi → med → lo), where the
// exact cycle of the low-res switch shifts the scanline right. Pick a shift and
// see the switch sequence on a zoomed line-start timeline, the cycle→pixel table,
// and a before/after illustration. Every figure is sourced from Hatari (C-007).
// Teaching-only: the trick needs an exact per-cycle landing that Talos documents
// rather than reproduces (no bench-proven stub exists, unlike the left border).

#pragma once

#include <QWidget>

#include "model/SyncScroll.h"

class QLabel;
class QPushButton;

// The diagram: a zoomed scanline-start timeline (the three switches) above a
// before/after strip showing the resulting right shift. No Q_OBJECT (just paints).
class SyncScrollDiagram : public QWidget
{
public:
    explicit SyncScrollDiagram(QWidget *parent = nullptr);
    void setStep(const SyncScroll::Step &s);
    QSize sizeHint() const override { return {520, 260}; }
    QSize minimumSizeHint() const override { return {360, 200}; }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    SyncScroll::Step m_step{20, 13, {}};
};

class SyncScrollView : public QWidget
{
    Q_OBJECT
public:
    explicit SyncScrollView(QWidget *parent = nullptr);

private:
    void selectStep(int index);

    QVector<SyncScroll::Step> m_steps;
    int m_index = 1;                       // default: cycle 20 → 13 px (the headline shift)
    QVector<QPushButton *> m_btn;
    SyncScrollDiagram *m_diagram = nullptr;
    QLabel *m_facts = nullptr;
};
