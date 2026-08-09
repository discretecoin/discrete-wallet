// Copyright (c) 2026, The Discrete developers
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "WindowsWebAuthnPrf.h"

#include <QJsonDocument>
#include <QJsonObject>

#include <cwchar>

#include <openssl/crypto.h>
#include <openssl/rand.h>

#ifdef Q_OS_WIN
#include <windows.h>
#include <webauthn.h>
#endif

namespace WalletGui {
namespace {

constexpr int kSecretSize = 32;
constexpr wchar_t kRpId[] = L"wallet.discrete.cash";
constexpr wchar_t kRpName[] = L"Discrete Wallet";

#ifdef Q_OS_WIN
HWND prepareWebAuthnParent(WId parentWindow) {
  HWND window = reinterpret_cast<HWND>(parentWindow);
  if (window != nullptr && IsWindow(window)) {
    // Do this synchronously immediately before the WebAuthn call. Qt's
    // activateWindow() is asynchronous and can otherwise raise the wallet
    // after Windows Security has already opened.
    SetForegroundWindow(window);
    SetFocus(window);
  }
  return window;
}

QByteArray randomBytes(int size, QString& error) {
  QByteArray bytes(size, Qt::Uninitialized);
  if (RAND_bytes(reinterpret_cast<unsigned char*>(bytes.data()), bytes.size()) != 1) {
    error = QObject::tr("OpenSSL could not generate the WebAuthn challenge.");
    return {};
  }
  return bytes;
}

QByteArray clientData(const char* type, QString& error) {
  const QByteArray challenge = randomBytes(kSecretSize, error);
  if (challenge.isEmpty()) return {};
  QJsonObject object;
  object.insert(QStringLiteral("type"), QLatin1String(type));
  object.insert(QStringLiteral("challenge"), QString::fromLatin1(
      challenge.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)));
  object.insert(QStringLiteral("origin"), QStringLiteral("https://wallet.discrete.cash"));
  object.insert(QStringLiteral("crossOrigin"), false);
  return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QString webAuthnError(HRESULT result, const QString& action) {
  const PCWSTR name = WebAuthNGetErrorName(result);
  const QString detail = name == nullptr
      ? QStringLiteral("0x%1").arg(static_cast<qulonglong>(result), 8, 16, QLatin1Char('0'))
      : QString::fromWCharArray(name);
  return QObject::tr("%1 failed: %2").arg(action, detail);
}

bool boolExtensionEnabled(const WEBAUTHN_EXTENSIONS& extensions,
                          const wchar_t* identifier) {
  if (extensions.pExtensions == nullptr) {
    return false;
  }
  for (DWORD index = 0; index < extensions.cExtensions; ++index) {
    const WEBAUTHN_EXTENSION& extension = extensions.pExtensions[index];
    if (extension.pwszExtensionIdentifier != nullptr &&
        std::wcscmp(extension.pwszExtensionIdentifier, identifier) == 0 &&
        extension.cbExtension == sizeof(BOOL) &&
        extension.pvExtension != nullptr) {
      return *static_cast<const BOOL*>(extension.pvExtension) != FALSE;
    }
  }
  return false;
}

#endif

}  // namespace

bool WindowsWebAuthnPrf::isSupported(QString& error) {
#ifdef Q_OS_WIN
  if (WebAuthNGetApiVersionNumber() < WEBAUTHN_API_VERSION_6) {
    error = QObject::tr("This Windows version does not provide the WebAuthn PRF API required for YubiKey protected spending.");
    return false;
  }
  error.clear();
  return true;
#else
  error = QObject::tr("YubiKey protected spending is currently available only on Windows.");
  return false;
#endif
}

bool WindowsWebAuthnPrf::enroll(WId parentWindow, const QByteArray& walletBinding,
                                Enrollment& enrollment, QString& error) {
#ifdef Q_OS_WIN
  if (!isSupported(error)) return false;
  if (walletBinding.size() != kSecretSize) {
    error = QObject::tr("The wallet identity binding is invalid.");
    return false;
  }

  QByteArray userId = walletBinding;
  QByteArray data = clientData("webauthn.create", error);
  if (data.isEmpty()) return false;

  WEBAUTHN_RP_ENTITY_INFORMATION rp{};
  rp.dwVersion = WEBAUTHN_RP_ENTITY_INFORMATION_CURRENT_VERSION;
  rp.pwszId = kRpId;
  rp.pwszName = kRpName;

  WEBAUTHN_USER_ENTITY_INFORMATION user{};
  user.dwVersion = WEBAUTHN_USER_ENTITY_INFORMATION_CURRENT_VERSION;
  user.cbId = static_cast<DWORD>(userId.size());
  user.pbId = reinterpret_cast<PBYTE>(userId.data());
  user.pwszName = L"wallet";
  user.pwszDisplayName = L"Discrete Wallet spending key";

  WEBAUTHN_COSE_CREDENTIAL_PARAMETER parameter{};
  parameter.dwVersion = WEBAUTHN_COSE_CREDENTIAL_PARAMETER_CURRENT_VERSION;
  parameter.pwszCredentialType = WEBAUTHN_CREDENTIAL_TYPE_PUBLIC_KEY;
  parameter.lAlg = WEBAUTHN_COSE_ALGORITHM_ECDSA_P256_WITH_SHA256;
  WEBAUTHN_COSE_CREDENTIAL_PARAMETERS parameters{1, &parameter};

  WEBAUTHN_CLIENT_DATA client{};
  client.dwVersion = WEBAUTHN_CLIENT_DATA_CURRENT_VERSION;
  client.cbClientDataJSON = static_cast<DWORD>(data.size());
  client.pbClientDataJSON = reinterpret_cast<PBYTE>(data.data());
  client.pwszHashAlgId = WEBAUTHN_HASH_ALGORITHM_SHA_256;

  BOOL enableHmacSecret = TRUE;
  WEBAUTHN_EXTENSION extension{};
  extension.pwszExtensionIdentifier = WEBAUTHN_EXTENSIONS_IDENTIFIER_HMAC_SECRET;
  extension.cbExtension = sizeof(enableHmacSecret);
  extension.pvExtension = &enableHmacSecret;

  WEBAUTHN_AUTHENTICATOR_MAKE_CREDENTIAL_OPTIONS options{};
  options.dwVersion = WEBAUTHN_AUTHENTICATOR_MAKE_CREDENTIAL_OPTIONS_VERSION_6;
  options.dwTimeoutMilliseconds = 120000;
  options.Extensions = WEBAUTHN_EXTENSIONS{1, &extension};
  options.dwAuthenticatorAttachment = WEBAUTHN_AUTHENTICATOR_ATTACHMENT_CROSS_PLATFORM;
  options.dwUserVerificationRequirement = WEBAUTHN_USER_VERIFICATION_REQUIREMENT_REQUIRED;
  options.dwAttestationConveyancePreference = WEBAUTHN_ATTESTATION_CONVEYANCE_PREFERENCE_NONE;
  options.bEnablePrf = TRUE;

  PWEBAUTHN_CREDENTIAL_ATTESTATION attestation = nullptr;
  const HRESULT result = WebAuthNAuthenticatorMakeCredential(
      prepareWebAuthnParent(parentWindow), &rp, &user, &parameters, &client,
      &options, &attestation);
  if (FAILED(result) || attestation == nullptr) {
    error = webAuthnError(result, QObject::tr("YubiKey enrollment"));
    if (attestation != nullptr) {
      WebAuthNFreeCredentialAttestation(attestation);
    }
    return false;
  }

  // Request both WebAuthn PRF and its CTAP hmac-secret capability. Windows
  // reports these through independent output paths and can leave bPrfEnabled
  // false while the explicitly requested hmac-secret output is true, including
  // with current YubiKey 5.8 firmware.
  const bool webAuthnPrfEnabled =
      attestation->dwVersion >= WEBAUTHN_CREDENTIAL_ATTESTATION_VERSION_5 &&
      attestation->bPrfEnabled;
  const bool hmacSecretEnabled =
      attestation->dwVersion >= WEBAUTHN_CREDENTIAL_ATTESTATION_VERSION_2 &&
      boolExtensionEnabled(attestation->Extensions,
                           WEBAUTHN_EXTENSIONS_IDENTIFIER_HMAC_SECRET);
  const bool prfEnabled = webAuthnPrfEnabled || hmacSecretEnabled;
  const DWORD attestationVersion = attestation->dwVersion;
  QByteArray credential(reinterpret_cast<const char*>(attestation->pbCredentialId),
                        static_cast<int>(attestation->cbCredentialId));
  WebAuthNFreeCredentialAttestation(attestation);
  if (!prfEnabled || credential.isEmpty()) {
    error = QObject::tr(
        "The selected security key did not create a PRF-enabled FIDO2 credential "
        "(attestation v%1, Windows PRF=%2, hmac-secret=%3). A YubiKey with FIDO2 "
        "hmac-secret support and a configured PIN is required.")
        .arg(attestationVersion)
        .arg(webAuthnPrfEnabled ? QObject::tr("yes") : QObject::tr("no"))
        .arg(hmacSecretEnabled ? QObject::tr("yes") : QObject::tr("no"));
    return false;
  }

  QByteArray salt = randomBytes(kSecretSize, error);
  QByteArray secret;
  if (salt.isEmpty() || !unlock(parentWindow, credential, salt, secret, error)) {
    return false;
  }
  enrollment = Enrollment{credential, salt, secret};
  return true;
#else
  Q_UNUSED(parentWindow)
  Q_UNUSED(walletBinding)
  Q_UNUSED(enrollment)
  return isSupported(error);
#endif
}

bool WindowsWebAuthnPrf::unlock(WId parentWindow, const QByteArray& credentialId,
                                const QByteArray& prfSalt, QByteArray& prfSecret,
                                QString& error) {
#ifdef Q_OS_WIN
  if (!prfSecret.isEmpty()) {
    OPENSSL_cleanse(prfSecret.data(), static_cast<size_t>(prfSecret.size()));
    prfSecret.clear();
  }
  if (!isSupported(error)) return false;
  if (credentialId.isEmpty() || prfSalt.size() != kSecretSize) {
    error = QObject::tr("The YubiKey credential metadata is invalid.");
    return false;
  }

  QByteArray data = clientData("webauthn.get", error);
  if (data.isEmpty()) return false;
  WEBAUTHN_CLIENT_DATA client{};
  client.dwVersion = WEBAUTHN_CLIENT_DATA_CURRENT_VERSION;
  client.cbClientDataJSON = static_cast<DWORD>(data.size());
  client.pbClientDataJSON = reinterpret_cast<PBYTE>(data.data());
  client.pwszHashAlgId = WEBAUTHN_HASH_ALGORITHM_SHA_256;

  WEBAUTHN_CREDENTIAL credential{};
  credential.dwVersion = WEBAUTHN_CREDENTIAL_CURRENT_VERSION;
  credential.cbId = static_cast<DWORD>(credentialId.size());
  credential.pbId = reinterpret_cast<PBYTE>(const_cast<char*>(credentialId.constData()));
  credential.pwszCredentialType = WEBAUTHN_CREDENTIAL_TYPE_PUBLIC_KEY;

  WEBAUTHN_HMAC_SECRET_SALT hmacSalt{};
  hmacSalt.cbFirst = static_cast<DWORD>(prfSalt.size());
  hmacSalt.pbFirst = reinterpret_cast<PBYTE>(const_cast<char*>(prfSalt.constData()));
  WEBAUTHN_CRED_WITH_HMAC_SECRET_SALT credentialSalt{};
  credentialSalt.cbCredID = credential.cbId;
  credentialSalt.pbCredID = credential.pbId;
  credentialSalt.pHmacSecretSalt = &hmacSalt;
  WEBAUTHN_HMAC_SECRET_SALT_VALUES saltValues{};
  saltValues.cCredWithHmacSecretSaltList = 1;
  saltValues.pCredWithHmacSecretSaltList = &credentialSalt;

  WEBAUTHN_AUTHENTICATOR_GET_ASSERTION_OPTIONS options{};
  options.dwVersion = WEBAUTHN_AUTHENTICATOR_GET_ASSERTION_OPTIONS_VERSION_6;
  options.dwTimeoutMilliseconds = 120000;
  options.CredentialList = WEBAUTHN_CREDENTIALS{1, &credential};
  options.dwAuthenticatorAttachment = WEBAUTHN_AUTHENTICATOR_ATTACHMENT_CROSS_PLATFORM;
  options.dwUserVerificationRequirement = WEBAUTHN_USER_VERIFICATION_REQUIREMENT_REQUIRED;
  options.pHmacSecretSaltValues = &saltValues;

  PWEBAUTHN_ASSERTION assertion = nullptr;
  const HRESULT result = WebAuthNAuthenticatorGetAssertion(
      prepareWebAuthnParent(parentWindow), kRpId, &client, &options, &assertion);
  if (FAILED(result) || assertion == nullptr) {
    error = webAuthnError(result, QObject::tr("YubiKey authorization"));
    if (assertion != nullptr) {
      WebAuthNFreeAssertion(assertion);
    }
    return false;
  }

  const bool valid = assertion->dwVersion >= WEBAUTHN_ASSERTION_VERSION_3 &&
                     assertion->pHmacSecret != nullptr &&
                     assertion->pHmacSecret->cbFirst == kSecretSize &&
                     assertion->pHmacSecret->pbFirst != nullptr;
  if (valid) {
    prfSecret = QByteArray(
        reinterpret_cast<const char*>(assertion->pHmacSecret->pbFirst), kSecretSize);
  }
  WebAuthNFreeAssertion(assertion);
  if (!valid) {
    error = QObject::tr("The security key did not return the required WebAuthn PRF secret.");
    return false;
  }
  return true;
#else
  Q_UNUSED(parentWindow)
  Q_UNUSED(credentialId)
  Q_UNUSED(prfSalt)
  Q_UNUSED(prfSecret)
  return isSupported(error);
#endif
}

}  // namespace WalletGui
