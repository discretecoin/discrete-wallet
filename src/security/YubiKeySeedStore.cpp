// Copyright (c) 2026, The Discrete developers
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "YubiKeySeedStore.h"

#include <algorithm>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/rand.h>

namespace WalletGui {
namespace {

constexpr int kSeedSize = 32;
constexpr int kPrfSize = 32;
constexpr int kSaltSize = 32;
constexpr int kNonceSize = 12;
constexpr int kTagSize = 16;
constexpr char kAlgorithm[] = "webauthn-prf-hkdf-sha256-aes-256-gcm";
constexpr char kAadDomain[] = "DiscreteWallet/YubiKeySeed/v1";
constexpr char kKdfInfo[] = "Discrete Wallet YubiKey protected spending v1";
constexpr int kMaxLabelSize = 64;

QByteArray encode(const QByteArray& value) {
  return value.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

bool decodeField(const QJsonObject& object, const char* name, int expectedSize,
                 QByteArray& value, QString& error) {
  const QJsonValue field = object.value(QLatin1String(name));
  if (!field.isString()) {
    error = QObject::tr("YubiKey metadata field '%1' is missing.").arg(QLatin1String(name));
    return false;
  }
  value = QByteArray::fromBase64(field.toString().toLatin1(), QByteArray::Base64UrlEncoding);
  if (value.size() != expectedSize) {
    error = QObject::tr("YubiKey metadata field '%1' has an invalid length.").arg(QLatin1String(name));
    return false;
  }
  return true;
}

bool decodeCredential(const QJsonObject& object, QByteArray& credentialId,
                      QString& error) {
  const QJsonValue credentialField = object.value(QStringLiteral("credential_id"));
  if (!credentialField.isString()) {
    error = QObject::tr("YubiKey metadata field 'credential_id' is missing.");
    return false;
  }
  credentialId = QByteArray::fromBase64(
      credentialField.toString().toLatin1(), QByteArray::Base64UrlEncoding);
  if (credentialId.isEmpty() || credentialId.size() > 1024) {
    error = QObject::tr("The YubiKey credential ID has an invalid length.");
    return false;
  }
  return true;
}

bool validLabel(const QString& label) {
  return !label.trimmed().isEmpty() && label.size() <= kMaxLabelSize;
}

bool decodeEnvelope(const QJsonObject& object, const QString& fallbackLabel,
                    YubiKeySeedEnvelope& envelope, QString& error) {
  const QJsonValue labelField = object.value(QStringLiteral("label"));
  const QString label = labelField.isString() ? labelField.toString().trimmed()
                                               : fallbackLabel;
  if (!validLabel(label)) {
    error = QObject::tr("The YubiKey label is missing or too long.");
    return false;
  }

  YubiKeySeedEnvelope parsed;
  parsed.label = label;
  if (!decodeCredential(object, parsed.credentialId, error) ||
      !decodeField(object, "prf_salt", kSaltSize, parsed.prfSalt, error) ||
      !decodeField(object, "nonce", kNonceSize, parsed.nonce, error) ||
      !decodeField(object, "ciphertext", kSeedSize, parsed.ciphertext, error) ||
      !decodeField(object, "tag", kTagSize, parsed.tag, error)) {
    return false;
  }
  envelope = parsed;
  return true;
}

bool validateMetadata(const YubiKeySeedMetadata& metadata, QString& error) {
  if (metadata.walletBinding.size() != 32) {
    error = QObject::tr("The YubiKey wallet binding has an invalid length.");
    return false;
  }
  if (metadata.keys.isEmpty() ||
      metadata.keys.size() > YubiKeySeedStore::MAX_KEY_COUNT) {
    error = QObject::tr("The YubiKey metadata must contain between 1 and %1 security keys.")
        .arg(YubiKeySeedStore::MAX_KEY_COUNT);
    return false;
  }

  QSet<QString> labels;
  QSet<QByteArray> credentials;
  for (const YubiKeySeedEnvelope& envelope : metadata.keys) {
    const QString normalizedLabel = envelope.label.trimmed().toCaseFolded();
    if (!validLabel(envelope.label) || labels.contains(normalizedLabel)) {
      error = QObject::tr("Every enrolled YubiKey must have a unique non-empty label of at most %1 characters.")
          .arg(kMaxLabelSize);
      return false;
    }
    if (envelope.credentialId.isEmpty() || envelope.credentialId.size() > 1024 ||
        credentials.contains(envelope.credentialId) ||
        envelope.prfSalt.size() != kSaltSize ||
        envelope.nonce.size() != kNonceSize ||
        envelope.ciphertext.size() != kSeedSize ||
        envelope.tag.size() != kTagSize) {
      error = QObject::tr("An enrolled YubiKey entry is invalid or duplicated.");
      return false;
    }
    labels.insert(normalizedLabel);
    credentials.insert(envelope.credentialId);
  }
  return true;
}

bool deriveKey(const QByteArray& prfSecret, const QByteArray& binding,
               QByteArray& key, QString& error) {
  if (prfSecret.size() != kPrfSize || binding.size() != 32) {
    error = QObject::tr("The YubiKey PRF result or wallet binding has an invalid length.");
    return false;
  }

  key.resize(32);
  EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr);
  if (ctx == nullptr || EVP_PKEY_derive_init(ctx) <= 0 ||
      EVP_PKEY_CTX_set_hkdf_md(ctx, EVP_sha256()) <= 0 ||
      EVP_PKEY_CTX_set1_hkdf_salt(ctx,
          reinterpret_cast<const unsigned char*>(binding.constData()), binding.size()) <= 0 ||
      EVP_PKEY_CTX_set1_hkdf_key(ctx,
          reinterpret_cast<const unsigned char*>(prfSecret.constData()), prfSecret.size()) <= 0 ||
      EVP_PKEY_CTX_add1_hkdf_info(ctx,
          reinterpret_cast<const unsigned char*>(kKdfInfo), sizeof(kKdfInfo) - 1) <= 0) {
    EVP_PKEY_CTX_free(ctx);
    OPENSSL_cleanse(key.data(), static_cast<size_t>(key.size()));
    key.clear();
    error = QObject::tr("OpenSSL could not initialize the YubiKey key derivation.");
    return false;
  }

  size_t length = static_cast<size_t>(key.size());
  const bool ok = EVP_PKEY_derive(ctx, reinterpret_cast<unsigned char*>(key.data()), &length) > 0 &&
                  length == static_cast<size_t>(key.size());
  EVP_PKEY_CTX_free(ctx);
  if (!ok) {
    OPENSSL_cleanse(key.data(), static_cast<size_t>(key.size()));
    key.clear();
    error = QObject::tr("OpenSSL could not derive the YubiKey encryption key.");
  }
  return ok;
}

QByteArray aad(const QByteArray& binding) {
  QByteArray result(kAadDomain, sizeof(kAadDomain) - 1);
  result.append('\0');
  result.append(binding);
  return result;
}

}  // namespace

QString YubiKeySeedStore::sidecarPath(const QString& walletPath) {
  return walletPath + QStringLiteral(".yubikey.json");
}

bool YubiKeySeedStore::exists(const QString& walletPath) {
  return QFile::exists(sidecarPath(walletPath));
}

QByteArray YubiKeySeedStore::walletBinding(const QByteArray& encodedTrackingKey) {
  return QCryptographicHash::hash(encodedTrackingKey, QCryptographicHash::Sha256);
}

QString YubiKeySeedStore::keyFingerprint(const YubiKeySeedEnvelope& envelope) {
  return QString::fromLatin1(
      QCryptographicHash::hash(envelope.credentialId, QCryptographicHash::Sha256)
          .first(4).toHex().toUpper());
}

bool YubiKeySeedStore::seal(const CryptoPQ::SeedMaster& seedMaster,
                            const QString& label,
                            const QByteArray& credentialId,
                            const QByteArray& prfSalt,
                            const QByteArray& prfSecret,
                            const QByteArray& binding,
                            YubiKeySeedEnvelope& envelope,
                            QString& error) {
  error.clear();
  const QString normalizedLabel = label.trimmed();
  if (!validLabel(normalizedLabel) || credentialId.isEmpty() || credentialId.size() > 1024 ||
      prfSalt.size() != kSaltSize ||
      prfSecret.size() != kPrfSize || binding.size() != 32) {
    error = QObject::tr("Cannot protect the seed because the YubiKey data is incomplete.");
    return false;
  }

  QByteArray key;
  if (!deriveKey(prfSecret, binding, key, error)) {
    return false;
  }

  QByteArray nonce(kNonceSize, Qt::Uninitialized);
  if (RAND_bytes(reinterpret_cast<unsigned char*>(nonce.data()), nonce.size()) != 1) {
    OPENSSL_cleanse(key.data(), static_cast<size_t>(key.size()));
    error = QObject::tr("OpenSSL could not generate an encryption nonce.");
    return false;
  }

  QByteArray plain(reinterpret_cast<const char*>(seedMaster.data()), seedMaster.size());
  QByteArray cipher(kSeedSize, Qt::Uninitialized);
  QByteArray tag(kTagSize, Qt::Uninitialized);
  const QByteArray associated = aad(binding);
  EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
  int length = 0;
  int written = 0;
  bool ok = ctx != nullptr &&
      EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1 &&
      EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, nonce.size(), nullptr) == 1 &&
      EVP_EncryptInit_ex(ctx, nullptr, nullptr,
          reinterpret_cast<const unsigned char*>(key.constData()),
          reinterpret_cast<const unsigned char*>(nonce.constData())) == 1 &&
      EVP_EncryptUpdate(ctx, nullptr, &length,
          reinterpret_cast<const unsigned char*>(associated.constData()), associated.size()) == 1 &&
      EVP_EncryptUpdate(ctx, reinterpret_cast<unsigned char*>(cipher.data()), &written,
          reinterpret_cast<const unsigned char*>(plain.constData()), plain.size()) == 1;
  int finalLength = 0;
  ok = ok && EVP_EncryptFinal_ex(ctx,
      reinterpret_cast<unsigned char*>(cipher.data()) + written, &finalLength) == 1;
  written += finalLength;
  ok = ok && written == kSeedSize &&
      EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, tag.size(), tag.data()) == 1;
  EVP_CIPHER_CTX_free(ctx);
  OPENSSL_cleanse(key.data(), static_cast<size_t>(key.size()));
  OPENSSL_cleanse(plain.data(), static_cast<size_t>(plain.size()));

  if (!ok) {
    error = QObject::tr("OpenSSL could not encrypt the wallet seed.");
    return false;
  }

  envelope = YubiKeySeedEnvelope{
      normalizedLabel, credentialId, prfSalt, nonce, cipher, tag};
  return true;
}

