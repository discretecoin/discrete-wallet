// Copyright (c) 2011-2016 The Cryptonote developers
// Copyright (c) 2015-2016 XDN developers
// Copyright (c) 2016 The Karbovanets developers
// Copyright (c) 2026 The Discrete developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QClipboard>
#include <QFileDialog>
#include <QBuffer>
#include <QUrl>
#include <QTime>

#include "MainWindow.h"
#include "ReceiveFrame.h"
#include "CurrencyAdapter.h"
#include "WalletAdapter.h"
#include "NodeAdapter.h"
#include "AccountNumber.h"
#include "ShowPaymentRequestDialog.h"

#include "ui_receiveframe.h"

namespace WalletGui {

ReceiveFrame::ReceiveFrame(QWidget* _parent) : QFrame(_parent), m_ui(new Ui::ReceiveFrame),
  m_accountNumberResolved(false), m_accountNumberFetchInProgress(false) {
  m_ui->setupUi(this);
  m_ui->m_requestAmountSpin->setSuffix(" " + CurrencyAdapter::instance().getCurrencyTicker().toUpper());
  connect(&WalletAdapter::instance(), &WalletAdapter::updateWalletAddressSignal, this, &ReceiveFrame::updateWalletAddress);
  connect(&WalletAdapter::instance(), &WalletAdapter::walletCloseCompletedSignal, this, &ReceiveFrame::walletClosed, Qt::QueuedConnection);
  connect(&WalletAdapter::instance(), &WalletAdapter::walletSynchronizationCompletedSignal, this, [this](int _error, const QString&) {
    if (_error != 0 || !WalletAdapter::instance().isOpen() || m_accountNumberResolved) {
      return;
    }

    fetchAccountNumber(WalletAdapter::instance().getAddress());
  });
}

ReceiveFrame::~ReceiveFrame() {
}

void ReceiveFrame::updateWalletAddress(const QString& _address) {
  wallet_address = _address;
  m_accountNumber.clear();
  m_accountNumberResolved = false;
  m_accountNumberFetchInProgress = false;
  fetchAccountNumber(_address);
}

void ReceiveFrame::walletClosed() {
  wallet_address.clear();
  m_accountNumber.clear();
  m_accountNumberResolved = false;
  m_accountNumberFetchInProgress = false;
}

void ReceiveFrame::fetchAccountNumber(const QString& _address) {
  if (_address.isEmpty() || m_accountNumberFetchInProgress) {
    return;
  }

  QString viewPubHex, spendPubHex;
  if (!WalletAdapter::instance().getOwnPqIdentityHex(viewPubHex, spendPubHex)) {
    return;
  }

  m_accountNumberFetchInProgress = true;
  const QString requestedAddress = _address;

  auto registered = std::make_shared<bool>(false);
  auto blockHeight = std::make_shared<uint32_t>(0);
  auto txIndex = std::make_shared<uint32_t>(0);

  NodeAdapter::instance().getPqAccount(viewPubHex.toStdString(), spendPubHex.toStdString(), *registered, *blockHeight, *txIndex,
    [this, registered, blockHeight, txIndex, requestedAddress](std::error_code ec) {
      QMetaObject::invokeMethod(this, [this, ec, registered, blockHeight, txIndex, requestedAddress]() {
        if (WalletAdapter::instance().getAddress() != requestedAddress) {
          m_accountNumberFetchInProgress = false;
          return;
        }

        // Node lookups can transiently fail; keep the current state and
        // retry on the next synchronization completion instead of clearing it.
        if (ec) {
          m_accountNumberFetchInProgress = false;
          return;
        }

        m_accountNumberResolved = true;
        m_accountNumberFetchInProgress = false;
        m_accountNumber = *registered
          ? QString::fromStdString(CryptoNote::AccountNumber{*blockHeight, *txIndex}.toString())
          : QString();
      }, Qt::QueuedConnection);
    });
}

void ReceiveFrame::createRequestPaymentClicked() {
  // The full bech32m address is ~5000 characters — too long for any QR code
  // to encode. Use the short account number instead when one is registered,
  // the same fix applied to the main address QR in AccountFrame::showQR.
  const QString target = !m_accountNumber.isEmpty() ? m_accountNumber : wallet_address;
  requestUri = "discrete:" + target;
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
