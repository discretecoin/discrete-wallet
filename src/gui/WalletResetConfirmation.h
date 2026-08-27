// Copyright (c) 2026 The Discrete developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include <functional>

class QWidget;

namespace WalletGui {

bool walletRebuildAllowed(bool walletOpen, bool rebuildInProgress,
                          bool discardUncommittedRebuild);

bool confirmDestructiveWalletReset(
    QWidget* parent, const std::function<bool()>& stillCurrent);

}  // namespace WalletGui
