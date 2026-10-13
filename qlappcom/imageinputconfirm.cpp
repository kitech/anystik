#include "imageinputconfirm.h"
#include "translator.h"
#ifdef QT3_BUILD
#include <qlabel.h>
#include <qpixmap.h>
#include <qlayout.h>
#include <qpushbutton.h>
#include <qstringlist.h>
#else
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSize>
#include <QStringList>
#endif

ImageInputConfirmDialog::ImageInputConfirmDialog(
    const QImage& img, const QString& path,
    const QString& sizeStr, const QString& source, QWidget* parent)
    : QDialog(parent), m_source(source) {
    qSetWindowTitle(this, _("paste_image.title"));

    QVBoxLayout* root = new QVBoxLayout(this);
    qSetLayoutMargin(root, 8);
    root->setSpacing(6);

    if (!m_source.isEmpty()) {
        QLabel* src = new QLabel(_A("paste_image.source", QStringList() << m_source), this);
#ifdef QT3_BUILD
        src->setPaletteForegroundColor(QColor(0x70, 0x70, 0x70));
#else
        src->setStyleSheet("color:#707070");
#endif
        root->addWidget(src);
    }

    if (!path.isEmpty()) {
        QString confirmText = sizeStr.isEmpty()
            ? path
            : _A("paste_image.confirm", QStringList() << path << sizeStr);
        QLabel* confirm = new QLabel(confirmText, this);
#ifdef QT3_BUILD
        confirm->setPaletteForegroundColor(QColor(0x70, 0x70, 0x70));
#else
        confirm->setStyleSheet("color:#707070");
        confirm->setWordWrap(true);
#endif
        root->addWidget(confirm);
    }

    QLabel* thumb = new QLabel(this);
    thumb->setFixedSize(200, 200);
    thumb->setAlignment(Qt::AlignCenter);
    int dw = img.width();
    int dh = img.height();
    int tw = dw, th = dh;
    if (dw > 200 && dw >= dh) { th = dh * 200 / dw; tw = 200; }
    else if (dh > 200)        { tw = dw * 200 / dh; th = 200; }
    QPixmap pm;
#ifdef QT3_BUILD
    QImage scaled = (tw < dw) ? img.smoothScale(tw, th, QImage::ScaleMin) : img;
    pm.convertFromImage(scaled);
#else
    pm = (tw < dw)
         ? QPixmap::fromImage(img.scaled(QSize(tw, th), Qt::KeepAspectRatio, Qt::SmoothTransformation))
         : QPixmap::fromImage(img);
#endif
    thumb->setPixmap(pm);
    root->addWidget(thumb, 0, Qt::AlignCenter);

    QLabel* res = new QLabel(
        _A("paste_image.resolution", QStringList() << QString::number(dw)
                                                  << QString::number(dh)), this);
    res->setAlignment(Qt::AlignCenter);
    root->addWidget(res);

    QLabel* cap = new QLabel(_("paste_image.caption"), this);
    root->addWidget(cap);
    m_captionEdit = new QLineEdit(this);
    connect(m_captionEdit, SIGNAL(returnPressed()), this, SLOT(accept()));
    root->addWidget(m_captionEdit);

    QHBoxLayout* btns = new QHBoxLayout;
    btns->addStretch(1);
    QPushButton* ok = new QPushButton(_("buttons.send"), this);
    connect(ok, SIGNAL(clicked()), this, SLOT(accept()));
    QPushButton* cancel = new QPushButton(_("buttons.cancel"), this);
    connect(cancel, SIGNAL(clicked()), this, SLOT(reject()));
    btns->addWidget(ok);
    btns->addWidget(cancel);
    btns->addStretch(1);
    root->addLayout(btns);
}

QString ImageInputConfirmDialog::caption() const {
    QString s = m_captionEdit->text();
#ifdef QT3_BUILD
    return s.stripWhiteSpace();
#else
    return s.trimmed();
#endif
}