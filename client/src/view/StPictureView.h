// StPictureView — Phase 6: view common Atari ST pictures (DEGAS, NEOchrome).
//
// Import a .PI1-3 / .PC1-3 / .NEO, decode it (model/StPicture), and show the
// picture (scaled to fit) plus its palette. A plain viewer — one palette per
// screen, unlike the Spectrum 512 tab's per-scanline palette storm.

#pragma once

#include <QWidget>

#include "model/StPicture.h"

class QLabel;

class StPictureView : public QWidget
{
    Q_OBJECT
public:
    explicit StPictureView(QWidget *parent = nullptr);

protected:
    void resizeEvent(QResizeEvent *) override;

private:
    void importPicture();
    void updatePicture();

    StPicture::Image m_img;
    QLabel *m_picture = nullptr;   // decoded image, scaled to fit
    QLabel *m_palette = nullptr;   // palette swatches
    QLabel *m_info = nullptr;
};
