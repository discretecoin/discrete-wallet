// Copyright (c) 2016 The Karbovanets developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QClipboard>
#include <QFileDialog>
#include <QMimeData>
#include <QTextStream>
#include <QTimer>
#include <QUrl>
#include "MainWindow.h"
#include "QRCodeDialog.h"
#include "ShowPaymentRequestDialog.h"

#include "ui_showpaymentrequest.h"

namespace WalletGui {

ShowPaymentRequestDialog::ShowPaymentRequestDialog(QWidget* _parent) : QDialog(_parent), m_ui(new Ui::ShowPaymentRequestDialog) {
  m_ui->setupUi(this);
  connect(m_ui->m_showQrCodeButton, &QPushButton::clicked,
          this, &ShowPaymentRequestDialog::showQrCode);
  connect(m_ui->m_paymentRequestUriText, &QTextBrowser::anchorClicked,
          this, [this](const QUrl&) { openPaymentRequest(); });
}

ShowPaymentRequestDialog::~ShowPaymentRequestDialog() {
}

void ShowPaymentRequestDialog::setData(const QString& _paymentRequest,
                                       bool _qrAvailable,
                                       const QString& _recipientStatus) {
  payment_request_uri = _paymentRequest;
  const QString escapedPaymentRequest = _paymentRequest.toHtmlEscaped();
  m_ui->m_paymentRequestUriText->setHtml(
    QStringLiteral("<a href=\"%1\">%2</a>")
      .arg(escapedPaymentRequest, escapedPaymentRequest));
  m_ui->m_recipientStatusLabel->setText(_recipientStatus);
  m_ui->m_showQrCodeButton->setVisible(_qrAvailable);

  if (_qrAvailable) {
    const int compactTextHeight = qMax(44,
      m_ui->m_paymentRequestUriText->fontMetrics().lineSpacing() + 24);
    m_ui->m_paymentRequestUriText->setLineWrapMode(QTextEdit::NoWrap);
    m_ui->m_paymentRequestUriText->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_ui->m_paymentRequestUriText->setMinimumHeight(compactTextHeight);
    m_ui->m_paymentRequestUriText->setMaximumHeight(compactTextHeight);
    m_ui->m_paymentRequestContent->setMinimumHeight(0);
    setMinimumSize(560, 0);
    resize(620, 220);
  } else {
    m_ui->m_paymentRequestUriText->setLineWrapMode(QTextEdit::WidgetWidth);
    m_ui->m_paymentRequestUriText->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_ui->m_paymentRequestUriText->setMinimumHeight(150);
    m_ui->m_paymentRequestUriText->setMaximumHeight(QWIDGETSIZE_MAX);
    m_ui->m_paymentRequestContent->setMinimumHeight(210);
    setMinimumSize(500, 300);
    resize(620, 380);
  }
}

void ShowPaymentRequestDialog::copyUri() {
  QMimeData* mimeData = new QMimeData();
  mimeData->setText(payment_request_uri);
  const QString escapedPaymentRequest = payment_request_uri.toHtmlEscaped();
  mimeData->setHtml(QStringLiteral("<a href=\"%1\">%2</a>")
    .arg(escapedPaymentRequest, escapedPaymentRequest));
  mimeData->setUrls({QUrl(payment_request_uri)});
  QApplication::clipboard()->setMimeData(mimeData);
}

void ShowPaymentRequestDialog::saveUri() {
 QString file = QFileDialog::getSaveFileName(&MainWindow::instance(), tr("Save as"), QDir::homePath(), "TXT (*.txt)");
   if (!file.isEmpty()) {
     QFile f(file);
      if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QTextStream outputStream(&f);
        outputStream << payment_request_uri;
        f.close();
      }
   }
}

void ShowPaymentRequestDialog::showQrCode() {
  if (payment_request_uri.isEmpty() || !m_ui->m_showQrCodeButton->isVisible()) {
    return;
  }

  QRCodeDialog dialog(tr("Payment request"), payment_request_uri, this);
  dialog.exec();
}

void ShowPaymentRequestDialog::openPaymentRequest() {
  if (payment_request_uri.isEmpty()) {
    return;
  }

  const QString request = payment_request_uri;
  accept();
  QTimer::singleShot(0, &MainWindow::instance(), [request]() {
    MainWindow::instance().handlePaymentRequest(request);
  });
}

}
