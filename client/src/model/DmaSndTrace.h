// DmaSndTrace — decode the B2 `dmatrace` dump into DMA-sound drain + LMC1992 EQ.
//
// Wire form (protocol/b1-protocol.md, F-209 tap): the reply is OK <count> then,
// per entry, five hex tokens: cycle_hi cycle_lo kind a b. Kinds (dmaSnd.h):
//   FRAME(start,end)  DRAIN(headAddr,0)  CTRL(control,mode)  LMC(param,value).

#pragma once

#include <QByteArray>
#include <QList>
#include <QPair>
#include <QVector>

struct DmaSndTrace {
    struct Drain {
        quint64 cycle = 0;
        quint32 addr = 0;   // frame counter (play head) at this sample
    };

    QVector<Drain> drain;             // play-head positions over time (~1/HBL)
    quint32 bufStart = 0, bufEnd = 0; // last sample-buffer bounds (from FRAME)
    int frames = 0;                   // FRAME events (buffer (re)starts / loops)

    bool haveCtrl = false;
    quint16 control = 0;              // DMASNDCTRL: bit0 play, bit1 loop
    quint16 mode = 0;                 // bits0-1 freq index, bit7 mono

    // Latest value per LMC1992 param (0 mix,1 bass,2 treble,3 master,4 R,5 L);
    // -1 = not seen in this capture.
    int lmc[6] = {-1, -1, -1, -1, -1, -1};
    QVector<QPair<int, int>> lmcSeq;  // (param, value) in order — the EQ history

    quint64 cycMin = 0, cycMax = 0;

    bool playing() const { return control & 0x1; }
    bool looping() const { return control & 0x2; }
    bool mono() const { return mode & 0x80; }
    int freqIndex() const { return mode & 0x3; }

    static DmaSndTrace parse(const QList<QByteArray> &reply);
};
