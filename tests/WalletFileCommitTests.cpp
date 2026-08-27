// Copyright (c) 2026 The Discrete developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <iostream>

#include "security/WalletFileCommit.h"

namespace {

bool require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

QByteArray readAll(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  return file.readAll();
}

}  // namespace

int main() {
  QTemporaryDir directory;
  if (!require(directory.isValid(), "temporary directory creation failed")) {
    return 1;
  }

  const QString wallet = directory.filePath(QStringLiteral("test.wallet"));
  QFile original(wallet);
  if (!require(original.open(QIODevice::WriteOnly),
               "could not create original wallet") ||
      !require(original.write("old") == 3, "could not write original wallet")) {
    return 1;
  }
  original.close();

  QString error;
  bool ok = true;
  ok = require(WalletGui::commitWalletFile(wallet, "new-wallet", error),
               "atomic wallet commit failed") && ok;
  ok = require(readAll(wallet) == QByteArrayLiteral("new-wallet"),
               "new wallet bytes were not installed") && ok;
  const QFileDevice::Permissions permissions = QFileInfo(wallet).permissions();
  ok = require(permissions.testFlag(QFileDevice::ReadOwner) &&
                   permissions.testFlag(QFileDevice::WriteOwner),
               "wallet owner permissions are missing") && ok;
#ifndef Q_OS_WIN
  ok = require(!(permissions & (QFileDevice::ReadGroup |
                                QFileDevice::WriteGroup |
                                QFileDevice::ReadOther |
                                QFileDevice::WriteOther)),
               "wallet permissions are broader than owner-only") && ok;
#endif
  ok = require(QDir(directory.path()).entryList(
                   QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot) ==
                   QStringList{QStringLiteral("test.wallet")},
               "successful commit left a staging file") && ok;

  const QString missingParent = directory.filePath(
      QStringLiteral("missing/test.wallet"));
  error.clear();
  ok = require(!WalletGui::commitWalletFile(missingParent, "data", error),
               "commit unexpectedly succeeded without a parent directory") && ok;
  ok = require(!error.isEmpty(), "failed commit returned no error") && ok;
  ok = require(readAll(wallet) == QByteArrayLiteral("new-wallet"),
               "failed commit changed the existing wallet") && ok;
  ok = require(QDir(directory.path()).entryList(
                   QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot) ==
                   QStringList{QStringLiteral("test.wallet")},
               "failed commit left a staging file") && ok;

  ok = require(QDir().mkpath(QFileInfo(missingParent).absolutePath()),
               "could not create the retry directory") && ok;
  error.clear();
  ok = require(WalletGui::commitWalletFile(missingParent, "retry-data", error),
               "commit retry failed") && ok;
  ok = require(readAll(missingParent) == QByteArrayLiteral("retry-data"),
               "retry did not install the expected wallet bytes") && ok;
  ok = require(QDir(QFileInfo(missingParent).absolutePath()).entryList(
                   QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot) ==
                   QStringList{QStringLiteral("test.wallet")},
               "retry left a staging file") && ok;

  if (!ok) {
    return 1;
  }
  std::cout << "WalletFileCommitTests: all checks passed\n";
  return 0;
}
