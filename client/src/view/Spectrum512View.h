// Spectrum512View — Phase 4 (F-211): import and *visualise* a Spectrum 512 (.SPU)
// picture. Talos decodes the file to an exact image (no hardware reproduction —
// see model/Spectrum512.h) and makes the per-scanline palette storm visible: the
// 48 colours a line uses and where each of the 16 registers switches across the
// beam. That is the Talos-shaped half of "Spectrum 512": show the timing.

#pragma once

#include <QWidget>

#include "model/Spectrum512.h"

class QLabel;
class QSpinBox;
class QPushButton;

// Paints the palette storm for one scanline: the 3x16 colour sets, the resolved
// 320px line, and tick marks where each register flips set1->set2 (and +160 to
// set3). No Q_OBJECT — it only paints.
class StormStrip : public QWidget
{
public:
    explicit StormStrip(QWidget *parent = nullptr);
    void setLine(const Spectrum512::Image *img, int screenLine);
    QSize sizeHint() const override { return {660, 132}; }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    const Spectrum512::Image *m_img = nullptr;
    int m_line = 100;
};

class Spectrum512View : public QWidget
{
    Q_OBJECT
public:
    explicit Spectrum512View(QWidget *parent = nullptr);

protected:
    void resizeEvent(QResizeEvent *) override;   // rescale the picture to fit

private:
    void importSpu();                                      // load a real .SPU/.SPC
    void importImage();                                    // load any image, convert to S512
    void exportSpu();                                      // save the current image as .SPU
    void presentImage(const QString &sourceName);          // show m_img (shared)
    void setLine(int line);
    void updatePicture();                                  // scale image to m_picture
    bool eventFilter(QObject *obj, QEvent *ev) override;   // click the picture

    Spectrum512::Image m_img;
    QLabel *m_picture = nullptr;   // decoded image (scaled to fit), click selects a line
    QSpinBox *m_lineSpin = nullptr;
    StormStrip *m_storm = nullptr;
    QLabel *m_info = nullptr;
    QLabel *m_lineInfo = nullptr;
    QPushButton *m_exportBtn = nullptr;
};
