// Copyright (c) 2011-2015 The Cryptonote developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include <QFrame>

namespace Ui {
class TransferFrame;
}

namespace WalletGui {

class TransferFrame : public QFrame {
  Q_OBJECT
  Q_DISABLE_COPY(TransferFrame)

public:
  TransferFrame(QWidget* _parent);
  ~TransferFrame();

  QString getAddress() const;
  QString getLabel() const;
  qreal getAmount() const;
  QString getAmountString() const;
  quint64 getMaximumAmount() const;

  void disableRemoveButton(bool _disable);
  void setAddress(QString _address);
  void setLabel(QString _label);
  void setAmount(quint64 _amount);

protected:
  void timerEvent(QTimerEvent* _event) Q_DECL_OVERRIDE;

signals:
    void amountValueChangedSignal();

private:
  QScopedPointer<Ui::TransferFrame> m_ui;
  int m_accountNumberInputTimer;
  // Resolves an H-I-A-C or H-I-A-T-C account number to its registered PQ address
  // for display, mirroring the canonical resolution in Wallet/PqRecipient.h.
  void resolveAccountNumber(const QString& _input);
  static bool looksLikeAccountNumber(const QString& _text);

  Q_SLOT void addressBookClicked();
  Q_SLOT void pasteClicked();
  Q_SLOT void amountValueChange();
  Q_SLOT void addressEdited(const QString& _text);
};

}
