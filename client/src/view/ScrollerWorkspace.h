// ScrollerWorkspace — Phase 4 (F-212): author an STE hardware fine-scroll
// message scroller, then build/verify/export/import like the raster workspace.
//
// Just a message field + speed, and actions that emit requests; MainWindow does
// the codegen/assemble/run. STE-only effect (hardware scroll), so MainWindow
// gates the build on the machine's capability.

#pragma once

#include <QString>
#include <QWidget>

#include "model/ScrollerCodegen.h"

class QLineEdit;
class QSpinBox;
class QLabel;

class ScrollerWorkspace : public QWidget
{
    Q_OBJECT
public:
    explicit ScrollerWorkspace(QWidget *parent = nullptr);

    QString message() const;
    int speed() const;
    const ScrollerCodegen::Font &font() const { return m_font; }   // built-in or imported
    void loadFrom(const QString &message, int speed);   // register-sequence import

    void setBusy(bool busy);                 // disable actions during build/verify
    void setResult(const QString &text, bool ok);

signals:
    void buildRequested(const QString &message, int speed);
    void verifyRequested(const QString &message, int speed);
    void exportRequested(const QString &message, int speed);
    void importRequested();

private:
    void refreshHint();   // live length/validity readout
    void importFont();    // load a font-sheet image as the scroller font

    QLineEdit *m_message = nullptr;
    QSpinBox *m_speed = nullptr;
    QSpinBox *m_cellW = nullptr;
    QSpinBox *m_cellH = nullptr;
    QLineEdit *m_firstChar = nullptr;
    QLabel *m_fontLabel = nullptr;
    QLabel *m_hint = nullptr;
    QLabel *m_result = nullptr;
    QWidget *m_actions = nullptr;
    ScrollerCodegen::Font m_font = ScrollerCodegen::builtinFont();
};
