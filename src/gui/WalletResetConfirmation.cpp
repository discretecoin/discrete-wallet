// Copyright (c) 2026 The Discrete developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "WalletResetConfirmation.h"

#include <QCoreApplication>
#include <QMessageBox>
#include <QPushButton>

namespace WalletGui {
namespace {

QString resetText(const char* text) {
  return QCoreApplication::translate("MainWindow", text);
}

}  // namespace

bool confirmDestructiveWalletReset(
    QWidget* parent, const std::function<bool()>& stillCurrent) {
  if (!stillCurrent || !stillCurrent()) {
    return false;
  }

  QMessageBox warning(
      QMessageBox::Warning,
      resetText(QT_TRANSLATE_NOOP("MainWindow", "Destructive wallet reset")),
      resetText(QT_TRANSLATE_NOOP(
          "MainWindow",
          "Reset logically removes every locally stored outgoing-recipient "
          "address and payment proof from the active wallet file, then rebuilds "
          "blockchain-derived history. Outgoing transactions remain visible, but "
          "their recipient may show (n/a). These details cannot be reconstructed "
          "from the blockchain or seed. Account keys and required spending "
          "authority remain.\n\n"
          "This is logical deletion, not forensic erasure. Existing backups, "
          "exports, copies, snapshots, cloud history, filesystem remnants, or SSD "
          "behavior may retain the removed data. Use Rescan unless you "
          "intentionally want to remove this metadata from the active wallet "
          "file.\n\nContinue to the final confirmation?")),
      QMessageBox::NoButton, parent);
  QPushButton* continueButton = warning.addButton(
      resetText(QT_TRANSLATE_NOOP("MainWindow", "Continue")),
      QMessageBox::DestructiveRole);
  QPushButton* cancelButton = warning.addButton(QMessageBox::Cancel);
  warning.setDefaultButton(cancelButton);
  warning.setEscapeButton(cancelButton);
  warning.exec();
  if (warning.clickedButton() != continueButton || !stillCurrent()) {
    return false;
  }

  QMessageBox finalWarning(
      QMessageBox::Critical,
      resetText(QT_TRANSLATE_NOOP("MainWindow", "Final reset confirmation")),
      resetText(QT_TRANSLATE_NOOP(
          "MainWindow",
          "Logically remove all stored outgoing-recipient addresses and payment "
          "proofs from the active wallet file? This cannot be undone from that "
          "file. Use Rescan to preserve this metadata.")),
      QMessageBox::NoButton, parent);
  QPushButton* resetButton = finalWarning.addButton(
      resetText(QT_TRANSLATE_NOOP("MainWindow", "Remove metadata and reset")),
      QMessageBox::DestructiveRole);
  QPushButton* finalCancelButton = finalWarning.addButton(QMessageBox::Cancel);
  finalWarning.setDefaultButton(finalCancelButton);
  finalWarning.setEscapeButton(finalCancelButton);
  if (!stillCurrent()) {
    return false;
  }
  finalWarning.exec();
  return finalWarning.clickedButton() == resetButton && stillCurrent();
}

}  // namespace WalletGui
