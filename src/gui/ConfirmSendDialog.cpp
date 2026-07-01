// Copyright (c) 2016 The Karbowanec developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "CurrencyAdapter.h"
#include "ConfirmSendDialog.h"

#include "ui_confirmsenddialog.h"

namespace WalletGui {

ConfirmSendDialog::ConfirmSendDialog(QWidget* _parent) : QDialog(_parent), m_ui(new Ui::ConfirmSendDialog) {
  m_ui->setupUi(this);
}

ConfirmSendDialog::~ConfirmSendDialog() {
}

void ConfirmSendDialog::showPasymentDetails(quint64 _total) {
    const QString ticker = CurrencyAdapter::instance().getCurrencyTicker().toUpper();
    setWindowTitle(QString(tr("Confirm sending %1 %2")).arg(CurrencyAdapter::instance().formatAmount(_total)).arg(ticker));
    m_ui->m_confirmLabel->setText(QString(tr("<html><head/><body><p>Are you sure you want to send <strong>%1 %2</strong>?</p></body></html>"))
      .arg(CurrencyAdapter::instance().formatAmount(_total)).arg(ticker));
}

}
