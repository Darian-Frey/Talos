// DisasmTracer — Phase 6: single-step a run of instructions from the current PC
// and record where each lands on the beam (live disassembly synced to the beam).
//
// Pure B1, no fork patch: break, redirect the debug console to a file (setstd),
// then loop { read regs (PC + beam counters), `console disasm` from PC, step }.
// Hatari computes the cycles; Talos only reads them (D-002). Afterwards the file
// holds every disassembled line; we map address→mnemonic and zip it with the beam
// positions captured per step. The disasm text is Hatari's own (never guessed).

#pragma once

#include <QObject>
#include <QString>
#include <QVector>

#include "model/WriteEvent.h"   // (beam fields mirror WriteEvent's meaning)

class RdbClient;

struct DisasmEntry
{
    quint32 pc = 0;
    QString text;          // Hatari's disassembly of the instruction at pc
    int scanline = 0;      // HBL — beam Y when this instruction is about to run
    int cycleInLine = 0;   // LineCycles — beam X
    quint32 frameCycle = 0;
    int cost = 0;          // cycles this instruction took (next.frameCycle - this)
};

class DisasmTracer : public QObject
{
    Q_OBJECT
public:
    explicit DisasmTracer(RdbClient *rdb, QObject *parent = nullptr);

    // Trace `count` instructions from the current PC; disasm text is redirected to
    // `outFile` (a scratch path the client can read back). Machine must be live.
    void start(int count, const QString &outFile);
    bool isRunning() const { return m_running; }
    const QVector<DisasmEntry> &entries() const { return m_entries; }

signals:
    void finished(bool ok, const QString &reason);

private:
    void iter();
    void finalize();

    RdbClient *m_rdb = nullptr;
    bool m_running = false;
    int m_count = 0;
    int m_i = 0;
    QString m_outFile;
    QVector<DisasmEntry> m_entries;
};
