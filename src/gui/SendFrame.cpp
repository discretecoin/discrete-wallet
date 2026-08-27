// Copyright (c) 2011-2015 The Cryptonote developers
// Copyright (c) 2015-2016 XDN developers
// Copyright (c) 2016-2021 The Karbo developers
// Copyright (c) 2026 The Discrete developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QRegularExpressionValidator>
#include <QRegularExpression>
#include <QInputDialog>
#include <QIcon>
#include <QMessageBox>
#include <QUrlQuery>
#include <QTime>
#include <QUrl>

#include <limits>

#include "CryptoNoteCore/CryptoNoteFormatUtils.h"
#include "AddressBookModel.h"
#include "CurrencyAdapter.h"
#include "MainWindow.h"
#include "NodeAdapter.h"
#include "SendFrame.h"
#include "TransferFrame.h"
#include "WalletAdapter.h"
#include "Common/SecureMemory.h"
#include "WalletEvents.h"
#include "Settings.h"
#include "OpenUriDialog.h"
#include "ConfirmSendDialog.h"
#include "ExportRawTxDialog.h"
#include "PasswordDialog.h"
#include <CryptoNoteConfig.h>

#include "ui_sendframe.h"

namespace WalletGui {

SendFrame::SendFrame(QWidget* _parent) : QFrame(_parent), m_ui(new Ui::SendFrame), m_glassFrame(new SendGlassFrame(nullptr))
{
  m_ui->setupUi(this);
  m_glassFrame->setObjectName("m_sendGlassFrame");
  clearAllClicked();
  amountValueChanged();

  connect(&WalletAdapter::instance(), &WalletAdapter::walletSendTransactionCompletedSignal, this, &SendFrame::sendTransactionCompleted,
    Qt::QueuedConnection);
  connect(&WalletAdapter::instance(), &WalletAdapter::walletActualBalanceUpdatedSignal, this, &SendFrame::walletActualBalanceUpdated,
    Qt::QueuedConnection);
  connect(&WalletAdapter::instance(), &WalletAdapter::walletCloseCompletedSignal, this, &SendFrame::reset);
  connect(&WalletAdapter::instance(), &WalletAdapter::walletSynchronizationCompletedSignal, this, &SendFrame::walletSynchronized,
    Qt::QueuedConnection);
  connect(&WalletAdapter::instance(), &WalletAdapter::walletSynchronizationProgressUpdatedSignal,
    this, &SendFrame::walletSynchronizationInProgress, Qt::QueuedConnection);

  m_ui->m_feeCurrencyLabel->setText(CurrencyAdapter::instance().getCurrencyTicker().toUpper());
  m_ui->m_remote_label->hide();
  //m_ui->m_sendButton->setEnabled(false);
  m_ui->m_feeSpin->setMinimum(getMinimalFee());

  QString connection = Settings::instance().getConnection();
  if(connection.compare("remote") == 0) {
    m_ui->m_remote_label->setText(tr("Remote mode"));
    m_ui->m_remote_label->show();
    amountValueChanged();
  }
  m_ui->m_advancedWidget->hide();
}

SendFrame::~SendFrame() {
  m_transfers.clear();
  m_glassFrame->deleteLater();
}

void SendFrame::walletSynchronized(int _error, const QString& _error_text) {
  m_ui->m_sendButton->setEnabled(true);
  m_glassFrame->remove();
}

void SendFrame::walletSynchronizationInProgress(quint64 _current, quint64 _total) {
  if (!NodeAdapter::instance().isOffline()) {
    m_glassFrame->install(this);
    m_glassFrame->updateSynchronizationState(_current, _total);
  }
}

void SendFrame::setAddress(const QString& _address) {
  Q_FOREACH (TransferFrame* transfer, m_transfers) {
    if (transfer->getAddress().isEmpty()) {
      transfer->setAddress(_address);
      return;
    }
  }

  addRecipientClicked();
  m_transfers.last()->setAddress(_address);
}

void SendFrame::addRecipientClicked() {
  TransferFrame* newTransfer = new TransferFrame(m_ui->m_transfersScrollarea);
  m_ui->m_send_frame_layout->insertWidget(m_transfers.size(), newTransfer);
  m_transfers.append(newTransfer);
  if (m_transfers.size() == 1) {
    newTransfer->disableRemoveButton(true);
  } else {
    m_transfers[0]->disableRemoveButton(false);
  }

  connect(newTransfer, &TransferFrame::destroyed, [this](QObject* _obj) {
    m_transfers.removeOne(static_cast<TransferFrame*>(_obj));
    if (m_transfers.size() == 1) {
      m_transfers[0]->disableRemoveButton(true);
    }
  });

  connect(newTransfer, &TransferFrame::amountValueChangedSignal, this, &SendFrame::amountValueChanged, Qt::QueuedConnection);

  amountValueChanged();
}

double SendFrame::getMinimalFee() {
  double fee = CurrencyAdapter::instance().formatAmount(NodeAdapter::instance().getMinimalFee()).toDouble();
  return fee;
}

void SendFrame::clearAllClicked() {
  Q_FOREACH (TransferFrame* transfer, m_transfers) {
    transfer->close();
  }
  m_transfers.clear();
  addRecipientClicked();
  amountValueChanged();
}

void SendFrame::reset() {
  amountValueChanged();
}

void SendFrame::amountValueChanged() {
  m_totalAmount = 0;
  Q_FOREACH (TransferFrame * transfer, m_transfers) {
    quint64 amount = CurrencyAdapter::instance().parseAmount(transfer->getAmountString());
    m_totalAmount += amount;
  }
}

void SendFrame::openUriClicked() {
  OpenUriDialog dlg(&MainWindow::instance());
  if (dlg.exec() == QDialog::Accepted) {
    QString uri = dlg.getURI();
    if (uri.isEmpty()) {
      return;
    }
    SendFrame::parsePaymentRequest(uri);
    Q_EMIT uriOpenSignal();
  }
}

void SendFrame::parsePaymentRequest(QString _request) {
  MainWindow::instance().showNormal();
  _request = _request.trimmed();
  if (_request.startsWith("discrete://", Qt::CaseInsensitive)) {
    _request.replace(0, 11, "discrete:");
  }
  if (!_request.startsWith("discrete:", Qt::CaseInsensitive)) {
    QCoreApplication::postEvent(&MainWindow::instance(), new ShowMessageEvent(tr("Payment request should start with discrete:"), QtCriticalMsg));
    return;
  }

  _request.remove(0, 9);
  const qsizetype querySeparator = _request.indexOf('?');
  const QString address = (querySeparator < 0 ? _request : _request.left(querySeparator)).trimmed();

  if (!CurrencyAdapter::instance().validateAddress(address)) {
    QCoreApplication::postEvent(
      &MainWindow::instance(),
      new ShowMessageEvent(tr("Invalid recipient address"), QtCriticalMsg));
    return;
  }

  const QString queryString = querySeparator < 0 ? QString() : _request.mid(querySeparator + 1);
  const QUrlQuery uriQuery(queryString);
  const bool hasAmount = uriQuery.hasQueryItem("amount");
  const QString amountText = uriQuery.queryItemValue("amount", QUrl::FullyDecoded).trimmed();
  quint64 amount = 0;
  if (hasAmount) {
    const quintptr decimalPlaces = CurrencyAdapter::instance().getNumberOfDecimalPlaces();
    const QString amountPattern = decimalPlaces == 0 ?
      QStringLiteral("^[0-9]+$") :
      QStringLiteral("^[0-9]+(?:\\.[0-9]{1,%1})?$").arg(decimalPlaces);
    const bool hasValidFormat = QRegularExpression(amountPattern).match(amountText).hasMatch();
    amount = CurrencyAdapter::instance().parseAmount(amountText);
    const bool isZeroAmount = QRegularExpression(
      QStringLiteral("^0+(?:\\.0+)?$")).match(amountText).hasMatch();
    if (!hasValidFormat || (amount == 0 && !isZeroAmount)) {
      QCoreApplication::postEvent(
        &MainWindow::instance(),
        new ShowMessageEvent(tr("Invalid payment request amount"), QtCriticalMsg));
      return;
    }

    if (amount > m_transfers.at(0)->getMaximumAmount()) {
      QCoreApplication::postEvent(
        &MainWindow::instance(),
        new ShowMessageEvent(tr("Payment request amount exceeds the wallet limit"), QtCriticalMsg));
      return;
    }
  }

  const QString label = uriQuery.queryItemValue("label", QUrl::FullyDecoded);

  // Apply a valid request to a clean form. Otherwise omitted amount/label
  // fields could silently retain values from a previous draft or URI.
  bool hasExistingDraft = m_transfers.size() > 1;
  Q_FOREACH (TransferFrame* transfer, m_transfers) {
    hasExistingDraft = hasExistingDraft || !transfer->getAddress().isEmpty() ||
      transfer->getAmount() != 0 || !transfer->getLabel().isEmpty();
  }
  if (hasExistingDraft && QMessageBox::question(
        &MainWindow::instance(), tr("Replace payment draft?"),
        tr("Opening this payment request will replace the current Send form."),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
    return;
  }

  clearAllClicked();
  m_transfers.at(0)->setAddress(address);
  if (amount != 0) {
    m_transfers.at(0)->setAmount(amount);
  }

  if (!label.isEmpty()) {
    m_transfers.at(0)->setLabel(label);
  }
}

void SendFrame::sendClicked() {
  amountValueChanged();

  const quint64 actualBalance = WalletAdapter::instance().getActualBalance();
  const quint64 minimumFee = NodeAdapter::instance().getMinimalFee();
  if (actualBalance <= minimumFee) {
    QCoreApplication::postEvent(
      &MainWindow::instance(),
      new ShowMessageEvent(tr("Insufficient available balance. Funds shown as Locked cannot be spent until they are confirmed and mature."), QtCriticalMsg));
    return;
  }

  std::vector<CryptoNote::WalletLegacyTransfer> walletTransfers;
  Q_FOREACH(TransferFrame * transfer, m_transfers) {
    QString address = transfer->getAddress();
    if (!CurrencyAdapter::instance().validateAddress(address)) {
      QCoreApplication::postEvent(
        &MainWindow::instance(),
        new ShowMessageEvent(tr("Invalid recipient address or account number"), QtCriticalMsg));
      return;
    }

    // An account number names a registration, not a recipient: the wallet has to
    // ask a node for the keys behind it and then pays whoever it is told. The
    // wallet itself refuses that against an untrusted node, so catch it here and
    // say why, rather than letting the send fail with a generic error after the
    // user has filled the whole form in.
    if (CurrencyAdapter::instance().isAccountNumber(address) &&
        !NodeAdapter::instance().isTrustedResolver()) {
      QCoreApplication::postEvent(
        &MainWindow::instance(),
        new ShowMessageEvent(
          tr("The connected node is not marked as trusted, so account numbers cannot "
             "be resolved. Mark it trusted in Connection settings if you trust its "
             "operator, or paste the recipient's full address instead."), QtCriticalMsg));
      return;
    }

    CryptoNote::WalletLegacyTransfer walletTransfer;
    walletTransfer.address = address.toStdString();
    uint64_t amount = CurrencyAdapter::instance().parseAmount(transfer->getAmountString());
    if (amount == 0) {
      QCoreApplication::postEvent(
        &MainWindow::instance(),
        new ShowMessageEvent(tr("Invalid amount"), QtCriticalMsg));
      return;
    }

    walletTransfer.amount = amount;
    walletTransfers.push_back(walletTransfer);
    QString label = transfer->getLabel();
    if (!label.isEmpty()) {
      AddressBookModel::instance().addAddress(label, address);
    }
  }

  // Fee. Discrete has no fee market: the consensus floor is size-based
  // (~1 atomic unit per 4 KB), and PQ transactions are large, so the correct
  // fee can only be known once the wallet has selected inputs and built the tx.
  // Passing 0 tells the wallet to compute that exact floor itself (see
  // PqSender: explicitFee==0 -> auto). A non-zero fee is used verbatim and is
  // rejected by the network if it falls below the floor, so only send one when
  // the user explicitly overrides.
  quint64 fee = getFee();

  if (m_ui->m_manualFeeCheckBox->isChecked() && fee < NodeAdapter::instance().getMinimalFee()) {
    QCoreApplication::postEvent(&MainWindow::instance(), new ShowMessageEvent(tr("Incorrect fee value"), QtCriticalMsg));
    return;
  }

  quint64 total_transaction_amount = 0;
  for (const auto& transfer : walletTransfers) {
    const quint64 amount = static_cast<quint64>(transfer.amount);
    if (amount > std::numeric_limits<quint64>::max() - total_transaction_amount) {
      QMessageBox::critical(this, tr("Invalid amount"), tr("The total amount is too large."), QMessageBox::Ok);
      return;
    }
    total_transaction_amount += amount;
  }
  // Auto-fee is encoded as zero for the backend, but the preflight must still
  // reserve at least the network floor. Check before subtracting to avoid an
  // unsigned underflow when a manual fee itself exceeds the available balance.
  const quint64 requiredFee = m_ui->m_manualFeeCheckBox->isChecked() ? fee : minimumFee;
  if (requiredFee > actualBalance || total_transaction_amount > actualBalance - requiredFee) {
    QMessageBox::critical(this, tr("Insufficient available balance"),
      tr("Available balance is insufficient to cover the amount and fee. Funds shown as Locked cannot be spent until they are confirmed and mature."), QMessageBox::Ok);
    return;
  }

  if (Settings::instance().isEncrypted()) {
    PasswordDialog pass_dlg(false, this);
    if (pass_dlg.exec() == QDialog::Accepted) {
      QString password = pass_dlg.getPassword();
      if (!WalletAdapter::instance().tryOpen(password)) {
        QMessageBox::critical(nullptr, tr("Incorrect password"), tr("Wrong password."), QMessageBox::Ok);
        return;
      }
    }
    else {
      return;
    }
  } else if (!WalletAdapter::instance().tryOpen("")) {
    return;
  }

  ConfirmSendDialog dlg(&MainWindow::instance());
  dlg.showPasymentDetails(m_totalAmount);
  if (dlg.exec() == QDialog::Accepted) {
    if (WalletAdapter::instance().isOpen()) {
      CryptoPQ::SeedMaster protectedSeed{};
      Tools::SecretLock scrubProtectedSeed(protectedSeed.data(), protectedSeed.size());
      const bool protectedSpending = WalletAdapter::instance().isYubiKeyProtected();
      if (protectedSpending) {
        QString unlockError;
        if (!WalletAdapter::instance().unlockYubiKeySeed(
                MainWindow::instance().winId(), protectedSeed, unlockError)) {
          QCoreApplication::postEvent(
              &MainWindow::instance(), new ShowMessageEvent(unlockError, QtCriticalMsg));
          return;
        }
      }
      if (m_ui->dontRelayCheckBox->isChecked()) {
        QString errorText;
        const QString rawTransaction = protectedSpending
            ? WalletAdapter::instance().prepareRawTransactionWithSeed(
                  protectedSeed, walletTransfers, fee, &errorText)
            : WalletAdapter::instance().prepareRawTransaction(
                  walletTransfers, fee, &errorText);
        if (rawTransaction.isEmpty()) {
          if (errorText.isEmpty()) {
            errorText = tr("Failed to prepare transaction.");
          }
          QCoreApplication::postEvent(
              &MainWindow::instance(), new ShowMessageEvent(errorText, QtCriticalMsg));
          return;
        }

        ExportRawTransactionDialog rawDialog(&MainWindow::instance());
        rawDialog.setTransaction(rawTransaction);
        rawDialog.exec();
      } else {
        if (protectedSpending) {
          WalletAdapter::instance().sendTransactionWithSeed(protectedSeed, walletTransfers, fee);
        } else {
          WalletAdapter::instance().sendTransaction(walletTransfers, fee);
        }
      }
    }
  }
}

void SendFrame::dontRelayToggled(bool _dontRelay) {
  if (_dontRelay) {
    m_ui->m_sendButton->setText(tr("Prepare"));
    m_ui->m_sendButton->setIcon(QIcon(":/icons/save"));
  } else {
    m_ui->m_sendButton->setText(tr("Send"));
    m_ui->m_sendButton->setIcon(QIcon(":/icons/btn-send"));
  }
}

quint64 SendFrame::getFee() {
  if (m_ui->m_manualFeeCheckBox->isChecked()) {
     return CurrencyAdapter::instance().parseAmount(m_ui->m_feeSpin->cleanText());
  }

  // 0 => let the wallet compute the exact size-based PQ fee floor (see
  // sendClicked). Discrete has no fee market, so the priority slider does not
  // affect the fee.
  return 0;
}

void SendFrame::sendTransactionCompleted(CryptoNote::TransactionId _id, bool _error, const QString& _errorText) {
  Q_UNUSED(_id);
  if (_error) {
    QCoreApplication::postEvent(
      &MainWindow::instance(),
      new ShowMessageEvent(_errorText, QtCriticalMsg));
  } else {
    clearAllClicked();
  }
}

void SendFrame::walletActualBalanceUpdated(quint64 _balance) {
  Q_UNUSED(_balance);
}

void SendFrame::advancedClicked(bool _show) {
  if (_show) {
    m_ui->m_advancedWidget->show();
  } else {
    m_ui->m_advancedWidget->hide();
  }
}

}
