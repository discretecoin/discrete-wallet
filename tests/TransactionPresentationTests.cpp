// Copyright (c) 2026 The Discrete developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <iostream>

#include "gui/TransactionPresentation.h"

namespace {

bool require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

}  // namespace

int main() {
  using WalletGui::TransactionType;
  using WalletGui::transactionRowAmount;
  using WalletGui::transactionTypeForRow;

  bool ok = true;
  const qint64 recoveredOutgoingAmount = transactionRowAmount(99999, true, 1000000);
  ok = require(recoveredOutgoingAmount == -1000000,
               "recipient transfer must determine the displayed outgoing amount") && ok;
  ok = require(transactionTypeForRow(false, false, recoveredOutgoingAmount) ==
                   TransactionType::OUTPUT,
               "negative displayed amount must be classified as outgoing") && ok;
  ok = require(transactionTypeForRow(false, false,
                                     transactionRowAmount(500000, false, 0)) ==
                   TransactionType::INPUT,
               "positive transaction amount must remain incoming") && ok;
  ok = require(transactionTypeForRow(false, true, -1000000) ==
                   TransactionType::INOUT,
               "self-transfer classification must take precedence over amount") && ok;
  ok = require(transactionTypeForRow(true, false, 500000) ==
                   TransactionType::MINED,
               "coinbase classification must take precedence over amount") && ok;
  return ok ? 0 : 1;
}
