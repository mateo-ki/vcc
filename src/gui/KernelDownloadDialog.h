#pragma once

#include <QButtonGroup>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QString>
#include <QVBoxLayout>

#include "kernel/KernelSource.h"
#include "kernel/KernelTypes.h"

namespace Gui {

class KernelDownloadDialog : public QDialog {
    Q_OBJECT

public:
    KernelDownloadDialog(Kernel::Kind kind,
                         Kernel::Source defaultSource,
                         bool proxyAvailable,
                         QWidget *parent = nullptr);

    Kernel::Source selectedSource() const;
    QString localFilePath() const;

protected:
    bool eventFilter(QObject *obj, QEvent *ev) override;

private:
    void updateUiState();

    Kernel::Kind kind_;
    bool proxyAvailable_ = false;
    QButtonGroup *sourceGroup_ = nullptr;
    QLineEdit *localPathEdit_ = nullptr;
    QPushButton *browseBtn_ = nullptr;
    QWidget *localFileContainer_ = nullptr;
    QLabel *warningLabel_ = nullptr;
    QPushButton *startBtn_ = nullptr;
    QPushButton *cancelBtn_ = nullptr;
};

} // namespace Gui
