// Copyright (c) 2011-2015 The Cryptonote developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include <QDialog>

namespace Ui {
class TransactionDetailsDialog;
}

namespace WalletGui {

class TransactionDetailsDialog : public QDialog {
  Q_OBJECT
  Q_DISABLE_COPY(TransactionDetailsDialog)

public:
  TransactionDetailsDialog(const QModelIndex &_index, QWidget* _parent);
  ~TransactionDetailsDialog();

private Q_SLOTS:
  void copyProof();

private:
  QScopedPointer<Ui::TransactionDetailsDialog> m_ui;
  const QString m_detailsTemplate;
  // The payment proof(s) for this transaction, joined for the clipboard; empty when
  // the tx carries none (incoming, or a send made before proofs were recorded).
  QString m_proofText;
};

}
