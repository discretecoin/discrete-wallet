// Copyright (c) 2011-2016 The Cryptonote developers
// Copyright (c) 2015-2016 XDN developers
// Copyright (c) 2016 The Karbovanets developers
// Copyright (c) 2026 The Discrete developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include <QFrame>

namespace Ui {
class ReceiveFrame;
}

namespace WalletGui {

class ReceiveFrame : public QFrame {
  Q_OBJECT
  Q_DISABLE_COPY(ReceiveFrame)

public:
  ReceiveFrame(QWidget* _parent);
  ~ReceiveFrame();

private:
  enum class PaymentRequestRecipientState {
    AccountNumber,
    AccountNumberNotReady,
    FullAddressFallback
  };

  QScopedPointer<Ui::ReceiveFrame> m_ui;

  void updateWalletAddress(const QString& _address);
  void walletClosed();
  void copyAddress();
  bool isCurrentPaymentRequest(const QString& _walletAddress, quint64 _requestGeneration) const;
  void completePaymentRequest(const QString& _walletAddress, const QString& _recipient,
                              const QString& _amount, const QString& _label,
                              quint64 _requestGeneration,
                              PaymentRequestRecipientState _recipientState);
  QString wallet_address;
  quint64 payment_request_generation = 0;
  bool payment_request_lookup_in_progress = false;

  Q_SLOT void createRequestPaymentClicked();

};

}
