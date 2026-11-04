#include "WebsiteLoginDialog.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QUuid>
#include <QVBoxLayout>
#include <utility>

#ifndef DISCRETE_WEBSITE_LOGIN_ORIGIN
#define DISCRETE_WEBSITE_LOGIN_ORIGIN "https://services.discrete.cash"
#endif

namespace {
QString randomSecret() {
  QByteArray bytes(32, '\0');
  for (int i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<char>(QRandomGenerator::system()->generate() & 0xff);
  return QString::fromLatin1(bytes.toHex());
}
QString handoffHash(const QString& secret) {
  return QString::fromLatin1(QCryptographicHash::hash(secret.toLatin1(), QCryptographicHash::Sha256).toHex());
}
}

namespace WalletGui {
QString WebsiteLoginDialog::websiteOrigin() { return QStringLiteral(DISCRETE_WEBSITE_LOGIN_ORIGIN); }
bool WebsiteLoginDialog::isLoginRequest(const QString& uri) {
  return uri.trimmed().startsWith("discrete:login", Qt::CaseInsensitive) || uri.trimmed().startsWith("discrete://login", Qt::CaseInsensitive);
}
bool WebsiteLoginDialog::isValidLoginRequest(const QString& uri) {
  return !loginIntent(uri).isEmpty();
}
QString WebsiteLoginDialog::loginIntent(const QString& uri) {
  const auto match = QRegularExpression("^discrete:(?://)?login\\?intent=([a-f0-9]{64})$", QRegularExpression::CaseInsensitiveOption).match(uri.trimmed());
  return match.hasMatch() ? match.captured(1).toLower() : QString();
}
bool WebsiteLoginDialog::isReservedLoginMessage(const QString& message) {
  return message.contains(" | Discrete Services | Wallet-directed browser login v1 | ");
}

WebsiteLoginDialog::WebsiteLoginDialog(Address address, Signer signer, QString intentHash, QWidget* parent)
    : QDialog(parent), m_address(std::move(address)), m_signer(std::move(signer)),
      m_network(new QNetworkAccessManager(this)) {
  m_intentHash = std::move(intentHash);
  setObjectName("website-login"); setWindowTitle(tr("Sign in to Discrete Services")); setMinimumWidth(470);
  auto* layout = new QVBoxLayout(this);
  auto* heading = new QLabel(tr("Confirm website sign-in"), this); layout->addWidget(heading);
  auto* domain = new QLabel(websiteOrigin(), this); domain->setTextFormat(Qt::PlainText); domain->setTextInteractionFlags(Qt::TextSelectableByMouse); layout->addWidget(domain);
  auto* explanation = new QLabel(tr("The wallet will create a new sign-in and open this website in your default browser. Start sign-in on the website in that browser first. No payment or transaction will be created."), this);
  explanation->setWordWrap(true); layout->addWidget(explanation);
  m_status = new QLabel(this); m_status->setObjectName("website-login-status"); m_status->setTextFormat(Qt::PlainText); m_status->setWordWrap(true); layout->addWidget(m_status);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
  m_confirm = buttons->addButton(tr("Confirm sign-in"), QDialogButtonBox::AcceptRole); m_confirm->setObjectName("confirm-website-login");
  connect(m_confirm, &QPushButton::clicked, this, [this] { if (m_browserUrl.isEmpty()) approve(); else openWebsite(); });
  connect(buttons, &QDialogButtonBox::rejected, this, &WebsiteLoginDialog::reject); layout->addWidget(buttons);
}

void WebsiteLoginDialog::reject() {
  m_cancelled = true; if (m_reply) m_reply->abort();
  m_handoff.fill(QChar('\0')); QDialog::reject();
}

void WebsiteLoginDialog::fail(const QString& message) {
  if (m_cancelled) return;
  m_status->setText(message); m_confirm->setEnabled(true);
}

void WebsiteLoginDialog::post(const QString& path, const QJsonObject& input,
                              std::function<void(const QJsonObject&)> done) {
  QNetworkRequest request(QUrl(websiteOrigin() + path));
  request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
  request.setRawHeader("X-Discrete-Wallet", "1");
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
  request.setTransferTimeout(10000);
  m_response.clear();
  auto* reply = m_network->post(request, QJsonDocument(input).toJson(QJsonDocument::Compact)); m_reply = reply;
  reply->setReadBufferSize(32769);
  connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
    m_response += reply->readAll(); if (m_response.size() > 32000) reply->abort();
  });
  connect(reply, &QNetworkReply::finished, this, [this, reply, done] {
    m_reply = nullptr; m_response += reply->readAll();
    const bool ok = !m_cancelled && reply->error() == QNetworkReply::NoError && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200 && m_response.size() <= 32000;
    const auto document = QJsonDocument::fromJson(m_response); reply->deleteLater();
    if (m_cancelled) return;
    if (!ok || !document.isObject()) { fail(tr("Website sign-in failed. Check the connection and try again. No sign-in was opened.")); return; }
    done(document.object());
  });
}

