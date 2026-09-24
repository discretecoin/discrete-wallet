// Copyright (c) 2026 The Discrete developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include <QtGlobal>

namespace WalletGui {

enum class TransactionType : quint8 {MINED, INPUT, OUTPUT, INOUT};

inline qint64 transactionRowAmount(qint64 transactionAmount, bool hasTransfer,
                                   qint64 transferAmount) {
  return hasTransfer ? -transferAmount : transactionAmount;
}

inline TransactionType transactionTypeForRow(bool isCoinbase, bool isSelfTransfer,
                                             qint64 rowAmount) {
  if (isCoinbase) {
    return TransactionType::MINED;
  }
  if (isSelfTransfer) {
    return TransactionType::INOUT;
  }
  return rowAmount < 0 ? TransactionType::OUTPUT : TransactionType::INPUT;
}

}  // namespace WalletGui
