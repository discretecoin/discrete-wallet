// Copyright (c) 2026 The Discrete developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include <QString>
#include <QStringList>

namespace WalletGui {

class YubiKeyWalletFiles {
public:
  static QStringList bypassFiles(const QString& walletPath);
  static bool removeBypassFiles(const QString& walletPath,
                                QStringList& removedFiles,
                                QStringList& failedFiles);
  static bool replaceFileAtomically(const QString& replacementPath,
                                    const QString& destinationPath);
};

}  // namespace WalletGui
