// Copyright (c) 2016 The Karbovanets developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QClipboard>
#include <QFileDialog>
#include <QTextStream>
#include "MainWindow.h"
#include "ShowPaymentRequestDialog.h"

#include "ui_showpaymentrequest.h"

namespace WalletGui {

ShowPaymentRequestDialog::ShowPaymentRequestDialog(QWidget* _parent) : QDialog(_parent), m_ui(new Ui::ShowPaymentRequestDialog) {
  m_ui->setupUi(this);
}

ShowPaymentRequestDialog::~ShowPaymentRequestDialog() {
}

void ShowPaymentRequestDialog::setData(const QString &paymentRequest) {
  m_ui->m_paymentRequestUriText->setPlainText(paymentRequest);
  payment_request_uri = paymentRequest;
}

void ShowPaymentRequestDialog::copyUri() {
  QApplication::clipboard()->setText(payment_request_uri);
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

}
