// Copyright (c) 2026 The Discrete developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include <QString>

#include <string>

namespace WalletGui {

bool commitWalletFile(const QString& destination, const std::string& data,
                      QString& errorText);

}  // namespace WalletGui
