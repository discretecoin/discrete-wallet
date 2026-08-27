// Copyright (c) 2011-2016 The Cryptonote developers
// Copyright (c) 2015-2016 XDN developers
// Copyright (c) 2016-2020 The Karbowanec developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "NewNodeDialog.h"

#include "CryptoNoteConfig.h"

#include "ui_newnodedialog.h"

namespace WalletGui {

NewNodeDialog::NewNodeDialog(QWidget* _parent) : QDialog(_parent), m_ui(new Ui::NewNodeDialog) {
  m_ui->setupUi(this);
  connect(m_ui->m_enableSSL, SIGNAL(stateChanged(const int &)), this, SLOT(enableSSLstateChanged(const int &)));
}

NewNodeDialog::~NewNodeDialog() {
}

void NewNodeDialog::enableSSLstateChanged(const int state) {
  // Move between the two defaults, and only between them. The old behaviour
  // added or subtracted a flat 100, which came from Karbo's port layout; on
  // Discrete the plain and TLS RPC ports are adjacent (9331 / 9332), so a fixed
  // offset lands on nothing. Anything the operator typed themselves is left
  // exactly as typed -- a checkbox must not silently rewrite a port they chose.
  const quint16 plainPort = static_cast<quint16>(CryptoNote::RPC_DEFAULT_PORT);
  const quint16 sslPort = static_cast<quint16>(CryptoNote::RPC_DEFAULT_SSL_PORT);
  const quint16 port = m_ui->m_portSpin->value();

  if (state == Qt::Checked && port == plainPort) {
    m_ui->m_portSpin->setValue(sslPort);
  } else if (state == Qt::Unchecked && port == sslPort) {
    m_ui->m_portSpin->setValue(plainPort);
  }
}

QString NewNodeDialog::getHost() const {
  return m_ui->m_hostEdit->text().trimmed();
}

quint16 NewNodeDialog::getPort() const {
  return m_ui->m_portSpin->value();
}

QString NewNodeDialog::getPath() const {
  return m_ui->m_pathEdit->text();
}

bool NewNodeDialog::getEnableSSL() const {
  int state = m_ui->m_enableSSL->checkState();
  bool res = false;
  if (state == Qt::Checked) res = true;
  return res;
}

bool NewNodeDialog::getTrusted() const {
  return m_ui->m_trusted->checkState() == Qt::Checked;
}

}