bool YubiKeySeedStore::unseal(const YubiKeySeedEnvelope& envelope,
                              const QByteArray& walletBinding,
                              const QByteArray& prfSecret,
                              CryptoPQ::SeedMaster& seedMaster,
                              QString& error) {
  error.clear();
  OPENSSL_cleanse(seedMaster.data(), seedMaster.size());
  QByteArray key;
  if (!deriveKey(prfSecret, walletBinding, key, error)) {
    return false;
  }

  QByteArray plain(kSeedSize, Qt::Uninitialized);
  const QByteArray associated = aad(walletBinding);
  EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
  int length = 0;
  int written = 0;
  bool ok = ctx != nullptr &&
      EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1 &&
      EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, envelope.nonce.size(), nullptr) == 1 &&
      EVP_DecryptInit_ex(ctx, nullptr, nullptr,
          reinterpret_cast<const unsigned char*>(key.constData()),
          reinterpret_cast<const unsigned char*>(envelope.nonce.constData())) == 1 &&
      EVP_DecryptUpdate(ctx, nullptr, &length,
          reinterpret_cast<const unsigned char*>(associated.constData()), associated.size()) == 1 &&
      EVP_DecryptUpdate(ctx, reinterpret_cast<unsigned char*>(plain.data()), &written,
          reinterpret_cast<const unsigned char*>(envelope.ciphertext.constData()),
          envelope.ciphertext.size()) == 1 &&
      EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, envelope.tag.size(),
                          const_cast<char*>(envelope.tag.constData())) == 1;
  int finalLength = 0;
  ok = ok && EVP_DecryptFinal_ex(ctx,
      reinterpret_cast<unsigned char*>(plain.data()) + written, &finalLength) == 1;
  written += finalLength;
  EVP_CIPHER_CTX_free(ctx);
  OPENSSL_cleanse(key.data(), static_cast<size_t>(key.size()));

  if (!ok || written != kSeedSize) {
    OPENSSL_cleanse(plain.data(), static_cast<size_t>(plain.size()));
    error = QObject::tr("The YubiKey response cannot decrypt this wallet seed. The wrong key may have been used or the metadata was changed.");
    return false;
  }

  std::copy(plain.cbegin(), plain.cend(), reinterpret_cast<char*>(seedMaster.data()));
  OPENSSL_cleanse(plain.data(), static_cast<size_t>(plain.size()));
  return true;
}

