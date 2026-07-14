#include "BeamGeometry.h"

// Constants from Hatari src/includes/video.h and the rendered-surface geometry in
// src/screen.c, confirmed 2026-07-10 (see docs/phase-1/beam-geometry.md):
//
//   surface (low-res, full overscan, status bar off) = 832 x 552
//     width  = (nBorderPixelsLeft 48 + display 320 + nBorderPixelsRight 48) * zoom 2
//     height = (OVERSCAN_TOP 29 + display 200 + MAX_OVERSCAN_BOTTOM 47)     * zoom 2
//   x = 0  <=>  cycle (LINE_START_CYCLE - nBorderPixelsLeft)
//   y = 0  <=>  scanline nFirstVisibleHbl
//   zoomX = zoomY = 2 (low-res doubling: nScreenZoomX/Y)
//
// Scope: ST low-res colour. The launcher pins this config, so the mapping is
// exact for it. Med/high res and STE fine-scroll shifts are later phases.

BeamGeometry::BeamGeometry(VideoRegion region, QSize framebufferSize)
    : m_region(region)
    , m_fb(framebufferSize)
{
    // High-res mono (video.h *_71HZ / *_MONO) has a distinct 640x400 surface with
    // no overscan borders and its own timing; the taken frame's height (400 vs the
    // colour 552) disambiguates it. Colour low- and medium-res share one geometry:
    // med renders 640 display px at zoom 1 exactly where low renders 320 at zoom 2,
    // so the (cycle,scanline)->surface-pixel mapping is identical (BUG-003).
    if (framebufferSize.height() > 0 && framebufferSize.height() <= 450) {
        m_cyclesPerLine = 224;      // CYCLES_PER_LINE_71HZ
        m_scanlinesPerFrame = 501;  // SCANLINES_PER_FRAME_71HZ
        m_firstVisibleHbl = 34;     // FIRST_VISIBLE_HBL_71HZ
        m_firstVisibleCycle = 0;    // LINE_START_CYCLE_71 (no left border)
        m_zoomX = 4.0;              // 640 display px / 160 display cycles
        m_zoomY = 1.0;              // no line doubling
        return;
    }
    if (region == VideoRegion::Pal50) {
        m_cyclesPerLine = 512;      // CYCLES_PER_LINE_50HZ
        m_scanlinesPerFrame = 313;  // SCANLINES_PER_FRAME_50HZ
        m_firstVisibleHbl = 34;     // FIRST_VISIBLE_HBL_50HZ
        m_firstVisibleCycle = 8;    // LINE_START_CYCLE_50 (56) - left border (48)
    } else {
        m_cyclesPerLine = 508;      // CYCLES_PER_LINE_60HZ
        m_scanlinesPerFrame = 263;  // SCANLINES_PER_FRAME_60HZ
        m_firstVisibleHbl = 5;      // FIRST_VISIBLE_HBL_60HZ (34 - 29)
        m_firstVisibleCycle = 4;    // LINE_START_CYCLE_60 (52) - left border (48)
    }
    m_zoomX = 2.0;
    m_zoomY = 2.0;
}

BeamMapping BeamGeometry::map(int scanline, int cycleInLine) const
{
    BeamMapping m;
    m.x = (cycleInLine - m_firstVisibleCycle) * m_zoomX;
    m.y = (scanline - m_firstVisibleHbl) * m_zoomY;
    m.xVisible = (m.x >= 0.0 && m.x < m_fb.width());
    m.yVisible = (m.y >= 0.0 && m.y < m_fb.height());
    return m;
}

std::optional<QPointF> BeamGeometry::toPixel(int scanline, int cycleInLine) const
{
    const BeamMapping m = map(scanline, cycleInLine);
    if (!m.xVisible || !m.yVisible)
        return std::nullopt;
    return QPointF(m.x, m.y);
}
