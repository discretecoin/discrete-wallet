// Copyright (c) 2026, The Discrete developers
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

#include "crypto_pq/PqSeed.h"

namespace WalletGui {

// One independently unlockable encrypted copy of the same wallet seed. Each
// entry is bound to a separate credential-scoped WebAuthn PRF result.
struct YubiKeySeedEnvelope {
  QString label;
  QByteArray credentialId;
  QByteArray prfSalt;
  QByteArray nonce;
  QByteArray ciphertext;
  QByteArray tag;
};

// Non-secret metadata stored next to a tracking-only .wallet file. Version 2
// stores one envelope per enrolled security key and a common wallet binding.
// load() also accepts the original single-key version 1 format.
struct YubiKeySeedMetadata {
  QList<YubiKeySeedEnvelope> keys;
  QByteArray walletBinding;
};

class YubiKeySeedStore {
public:
  static constexpr int MAX_KEY_COUNT = 8;

  static QString sidecarPath(const QString& walletPath);
  static bool exists(const QString& walletPath);

  static QByteArray walletBinding(const QByteArray& encodedTrackingKey);
  static QString keyFingerprint(const YubiKeySeedEnvelope& envelope);
  static bool seal(const CryptoPQ::SeedMaster& seedMaster,
                   const QString& label,
                   const QByteArray& credentialId,
                   const QByteArray& prfSalt,
                   const QByteArray& prfSecret,
                   const QByteArray& walletBinding,
                   YubiKeySeedEnvelope& envelope,
                   QString& error);
  static bool unseal(const YubiKeySeedEnvelope& envelope,
                     const QByteArray& walletBinding,
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
