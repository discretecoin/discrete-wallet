// Copyright (c) 2016-2017 The Karbowanec developers
// Copyright (c) 2026 The Discrete developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include <QDialog>
#include <QScopedPointer>

namespace Ui {
class ExportRawTransactionDialog;
}

namespace WalletGui {

class ExportRawTransactionDialog : public QDialog {
  Q_OBJECT
  Q_DISABLE_COPY(ExportRawTransactionDialog)

public:
  explicit ExportRawTransactionDialog(QWidget* _parent);
  ~ExportRawTransactionDialog();

  void setTransaction(const QString& _transaction);

private:
  Q_SLOT void copyTx();
  Q_SLOT void saveTxToFile();

  QScopedPointer<Ui::ExportRawTransactionDialog> m_ui;
};

}