bool YubiKeySeedStore::save(const QString& walletPath,
                            const YubiKeySeedMetadata& metadata,
                            QString& error) {
  error.clear();
  if (!validateMetadata(metadata, error)) {
    return false;
  }

  QJsonObject object;
  object.insert(QStringLiteral("version"), 2);
  object.insert(QStringLiteral("algorithm"), QLatin1String(kAlgorithm));
  object.insert(QStringLiteral("wallet_binding"), QString::fromLatin1(encode(metadata.walletBinding)));
  QJsonArray keys;
  for (const YubiKeySeedEnvelope& envelope : metadata.keys) {
    QJsonObject key;
    key.insert(QStringLiteral("label"), envelope.label.trimmed());
    key.insert(QStringLiteral("credential_id"), QString::fromLatin1(encode(envelope.credentialId)));
    key.insert(QStringLiteral("prf_salt"), QString::fromLatin1(encode(envelope.prfSalt)));
    key.insert(QStringLiteral("nonce"), QString::fromLatin1(encode(envelope.nonce)));
    key.insert(QStringLiteral("ciphertext"), QString::fromLatin1(encode(envelope.ciphertext)));
    key.insert(QStringLiteral("tag"), QString::fromLatin1(encode(envelope.tag)));
    keys.append(key);
  }
  object.insert(QStringLiteral("keys"), keys);

  QSaveFile file(sidecarPath(walletPath));
  if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(object).toJson(QJsonDocument::Indented)) < 0 ||
      !file.commit()) {
    error = QObject::tr("Could not atomically write the YubiKey metadata file: %1").arg(file.errorString());
    return false;
  }
  error.clear();
  return true;
}

