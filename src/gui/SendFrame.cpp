// Copyright (c) 2011-2015 The Cryptonote developers
// Copyright (c) 2015-2016 XDN developers
// Copyright (c) 2016-2021 The Karbo developers
// Copyright (c) 2026 The Discrete developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QRegularExpressionValidator>
#include <QRegularExpression>
#include <QInputDialog>
#include <QMessageBox>
#include <QUrlQuery>
#include <QTime>
#include <QUrl>

#include "CryptoNoteCore/CryptoNoteFormatUtils.h"
#include "AddressBookModel.h"
#include "CurrencyAdapter.h"
#include "MainWindow.h"
#include "NodeAdapter.h"
#include "SendFrame.h"
#include "TransferFrame.h"
#include "WalletAdapter.h"
#include "WalletEvents.h"
#include "Settings.h"
#include "OpenUriDialog.h"
#include "ConfirmSendDialog.h"
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

  m_ui->m_feeSpin->setSuffix(" " + CurrencyAdapter::instance().getCurrencyTicker().toUpper());
  m_ui->m_remote_label->hide();
  //m_ui->m_sendButton->setEnabled(false);
  m_ui->m_feeSpin->setMinimum(getMinimalFee());
  updateFeeEstimate();

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
    m_ui->m_sendAllButton->setEnabled(true);
  } else {
    m_transfers[0]->disableRemoveButton(false);
    m_ui->m_sendAllButton->setEnabled(false);
  }

  connect(newTransfer, &TransferFrame::destroyed, [this](QObject* _obj) {
    m_transfers.removeOne(static_cast<TransferFrame*>(_obj));
    if (m_transfers.size() == 1) {
      m_transfers[0]->disableRemoveButton(true);
      m_ui->m_sendAllButton->setEnabled(true);
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
  m_ui->m_sendAllButton->setEnabled(true);

  Q_FOREACH (TransferFrame* transfer, m_transfers) {
    transfer->close();
  }
  m_transfers.clear();
  addRecipientClicked();
  amountValueChanged();
}

void SendFrame::reset() {
  m_ui->m_sendAllButton->setEnabled(true);
  amountValueChanged();
}

void SendFrame::amountValueChanged() {
  m_totalAmount = 0;
  Q_FOREACH (TransferFrame * transfer, m_transfers) {
    quint64 amount = CurrencyAdapter::instance().parseAmount(transfer->getAmountString());
    m_totalAmount += amount;
  }
  updateFeeEstimate();
}

// Discrete's fee is size-based (~1 atomic unit per 4 KB, see sendClicked), so
// the exact fee is only known once the wallet selects inputs and builds the tx.
// Show an approximate estimate from the recipient count; a manual override, if
// set, is shown verbatim.
void SendFrame::updateFeeEstimate() {
  quint64 feeAtomic;
  bool manual = m_ui->m_manualFeeCheckBox->isChecked();
  if (manual) {
    feeAtomic = CurrencyAdapter::instance().parseAmount(m_ui->m_feeSpin->cleanText());
  } else {
    // outputs = recipients + 1 change; assume ~2 inputs (typical). Each PQ
    // input ~5.4 KB, each output ~1.2 KB, ~0.6 KB overhead; floor ~1 atomic/4 KB.
    const quint64 outputs = static_cast<quint64>(m_transfers.size()) + 1;
    const quint64 inputs = 2;
    const quint64 size = inputs * 5400ULL + outputs * 1200ULL + 600ULL;
    feeAtomic = (size + 3999ULL) / 4000ULL + 1ULL;
  }
  const QString ticker = CurrencyAdapter::instance().getCurrencyTicker().toUpper();
  const QString text = (manual ? QString() : QStringLiteral("≈ ")) +
    CurrencyAdapter::instance().formatAmount(feeAtomic) + " " + ticker;
  m_ui->m_feeEstimateLabel->setText(text);
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
  if(_request.startsWith("discrete://", Qt::CaseInsensitive))
  {
    _request.replace(0, 11, "discrete:");
  }
  if(!_request.startsWith("discrete:", Qt::CaseInsensitive)) {
    QCoreApplication::postEvent(&MainWindow::instance(), new ShowMessageEvent(tr("Payment request should start with discrete:"), QtCriticalMsg));
    return;
  }

  if(_request.startsWith("discrete:", Qt::CaseInsensitive))
  {
    _request.remove(0, 9);
  }

  QString address = _request.split("?").at(0);

  if (!CurrencyAdapter::instance().validateAddress(address)) {
    QCoreApplication::postEvent(
      &MainWindow::instance(),
      new ShowMessageEvent(tr("Invalid recipient address"), QtCriticalMsg));
    return;
  }
  m_transfers.at(0)->TransferFrame::setAddress(address);

  _request.replace("?", "&");

  QUrlQuery uriQuery(_request);

  quint64 amount = CurrencyAdapter::instance().parseAmount(uriQuery.queryItemValue("amount"));
  if(amount != 0){
    m_transfers.at(0)->TransferFrame::setAmount(amount);
  }

  QString label = uriQuery.queryItemValue("label");
  if(!label.isEmpty()){
    m_transfers.at(0)->TransferFrame::setLabel(label);
  }
}

void SendFrame::sendClicked() {
  amountValueChanged();

  quint64 actualBalance = WalletAdapter::instance().getActualBalance();
  if (actualBalance <= NodeAdapter::instance().getMinimalFee()) {
    QCoreApplication::postEvent(
      &MainWindow::instance(),
      new ShowMessageEvent(tr("Insufficient balance."), QtCriticalMsg));
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
  for (size_t i = 0; i < walletTransfers.size(); i++) {
    total_transaction_amount += walletTransfers.at(i).amount;
  }
  if (total_transaction_amount > (WalletAdapter::instance().getActualBalance() - fee)) {
    QMessageBox::critical(this, tr("Insufficient balance"), tr("Available balance is insufficient to send this transaction. Have you excluded a fee?"), QMessageBox::Ok);
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
      WalletAdapter::instance().sendTransaction(walletTransfers, fee);
    }
  }
}

void SendFrame::feeValueChanged(double _value) {
  Q_UNUSED(_value);
  updateFeeEstimate();
}

void SendFrame::feeOverrideToggled(bool _override) {
  Q_UNUSED(_override);
  updateFeeEstimate();
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

void SendFrame::sendAllClicked() {
  quint64 actualBalance = WalletAdapter::instance().getActualBalance();
  if (actualBalance < NodeAdapter::instance().getMinimalFee()) {
    QCoreApplication::postEvent(
      &MainWindow::instance(),
      new ShowMessageEvent(tr("Insufficient balance."), QtCriticalMsg));
    return;
  }

  // "Send all" must leave room for the fee, but the exact (size-based) fee is
  // only known once the wallet builds the tx. Reserve a conservative estimate
  // from the number of unlocked outputs it will spend — each PQ input is ~5 KB
  // and the floor is ~1 atomic unit per 4 KB — so the wallet's own auto-computed
  // fee (see sendClicked) fits and at most a tiny change is left over. A manual
  // override, if set, is used as-is.
  quint64 fee;
  if (m_ui->m_manualFeeCheckBox->isChecked()) {
    fee = getFee();
  } else {
    quint64 inputs = WalletAdapter::instance().getUnlockedOutputsCount();
    if (inputs == 0) {
      inputs = 1;
    }
    const quint64 estimatedSize = inputs * 5400ULL + 2000ULL; // inputs + one output + overhead
    fee = (estimatedSize + 3999ULL) / 4000ULL + 2ULL;         // ceil(size/4KB) + PqSender's +1 + safety
  }

  quint64 amount = actualBalance > fee ? actualBalance - fee : 0;
  m_transfers[0]->setAmount(amount);
}

}
