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

#include <limits>

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
  updateFeeEstimate();
}

// Discrete's fee is a fixed, size-based floor with no fee market, so there is
// nothing for the user to choose: show a plain "automatic" note by default and
// only display a concrete amount when the user sets a manual override. The
// exact fee is computed by the wallet at send time (see sendClicked).
void SendFrame::updateFeeEstimate() {
  if (m_ui->m_manualFeeCheckBox->isChecked()) {
    const quint64 feeAtomic = CurrencyAdapter::instance().parseAmount(m_ui->m_feeSpin->cleanText());
    const QString ticker = CurrencyAdapter::instance().getCurrencyTicker().toUpper();
    m_ui->m_feeEstimateLabel->setText(CurrencyAdapter::instance().formatAmount(feeAtomic) + " " + ticker);
  } else {
    m_ui->m_feeEstimateLabel->setText(tr("Automatic (based on size)"));
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

}
