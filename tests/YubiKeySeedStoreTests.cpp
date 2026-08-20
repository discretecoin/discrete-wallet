// Copyright (c) 2026, The Discrete developers
// SPDX-License-Identifier: LGPL-3.0-or-later

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <iostream>

#include "security/YubiKeySeedStore.h"
#include "security/YubiKeyWalletFiles.h"
#include "WalletLegacy/WalletLegacySerializer.h"

using WalletGui::YubiKeySeedEnvelope;
using WalletGui::YubiKeySeedMetadata;
using WalletGui::YubiKeySeedStore;
using WalletGui::YubiKeyWalletFiles;

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

QString encoded(const QByteArray& value) {
  return QString::fromLatin1(value.toBase64(
      QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

bool sameEnvelope(const YubiKeySeedEnvelope& left,
                  const YubiKeySeedEnvelope& right) {
  return left.label == right.label &&
      left.credentialId == right.credentialId &&
      left.prfSalt == right.prfSalt &&
      left.nonce == right.nonce &&
      left.ciphertext == right.ciphertext &&
      left.tag == right.tag;
}

bool writeVersion1Sidecar(const QString& walletPath,
                          const YubiKeySeedEnvelope& envelope,
                          const QByteArray& binding) {
  QJsonObject object;
  object.insert(QStringLiteral("version"), 1);
  object.insert(QStringLiteral("algorithm"),
                QStringLiteral("webauthn-prf-hkdf-sha256-aes-256-gcm"));
  object.insert(QStringLiteral("credential_id"), encoded(envelope.credentialId));
  object.insert(QStringLiteral("prf_salt"), encoded(envelope.prfSalt));
  object.insert(QStringLiteral("nonce"), encoded(envelope.nonce));
  object.insert(QStringLiteral("ciphertext"), encoded(envelope.ciphertext));
  object.insert(QStringLiteral("tag"), encoded(envelope.tag));
  object.insert(QStringLiteral("wallet_binding"), encoded(binding));

  QFile file(YubiKeySeedStore::sidecarPath(walletPath));
  return file.open(QIODevice::WriteOnly) &&
      file.write(QJsonDocument(object).toJson(QJsonDocument::Indented)) > 0;
}

bool writeWalletVersionMarker(const QString& path, uint32_t version) {
  QFile file(path);
  const char marker = static_cast<char>(version);
  return file.open(QIODevice::WriteOnly) && file.write(&marker, 1) == 1;
}

bool writeFileBytes(const QString& path, const QByteArray& contents) {
  QFile file(path);
  return file.open(QIODevice::WriteOnly) &&
      file.write(contents) == contents.size() && file.flush();
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QString error;
  CryptoPQ::SeedMaster seed{};
  for (std::size_t i = 0; i < seed.size(); ++i) {
    seed[i] = static_cast<uint8_t>(i + 1);
  }

  const QByteArray binding = YubiKeySeedStore::walletBinding(bytes(100, 19));
  const QByteArray primaryPrf = bytes(32, 11);
  YubiKeySeedEnvelope primary;
  if (!require(YubiKeySeedStore::seal(
          seed, QStringLiteral("Primary YubiKey"), bytes(64, 3), bytes(32, 7),
          primaryPrf, binding, primary, error), qPrintable(error))) {
    return 1;
  }

  CryptoPQ::SeedMaster recovered{};
  if (!require(YubiKeySeedStore::unseal(
          primary, binding, primaryPrf, recovered, error), qPrintable(error)) ||
      !require(recovered == seed, "primary round-trip seed mismatch")) {
    return 1;
  }

  CryptoPQ::SeedMaster rejected{};
  if (!require(!YubiKeySeedStore::unseal(
          primary, binding, bytes(32, 12), rejected, error),
          "wrong PRF unexpectedly decrypted the seed")) {
    return 1;
  }
  QByteArray tamperedBinding = binding;
  tamperedBinding[0] ^= 1;
  if (!require(!YubiKeySeedStore::unseal(
          primary, tamperedBinding, primaryPrf, rejected, error),
          "tampered wallet binding unexpectedly decrypted the seed")) {
    return 1;
  }
  YubiKeySeedEnvelope tamperedEnvelope = primary;
  tamperedEnvelope.tag[0] ^= 1;
  if (!require(!YubiKeySeedStore::unseal(
          tamperedEnvelope, binding, primaryPrf, rejected, error),
          "tampered envelope unexpectedly decrypted the seed")) {
    return 1;
  }

  const QByteArray backupPrf = bytes(32, 51);
  YubiKeySeedEnvelope backup;
  if (!require(YubiKeySeedStore::seal(
          seed, QStringLiteral("Safe backup"), bytes(80, 23), bytes(32, 31),
          backupPrf, binding, backup, error), qPrintable(error)) ||
      !require(YubiKeySeedStore::unseal(
          backup, binding, backupPrf, recovered, error), qPrintable(error)) ||
      !require(recovered == seed, "backup round-trip seed mismatch") ||
      !require(!YubiKeySeedStore::keyFingerprint(primary).isEmpty() &&
               YubiKeySeedStore::keyFingerprint(primary) !=
                   YubiKeySeedStore::keyFingerprint(backup),
               "key fingerprints are missing or identical")) {
    return 1;
  }

  YubiKeySeedMetadata metadata;
  metadata.walletBinding = binding;
  metadata.keys = {primary, backup};

  QByteArray embedded;
  if (!require(YubiKeySeedStore::serialize(metadata, embedded, error),
               qPrintable(error)) ||
      !require(!embedded.isEmpty(), "embedded metadata serialization is empty")) {
    return 1;
  }
  YubiKeySeedMetadata embeddedLoaded;
  if (!require(YubiKeySeedStore::deserialize(
          embedded, embeddedLoaded, error), qPrintable(error)) ||
      !require(embeddedLoaded.walletBinding == metadata.walletBinding &&
               embeddedLoaded.keys.size() == 2 &&
               sameEnvelope(embeddedLoaded.keys.at(0), primary) &&
               sameEnvelope(embeddedLoaded.keys.at(1), backup),
               "embedded metadata round-trip mismatch")) {
    return 1;
  }
  QByteArray tamperedEmbedded = embedded;
  tamperedEmbedded[0] = '[';
  if (!require(!YubiKeySeedStore::deserialize(
          tamperedEmbedded, embeddedLoaded, error),
          "tampered embedded metadata was accepted")) {
    return 1;
  }

  QTemporaryDir directory;
  if (!require(directory.isValid(), "temporary directory creation failed")) {
    return 1;
  }

  const QString cleanupWalletPath =
      directory.filePath(QStringLiteral("cleanup.wallet"));
  const QString preYubiKeyPath = directory.filePath(
      QStringLiteral("cleanup.pre-yubikey-20260810-120000-000.wallet"));
  const QString unrelatedPreYubiKeyPath = directory.filePath(
      QStringLiteral("unrelated.pre-yubikey-20260810-120000-000.wallet"));
  const QString fullBackupPath =
      cleanupWalletPath + QStringLiteral(".backup");
  const QString fullTempPath = cleanupWalletPath + QStringLiteral(".temp");
  if (!require(writeWalletVersionMarker(
                   preYubiKeyPath,
                   CryptoNote::WalletLegacySerializer::STANDARD_VERSION) &&
               writeWalletVersionMarker(
                   unrelatedPreYubiKeyPath,
                   CryptoNote::WalletLegacySerializer::STANDARD_VERSION) &&
               writeWalletVersionMarker(
                   fullBackupPath,
                   CryptoNote::WalletLegacySerializer::STANDARD_VERSION) &&
               writeWalletVersionMarker(
                   fullTempPath,
                   CryptoNote::WalletLegacySerializer::STANDARD_VERSION),
               "bypass test files could not be created")) {
    return 1;
  }
  const QStringList bypassFiles =
      YubiKeyWalletFiles::bypassFiles(cleanupWalletPath);
  if (!require(bypassFiles.size() == 3,
               "known full-wallet bypass files were not detected") ||
      !require(bypassFiles.contains(preYubiKeyPath) &&
                   bypassFiles.contains(fullBackupPath) &&
                   bypassFiles.contains(fullTempPath) &&
                   !bypassFiles.contains(unrelatedPreYubiKeyPath),
               "bypass detection escaped the selected wallet scope")) {
    return 1;
  }
  QStringList removedBypassFiles;
  QStringList failedBypassFiles;
  if (!require(YubiKeyWalletFiles::removeBypassFiles(
                   cleanupWalletPath, removedBypassFiles,
                   failedBypassFiles),
               "known bypass files could not be directly removed") ||
      !require(removedBypassFiles.size() == 3 &&
                   failedBypassFiles.isEmpty() &&
                   !QFile::exists(preYubiKeyPath) &&
                   !QFile::exists(fullBackupPath) &&
                   !QFile::exists(fullTempPath) &&
                   QFile::exists(unrelatedPreYubiKeyPath),
               "bypass removal deleted the wrong files or retained a target")) {
    return 1;
  }
  if (!require(writeWalletVersionMarker(
                   fullBackupPath,
                   CryptoNote::WalletLegacySerializer::
                       PROTECTED_SPEND_VERSION) &&
               writeWalletVersionMarker(
                   fullTempPath,
                   CryptoNote::WalletLegacySerializer::
                       PROTECTED_SPEND_VERSION) &&
               YubiKeyWalletFiles::bypassFiles(cleanupWalletPath).isEmpty(),
               "protected version-3 wallet copies were treated as bypass files")) {
    return 1;
  }

  const QString replacementPath =
      directory.filePath(QStringLiteral("replacement.tmp"));
  const QString destinationPath =
      directory.filePath(QStringLiteral("replacement.wallet"));
  if (!require(writeFileBytes(replacementPath,
                              QByteArrayLiteral("protected")),
               "atomic replacement source could not be created") ||
      !require(writeFileBytes(destinationPath, QByteArrayLiteral("full")),
               "atomic replacement destination could not be created")) {
    return 1;
  }
  if (!require(YubiKeyWalletFiles::replaceFileAtomically(
                   replacementPath, destinationPath),
               "atomic protected-wallet replacement failed") ||
      !require(!QFile::exists(replacementPath),
               "atomic replacement left its source path behind")) {
    return 1;
  }
  QFile replaced(destinationPath);
  if (!require(replaced.open(QIODevice::ReadOnly) &&
                   replaced.readAll() == QByteArrayLiteral("protected"),
               "atomic replacement did not install the protected bytes")) {
    return 1;
  }

  const QString walletPath = directory.filePath(QStringLiteral("multi.wallet"));
  if (!require(YubiKeySeedStore::save(walletPath, metadata, error), qPrintable(error))) {
    return 1;
  }

  QFile savedFile(YubiKeySeedStore::sidecarPath(walletPath));
  if (!require(savedFile.open(QIODevice::ReadOnly), "saved sidecar cannot be read")) {
    return 1;
  }
  const QJsonObject savedObject = QJsonDocument::fromJson(savedFile.readAll()).object();
  if (!require(savedObject.value(QStringLiteral("version")).toInt() == 2,
               "multi-key sidecar was not written as version 2") ||
      !require(savedObject.value(QStringLiteral("keys")).toArray().size() == 2,
               "multi-key sidecar did not persist both entries")) {
    return 1;
  }

  YubiKeySeedMetadata loaded;
  if (!require(YubiKeySeedStore::load(walletPath, loaded, error), qPrintable(error)) ||
      !require(loaded.walletBinding == metadata.walletBinding &&
               loaded.keys.size() == 2 &&
               sameEnvelope(loaded.keys.at(0), primary) &&
               sameEnvelope(loaded.keys.at(1), backup),
               "version 2 metadata persistence mismatch")) {
    return 1;
  }

  const QString legacyWalletPath = directory.filePath(QStringLiteral("legacy.wallet"));
  if (!require(writeVersion1Sidecar(legacyWalletPath, primary, binding),
               "version 1 sidecar creation failed")) {
    return 1;
  }
  YubiKeySeedMetadata migrated;
  if (!require(YubiKeySeedStore::load(legacyWalletPath, migrated, error), qPrintable(error)) ||
      !require(migrated.walletBinding == binding && migrated.keys.size() == 1 &&
               sameEnvelope(migrated.keys.first(), primary),
               "version 1 sidecar did not load as one key") ||
      !require(YubiKeySeedStore::unseal(
          migrated.keys.first(), migrated.walletBinding, primaryPrf,
          recovered, error), qPrintable(error)) ||
      !require(recovered == seed, "version 1 compatibility seed mismatch")) {
    return 1;
  }
  if (!require(YubiKeySeedStore::save(legacyWalletPath, migrated, error),
               qPrintable(error))) {
    return 1;
  }
  QFile upgradedFile(YubiKeySeedStore::sidecarPath(legacyWalletPath));
  if (!require(upgradedFile.open(QIODevice::ReadOnly),
               "upgraded sidecar cannot be read") ||
      !require(QJsonDocument::fromJson(upgradedFile.readAll()).object()
                   .value(QStringLiteral("version")).toInt() == 2,
               "version 1 sidecar was not upgraded to version 2 on save")) {
    return 1;
  }

  YubiKeySeedMetadata duplicateLabel = metadata;
  duplicateLabel.keys[1].label = QStringLiteral("primary yubikey");
  if (!require(!YubiKeySeedStore::save(
          directory.filePath(QStringLiteral("duplicate-label.wallet")),
          duplicateLabel, error), "duplicate labels were accepted")) {
    return 1;
  }
  YubiKeySeedMetadata duplicateCredential = metadata;
  duplicateCredential.keys[1].credentialId = primary.credentialId;
  if (!require(!YubiKeySeedStore::save(
          directory.filePath(QStringLiteral("duplicate-credential.wallet")),
          duplicateCredential, error), "duplicate credentials were accepted")) {
    return 1;
  }
  YubiKeySeedMetadata empty;
  empty.walletBinding = binding;
  if (!require(!YubiKeySeedStore::save(
          directory.filePath(QStringLiteral("empty.wallet")), empty, error),
          "empty key list was accepted")) {
    return 1;
  }

  std::cout << "YubiKeySeedStoreTests: all checks passed\n";
  return 0;
}
