// Copyright (c) 2016 The Karbowanec developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "PrivateKeysDialog.h"
#include "ui_privatekeysdialog.h"
#include <QClipboard>
#include <Common/StringTools.h>
#include "CryptoNoteCore/CryptoNoteBasic.h"
#include "CurrencyAdapter.h"
#include "Settings.h"
#include "WalletAdapter.h"

namespace WalletGui {

PrivateKeysDialog::PrivateKeysDialog(QWidget* _parent) : QDialog(_parent), m_ui(new Ui::PrivateKeysDialog) {
  m_ui->setupUi(this);
  connect(&WalletAdapter::instance(), &WalletAdapter::walletInitCompletedSignal, this, &PrivateKeysDialog::walletOpened, Qt::QueuedConnection);
  connect(&WalletAdapter::instance(), &WalletAdapter::walletCloseCompletedSignal, this, &PrivateKeysDialog::walletClosed, Qt::QueuedConnection);
}

PrivateKeysDialog::~PrivateKeysDialog() {
}

void PrivateKeysDialog::walletOpened() {
  // Discrete has a single secret backing the whole identity (the 32-byte
  // master seed the mnemonic words encode — see MnemonicSeedDialog). This
  // shows it as raw hex, for users who'd rather back up/import a hex string
  // than the word list. It is intentionally short: it is a seed the full
  // ML-KEM-768/ML-DSA-65 keypairs are deterministically derived from, not
  // those keys themselves.
  if (Settings::instance().isTrackingMode()) {
    m_ui->m_privateKeyEdit->setText(tr("This is a watch-only (tracking) wallet — it has no spend secret to export."));
    m_ui->m_copyPrivateKeyButton->setEnabled(false);
    return;
  }

  CryptoNote::AccountKeys keys;
  if (!WalletAdapter::instance().getAccountKeys(keys) || keys.spendSecretKey == CryptoNote::NULL_SECRET_KEY) {
    m_ui->m_privateKeyEdit->setText(tr("Failed to read the wallet's private key."));
    m_ui->m_copyPrivateKeyButton->setEnabled(false);
    return;
  }

  QString masterSeedHex = QString::fromStdString(Common::podToHex(keys.spendSecretKey));
  m_ui->m_privateKeyEdit->setText(masterSeedHex);
  m_ui->m_copyPrivateKeyButton->setEnabled(true);
}

void PrivateKeysDialog::walletClosed() {
  m_ui->m_privateKeyEdit->clear();
}

void PrivateKeysDialog::copyKey() {
  QApplication::clipboard()->setText(m_ui->m_privateKeyEdit->toPlainText());
}

}
