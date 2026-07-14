// Copyright (c) 2011-2015 The Cryptonote developers
// Copyright (c) 2026 The Discrete developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include "crypto/crypto.h"
#include "CurrencyAdapter.h"
#include "TransactionDetailsDialog.h"
#include "TransactionsModel.h"
#include <IWalletLegacy.h>
#include "WalletAdapter.h"
#include "Common/StringTools.h"
#include "CryptoNoteCore/CryptoNoteBasic.h"
#include "Wallet/SentPaymentsStore.h"

#include "ui_transactiondetailsdialog.h"

using namespace CryptoNote;

namespace WalletGui {

TransactionDetailsDialog::TransactionDetailsDialog(const QModelIndex& _index, QWidget* _parent) : QDialog(_parent),
  m_ui(new Ui::TransactionDetailsDialog), m_detailsTemplate(tr(
    "<html><head><meta name=\"qrichtext\" content=\"1\" /><style type=\"text/css\">\n"
    "</style></head><body style=\" font-family:'Cantarell'; font-size:11pt; font-weight:400; font-style:normal;\">\n"
    "<p><span style=\" font-weight:600;\">Status: </span>%1<br>\n"
    "<span style=\" font-weight:600;\">Date: </span>%2<br>\n"
    "<span style=\" font-weight:600;\">To: </span>%3<br>\n"
    "<span style=\" font-weight:600;\">Amount: </span>%4<br>\n"
    "<span style=\" font-weight:600;\">Fee: </span>%5<br>\n"
    "<span style=\" font-weight:600;\">Transaction Hash: </span>%6\n</p></body></html>")) {
  m_ui->setupUi(this);

  QModelIndex index = TransactionsModel::instance().index(
    _index.data(TransactionsModel::ROLE_ROW).toInt(), TransactionsModel::COLUMN_STATE);

  quint64 numberOfConfirmations = index.data(TransactionsModel::ROLE_NUMBER_OF_CONFIRMATIONS).value<quint64>();
  QString amountText = index.sibling(index.row(), TransactionsModel::COLUMN_AMOUNT).data().toString() + " " +
    CurrencyAdapter::instance().getCurrencyTicker().toUpper();
  QString feeText = CurrencyAdapter::instance().formatAmount(index.data(TransactionsModel::ROLE_FEE).value<quint64>()) + " " +
    CurrencyAdapter::instance().getCurrencyTicker().toUpper();

  QString state;
  CryptoNote::WalletLegacyTransaction transaction;
  CryptoNote::TransactionId transactionId =
    index.data(TransactionsModel::ROLE_TRANSACTION_ID).value<CryptoNote::TransactionId>();
  const bool hasTransaction = WalletAdapter::instance().getTransaction(transactionId, transaction);
  if (hasTransaction) {
    if (transaction.state == CryptoNote::WalletLegacyTransactionState::Failed) {
      state = tr("Failed");
    } else if (transaction.state == CryptoNote::WalletLegacyTransactionState::Cancelled) {
      state = tr("Cancelled");
    }
  }
  if (state.isEmpty()) {
    state = QString(tr("%n confirmation(s)", "", numberOfConfirmations));
  }

  QString transactionHash = index.sibling(index.row(), TransactionsModel::COLUMN_HASH).data().toString();

  QString html = m_detailsTemplate.arg(state).
    arg(index.sibling(index.row(), TransactionsModel::COLUMN_DATE).data().toString()).arg(index.sibling(index.row(),
    TransactionsModel::COLUMN_ADDRESS).data().toString()).arg(amountText).arg(feeText).
    arg(transactionHash);

  // Payer-side recipients and their off-chain payment proofs, captured at send time.
  // Present only for outgoing transactions this wallet sent; the History "To" column
  // above shows an elided address, so here we surface the full address, the amount to
  // each recipient, and the copyable proof string (disctxp1…) verifiers consume.
  if (hasTransaction) {
    CryptoNote::SentPaymentRecord record;
    if (WalletAdapter::instance().getPaymentProofs(transaction.hash, record) && !record.recipients.empty()) {
      QString ticker = CurrencyAdapter::instance().getCurrencyTicker().toUpper();
      QString extra = QStringLiteral(
        "<p><span style=\" font-weight:600;\">Recipients &amp; payment proofs:</span></p>");
      QStringList proofs;
      for (const CryptoNote::SentPaymentEntry& r : record.recipients) {
        QString address = QString::fromStdString(r.address).toHtmlEscaped();
        QString amount = CurrencyAdapter::instance().formatAmount(r.amount) + " " + ticker;
        extra += QStringLiteral("<p><span style=\" font-weight:600;\">Address: </span>%1<br>\n"
                                "<span style=\" font-weight:600;\">Amount: </span>%2")
                     .arg(address, amount);
        if (!r.proof.empty()) {
          QString proof = QString::fromStdString(r.proof);
          proofs << proof;
          extra += QStringLiteral(
                       "<br>\n<span style=\" font-weight:600;\">Payment proof: </span>"
                       "<span style=\"font-family:monospace; word-break:break-all;\">%1</span>")
                       .arg(proof.toHtmlEscaped());
        }
        extra += QStringLiteral("</p>");
      }
      html.insert(html.lastIndexOf(QStringLiteral("</body>")), extra);

      if (!proofs.isEmpty()) {
        m_proofText = proofs.join(QChar('\n'));
        m_ui->m_copyProofButton->setVisible(true);
        connect(m_ui->m_copyProofButton, &QPushButton::clicked, this, &TransactionDetailsDialog::copyProof);
      }
    }
  }

  m_ui->m_detailsBrowser->setHtml(html);
}

void TransactionDetailsDialog::copyProof() {
  if (!m_proofText.isEmpty()) {
    QApplication::clipboard()->setText(m_proofText);
  }
}

TransactionDetailsDialog::~TransactionDetailsDialog() {
}

}
