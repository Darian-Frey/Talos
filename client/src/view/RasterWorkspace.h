// RasterWorkspace — Phase 4 (F-211): author a raster-bar effect, then build/verify.
//
// A small table of (scanline, colour) bars plus actions:
//   Build & Run  — codegen -> vasm -> relaunch Hatari with the effect (preview)
//   Verify       — run the exported stub through the round-trip harness
// The widget only edits the bar list and emits requests; MainWindow does the work.

#pragma once

#include "model/RasterCodegen.h"

#include <QVector>
#include <QWidget>

class QTableWidget;
class QLabel;
class QComboBox;

class RasterWorkspace : public QWidget
{
    Q_OBJECT
public:
    explicit RasterWorkspace(QWidget *parent = nullptr);

    enum Mode { Bars = 0, Bands = 1 };       // horizontal raster bars / vertical intra-line bands
    Mode mode() const;
    QVector<RasterCodegen::Bar> bars() const;
    QVector<quint16> colours() const;        // colour column in row order (Bands mode)
    QVector<RasterCodegen::Bar> columnBars() const;   // (column, colour) sorted (Bands mode)
    // Click-to-place from the framebuffer: `line` is a visible scanline index
    // (Bars), `column` is a framebuffer image column (Bands).
    void placeFromClick(int line, int column);
    void setBusy(bool busy);                 // disable actions during build/verify
    void setResult(const QString &text, bool ok);

    // Replace the table from an imported sequence: set the mode and rows
    // (each entry is scanline-or-column + colour). Used by register-sequence import.
    void loadEntries(Mode mode, const QVector<QPair<int, quint16>> &entries);

signals:
    void buildRequested(const QVector<RasterCodegen::Bar> &bars);
    void verifyRequested(const QVector<RasterCodegen::Bar> &bars);
    void exportRequested(const QVector<RasterCodegen::Bar> &bars);
    void importRequested();
    void contentChanged();   // the bar/band list or mode changed (for the budget gauge)

private:
    void addBar(int line, quint16 colour);
    void recolourRow(int row, int col);   // matches QTableWidget::cellChanged

    QComboBox *m_mode = nullptr;    // Bars / Bands
    QTableWidget *m_table = nullptr;
    QLabel *m_result = nullptr;
    QWidget *m_actions = nullptr;   // the button row (disabled while busy)
};
