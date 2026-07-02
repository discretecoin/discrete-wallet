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
#include <QRegularExpression>
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
// PQ addresses are ~3000-character bech32m strings — far too long to display
// in full. Elide to a short prefix/suffix, like other crypto wallets; the full
// address is always available via the copy button and context menu. The
// sidebar is narrow, so keep the elision short.
constexpr int ADDRESS_ELIDE_PREFIX = 13;
constexpr int ADDRESS_ELIDE_SUFFIX = 6;

QString getCopyableAddressText() {
  return WalletAdapter::instance().getAddress();
}

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
  return QString(
    "<div style=\"line-height:1.15;\">"
      "<span style=\"font-size:%1px; color:#7f8b94; letter-spacing:1px;\">%2</span><br>"
      "<span style=\"font-size:22px; font-weight:600;\">%3</span>"
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
    "<span style=\"font-size:12px;\">%2</span>")
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
  m_registrationPending(false) {
  m_ui->setupUi(this);
  connect(&WalletAdapter::instance(), &WalletAdapter::updateWalletAddressSignal, this, &AccountFrame::updateWalletAddress);
  connect(&WalletAdapter::instance(), &WalletAdapter::walletActualBalanceUpdatedSignal, this, &AccountFrame::updateActualBalance,
    Qt::QueuedConnection);
  connect(&WalletAdapter::instance(), &WalletAdapter::walletPendingBalanceUpdatedSignal, this, &AccountFrame::updatePendingBalance,
    Qt::QueuedConnection);
  connect(&WalletAdapter::instance(), &WalletAdapter::walletCloseCompletedSignal, this, &AccountFrame::reset);
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

  QFont accountNumberFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
  accountNumberFont.setPixelSize(ACCOUNT_NUMBER_VALUE_FONT_SIZE);
  accountNumberFont.setBold(true);

  m_ui->m_addressLabel->setFont(addressFont);
  m_ui->m_addressLabel->setWordWrap(false);
  m_ui->m_addressLabel->setTextFormat(Qt::PlainText);
  m_ui->m_addressLabel->installEventFilter(this);
  m_ui->m_addressLabel->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(m_ui->m_addressLabel, &QLabel::customContextMenuRequested, this, [this](const QPoint& _pos) {
    if (WalletAdapter::instance().getAddress().isEmpty()) {
      return;
    }
    // Non-blocking popup() with WA_DeleteOnClose, not a stack QMenu + exec():
    // Qlementine fades menus out on close, and destroying the QMenu the instant
    // exec() returns (stack unwind) orphans the fading popup, leaving a ghost
    // "Copy address" box on screen. Letting the menu delete itself after it has
    // finished closing avoids that.
    QMenu* menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    QAction* copyAction = menu->addAction(tr("Copy address"));
    connect(copyAction, &QAction::triggered, this, []() {
      QApplication::clipboard()->setText(getCopyableAddressText());
    });
    menu->popup(m_ui->m_addressLabel->mapToGlobal(_pos));
  });
  m_ui->m_accountNumberLabel->setFont(accountNumberFont);
  m_ui->m_accountNumberLabel->setTextFormat(Qt::PlainText);
  m_ui->m_copyButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
  m_ui->m_copyAccountNumberButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
  m_ui->m_copyButton->setIconSize(QSize(15, 15));
  m_ui->m_copyAccountNumberButton->setIconSize(QSize(15, 15));
  m_ui->m_copyButton->setText(QString());
  m_ui->m_copyAccountNumberButton->setText(QString());
  m_ui->m_copyButton->setFocusPolicy(Qt::NoFocus);
  m_ui->m_copyAccountNumberButton->setFocusPolicy(Qt::NoFocus);
}

AccountFrame::~AccountFrame() {
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
      const QString copyText = getCopyableAddressText();
      if (copyText.isEmpty()) {
        return false;
      }

      QApplication::clipboard()->setText(copyText);
      _event->accept();
      return true;
    }
  }

  return QFrame::eventFilter(_object, _event);
}

void AccountFrame::updateWalletAddress(const QString& _address) {
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
  updateAccountNumberDisplay();
  fetchAccountNumber(_address);
}

void AccountFrame::copyAddress() {
  QApplication::clipboard()->setText(WalletAdapter::instance().getAddress());
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
          m_accountNumberResolved = true;
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
      m_ui->m_accountNumberLabel->setText(tr("Registration pending..."));
      m_ui->m_accountNumberLabel->setToolTip(tr("A registration transaction has been sent. "
        "Your account number will appear here once it confirms."));
      m_ui->m_accountNumberLabel->setVisible(true);
      m_ui->m_copyAccountNumberButton->setVisible(false);
      m_ui->m_registerAccountButton->setVisible(false);
      return;
    }
    m_ui->m_accountNumberLabel->setVisible(!canRegister);
    if (!canRegister) {
      m_ui->m_accountNumberLabel->setText(tr("Not registered"));
      m_ui->m_accountNumberLabel->setToolTip(tr("This wallet does not have a registered account number."));
    }
    m_ui->m_copyAccountNumberButton->setVisible(false);
    m_ui->m_registerAccountButton->setVisible(canRegister);
  } else {
    m_ui->m_accountNumberLabel->setText(m_accountNumber);
    m_ui->m_accountNumberLabel->setToolTip(m_accountNumber);
    m_ui->m_accountNumberLabel->setVisible(true);
    m_ui->m_copyAccountNumberButton->setVisible(true);
    m_ui->m_registerAccountButton->setVisible(false);
  }
}

void AccountFrame::copyAccountNumber() {
  if (!m_accountNumber.isEmpty()) {
    QApplication::clipboard()->setText(m_accountNumber);
  }
}

void AccountFrame::registerAccountNumber() {
  if (!WalletAdapter::instance().isOpen()) {
    return;
  }

  if (Settings::instance().isTrackingMode()) {
    QMessageBox::critical(this, tr("Error"), tr("Cannot register account number from a tracking wallet."));
    return;
  }

  if (QMessageBox::question(this, tr("Register Account Number"),
      tr("Register an account number for easy payments?\nA small fee will be charged."),
      QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
    // Hide the Register button BEFORE handing off to the wallet so that
    // even if the send takes a moment the user can't double-click it.
    // sendTransaction is fire-and-forget here; consensus only honors the
    // first registration per address per block, so duplicates are
    // pure waste of fees + dust.
    m_registrationPending = true;
    m_accountNumberResolved = false;
    updateAccountNumberDisplay();
    WalletAdapter::instance().registerAccountNumber();
  }
}

void AccountFrame::reset() {
  updateActualBalance(0);
  updatePendingBalance(0);
  m_ui->m_addressLabel->clear();
  m_ui->m_addressLabel->setToolTip(tr("Your receiving address"));
  m_accountNumber.clear();
  m_accountNumberResolved = false;
  m_accountNumberFetchInProgress = false;
  // Wallet closed — drop any registration-pending suppression so a fresh
  // wallet open never inherits stale UI state from the prior session.
  m_registrationPending = false;
  m_ui->m_accountNumberLabel->clear();
  m_ui->m_accountNumberLabel->setVisible(false);
  m_ui->m_copyAccountNumberButton->setVisible(false);
  m_ui->m_registerAccountButton->setVisible(false);
}

}
