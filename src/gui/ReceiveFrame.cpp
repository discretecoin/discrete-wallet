// Copyright (c) 2011-2016 The Cryptonote developers
// Copyright (c) 2015-2016 XDN developers
// Copyright (c) 2016 The Karbovanets developers
// Copyright (c) 2026 The Discrete developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QApplication>
#include <QClipboard>
#include <QFontDatabase>
#include <QToolTip>
#include <QUrl>

#include "MainWindow.h"
#include "ReceiveFrame.h"
#include "CurrencyAdapter.h"
#include "WalletAdapter.h"
#include "ShowPaymentRequestDialog.h"

#include "ui_receiveframe.h"

namespace WalletGui {

ReceiveFrame::ReceiveFrame(QWidget* _parent) : QFrame(_parent), m_ui(new Ui::ReceiveFrame) {
  m_ui->setupUi(this);
  m_ui->m_requestAmountCurrencyLabel->setText(CurrencyAdapter::instance().getCurrencyTicker().toUpper());
  QFont addressFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
  addressFont.setPixelSize(12);
  m_ui->m_addressText->setFont(addressFont);
  connect(&WalletAdapter::instance(), &WalletAdapter::updateWalletAddressSignal, this, &ReceiveFrame::updateWalletAddress);
  connect(&WalletAdapter::instance(), &WalletAdapter::walletCloseCompletedSignal, this, &ReceiveFrame::walletClosed, Qt::QueuedConnection);
  connect(m_ui->m_copyAddressButton, &QPushButton::clicked, this, &ReceiveFrame::copyAddress);
}

ReceiveFrame::~ReceiveFrame() {
}

void ReceiveFrame::updateWalletAddress(const QString& _address) {
  wallet_address = _address;
  m_ui->m_addressText->setPlainText(_address);
  m_ui->m_copyAddressButton->setEnabled(!_address.isEmpty());
}

void ReceiveFrame::walletClosed() {
  wallet_address.clear();
  m_ui->m_addressText->clear();
  m_ui->m_copyAddressButton->setEnabled(false);
}

void ReceiveFrame::copyAddress() {
  if (wallet_address.isEmpty()) {
    return;
  }

  QClipboard* clipboard = QApplication::clipboard();
  clipboard->setText(wallet_address, QClipboard::Clipboard);
  if (clipboard->supportsSelection()) {
    clipboard->setText(wallet_address, QClipboard::Selection);
  }
  QToolTip::showText(
    m_ui->m_copyAddressButton->mapToGlobal(m_ui->m_copyAddressButton->rect().center()),
    tr("Copied"), m_ui->m_copyAddressButton);
}

void ReceiveFrame::createRequestPaymentClicked() {
  requestUri = "discrete:" + wallet_address;
  if(CurrencyAdapter::instance().parseAmount(m_ui->m_requestAmountSpin->cleanText()) != 0){
    requestUri.append("?amount=" + m_ui->m_requestAmountSpin->cleanText());
  }

  if(!m_ui->m_payerLabel->text().isEmpty()) {
    requestUri.append((requestUri.contains('?') ? "&label=" : "?label=") + QUrl::toPercentEncoding(m_ui->m_payerLabel->text()));
  }

  ShowPaymentRequestDialog dlg(&MainWindow::instance());
  dlg.setData(requestUri);
  dlg.exec();
}

}
