// Copyright (c) 2011-2015 The Cryptonote developers
// Copyright (c) 2016-2018 The Karbowanec developers
// Copyright (c) 2026 The Discrete developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include <QString>

#include "CryptoNoteCore/Currency.h"

namespace WalletGui {

class CurrencyAdapter {

public:
  static CurrencyAdapter& instance();

  CryptoNote::Currency& getCurrency();
  QString getCurrencyDisplayName() const;
  QString getCurrencyName() const;
  QString getCurrencyTicker() const;
  quint64 getMinimumFee() const;
  quintptr getNumberOfDecimalPlaces() const;
  QString formatAmount(quint64 _amount) const;
  quint64 parseAmount(const QString& _amountString) const;
  bool isTestnet() const;

  // The network prefix fed to makePqAddress()/the bech32m HRP selection. Despite
  // the legacy name on Currency, this is not a base58 byte tag any more — it is
  // just the network-identifying constant baked into every PQ address.
  quint64 getNetworkPrefix() const;

  // True if `_address` is a well-formed destination: either a raw bech32m PQ
  // address for THIS network, or a syntactically valid account number (H-I-A-C or
  // the H-I-A-T-C deposit-subaddress form). This is a cheap, local syntax check
  // only — it does not confirm an account number is actually registered on
  // chain. That confirmation happens at send time (see PqRecipient.h).
  bool validateAddress(const QString& _address) const;

  // True if `_address` is an account number rather than a full address, i.e. a
  // destination the wallet can only complete by asking a node who the recipient
  // is. Callers use this to check the node is trusted for that before sending;
  // see NodeAdapter::isTrustedResolver().
  bool isAccountNumber(const QString& _address) const;

private:
  CryptoNote::Currency m_currency;

  CurrencyAdapter();
  ~CurrencyAdapter();
};

}
