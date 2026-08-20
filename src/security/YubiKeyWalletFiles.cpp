// Copyright (c) 2026 The Discrete developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "YubiKeyWalletFiles.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <cstdio>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

#include "WalletLegacy/WalletLegacySerializer.h"

namespace WalletGui {

QStringList YubiKeyWalletFiles::bypassFiles(const QString& walletPath) {
  QStringList files;
  if (walletPath.isEmpty()) {
    return files;
  }

  const QFileInfo walletInfo(walletPath);
  const QString preYubiKeyPattern =
      walletInfo.completeBaseName() + QStringLiteral(".pre-yubikey-*.wallet");
  const QFileInfoList preYubiKeyFiles = walletInfo.dir().entryInfoList(
      QStringList{preYubiKeyPattern}, QDir::Files | QDir::NoSymLinks,
      QDir::Name);
  for (const QFileInfo& file : preYubiKeyFiles) {
    files.append(file.absoluteFilePath());
  }

  // The wallet serialization version is the first varint byte and is not
  // encrypted. Version 3 is the tracking-only protected-spend format; older
  // or unreadable automatic files are treated as bypass candidates.
  const auto appendIfNotProtected = [&files](const QString& path) {
    if (!QFile::exists(path)) {
      return;
    }
    QFile file(path);
    char version = 0;
    const bool readVersion = file.open(QIODevice::ReadOnly) &&
        file.read(&version, 1) == 1;
    if (!readVersion || static_cast<unsigned char>(version) !=
                            CryptoNote::WalletLegacySerializer::
                                PROTECTED_SPEND_VERSION) {
      files.append(QFileInfo(path).absoluteFilePath());
    }
  };
  appendIfNotProtected(walletPath + QStringLiteral(".backup"));
  appendIfNotProtected(walletPath + QStringLiteral(".temp"));

  files.removeDuplicates();
  return files;
}

bool YubiKeyWalletFiles::removeBypassFiles(
    const QString& walletPath, QStringList& removedFiles,
    QStringList& failedFiles) {
  removedFiles.clear();
  failedFiles.clear();
  const QStringList targets = bypassFiles(walletPath);
  for (const QString& target : targets) {
    if (!QFile::exists(target)) {
      continue;
    }
    // QFile::remove maps to direct filesystem deletion. It does not call the
    // Windows shell and therefore never sends the file to the Recycle Bin.
    if (QFile::remove(target)) {
      removedFiles.append(target);
    } else {
      failedFiles.append(target);
    }
  }
  return failedFiles.isEmpty();
}

bool YubiKeyWalletFiles::replaceFileAtomically(
    const QString& replacementPath, const QString& destinationPath) {
  if (!QFile::exists(replacementPath)) {
    return false;
  }
#ifdef Q_OS_WIN
  return ::MoveFileExW(
      reinterpret_cast<LPCWSTR>(replacementPath.utf16()),
      reinterpret_cast<LPCWSTR>(destinationPath.utf16()),
      MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
  const QByteArray replacement = QFile::encodeName(replacementPath);
  const QByteArray destination = QFile::encodeName(destinationPath);
  return std::rename(replacement.constData(), destination.constData()) == 0;
#endif
}

}  // namespace WalletGui
