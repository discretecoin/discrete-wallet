// Copyright (c) 2011-2016 The Cryptonote developers
// Copyright (c) 2015-2016 XDN developers
// Copyright (c) 2016 The Karbovanets developers
// Copyright (c) 2026 The Discrete developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QApplication>
#include <QClipboard>
#include <QFontDatabase>
#include <QMetaObject>
#include <QPointer>
#include <QTimer>
#include <QToolTip>
#include <QUrl>

#include <memory>
#include <system_error>

#include "AccountNumber.h"
#include "Common/StringTools.h"
#include "MainWindow.h"
#include "ReceiveFrame.h"
#include "CurrencyAdapter.h"
#include "NodeAdapter.h"
#include "PqAddress.h"
#include "WalletAdapter.h"
#include "ShowPaymentRequestDialog.h"

#include "ui_receiveframe.h"

namespace WalletGui {

namespace {

constexpr int ACCOUNT_NUMBER_LOOKUP_TIMEOUT_MS = 10000;
constexpr int QR_MAX_BYTE_MODE_PAYLOAD = 2953;

}

ReceiveFrame::ReceiveFrame(QWidget* _parent) : QFrame(_parent), m_ui(new Ui::ReceiveFrame) {
  m_ui->setupUi(this);
  m_ui->m_requestAmountSpin->setSuffix(" " + CurrencyAdapter::instance().getCurrencyTicker().toUpper());
  QFont addressFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
  addressFont.setPixelSize(12);
  m_ui->m_addressText->setFont(addressFont);
  connect(&WalletAdapter::instance(), &WalletAdapter::updateWalletAddressSignal, this, &ReceiveFrame::updateWalletAddress);
  connect(&WalletAdapter::instance(), &WalletAdapter::walletCloseCompletedSignal, this, &ReceiveFrame::walletClosed);
  connect(m_ui->m_copyAddressButton, &QPushButton::clicked, this, &ReceiveFrame::copyAddress);
}

ReceiveFrame::~ReceiveFrame() {
}

void ReceiveFrame::updateWalletAddress(const QString& _address) {
  ++payment_request_generation;
  payment_request_lookup_in_progress = false;
  wallet_address = _address;
  m_ui->m_addressText->setText(_address);
  m_ui->m_addressText->setCursorPosition(0);
  m_ui->m_copyAddressButton->setEnabled(!_address.isEmpty());
  m_ui->m_createPaymentRequest->setEnabled(!_address.isEmpty());
}

void ReceiveFrame::walletClosed() {
  ++payment_request_generation;
  payment_request_lookup_in_progress = false;
  wallet_address.clear();
  m_ui->m_addressText->clear();
  m_ui->m_copyAddressButton->setEnabled(false);
  m_ui->m_createPaymentRequest->setEnabled(false);
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

bool ReceiveFrame::isCurrentPaymentRequest(const QString& _walletAddress,
                                           quint64 _requestGeneration) const {
  return payment_request_lookup_in_progress &&
         payment_request_generation == _requestGeneration &&
         wallet_address == _walletAddress &&
         WalletAdapter::instance().isOpen();
}

void ReceiveFrame::completePaymentRequest(const QString& _walletAddress,
                                          const QString& _recipient,
                                          const QString& _amount,
                                          const QString& _label,
                                          quint64 _requestGeneration,
                                          PaymentRequestRecipientState _recipientState) {
  if (!isCurrentPaymentRequest(_walletAddress, _requestGeneration)) {
    return;
  }

  payment_request_lookup_in_progress = false;
  m_ui->m_createPaymentRequest->setEnabled(true);

  // An asynchronous lookup can finish while a password or another modal
  // dialog is open. Never reveal a payment request over that dialog.
  if (QApplication::activeModalWidget() != nullptr) {
    return;
  }

  QString requestUri = "discrete:" + _recipient;
  if (!_amount.isEmpty()) {
    requestUri.append("?amount=" + _amount);
  }

  if (!_label.isEmpty()) {
    requestUri.append((requestUri.contains('?') ? "&label=" : "?label=") +
                      QUrl::toPercentEncoding(_label));
  }

  const bool usesAccountNumber =
    _recipientState == PaymentRequestRecipientState::AccountNumber;
  const bool qrAvailable = usesAccountNumber &&
    requestUri.toUtf8().size() <= QR_MAX_BYTE_MODE_PAYLOAD;
  QString recipientStatus;
  if (qrAvailable) {
    recipientStatus = tr("Using a verified account number. This compact payment request can be scanned as a QR code.");
  } else if (usesAccountNumber) {
    recipientStatus = tr("Using a verified account number, but the complete payment request is too large for a QR code.");
  } else if (_recipientState == PaymentRequestRecipientState::AccountNumberNotReady) {
    recipientStatus = tr("The account number is not payable yet. This request uses the full wallet address and is too large for a QR code.");
  } else {
    recipientStatus = tr("A payable account number could not be verified. This request uses the full wallet address and is too large for a QR code.");
  }

  ShowPaymentRequestDialog dlg(&MainWindow::instance());
  dlg.setData(requestUri, qrAvailable, recipientStatus);
  dlg.exec();
}

void ReceiveFrame::createRequestPaymentClicked() {
  if (wallet_address.isEmpty() || payment_request_lookup_in_progress) {
    return;
  }

  const QString requestedWalletAddress = wallet_address;
  const quint64 requestedPaymentRequestGeneration = ++payment_request_generation;
  const QString requestedAmount =
    CurrencyAdapter::instance().parseAmount(m_ui->m_requestAmountSpin->cleanText()) != 0 ?
      m_ui->m_requestAmountSpin->cleanText() : QString();
  const QString requestedLabel = m_ui->m_payerLabel->text();

  payment_request_lookup_in_progress = true;
  m_ui->m_createPaymentRequest->setEnabled(false);

  // Account-number lookup must never make invoice creation hang. If the node
  // is unreachable or too old to answer, fall back to the self-contained PQ
  // address. A later callback is ignored by the per-request generation check.
  QPointer<ReceiveFrame> self(this);
  QTimer::singleShot(ACCOUNT_NUMBER_LOOKUP_TIMEOUT_MS, this,
    [self, requestedWalletAddress, requestedAmount, requestedLabel,
     requestedPaymentRequestGeneration]() {
      if (self) {
        self->completePaymentRequest(requestedWalletAddress, requestedWalletAddress,
          requestedAmount, requestedLabel, requestedPaymentRequestGeneration,
          PaymentRequestRecipientState::FullAddressFallback);
      }
    });

  CryptoNote::PqAddress ownAddress;
  if (!CryptoNote::decodePqAddress(requestedWalletAddress.toStdString(),
                                   CurrencyAdapter::instance().isTestnet(), ownAddress)) {
    completePaymentRequest(requestedWalletAddress, requestedWalletAddress,
      requestedAmount, requestedLabel, requestedPaymentRequestGeneration,
      PaymentRequestRecipientState::FullAddressFallback);
    return;
  }

  // The displayed address is the canonical identity here. In particular,
  // tracking wallets do not have the spend secret used by getOwnPqIdentityHex.
  const QString ownViewPubHex = QString::fromStdString(
    Common::toHex(ownAddress.viewPub.data(), ownAddress.viewPub.size()));
  const QString ownSpendPubHex = QString::fromStdString(
    Common::toHex(ownAddress.spendPub.data(), ownAddress.spendPub.size()));

  const uint32_t fingerprint = CryptoNote::pqAccountFingerprint(
    CurrencyAdapter::instance().isTestnet(),
    ownAddress.spendPub.data(), ownAddress.spendPub.size(),
    ownAddress.viewPub.data(), ownAddress.viewPub.size());
  auto registered = std::make_shared<bool>(false);
  auto blockHeight = std::make_shared<uint32_t>(0);
  auto txIndex = std::make_shared<uint32_t>(0);

  // Registration visibility is not sufficient: getPqAccount can return (H,I)
  // before first-seen finality, while a payer still cannot resolve that number.
  // Use the payer-side resolve path below as the actual payability gate.
  NodeAdapter::instance().getPqAccount(
    ownViewPubHex.toStdString(), ownSpendPubHex.toStdString(),
    *registered, *blockHeight, *txIndex,
    [self, registered, blockHeight, txIndex, requestedWalletAddress,
     requestedAmount, requestedLabel, requestedPaymentRequestGeneration, fingerprint,
     ownViewPubHex, ownSpendPubHex](std::error_code ec) {
      QMetaObject::invokeMethod(qApp,
        [self, ec, registered, blockHeight, txIndex, requestedWalletAddress,
         requestedAmount, requestedLabel, requestedPaymentRequestGeneration, fingerprint,
         ownViewPubHex, ownSpendPubHex]() {
          if (!self || !self->isCurrentPaymentRequest(requestedWalletAddress,
                                                       requestedPaymentRequestGeneration)) {
            return;
          }

          if (ec || !*registered) {
            self->completePaymentRequest(requestedWalletAddress, requestedWalletAddress,
              requestedAmount, requestedLabel, requestedPaymentRequestGeneration,
              PaymentRequestRecipientState::FullAddressFallback);
            return;
          }

          auto found = std::make_shared<bool>(false);
          auto resolvedViewPubHex = std::make_shared<std::string>();
          auto resolvedSpendPubHex = std::make_shared<std::string>();
          NodeAdapter::instance().resolvePqAccount(
            *blockHeight, *txIndex, *found, *resolvedViewPubHex, *resolvedSpendPubHex,
            [self, found, resolvedViewPubHex, resolvedSpendPubHex, blockHeight, txIndex,
             requestedWalletAddress, requestedAmount, requestedLabel,
              requestedPaymentRequestGeneration, fingerprint, ownViewPubHex,
              ownSpendPubHex](std::error_code resolveError) {
              QMetaObject::invokeMethod(qApp,
                [self, resolveError, found, resolvedViewPubHex, resolvedSpendPubHex,
                 blockHeight, txIndex, requestedWalletAddress, requestedAmount,
                 requestedLabel, requestedPaymentRequestGeneration, fingerprint,
                 ownViewPubHex, ownSpendPubHex]() {
                  if (!self || !self->isCurrentPaymentRequest(requestedWalletAddress,
                                                               requestedPaymentRequestGeneration)) {
                    return;
                  }

                  QString recipient = requestedWalletAddress;
                  PaymentRequestRecipientState recipientState =
                    PaymentRequestRecipientState::FullAddressFallback;
                  // Do not trust the short fingerprint alone. Only publish the
                  // account number when its resolved keys exactly match the
                  // keys captured from the wallet that initiated this request.
                  if (!resolveError && *found &&
                      QString::fromStdString(*resolvedViewPubHex).compare(
                        ownViewPubHex, Qt::CaseInsensitive) == 0 &&
                      QString::fromStdString(*resolvedSpendPubHex).compare(
                        ownSpendPubHex, Qt::CaseInsensitive) == 0) {
                    recipient = QString::fromStdString(
                      CryptoNote::AccountNumber{*blockHeight, *txIndex}.toString(fingerprint));
                    recipientState = PaymentRequestRecipientState::AccountNumber;
                  } else if (!resolveError && !*found) {
                    recipientState = PaymentRequestRecipientState::AccountNumberNotReady;
                  }

                  self->completePaymentRequest(requestedWalletAddress, recipient,
                    requestedAmount, requestedLabel, requestedPaymentRequestGeneration,
                    recipientState);
                },
                Qt::QueuedConnection);
            });
        },
        Qt::QueuedConnection);
    });
}

}
