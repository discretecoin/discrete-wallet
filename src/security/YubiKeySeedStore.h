// Copyright (c) 2026, The Discrete developers
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QString>

#include "crypto_pq/PqSeed.h"

namespace WalletGui {

// Non-secret metadata stored next to a tracking-only .wallet file. The seed is
// encrypted with AES-256-GCM under a key derived from a credential-scoped
// WebAuthn PRF (FIDO2 hmac-secret) result.
struct YubiKeySeedMetadata {
  QByteArray credentialId;
  QByteArray prfSalt;
  QByteArray nonce;
  QByteArray ciphertext;
  QByteArray tag;
  QByteArray walletBinding;
};

class YubiKeySeedStore {
public:
  static QString sidecarPath(const QString& walletPath);
  static bool exists(const QString& walletPath);

  static QByteArray walletBinding(const QByteArray& encodedTrackingKey);
  static bool seal(const CryptoPQ::SeedMaster& seedMaster,
                   const QByteArray& credentialId,
                   const QByteArray& prfSalt,
                   const QByteArray& prfSecret,
                   const QByteArray& walletBinding,
                   YubiKeySeedMetadata& metadata,
                   QString& error);
  static bool unseal(const YubiKeySeedMetadata& metadata,
                     const QByteArray& prfSecret,
                     CryptoPQ::SeedMaster& seedMaster,
                     QString& error);

  static bool save(const QString& walletPath,
                   const YubiKeySeedMetadata& metadata,
                   QString& error);
  static bool load(const QString& walletPath,
                   YubiKeySeedMetadata& metadata,
                   QString& error);
};

}  // namespace WalletGui
