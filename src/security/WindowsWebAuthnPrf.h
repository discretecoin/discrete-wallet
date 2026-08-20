// Copyright (c) 2026, The Discrete developers
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QString>
#include <QtGui/qwindowdefs.h>

namespace WalletGui {

class WindowsWebAuthnPrf {
public:
  struct Enrollment {
    QByteArray credentialId;
    QByteArray prfSalt;
    QByteArray prfSecret;
  };

  static bool isSupported(QString& error);
  static bool enroll(WId parentWindow, const QByteArray& walletBinding,
                     Enrollment& enrollment, QString& error);
  static bool unlock(WId parentWindow, const QByteArray& credentialId,
                     const QByteArray& prfSalt, QByteArray& prfSecret,
                     QString& error);
};

}  // namespace WalletGui
