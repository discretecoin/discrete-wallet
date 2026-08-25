// Copyright (c) 2026 The Discrete developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QAbstractButton>
#include <QApplication>
#include <QKeyEvent>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>

#include <iostream>

#include "gui/WalletResetConfirmation.h"

namespace {

enum class Choice { Cancel, Default, Escape, Approve };

struct Result {
  bool valid = true;
  int dialogs = 0;
  QString error;
};

void fail(QMessageBox* box, Result& result, const QString& error) {
  result.valid = false;
  result.error = error;
  if (auto* cancel = box->button(QMessageBox::Cancel)) {
    cancel->click();
  } else {
    box->reject();
  }
}

QAbstractButton* destructiveButton(QMessageBox* box) {
  for (QAbstractButton* button : box->buttons()) {
    if (box->buttonRole(button) == QMessageBox::DestructiveRole) {
      return button;
    }
  }
  return nullptr;
}

bool inspectDialog(QMessageBox* box, Result& result,
                   QMessageBox::Icon expectedIcon, bool first) {
  QAbstractButton* cancel = box->button(QMessageBox::Cancel);
  if (box->icon() != expectedIcon || cancel == nullptr ||
      box->defaultButton() != cancel || box->escapeButton() != cancel ||
      destructiveButton(box) == nullptr) {
    fail(box, result, QStringLiteral("dialog is not fail-closed"));
    return false;
  }

  const QString text = box->text().toLower();
  const QStringList required = first
      ? QStringList{QStringLiteral("recipient"), QStringLiteral("payment proof"),
                    QStringLiteral("(n/a)"), QStringLiteral("blockchain or seed"),
                    QStringLiteral("account keys"), QStringLiteral("authority"),
                    QStringLiteral("logical deletion"),
                    QStringLiteral("not forensic erasure"),
                    QStringLiteral("backups"), QStringLiteral("exports"),
                    QStringLiteral("copies"), QStringLiteral("snapshots"),
                    QStringLiteral("cloud"), QStringLiteral("filesystem"),
                    QStringLiteral("ssd"), QStringLiteral("rescan")}
      : QStringList{QStringLiteral("recipient"), QStringLiteral("payment proof"),
                    QStringLiteral("cannot be undone"),
                    QStringLiteral("rescan")};
  for (const QString& phrase : required) {
    if (!text.contains(phrase)) {
      fail(box, result,
           QStringLiteral("missing disclosure: %1").arg(phrase));
      return false;
    }
  }
  return true;
}

void choose(QMessageBox* box, Choice choice) {
  if (choice == Choice::Cancel) {
    box->button(QMessageBox::Cancel)->click();
  } else if (choice == Choice::Default) {
    box->defaultButton()->click();
  } else if (choice == Choice::Escape) {
    QKeyEvent press(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(box, &press);
    QKeyEvent release(QEvent::KeyRelease, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(box, &release);
  } else {
    destructiveButton(box)->click();
  }
}

bool runScenario(const char* name, Choice firstChoice, Choice secondChoice,
                 bool expected, int expectedDialogs,
                 bool invalidateAfterFirst = false,
                 bool invalidateDuringSecond = false,
                 bool initiallyCurrent = true) {
  Result result;
  bool current = initiallyCurrent;
  if (current) {
    QTimer::singleShot(0, [&]() {
      auto* first = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
      if (first == nullptr) {
        result.valid = false;
        result.error = QStringLiteral("first dialog missing");
        return;
      }
      ++result.dialogs;
      if (!inspectDialog(first, result, QMessageBox::Warning, true)) {
        return;
      }
      if (firstChoice == Choice::Approve) {
        if (invalidateAfterFirst) {
          current = false;
        } else {
          QTimer::singleShot(0, [&]() {
            auto* second = qobject_cast<QMessageBox*>(
                QApplication::activeModalWidget());
            if (second == nullptr) {
              result.valid = false;
              result.error = QStringLiteral("second dialog missing");
              return;
            }
            ++result.dialogs;
            if (!inspectDialog(second, result, QMessageBox::Critical, false)) {
              return;
            }
            if (invalidateDuringSecond) {
              current = false;
            }
            choose(second, secondChoice);
          });
        }
      }
      choose(first, firstChoice);
    });
  }

  const bool approved = WalletGui::confirmDestructiveWalletReset(
      nullptr, [&]() { return current; });
  QApplication::processEvents();
  if (!result.valid || approved != expected ||
      result.dialogs != expectedDialogs) {
    std::cerr << name << " failed: "
              << (result.error.isEmpty() ? "unexpected result"
                                         : result.error.toStdString())
              << "; approved=" << approved
              << "; dialogs=" << result.dialogs << '\n';
    return false;
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  bool ok = true;
  ok = runScenario("first Cancel", Choice::Cancel, Choice::Cancel,
                   false, 1) && ok;
  ok = runScenario("first default", Choice::Default, Choice::Cancel,
                   false, 1) && ok;
  ok = runScenario("first Escape", Choice::Escape, Choice::Cancel,
                   false, 1) && ok;
  ok = runScenario("second Cancel", Choice::Approve, Choice::Cancel,
                   false, 2) && ok;
  ok = runScenario("second default", Choice::Approve, Choice::Default,
                   false, 2) && ok;
  ok = runScenario("second Escape", Choice::Approve, Choice::Escape,
                   false, 2) && ok;
  ok = runScenario("two approvals", Choice::Approve, Choice::Approve,
                   true, 2) && ok;
  ok = runScenario("stale after first", Choice::Approve, Choice::Approve,
                   false, 1, true) && ok;
  ok = runScenario("stale during second", Choice::Approve, Choice::Approve,
                   false, 2, false, true) && ok;
  ok = runScenario("already stale", Choice::Approve, Choice::Approve,
                   false, 0, false, false, false) && ok;

  if (!ok) {
    return 1;
  }
  std::cout << "WalletResetConfirmationTests: all checks passed\n";
  return 0;
}
