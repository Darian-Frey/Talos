// ReconstructView — F-218: a screen rebuilt *from the captured register writes*,
// shown beside Hatari's taken framebuffer, so you see *why* the picture looks as
// it does (and where the register field and reality diverge).
//
// Strictly secondary and additional — it never replaces the taken framebuffer
// (D-007). Talos does not emulate here: it folds the captured palette writes onto
// one frame by their beam position (BeamGeometry) and paints each pixel with the
// palette colour in effect there (Palette::decode). Meaningful for palette-
// register captures ($ff8240–$ff825e) — e.g. $ff8240 reconstructs the background
// colour field of a raster-bar / vertical-band effect.

#pragma once

#include <QImage>
#include <QVector>
#include <QWidget>

#include "model/WriteEvent.h"
#include "view/BeamGeometry.h"   // VideoRegion

class ReconstructView : public QWidget
{
public:
    explicit ReconstructView(QWidget *parent = nullptr);

    void setFrame(const QImage &frame);   // left panel: Hatari's taken frame
    void setReconstruction(const QVector<WriteEvent> &writes, VideoRegion region,
                           quint32 address);   // right panel: rebuilt from registers

    QSize sizeHint() const override { return {560, 260}; }
    QSize minimumSizeHint() const override { return {360, 180}; }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    void rebuild();   // (re)compute m_recon from m_writes + m_frame size

    QImage m_frame;
    QImage m_recon;
    QVector<WriteEvent> m_writes;
    VideoRegion m_region = VideoRegion::Pal50;
    quint32 m_address = 0xff8240;
    bool m_paletteReg = true;
    QString m_sig;    // signature of the last rebuild (skip redundant work)
};
