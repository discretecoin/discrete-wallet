// Copyright (c) 2011-2015 The Cryptonote developers
// Copyright (c) 2016-2026 The Karbo developers
// Copyright (c) 2026 The Discrete developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QClipboard>
#include <QEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMenu>
#include <QTimer>
#include <QFontDatabase>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QRegularExpression>
#include <QToolButton>
#include <QToolTip>
#include <future>
#include "AccountFrame.h"
#include "WalletAdapter.h"
#include "NodeAdapter.h"
#include "CurrencyAdapter.h"
#include "Settings.h"
#include "AccountNumber.h"

#include "ui_accountframe.h"

namespace WalletGui {

namespace {

constexpr int CAPTION_FONT_SIZE = 10;
constexpr int ADDRESS_FONT_SIZE = 15;
constexpr int ACCOUNT_NUMBER_VALUE_FONT_SIZE = 23;
// Status messages shown in place of an account number ("Registration pending...",
// "Not registered") are much longer than an account number, so they render at a
// smaller size to fit the narrow sidebar instead of clipping at the hero size.
constexpr int ACCOUNT_NUMBER_STATUS_FONT_SIZE = 14;
// PQ addresses are ~3000-character bech32m strings — far too long to display
// in full. Elide to a short prefix/suffix, like other crypto wallets; the full
// address is always available via the copy button and context menu. The
// sidebar is narrow, so keep the elision short.
constexpr int ADDRESS_ELIDE_PREFIX = 13;
constexpr int ADDRESS_ELIDE_SUFFIX = 6;

QString formatDisplayAddress(const QString& address) {
  if (address.isEmpty()) {
    return QString();
  }
  if (address.size() > ADDRESS_ELIDE_PREFIX + ADDRESS_ELIDE_SUFFIX + 1) {
    return address.left(ADDRESS_ELIDE_PREFIX) + QStringLiteral("…") + address.right(ADDRESS_ELIDE_SUFFIX);
  }
  return address;
}

// Primary balance (Available): a small muted caption above a large value.
QString formatPrimaryBalance(const QString& title, const QString& amount, const QString& ticker) {
  // Explicit colors throughout so the amount doesn't depend on the inherited
  // palette text color (the value is the bright #F5F7F8, captions muted).
  return QString(
    "<div style=\"line-height:1.15;\">"
      "<span style=\"font-size:%1px; color:#7f8b94; letter-spacing:1px;\">%2</span><br>"
      "<span style=\"font-size:22px; font-weight:600; color:#F5F7F8;\">%3</span>"
      "<span style=\"font-size:12px; color:#7f8b94;\"> %4</span>"
    "</div>")
    .arg(CAPTION_FONT_SIZE)
    .arg(title.toUpper().toHtmlEscaped())
    .arg(amount.toHtmlEscaped())
    .arg(ticker.toHtmlEscaped());
}

// Secondary balance (Pending / Total): muted caption + inline value.
QString formatSecondaryBalance(const QString& title, const QString& amount) {
  return QString(
    "<span style=\"font-size:12px; color:#8a95a0;\">%1 </span>"
    "<span style=\"font-size:12px; color:#d9e0e5;\">%2</span>")
    .arg(title.toHtmlEscaped())
    .arg(amount.toHtmlEscaped());
}

}

QStringList AccountFrame::divideAmount(quint64 _val) {
  QStringList list;
  QString str = CurrencyAdapter::instance().formatAmount(_val).remove(',');

  quint32 offset = str.indexOf(".") + 3; // add two digits .00
  QString before = str.left(offset);
  QString after  = str.mid(offset);

  list << before << after;

  return list;
}

AccountFrame::AccountFrame(QWidget* _parent) : QFrame(_parent), m_ui(new Ui::AccountFrame),
  m_accountNumberResolved(false), m_accountNumberFetchInProgress(false),
  m_registrationPending(false), m_registrationProgressDialog(nullptr) {
  m_ui->setupUi(this);
  connect(&WalletAdapter::instance(), &WalletAdapter::updateWalletAddressSignal, this, &AccountFrame::updateWalletAddress);
  connect(&WalletAdapter::instance(), &WalletAdapter::walletActualBalanceUpdatedSignal, this, &AccountFrame::updateActualBalance,
    Qt::QueuedConnection);
  connect(&WalletAdapter::instance(), &WalletAdapter::walletPendingBalanceUpdatedSignal, this, &AccountFrame::updatePendingBalance,
    Qt::QueuedConnection);
  connect(&WalletAdapter::instance(), &WalletAdapter::walletCloseCompletedSignal, this, &AccountFrame::reset);
  connect(&WalletAdapter::instance(), &WalletAdapter::accountRegistrationCompletedSignal, this, &AccountFrame::accountRegistrationCompleted,
    Qt::QueuedConnection);
  connect(&WalletAdapter::instance(), &WalletAdapter::walletStateChangedSignal, this, &AccountFrame::updateRegistrationProgressText,
    Qt::QueuedConnection);
  connect(&WalletAdapter::instance(), &WalletAdapter::walletSynchronizationCompletedSignal, this, [this](int _error, const QString&) {
    if (_error != 0 || !WalletAdapter::instance().isOpen() || m_accountNumberResolved) {
      return;
    }

    fetchAccountNumber(WalletAdapter::instance().getAddress());
  });

  // Style the account frame with a slightly brighter background
  applyFramePalette();

  m_ui->m_accountNumberLabel->setVisible(false);
  m_ui->m_copyAccountNumberButton->setVisible(false);
  m_ui->m_registerAccountButton->setVisible(false);

  QFont addressFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
  addressFont.setPixelSize(ADDRESS_FONT_SIZE);
  addressFont.setBold(true);

  m_accountNumberFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
  m_accountNumberFont.setPixelSize(ACCOUNT_NUMBER_VALUE_FONT_SIZE);
  m_accountNumberFont.setBold(true);

  // Proportional (default UI) font for status text — narrower than the fixed-width
  // hero font and reads as a message rather than a value.
  m_accountNumberStatusFont = font();
  m_accountNumberStatusFont.setPixelSize(ACCOUNT_NUMBER_STATUS_FONT_SIZE);
  m_accountNumberStatusFont.setBold(false);

  m_ui->m_addressLabel->setFont(addressFont);
  m_ui->m_addressLabel->setWordWrap(false);
  m_ui->m_addressLabel->setTextFormat(Qt::PlainText);
  m_ui->m_addressLabel->installEventFilter(this);
  // Right-click "Copy address" menu — same pattern as the original Karbo wallet.
  m_ui->m_addressLabel->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(m_ui->m_addressLabel, &QLabel::customContextMenuRequested, this, [this](const QPoint& _pos) {
    QMenu menu(this);
    QAction* copyAction = menu.addAction(tr("Copy address"));
    copyAction->setEnabled(!m_address.isEmpty());

    if (menu.exec(m_ui->m_addressLabel->mapToGlobal(_pos)) == copyAction) {
      copyTextToClipboard(m_address, m_ui->m_addressLabel);
    }
  });
  m_ui->m_accountNumberLabel->setFont(m_accountNumberFont);
  m_ui->m_accountNumberLabel->setTextFormat(Qt::PlainText);
  m_ui->m_copyButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
  m_ui->m_copyAccountNumberButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
  m_ui->m_copyButton->setIconSize(QSize(15, 15));
  m_ui->m_copyAccountNumberButton->setIconSize(QSize(15, 15));
  m_ui->m_copyButton->setText(QString());
  m_ui->m_copyAccountNumberButton->setText(QString());
  m_ui->m_copyButton->setFocusPolicy(Qt::NoFocus);
  m_ui->m_copyAccountNumberButton->setFocusPolicy(Qt::NoFocus);
  connect(m_ui->m_copyButton, &QToolButton::clicked, this, &AccountFrame::copyAddress);
  connect(m_ui->m_copyAccountNumberButton, &QToolButton::clicked, this, &AccountFrame::copyAccountNumber);
}

AccountFrame::~AccountFrame() {
  closeRegistrationProgressDialog();
}

void AccountFrame::changeEvent(QEvent* _event) {
  QFrame::changeEvent(_event);
  if (_event->type() == QEvent::PaletteChange || _event->type() == QEvent::StyleChange) {
    applyFramePalette();
  }
}

void AccountFrame::applyFramePalette() {
  // The two cards. Object-name-scoped so the fill/border don't cascade to the
  // labels nested inside them.
  const QString cardCss =
    QStringLiteral("QFrame#%1 { background-color: #182029; border: 1px solid #2A343D; border-radius: 12px; }");
  m_ui->m_accountNumberPanel->setStyleSheet(cardCss.arg(QStringLiteral("m_accountNumberPanel")));
  m_ui->m_accountBalances->setStyleSheet(cardCss.arg(QStringLiteral("m_accountBalances")));

  // These labels sit in cards whose stylesheet fill can otherwise leave the
  // text resolving against a low-contrast palette; set colors explicitly.
  // Account number is the hero — brand mint; captions muted; address muted.
  // The "ADDRESS" caption uses the same muted uppercase style as the
  // "ACCOUNT NUMBER" / "AVAILABLE" captions.
  const QString captionCss = QStringLiteral("color:#7f8b94; font-size:10px; letter-spacing:1px;");
  m_ui->m_accountNumberTitleLabel->setStyleSheet(captionCss);
  m_ui->label->setStyleSheet(captionCss);
  m_ui->m_accountNumberLabel->setStyleSheet(QStringLiteral("color:#5FE29F;"));
  m_ui->m_addressLabel->setStyleSheet(QStringLiteral("color:#9AA7B2;"));
}

bool AccountFrame::eventFilter(QObject* _object, QEvent* _event) {
  if (_object == m_ui->m_addressLabel &&
      (_event->type() == QEvent::KeyPress || _event->type() == QEvent::ShortcutOverride)) {
    auto* keyEvent = static_cast<QKeyEvent*>(_event);
    if (keyEvent->matches(QKeySequence::Copy)) {
      if (m_address.isEmpty()) {
        return false;
      }

      copyTextToClipboard(m_address, m_ui->m_addressLabel);
      _event->accept();
      return true;
    }
  }

  return QFrame::eventFilter(_object, _event);
}

void AccountFrame::updateWalletAddress(const QString& _address) {
  m_address = _address;
  m_ui->m_addressLabel->setText(formatDisplayAddress(_address));
  // The full address is ~3000 characters (bech32m-encoded PQ keys) — showing
  // it as a tooltip is unreadable. Use the copy button/context menu for that;
  // the tooltip just explains what the elided text is.
  m_ui->m_addressLabel->setToolTip(tr("Your receiving address"));
  m_accountNumber.clear();
  m_accountNumberResolved = false;
  m_accountNumberFetchInProgress = false;
  // The address changed, so any previous registration-suppression flag is
  // moot — the new address has its own registration state to discover.
  m_registrationPending = false;
  m_registrationTransactionHash.clear();
  updateAccountNumberDisplay();
  fetchAccountNumber(_address);
}

void AccountFrame::copyAddress() {
  copyTextToClipboard(m_address, m_ui->m_copyButton);
}

void AccountFrame::updateActualBalance(quint64 _balance) {
  const QString ticker = CurrencyAdapter::instance().getCurrencyTicker().toUpper();
  m_ui->m_actualBalanceLabel->setText(formatPrimaryBalance(tr("Available"), divideAmount(_balance).first(), ticker));

  quint64 pendingBalance = WalletAdapter::instance().getPendingBalance();
  m_ui->m_totalBalanceLabel->setText(formatSecondaryBalance(tr("Total"), divideAmount(_balance + pendingBalance).first()));
}

void AccountFrame::updatePendingBalance(quint64 _balance) {
  m_ui->m_pendingBalanceLabel->setText(formatSecondaryBalance(tr("Pending"), divideAmount(_balance).first()));

  quint64 actualBalance = WalletAdapter::instance().getActualBalance();
  m_ui->m_totalBalanceLabel->setText(formatSecondaryBalance(tr("Total"), divideAmount(_balance + actualBalance).first()));
}

void AccountFrame::fetchAccountNumber(const QString& _address) {
  if (_address.isEmpty() || m_accountNumberFetchInProgress) {
    return;
  }

  QString viewPubHex, spendPubHex;
  if (!WalletAdapter::instance().getOwnPqIdentityHex(viewPubHex, spendPubHex)) {
    return;
  }

  m_accountNumberFetchInProgress = true;
  const QString requestedAddress = _address;

  auto registered = std::make_shared<bool>(false);
  auto blockHeight = std::make_shared<uint32_t>(0);
  auto txIndex = std::make_shared<uint32_t>(0);

  NodeAdapter::instance().getPqAccount(viewPubHex.toStdString(), spendPubHex.toStdString(), *registered, *blockHeight, *txIndex,
    [this, registered, blockHeight, txIndex, requestedAddress](std::error_code ec) {
      QMetaObject::invokeMethod(this, [this, ec, registered, blockHeight, txIndex, requestedAddress]() {
        if (WalletAdapter::instance().getAddress() != requestedAddress) {
          m_accountNumberFetchInProgress = false;
          return;
        }

        // In case Node lookups can transiently fail keep the current display and retry on next
        // synchronization completion instead of clearing it.
        if (ec) {
          m_accountNumberFetchInProgress = false;
          return;
        }

        if (!*registered) {
          // A registration we just submitted only takes effect once its tx is
          // mined, so while one is pending this "not registered" answer is not
          // final: keep m_accountNumberResolved false so every sync-completion
          // re-polls until the account number appears. Otherwise the latch trips
          // on the first poll (tx still in mempool) and the guard in the
          // synchronization handler blocks all further fetches — leaving
          // "Registration pending..." stuck even after the tx confirms.
          m_accountNumberResolved = !m_registrationPending;
          m_accountNumber.clear();
          m_accountNumberFetchInProgress = false;
          updateAccountNumberDisplay();
          return;
        }

        m_accountNumberResolved = true;
        m_accountNumber = QString::fromStdString(CryptoNote::AccountNumber{*blockHeight, *txIndex}.toString());
        // Registration confirmed — drop the suppression flag so future
        // address-clearing scenarios behave normally.
        m_registrationPending = false;
        m_registrationTransactionHash.clear();
        m_accountNumberFetchInProgress = false;
        updateAccountNumberDisplay();

      }, Qt::QueuedConnection);
    });
}

void AccountFrame::updateAccountNumberDisplay() {
  if (m_accountNumber.isEmpty()) {
    const bool canRegister = WalletAdapter::instance().isOpen() && !Settings::instance().isTrackingMode();
    m_ui->m_accountNumberLabel->clear();
    if (m_registrationPending && canRegister) {
      // We've already submitted a registration tx; show a transient hint
      // instead of the Register button so the user doesn't fire off
      // duplicate registrations while the first one is in mempool.
      m_ui->m_accountNumberLabel->setFont(m_accountNumberStatusFont);
      m_ui->m_accountNumberLabel->setText(tr("Registration pending..."));
      QString tooltip = tr("A registration transaction has been sent. "
        "Your account number will appear here once it confirms.");
      if (!m_registrationTransactionHash.isEmpty()) {
        tooltip += QStringLiteral("\n") + tr("Transaction hash: %1").arg(m_registrationTransactionHash);
      }
      m_ui->m_accountNumberLabel->setToolTip(tooltip);
      m_ui->m_accountNumberLabel->setVisible(true);
      m_ui->m_copyAccountNumberButton->setVisible(false);
      m_ui->m_registerAccountButton->setVisible(false);
      return;
    }
    m_ui->m_accountNumberLabel->setVisible(!canRegister);
    if (!canRegister) {
      m_ui->m_accountNumberLabel->setFont(m_accountNumberStatusFont);
      m_ui->m_accountNumberLabel->setText(tr("Not registered"));
      m_ui->m_accountNumberLabel->setToolTip(tr("This wallet does not have a registered account number."));
    }
    m_ui->m_copyAccountNumberButton->setVisible(false);
    m_ui->m_registerAccountButton->setVisible(canRegister);
  } else {
    m_ui->m_accountNumberLabel->setFont(m_accountNumberFont);
    m_ui->m_accountNumberLabel->setText(m_accountNumber);
    m_ui->m_accountNumberLabel->setToolTip(m_accountNumber);
    m_ui->m_accountNumberLabel->setVisible(true);
    m_ui->m_copyAccountNumberButton->setVisible(true);
    m_ui->m_registerAccountButton->setVisible(false);
  }
}

void AccountFrame::copyAccountNumber() {
  copyTextToClipboard(m_accountNumber, m_ui->m_copyAccountNumberButton);
}

void AccountFrame::copyTextToClipboard(const QString& _text, QWidget* _anchor) {
  if (_text.isEmpty()) {
    return;
  }

  QClipboard* clipboard = QApplication::clipboard();
  clipboard->setText(_text, QClipboard::Clipboard);
  if (clipboard->supportsSelection()) {
    clipboard->setText(_text, QClipboard::Selection);
  }

  if (_anchor != nullptr) {
    QToolTip::showText(_anchor->mapToGlobal(_anchor->rect().center()), tr("Copied"), _anchor);
  }
}

void AccountFrame::accountRegistrationCompleted(int _error, const QString& _errorText, const QString& _transactionHash) {
  closeRegistrationProgressDialog();

  if (_error != 0) {
    m_registrationPending = false;
    m_registrationTransactionHash.clear();
    updateAccountNumberDisplay();
    QMessageBox::critical(this, tr("Registration failed"), _errorText);
    return;
  }

  m_registrationPending = true;
  m_registrationTransactionHash = _transactionHash;
  updateAccountNumberDisplay();
}

void AccountFrame::showRegistrationProgressDialog(bool _freeRegistration) {
  closeRegistrationProgressDialog();

  const QString labelText = _freeRegistration ?
    tr("Solving registration proof-of-work...\nThis can take a few minutes.") :
    tr("Submitting paid registration...");

  m_registrationProgressDialog = new QProgressDialog(labelText, QString(), 0, 0, this);
  m_registrationProgressDialog->setWindowTitle(tr("Register Account Number"));
  m_registrationProgressDialog->setCancelButton(nullptr);
  m_registrationProgressDialog->setWindowModality(Qt::WindowModal);
  m_registrationProgressDialog->setMinimumDuration(0);
  m_registrationProgressDialog->setAutoClose(false);
  m_registrationProgressDialog->setAutoReset(false);
  m_registrationProgressDialog->setValue(0);
  m_registrationProgressDialog->show();
}

void AccountFrame::updateRegistrationProgressText(const QString& _stateText) {
  if (m_registrationProgressDialog == nullptr || _stateText.isEmpty()) {
    return;
  }

  if (_stateText.contains(tr("proof-of-work")) ||
      _stateText.contains(tr("Relaying free registration")) ||
      _stateText.contains(tr("Sending paid registration")) ||
      _stateText.contains(tr("Registering account number"))) {
    m_registrationProgressDialog->setLabelText(_stateText);
  }
}

void AccountFrame::closeRegistrationProgressDialog() {
  if (m_registrationProgressDialog == nullptr) {
    return;
  }

  m_registrationProgressDialog->close();
  m_registrationProgressDialog->deleteLater();
  m_registrationProgressDialog = nullptr;
}

void AccountFrame::registerAccountNumber() {
  if (!WalletAdapter::instance().isOpen()) {
    return;
  }

  if (Settings::instance().isTrackingMode()) {
    QMessageBox::critical(this, tr("Error"), tr("Cannot register account number from a tracking wallet."));
    return;
  }

  QMessageBox messageBox(this);
  messageBox.setWindowTitle(tr("Register Account Number"));
  messageBox.setIcon(QMessageBox::Question);
  messageBox.setText(tr("Register an account number for easy payments?"));
  messageBox.setInformativeText(tr("Free registration solves a small anti-spam proof-of-work. Paid registration uses wallet funds and pays the normal transaction fee."));

  QPushButton* freeButton = messageBox.addButton(tr("Free"), QMessageBox::AcceptRole);
  QPushButton* paidButton = nullptr;
  if (WalletAdapter::instance().getActualBalance() > 0) {
    paidButton = messageBox.addButton(tr("Paid"), QMessageBox::AcceptRole);
  }
  messageBox.addButton(QMessageBox::Cancel);
  messageBox.setDefaultButton(freeButton);
  messageBox.exec();

  const QPushButton* clickedButton = qobject_cast<QPushButton*>(messageBox.clickedButton());
  if (clickedButton == freeButton || (paidButton != nullptr && clickedButton == paidButton)) {
    // Hide the Register button BEFORE handing off to the wallet so that
    // even if the send takes a moment the user can't double-click it.
    // Consensus only honors the first registration for an identity, so
    // duplicate submissions are pure waste.
    m_registrationPending = true;
    m_registrationTransactionHash.clear();
    m_accountNumberResolved = false;
    updateAccountNumberDisplay();

    const WalletAdapter::AccountRegistrationMode mode = paidButton != nullptr && clickedButton == paidButton ?
      WalletAdapter::AccountRegistrationMode::Paid :
      WalletAdapter::AccountRegistrationMode::Free;
    showRegistrationProgressDialog(mode == WalletAdapter::AccountRegistrationMode::Free);
    WalletAdapter::instance().registerAccountNumber(mode);
  }
}

void AccountFrame::reset() {
  updateActualBalance(0);
  updatePendingBalance(0);
  m_address.clear();
  m_ui->m_addressLabel->clear();
  m_ui->m_addressLabel->setToolTip(tr("Your receiving address"));
  m_accountNumber.clear();
  m_accountNumberResolved = false;
  m_accountNumberFetchInProgress = false;
  // Wallet closed — drop any registration-pending suppression so a fresh
  // wallet open never inherits stale UI state from the prior session.
  m_registrationPending = false;
  m_registrationTransactionHash.clear();
  closeRegistrationProgressDialog();
  m_ui->m_accountNumberLabel->clear();
  m_ui->m_accountNumberLabel->setVisible(false);
  m_ui->m_copyAccountNumberButton->setVisible(false);
  m_ui->m_registerAccountButton->setVisible(false);
}

}