void WebsiteLoginDialog::approve() {
  if (!QRegularExpression("^[a-f0-9]{64}$").match(m_intentHash).hasMatch()) { fail(tr("Start sign-in on the Discrete Services website first.")); return; }
  m_walletAddress = m_address();
  if (!m_walletAddress.startsWith("disc1") || m_walletAddress.size() > 6000) { fail(tr("Open a full mainnet wallet before signing in.")); return; }
  m_confirm->setEnabled(false); m_status->setText(tr("Creating a new sign-in…"));
  // These values are generated here after consent. The URI contributes only
  // a browser intent hash, never a challenge, callback or handoff secret.
  m_nonce = randomSecret(); m_handoff = randomSecret();
  const QString binding = handoffHash(m_handoff);
  post("/api/auth/wallet/start", {{"address", m_walletAddress}, {"walletNonce", m_nonce}, {"intentHash", m_intentHash}, {"handoffHash", binding}},
       [this, binding](const QJsonObject& result) {
    m_id = result.value("id").toString();
    const QString identity = result.value("identity").toString();
    const qint64 expires = result.value("expires").toVariant().toLongLong();
    const QString stamp = QDateTime::fromMSecsSinceEpoch(expires, Qt::UTC).toString(Qt::ISODateWithMs);
    const QString expected = QString("Sign in to %1 | Discrete Services | Wallet-directed browser login v1 | Network: mainnet | Identity: %2 | Challenge: %3 | Wallet nonce: %4 | Browser intent: %5 | Browser handoff: %6 | Expires: %7 | This signature opens a website session. It does not transfer funds.").arg(websiteOrigin(), identity, m_id, m_nonce, m_intentHash, binding, stamp);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (QUuid(m_id).isNull() || QUuid(m_id).toString(QUuid::WithoutBraces) != m_id ||
        !QRegularExpression("^[a-f0-9]{64}$").match(identity).hasMatch() ||
        result.value("network").toString() != "mainnet" || expires <= now || expires > now + 180000 ||
        result.value("message").toString() != expected || m_address() != m_walletAddress) {
      fail(tr("The website returned an unexpected sign-in request. Nothing was signed.")); return;
    }
    QString signature;
    try { signature = m_signer(expected); }
    catch (...) { fail(tr("The wallet could not sign this request. Try again.")); return; }
    if (signature.isEmpty() || m_address() != m_walletAddress) { fail(tr("Signing was cancelled or the wallet changed. No sign-in was opened.")); return; }
    m_status->setText(tr("Verifying wallet approval…"));
    post("/api/auth/wallet/prove", {{"id", m_id}, {"handoff", m_handoff}, {"signature", signature}}, [this](const QJsonObject& result) {
      if (result.value("approved").toBool() != true) { fail(tr("The website did not accept this approval. Try again.")); return; }
      m_browserUrl = QUrl(websiteOrigin() + "/account/");
      m_browserUrl.setFragment("wallet-login=" + m_id + "." + m_handoff);
      m_confirm->setText(tr("Open website")); openWebsite();
    });
  });
}

void WebsiteLoginDialog::openWebsite() {
  if (!QDesktopServices::openUrl(m_browserUrl)) { fail(tr("Could not open your browser. Click Open website to retry.")); return; }
  m_handoff.fill(QChar('\0')); accept();
}
}
