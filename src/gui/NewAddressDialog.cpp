// Copyright (c) 2011-2015 The Cryptonote developers
// Copyright (c) 2016 The Karbowanec developers
// Copyright (c) 2026 The Discrete developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QRegularExpressionValidator>
#include <QRegularExpression>
#include "NewAddressDialog.h"
#include "NodeAdapter.h"
#include "CurrencyAdapter.h"
#include "AccountNumber.h"
#include "PqAddress.h"
#include "Common/StringTools.h"

#include "ui_newaddressdialog.h"

namespace WalletGui {

NewAddressDialog::NewAddressDialog(QWidget* _parent) : QDialog(_parent), m_ui(new Ui::NewAddressDialog) {
  m_ui->setupUi(this);

  connect(m_ui->m_addressEdit, &QLineEdit::textEdited, this, &NewAddressDialog::onAddressEdited);
}

NewAddressDialog::~NewAddressDialog() {
}

QString NewAddressDialog::getAddress() const {
  if (!m_resolvedAddress.isEmpty()) {
    return m_resolvedAddress;
  }
  return m_ui->m_addressEdit->text();
}

QString NewAddressDialog::getLabel() const {
  return m_ui->m_labelEdit->text();
}

void NewAddressDialog::setEditLabel(QString label) {
  m_ui->m_labelEdit->setText(label);
}

void NewAddressDialog::setEditAddress(QString address) {
  m_ui->m_addressEdit->setText(address);
  m_resolvedAddress.clear();
}

bool NewAddressDialog::looksLikeAccountNumber(const QString& _text) {
  // H-I-A-C (base account) or H-I-A-T-C (deposit subaddress).
  static QRegularExpression re("^\\d+-\\d+(-\\d+)?-[0-9A-Za-z]$");
  return re.match(_text).hasMatch();
}

void NewAddressDialog::onAddressEdited(const QString& _text) {
  m_resolvedAddress.clear();
  QString trimmed = _text.trimmed();
  if (looksLikeAccountNumber(trimmed)) {
    resolveAccountNumber(trimmed);
  }
}

void NewAddressDialog::resolveAccountNumber(const QString& _input) {
  // This resolution is not merely informational: the address it produces is what
  // getAddress() returns and what gets saved in the address book, and from then
  // on it is an ordinary full address that nothing downstream will question. So
  // it has to meet the same bar the send path applies to an account number --
  // otherwise a node the user never trusted could write a recipient of its
  // choosing into their address book, permanently.
  if (!NodeAdapter::instance().isTrustedResolver()) {
    m_ui->m_addressEdit->setToolTip(
      tr("The connected node is not marked as trusted, so this account number was "
         "not looked up. Mark the node trusted in Connection settings, or paste "
         "the full address."));
    return;
  }

  CryptoNote::AccountNumber acct;
  uint32_t subaddrIndex = 0;
  uint32_t wantFingerprint = 0;
  bool isHitc = CryptoNote::AccountNumber::fromStringWithIndex(_input.toStdString(), acct, subaddrIndex, wantFingerprint);
  if (!isHitc && !CryptoNote::AccountNumber::fromString(_input.toStdString(), acct, wantFingerprint)) {
    return;
  }

  auto found = std::make_shared<bool>(false);
  auto viewPubHex = std::make_shared<std::string>();
  auto spendPubHex = std::make_shared<std::string>();

  NodeAdapter::instance().resolvePqAccount(acct.blockHeight, acct.txIndex, *found, *viewPubHex, *spendPubHex,
    [this, _input, found, viewPubHex, spendPubHex, wantFingerprint](std::error_code ec) {
      if (ec || !*found) {
        return;
      }

      CryptoPQ::KemPublicKey viewPub;
      CryptoPQ::DsaPublicKey spendPub;
      size_t sz = 0;
      if (!Common::fromHex(*viewPubHex, viewPub.data(), viewPub.size(), sz) || sz != viewPub.size() ||
          !Common::fromHex(*spendPubHex, spendPub.data(), spendPub.size(), sz) || sz != spendPub.size()) {
        return;
      }

      // Same failsafe the send path applies: the keys the node returned must
      // fingerprint back to the A character carried in the number itself. Short,
      // so not an authentication -- but it catches a typo, a reorg, and a
      // careless substitution before any of it reaches the address book.
      const uint32_t gotFingerprint = CryptoNote::pqAccountFingerprint(
        CurrencyAdapter::instance().isTestnet(),
        spendPub.data(), spendPub.size(), viewPub.data(), viewPub.size());
      if (gotFingerprint != wantFingerprint) {
        QMetaObject::invokeMethod(this, [this]() {
          m_ui->m_addressEdit->setToolTip(
            tr("The keys on chain do not match this account number's check "
               "character. Check the number, and do not send to it."));
        }, Qt::QueuedConnection);
        return;
      }

      CryptoNote::PqAddress addr = CryptoNote::makePqAddress(CurrencyAdapter::instance().getNetworkPrefix(), viewPub, spendPub);
      const std::string hrp = CryptoNote::pqBech32Hrp(CurrencyAdapter::instance().isTestnet());
      const QString resolvedAddress = QString::fromStdString(CryptoNote::encodePqAddress(addr, hrp));

      QMetaObject::invokeMethod(this, [this, _input, resolvedAddress]() {
        m_resolvedAddress = resolvedAddress;
        // Don't put the ~5000-character resolved address in the visible text
        // field — it's unreadable and makes the field look broken. getAddress()
        // already prefers m_resolvedAddress over the field's text.
        const QString elided = resolvedAddress.left(20) + QStringLiteral("…") + resolvedAddress.right(12);
        m_ui->m_addressEdit->setText(QString("%1 (%2)").arg(_input, elided));
        m_ui->m_addressEdit->setToolTip(tr("Resolved to %1").arg(elided));
      }, Qt::QueuedConnection);
    });
}

}
