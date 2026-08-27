// Copyright (c) 2026 The Discrete developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "WalletFileCommit.h"

#include <QSaveFile>

namespace WalletGui {

bool commitWalletFile(const QString& destination, const std::string& data,
                      QString& errorText) {
  errorText.clear();
  QSaveFile file(destination);
  file.setDirectWriteFallback(false);
  if (!file.open(QIODevice::WriteOnly)) {
    errorText = file.errorString();
    return false;
  }
  if (!file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
    errorText = file.errorString();
    file.cancelWriting();
    return false;
  }

  qint64 offset = 0;
  const qint64 size = static_cast<qint64>(data.size());
  while (offset < size) {
    const qint64 written = file.write(data.data() + offset, size - offset);
    if (written <= 0) {
      errorText = file.errorString();
      file.cancelWriting();
      return false;
    }
    offset += written;
  }
  if (!file.commit()) {
    errorText = file.errorString();
    return false;
  }
  return true;
}

}  // namespace WalletGui
