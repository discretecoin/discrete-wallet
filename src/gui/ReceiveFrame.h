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
  QScopedPointer<Ui::ReceiveFrame> m_ui;

  void updateWalletAddress(const QString& _address);
  void walletClosed();
  // Resolves the wallet's registered account number, mirroring
  // AccountFrame::fetchAccountNumber, so the payment request URI/QR can use
  // the short account number instead of the ~5000-character PQ address.
  void fetchAccountNumber(const QString& _address);
  QString wallet_address;
  QString requestUri;
  QString m_accountNumber;
  bool m_accountNumberResolved;
  bool m_accountNumberFetchInProgress;

  Q_SLOT void createRequestPaymentClicked();

};

}
