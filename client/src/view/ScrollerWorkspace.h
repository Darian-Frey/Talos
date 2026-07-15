// ScrollerWorkspace — Phase 4 (F-212): author an STE hardware fine-scroll
// message scroller, then build/verify/export/import like the raster workspace.
//
// Just a message field + speed, and actions that emit requests; MainWindow does
// the codegen/assemble/run. STE-only effect (hardware scroll), so MainWindow
// gates the build on the machine's capability.

#pragma once

#include <QString>
#include <QWidget>

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

    QLineEdit *m_message = nullptr;
    QSpinBox *m_speed = nullptr;
    QLabel *m_hint = nullptr;
    QLabel *m_result = nullptr;
    QWidget *m_actions = nullptr;
};
