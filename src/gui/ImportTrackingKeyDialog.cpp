// Copyright (c) 2011-2016 The Cryptonote developers
// Copyright (c) 2015-2016 XDN developers
// Copyright (c) 2016-2022 The Karbowanec developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QApplication>
#include <QFileDialog>
#include <QMessageBox>
#include <QStandardPaths>

#include <boost/utility/value_init.hpp>
#include "Common/StringTools.h"
#include "CurrencyAdapter.h"

#include "ImportTrackingKeyDialog.h"

#include "ui_importtrackingkeydialog.h"

namespace WalletGui {

ImportTrackingKeyDialog::ImportTrackingKeyDialog(QWidget* _parent) : QDialog(_parent), m_ui(new Ui::ImportTrackingKeyDialog) {
  m_ui->setupUi(this);
  m_ui->m_okButton->setEnabled(false);
}

ImportTrackingKeyDialog::~ImportTrackingKeyDialog() {
}

QString ImportTrackingKeyDialog::getKeyString() const {
  return m_ui->m_keyEdit->toPlainText().trimmed();
}

QString ImportTrackingKeyDialog::getFilePath() const {
  return m_ui->m_pathEdit->text().trimmed();
}

quint32 ImportTrackingKeyDialog::getSyncHeight() const {
  return m_ui->m_syncHeight->value();
}

CryptoNote::AccountKeys ImportTrackingKeyDialog::getAccountKeys() const {
  return m_keys;
}

CryptoNote::PqTrackingKeys ImportTrackingKeyDialog::getTrackingKeys() const {
  return m_trackingKeys;
}

void ImportTrackingKeyDialog::selectPathClicked() {
  QString filePath = QFileDialog::getSaveFileName(this, tr("Tracking wallet file"),
#ifdef Q_OS_WIN
    //QApplication::applicationDirPath(),
      QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
#else
    QDir::homePath(),
#endif
    tr("Tracking wallets (*.wallet)")
    );

  if (!filePath.isEmpty() && !filePath.endsWith(".wallet")) {
    filePath.append(".wallet");
  }

  m_ui->m_pathEdit->setText(filePath);
}

void ImportTrackingKeyDialog::onTextChanged() {
  CryptoNote::PqTrackingKeys probe;
  m_ui->m_okButton->setEnabled(CryptoNote::decodePqTrackingKey(getKeyString().toStdString(), probe));
}

void ImportTrackingKeyDialog::onAccept() {
  QString keyString = getKeyString().trimmed();

  if (keyString.isEmpty() || !CryptoNote::decodePqTrackingKey(keyString.toStdString(), m_trackingKeys)) {
    QMessageBox::warning(this, tr("Tracking key is not valid"), tr("The tracking key you entered is not valid."), QMessageBox::Ok);
    return;
  }

  m_keys = CryptoNote::AccountKeys{};

  if (getFilePath().isEmpty()) {
    QMessageBox::critical(nullptr, tr("File path is empty"), tr("Please enter the path where to save the wallet file and its name."), QMessageBox::Ok);
    return;
  }

  accept();
}

}
