// Interactive hardware smoke test. It never prints PRF material.

#include <QApplication>
#include <QCryptographicHash>
#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>

#include <openssl/crypto.h>

#include <iostream>

#include "security/WindowsWebAuthnPrf.h"

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  QWidget parent;
  parent.setWindowTitle(QObject::tr("Discrete Wallet YubiKey hardware test"));
  auto* layout = new QVBoxLayout(&parent);
  layout->addWidget(new QLabel(QObject::tr(
      "Follow the Windows Security prompt. Enter the YubiKey PIN and touch the key when requested."),
      &parent));
  parent.resize(520, 100);
  parent.show();
  app.processEvents();

  QString error;
  const QByteArray binding = QCryptographicHash::hash(
      QByteArrayLiteral("Discrete Wallet isolated WebAuthn PRF hardware smoke"),
      QCryptographicHash::Sha256);
  WalletGui::WindowsWebAuthnPrf::Enrollment enrollment;
  if (!WalletGui::WindowsWebAuthnPrf::enroll(parent.winId(), binding, enrollment, error)) {
    std::cerr << "Enrollment failed: " << error.toStdString() << '\n';
    return 1;
  }

  QByteArray repeated;
  const bool unlocked = WalletGui::WindowsWebAuthnPrf::unlock(
      parent.winId(), enrollment.credentialId, enrollment.prfSalt, repeated, error);
  const bool same = unlocked && repeated == enrollment.prfSecret;
  OPENSSL_cleanse(enrollment.prfSecret.data(), static_cast<size_t>(enrollment.prfSecret.size()));
  OPENSSL_cleanse(repeated.data(), static_cast<size_t>(repeated.size()));
  if (!same) {
    std::cerr << "Repeat authorization failed: " << error.toStdString() << '\n';
    return 1;
  }

  std::cout << "WebAuthnPrfSmoke: enrollment and repeat PRF authorization matched\n";
  return 0;
}
