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

inline qint64 transactionDisplayAmount(qint64 transactionAmount, quint64 fee,
                                      bool hasTransfer, qint64 transferAmount) {
  if (hasTransfer || transactionAmount >= 0 || fee == 0) {
    return transactionRowAmount(transactionAmount, hasTransfer, transferAmount);
  }

  // A PQ history row without a saved recipient transfer contains a net debit.
  // That debit includes the fee, which History displays in a separate column.
  const quint64 debit = static_cast<quint64>(-(transactionAmount + 1)) + 1;
  if (fee > debit) {
    return transactionAmount;
  }
  return -static_cast<qint64>(debit - fee);
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