bool YubiKeySeedStore::load(const QString& walletPath,
                            YubiKeySeedMetadata& metadata,
                            QString& error) {
  QFile file(sidecarPath(walletPath));
  if (!file.open(QIODevice::ReadOnly)) {
    error = QObject::tr("Could not open the YubiKey metadata file: %1").arg(file.errorString());
    return false;
  }
  QJsonParseError parseError;
  const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
    error = QObject::tr("The YubiKey metadata file is not valid JSON.");
    return false;
  }
  const QJsonObject object = document.object();
  const int version = object.value(QStringLiteral("version")).toInt();
  if ((version != 1 && version != 2) ||
      object.value(QStringLiteral("algorithm")).toString() != QLatin1String(kAlgorithm)) {
    error = QObject::tr("This YubiKey metadata version or algorithm is not supported.");
    return false;
  }

  YubiKeySeedMetadata parsed;
  if (!decodeField(object, "wallet_binding", 32, parsed.walletBinding, error)) {
    return false;
  }

  if (version == 1) {
    YubiKeySeedEnvelope envelope;
    if (!decodeEnvelope(object, QObject::tr("Primary YubiKey"), envelope, error)) {
      return false;
    }
    parsed.keys.append(envelope);
  } else {
    const QJsonValue keysField = object.value(QStringLiteral("keys"));
    if (!keysField.isArray()) {
      error = QObject::tr("YubiKey metadata field 'keys' is missing.");
      return false;
    }
    const QJsonArray keys = keysField.toArray();
    if (keys.isEmpty() || keys.size() > MAX_KEY_COUNT) {
      error = QObject::tr("The YubiKey metadata must contain between 1 and %1 security keys.")
          .arg(MAX_KEY_COUNT);
      return false;
    }
    for (const QJsonValue& value : keys) {
      if (!value.isObject()) {
        error = QObject::tr("A YubiKey metadata entry is not a JSON object.");
        return false;
      }
      YubiKeySeedEnvelope envelope;
      if (!decodeEnvelope(value.toObject(), QString(), envelope, error)) {
        return false;
      }
      parsed.keys.append(envelope);
    }
  }

  if (!validateMetadata(parsed, error)) {
    return false;
  }
  metadata = parsed;
  error.clear();
  return true;
}

}  // namespace WalletGui
