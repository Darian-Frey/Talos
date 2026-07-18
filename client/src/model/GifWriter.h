// GifWriter — Phase 6: encode a sequence of frames to an animated GIF89a.
//
// Qt can read but not write animated GIFs, so this is a small self-contained
// encoder. It suits Talos because ST frames are palette-indexed (few colours): a
// global colour table is built from all frames (exact when the clip uses <=256
// colours; a nearest-colour reduction kicks in otherwise). Used by the live-view
// frame recorder to export a shareable clip of an effect.

#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>
#include <QVector>

class GifWriter
{
public:
    // Add one frame; delayCs is the on-screen time in 1/100 s (GIF's unit).
    void addFrame(const QImage &frame, int delayCs);
    int frameCount() const { return m_frames.size(); }
    QSize size() const { return m_frames.isEmpty() ? QSize() : m_frames.first().img.size(); }
    void clear() { m_frames.clear(); }

    // Encode + write the GIF (loops forever). false on I/O or empty input.
    bool write(const QString &path) const;

private:
    struct Frame { QImage img; int delayCs; };
    QVector<Frame> m_frames;
};
