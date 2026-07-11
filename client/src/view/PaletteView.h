// PaletteView — the machine's colour palette, decoded (F-204/F-207, C-008).
//
// Shows the 16 live colour-register entries as swatches, plus a per-gun intensity
// ramp at the machine's resolution (8 levels on ST = 512 colours, 16 on STE =
// 4096) so the ST<->STE palette difference is legible at a glance.

#pragma once

#include <QColor>
#include <QVector>
#include <QWidget>

class PaletteView : public QWidget
{
    Q_OBJECT
public:
    explicit PaletteView(QWidget *parent = nullptr);

    void setPalette(const QVector<QColor> &colours, const QVector<quint16> &regs,
                    const QString &machineName, int colourCount, int bitsPerGun);

    QSize sizeHint() const override { return {240, 196}; }        // content height
    QSize minimumSizeHint() const override { return {200, 196}; }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QVector<QColor> m_colours;
    QVector<quint16> m_regs;
    QString m_machine;
    int m_colourCount = 512;
    int m_bits = 3;
};
