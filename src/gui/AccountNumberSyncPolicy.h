// Copyright (c) 2026 The Discrete developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include <QtGlobal>

namespace WalletGui {

inline bool accountNumberLookupReady(bool walletOpen, bool walletSynchronized,
                                     bool trustedResolver, bool nodeConnected,
                                     quint64 localHeight, quint64 knownHeight) {
  return walletOpen && walletSynchronized && trustedResolver && nodeConnected &&
         knownHeight > 0 && localHeight >= knownHeight;
}

inline bool showAccountNumberRegistration(bool hasNumber, bool pending,
                                          bool lookupResolved, bool lookupInProgress,
                                          bool lookupReady, bool canRegister) {
  return !hasNumber && !pending && lookupResolved && !lookupInProgress &&
         lookupReady && canRegister;
}

}  // namespace WalletGui
