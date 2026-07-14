// Copyright (c) 2016 The Karbowanec developers
// Copyright (c) 2026 The Discrete developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "ExportRawTxDialog.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QTextStream>

#include "MainWindow.h"
#include "ui_exportrawtxdialog.h"

namespace WalletGui {

ExportRawTransactionDialog::ExportRawTransactionDialog(QWidget* _parent)
    : QDialog(_parent), m_ui(new Ui::ExportRawTransactionDialog) {
  m_ui->setupUi(this);
}

ExportRawTransactionDialog::~ExportRawTransactionDialog() = default;

void ExportRawTransactionDialog::setTransaction(const QString& _transaction) {
  m_ui->m_txEdit->setPlainText(_transaction);
  m_ui->m_txEdit->selectAll();
  m_ui->m_txEdit->setFocus();
}

void ExportRawTransactionDialog::copyTx() {
  QApplication::clipboard()->setText(m_ui->m_txEdit->toPlainText());
}

void ExportRawTransactionDialog::saveTxToFile() {
  const QString fileName = QFileDialog::getSaveFileName(
      &MainWindow::instance(), tr("Save transaction to..."), QDir::homePath(),
      tr("Raw hex transaction (*.txt)"));
  if (fileName.isEmpty()) {
    return;
  }

  QFile file(fileName);
  if (file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
    QTextStream outputStream(&file);
    outputStream << m_ui->m_txEdit->toPlainText();
  }
}

}
