// BeamGeometry — maps ST video (scanline, cycle-in-line) to a pixel in the taken
// framebuffer, so the beam overlay registers exactly to Hatari's rendered frame.
//
// Timing constants are sourced from Hatari's src/includes/video.h (C-007), never
// guessed. The horizontal left-offset and zoom are confirmed against the actual
// rendered surface geometry (see docs/phase-1/beam-geometry.md).
//
// Scope: ST low-resolution colour, PAL 50 Hz and NTSC 60 Hz. Other resolutions
// and STE fine-scroll shifts come with later phases.

#pragma once

#include <QPointF>
#include <QSize>
#include <optional>

enum class VideoRegion { Pal50, Ntsc60 };

// Where the beam maps in the taken frame. Split axes so the overlay can show the
// scanline even when the cycle is in horizontal blanking, and flag vertical
// blanking (when the beam is above/below the rendered rows — e.g. after a break,
// which stops at the top of the frame).
struct BeamMapping
{
    bool yVisible = false;   // scanline within the rendered rows
    bool xVisible = false;   // cycle within the rendered columns
    double x = 0.0;          // image px (meaningful when xVisible)
    double y = 0.0;          // image px (meaningful when yVisible)
};

class BeamGeometry
{
public:
    // framebufferSize is the size of the taken frame (QImage from the screenshot).
    BeamGeometry(VideoRegion region, QSize framebufferSize);

    // scanline: absolute line from the top of the frame (Hatari nHBL / "HBL").
    // cycleInLine: 0..cyclesPerLine (Hatari LineCycles).
    BeamMapping map(int scanline, int cycleInLine) const;

    // Convenience: the pixel when the beam is fully on-frame, else nullopt.
    std::optional<QPointF> toPixel(int scanline, int cycleInLine) const;

    // Inverse (for click-to-place authoring): an image pixel -> approximate
    // absolute scanline / cycle-in-line. Not sub-cycle exact — for authoring.
    int scanlineAtY(double imageY) const {
        return static_cast<int>(imageY / m_zoomY) + m_firstVisibleHbl;
    }
    int cycleAtX(double imageX) const {
        return static_cast<int>(imageX / m_zoomX) + m_firstVisibleCycle;
    }
    int firstVisibleHbl() const { return m_firstVisibleHbl; }

    VideoRegion region() const { return m_region; }
    int cyclesPerLine() const { return m_cyclesPerLine; }
    int scanlinesPerFrame() const { return m_scanlinesPerFrame; }

private:
    VideoRegion m_region;
    QSize m_fb;

    int m_cyclesPerLine;       // 512 (PAL) / 508 (NTSC)
    int m_scanlinesPerFrame;   // 313 (PAL) / 263 (NTSC)
    int m_firstVisibleHbl;     // scanline mapped to image row 0 (pre-zoom)
    int m_firstVisibleCycle;   // cycle mapped to image column 0 (pre-zoom)
    double m_zoomX;            // image px per video cycle
    double m_zoomY;            // image px per scanline
};
