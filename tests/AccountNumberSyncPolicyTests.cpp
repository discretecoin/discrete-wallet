// Copyright (c) 2026 The Discrete developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <iostream>

#include "gui/AccountNumberSyncPolicy.h"

namespace {

bool require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

}  // namespace

int main() {
  using WalletGui::accountNumberLookupReady;
  using WalletGui::showAccountNumberRegistration;

  bool ok = true;
  const bool early = accountNumberLookupReady(true, true, true, true, 8000, 57329);
  ok = require(!early, "an early wallet sync must not expose registration") && ok;
  ok = require(!accountNumberLookupReady(true, false, true, true, 57331, 57331),
               "node catch-up alone is insufficient while the wallet scans") && ok;
  ok = require(!accountNumberLookupReady(true, true, true, true, 57331, 0),
               "unknown network height is not full synchronization") && ok;
  ok = require(!accountNumberLookupReady(true, true, true, false, 57331, 57331),
               "disconnected node cannot confirm missing registration") && ok;
  ok = require(!accountNumberLookupReady(true, true, false, true, 57331, 57331),
               "untrusted resolver cannot confirm missing registration") && ok;
  ok = require(!accountNumberLookupReady(false, true, true, true, 57331, 57331),
               "closed wallet cannot offer registration") && ok;

  const bool ready = accountNumberLookupReady(true, true, true, true, 57331, 57331);
  ok = require(ready, "fully synchronized trusted lookup must proceed") && ok;
  ok = require(!showAccountNumberRegistration(false, false, false, false, ready, true),
               "button must stay hidden until lookup succeeds") && ok;
  ok = require(!showAccountNumberRegistration(false, false, false, true, ready, true),
               "button must stay hidden while lookup is running") && ok;
  ok = require(!showAccountNumberRegistration(false, false, true, false, early, true),
               "stale no-registration result must not expose button during catch-up") && ok;
  ok = require(showAccountNumberRegistration(false, false, true, false, ready, true),
               "confirmed absence after full sync may offer registration") && ok;
  ok = require(!showAccountNumberRegistration(true, false, true, false, ready, true),
               "registered account must never offer another registration") && ok;
  ok = require(!showAccountNumberRegistration(false, true, true, false, ready, true),
               "pending registration must not offer a duplicate") && ok;
  ok = require(!showAccountNumberRegistration(false, false, true, false, ready, false),
               "tracking or protected wallet must not offer registration") && ok;
  return ok ? 0 : 1;
}
