#ifndef IMAGEINPUTCONFIRM_H
#define IMAGEINPUTCONFIRM_H

#include "compat34.h"
#ifdef QT3_BUILD
#include <qdialog.h>
#include <qlineedit.h>
#include <qimage.h>
#else
#include <QDialog>
#include <QLineEdit>
#include <QImage>
#endif

class ImageInputConfirmDialog : public QDialog {
public:
    ImageInputConfirmDialog(const QImage& img, const QString& path,
                            const QString& sizeStr, const QString& source,
                            QWidget* parent = 0);
    QString caption() const;

private:
    QLineEdit* m_captionEdit;
    QString m_source;
};

#endif