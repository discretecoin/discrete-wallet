// Copyright (c) 2011-2015 The Cryptonote developers
// Copyright (c) 2015 XDN developers
// Copyright (c) 2016-2026 The Karbovanets developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QApplication>
#include <QClipboard>
#include <QRegularExpression>

#include "TransferFrame.h"
#include "AddressBookDialog.h"
#include "MainWindow.h"
#include "CurrencyAdapter.h"
#include "NodeAdapter.h"
#include "DnsLookup.h"
#include "AccountNumber.h"
#include "PqAddress.h"
#include "Common/StringTools.h"

#include "ui_transferframe.h"

namespace WalletGui {

Q_DECL_CONSTEXPR quint32 ADDRESS_INPUT_INTERVAL = 1500;

TransferFrame::TransferFrame(QWidget* _parent) : QFrame(_parent), m_ui(new Ui::TransferFrame), m_aliasProvider(new DnsManager(this)), m_addressInputTimer(-1), m_accountNumberInputTimer(-1) {
  m_ui->setupUi(this);
  setAttribute(Qt::WA_DeleteOnClose);
  m_ui->m_amountSpin->setSuffix(" " + CurrencyAdapter::instance().getCurrencyTicker().toUpper());
  connect(m_aliasProvider, &DnsManager::aliasFoundSignal, this, &TransferFrame::onAliasFound);
}

TransferFrame::~TransferFrame() {
}

QString TransferFrame::getAddress() const {
  QString address = m_ui->m_addressEdit->text().trimmed();
    if (address.contains('<')) {
      int startPos = address.indexOf('<');
      int endPos = address.indexOf('>');
      address = address.mid(startPos + 1, endPos - startPos - 1);
    }
  return address;
}

QString TransferFrame::getLabel() const {
  return m_ui->m_labelEdit->text().trimmed();
}

qreal TransferFrame::getAmount() const {
  return m_ui->m_amountSpin->value();
}

QString TransferFrame::getAmountString() const {
  return m_ui->m_amountSpin->cleanText();
}

void TransferFrame::disableRemoveButton(bool _disable) {
  m_ui->m_removeButton->setDisabled(_disable);
}

void TransferFrame::addressBookClicked() {
  AddressBookDialog dlg(&MainWindow::instance());
  if(dlg.exec() == QDialog::Accepted) {
    m_ui->m_addressEdit->setText(dlg.getAddress());
  }
}

void TransferFrame::timerEvent(QTimerEvent* _event) {
  if (_event->timerId() == m_addressInputTimer) {
    m_aliasProvider->getAddresses(m_ui->m_addressEdit->text().trimmed());
    killTimer(m_addressInputTimer);
    m_addressInputTimer = -1;
    return;
  }

  if (_event->timerId() == m_accountNumberInputTimer) {
    resolveAccountNumber(m_ui->m_addressEdit->text().trimmed());
    killTimer(m_accountNumberInputTimer);
    m_accountNumberInputTimer = -1;
    return;
  }

  QFrame::timerEvent(_event);
}

void TransferFrame::onAliasFound(const QString& _name, const QString& _address) {
  m_ui->m_addressEdit->setText(QString("%1 <%2>").arg(_name).arg(_address));
  m_ui->m_addressStatusLabel->hide();
}

void TransferFrame::addressEdited(const QString& _text) {
  m_ui->m_addressStatusLabel->hide();
  if(!_text.isEmpty() && _text.contains('.')) {
    if (m_addressInputTimer != -1) {
      killTimer(m_addressInputTimer);
    }
    m_addressInputTimer = startTimer(ADDRESS_INPUT_INTERVAL);
  } else if (looksLikeAccountNumber(_text.trimmed())) {
    if (m_accountNumberInputTimer != -1) {
      killTimer(m_accountNumberInputTimer);
    }
    m_accountNumberInputTimer = startTimer(ADDRESS_INPUT_INTERVAL);
  }
}

void TransferFrame::pasteClicked() {
  QString text = QApplication::clipboard()->text();
  m_ui->m_addressEdit->setText(text);
  addressEdited(text);
}

void TransferFrame::amountValueChange() {
  Q_EMIT amountValueChangedSignal();
}

bool TransferFrame::looksLikeAccountNumber(const QString& _text) {
  // H-I-C (base account) or H-I-T-C (deposit subaddress).
  static QRegularExpression re("^\\d+-\\d+(-\\d+)?-[0-9A-Za-z]$");
  return re.match(_text).hasMatch();
}

void TransferFrame::resolveAccountNumber(const QString& _input) {
  CryptoNote::AccountNumber acct;
  uint32_t subaddrIndex = 0;
  bool isHitc = CryptoNote::AccountNumber::fromStringWithIndex(_input.toStdString(), acct, subaddrIndex);
  if (!isHitc && !CryptoNote::AccountNumber::fromString(_input.toStdString(), acct)) {
    m_ui->m_addressStatusLabel->setStyleSheet(QStringLiteral("color:#D96A73;"));
    m_ui->m_addressStatusLabel->setText(tr("Invalid account number"));
    m_ui->m_addressStatusLabel->show();
    return;
  }

  // Just confirm the account number resolves on chain; don't rewrite the field.
  // The full recipient keys are ~5000 characters — expanding them into the
  // input is unreadable, and unnecessary: the send path resolves the account
  // number again to the real keys (see WalletLegacy::sendTransaction /
  // PqRecipient), and validateAddress() accepts account numbers directly. So
  // the H-I-C / H-I-T-C string stays in the field and is sent as-is.
  auto found = std::make_shared<bool>(false);
  auto viewPubHex = std::make_shared<std::string>();
  auto spendPubHex = std::make_shared<std::string>();

  NodeAdapter::instance().resolvePqAccount(acct.blockHeight, acct.txIndex, *found, *viewPubHex, *spendPubHex,
    [this, _input, found](std::error_code ec) {
      QMetaObject::invokeMethod(this, [this, _input, ec, found]() {
        // A newer edit may have changed the field since this lookup started.
        if (m_ui->m_addressEdit->text().trimmed() != _input) {
          return;
        }
        if (!ec && *found) {
          m_ui->m_addressStatusLabel->setStyleSheet(QStringLiteral("color:#5FE29F;"));
          m_ui->m_addressStatusLabel->setText(tr("Account number found"));
        } else {
          m_ui->m_addressStatusLabel->setStyleSheet(QStringLiteral("color:#D96A73;"));
          m_ui->m_addressStatusLabel->setText(tr("Account number not found"));
        }
        m_ui->m_addressStatusLabel->show();
      }, Qt::QueuedConnection);
    });
}

void TransferFrame::setAddress(QString _address) {
  m_ui->m_addressEdit->setText(_address);
}

void TransferFrame::setLabel(QString _label) {
  m_ui->m_labelEdit->setText(_label);
}

void TransferFrame::setAmount(quint64 _amount) {
  m_ui->m_amountSpin->setValue(CurrencyAdapter::instance().formatAmount(_amount).toDouble());
}

}
