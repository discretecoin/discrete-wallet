// Copyright (c) 2026, The Discrete developers
// SPDX-License-Identifier: LGPL-3.0-or-later

#include <QCoreApplication>
#include <QTemporaryDir>

#include <iostream>

#include "security/YubiKeySeedStore.h"

using WalletGui::YubiKeySeedMetadata;
using WalletGui::YubiKeySeedStore;

namespace {

bool require(bool condition, const char* message) {
  if (!condition) std::cerr << message << '\n';
  return condition;
}

QByteArray bytes(int size, char start) {
  QByteArray value(size, Qt::Uninitialized);
  for (int i = 0; i < size; ++i) value[i] = static_cast<char>(start + i);
  return value;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QString error;
  CryptoPQ::SeedMaster seed{};
  for (std::size_t i = 0; i < seed.size(); ++i) seed[i] = static_cast<uint8_t>(i + 1);

  const QByteArray credential = bytes(64, 3);
  const QByteArray salt = bytes(32, 7);
  const QByteArray prf = bytes(32, 11);
  const QByteArray binding = YubiKeySeedStore::walletBinding(bytes(100, 19));

  YubiKeySeedMetadata metadata;
  if (!require(YubiKeySeedStore::seal(seed, credential, salt, prf, binding, metadata, error),
               qPrintable(error))) return 1;

  CryptoPQ::SeedMaster recovered{};
  if (!require(YubiKeySeedStore::unseal(metadata, prf, recovered, error), qPrintable(error)) ||
      !require(recovered == seed, "round-trip seed mismatch")) return 1;

  CryptoPQ::SeedMaster rejected{};
  const QByteArray wrongPrf = bytes(32, 12);
  if (!require(!YubiKeySeedStore::unseal(metadata, wrongPrf, rejected, error),
               "wrong PRF unexpectedly decrypted the seed")) return 1;

  YubiKeySeedMetadata tampered = metadata;
  tampered.walletBinding[0] ^= 1;
  if (!require(!YubiKeySeedStore::unseal(tampered, prf, rejected, error),
               "tampered wallet binding unexpectedly decrypted the seed")) return 1;

  QTemporaryDir directory;
  if (!require(directory.isValid(), "temporary directory creation failed")) return 1;
  const QString walletPath = directory.filePath(QStringLiteral("test.wallet"));
  if (!require(YubiKeySeedStore::save(walletPath, metadata, error), qPrintable(error))) return 1;
  YubiKeySeedMetadata loaded;
  if (!require(YubiKeySeedStore::load(walletPath, loaded, error), qPrintable(error)) ||
      !require(loaded.credentialId == metadata.credentialId &&
               loaded.prfSalt == metadata.prfSalt &&
               loaded.nonce == metadata.nonce &&
               loaded.ciphertext == metadata.ciphertext &&
               loaded.tag == metadata.tag &&
               loaded.walletBinding == metadata.walletBinding,
               "metadata persistence mismatch")) return 1;

  std::cout << "YubiKeySeedStoreTests: all checks passed\n";
  return 0;
}
