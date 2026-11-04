#include "gui/WebsiteLoginDialog.h"
#include <QApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QNetworkProxy>
#include <QPointer>
#include <QPushButton>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <iostream>
#include <memory>
#include <stdexcept>

using WalletGui::WebsiteLoginDialog;
static void require(bool ok, const char* reason) { if (!ok) throw std::runtime_error(reason); }
static bool waitFor(const std::function<bool()>& condition, int ms = 4000) {
  QElapsedTimer timer; timer.start();
  while (!condition() && timer.elapsed() < ms) { QApplication::processEvents(); QThread::msleep(1); }
  return condition();
}
class BrowserSink : public QObject {
  Q_OBJECT
public: QUrl url; int count = 0;
public slots: void capture(const QUrl& value) { url = value; ++count; }
};
class Server : public QTcpServer {
public:
  QString mode; QJsonObject lastStart; int starts = 0, proofs = 0;
  std::function<void()> afterStart;
  Server() {
    require(listen(QHostAddress::LocalHost, QUrl(WebsiteLoginDialog::websiteOrigin()).port()), "test server bind");
    connect(this, &QTcpServer::newConnection, this, [this] {
      auto* socket = nextPendingConnection(); auto bytes = std::make_shared<QByteArray>();
      connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
      connect(socket, &QTcpSocket::readyRead, socket, [this, socket, bytes] {
        if (socket->property("handled").toBool()) return;
        *bytes += socket->readAll(); const int split = bytes->indexOf("\r\n\r\n"); if (split < 0) return;
        int length = -1;
        for (const auto& line : bytes->left(split).split('\n')) if (line.toLower().startsWith("content-length:")) length = line.mid(15).trimmed().toInt();
        if (length < 0 || bytes->size() < split + 4 + length) return;
        const auto input = QJsonDocument::fromJson(bytes->mid(split + 4, length)).object();
        const auto path = bytes->split(' ').value(1); socket->setProperty("handled", true);
        QByteArray response; int status = 200;
        if (path == "/api/auth/wallet/start") {
          ++starts; lastStart = input;
          const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
          const qint64 expires = QDateTime::currentMSecsSinceEpoch() + 120000;
          QString message = QString("Sign in to %1 | Discrete Services | Wallet-directed browser login v1 | Network: mainnet | Identity: %2 | Challenge: %3 | Wallet nonce: %4 | Browser intent: %5 | Browser handoff: %6 | Expires: %7 | This signature opens a website session. It does not transfer funds.")
              .arg(WebsiteLoginDialog::websiteOrigin(), QString(64, 'a'), id, input.value("walletNonce").toString(), mode == "intentchanged" ? QString(64, 'b') : input.value("intentHash").toString(), input.value("handoffHash").toString(), QDateTime::fromMSecsSinceEpoch(expires, Qt::UTC).toString(Qt::ISODateWithMs));
          if (mode == "changed") message += " changed";
          response = QJsonDocument(QJsonObject{{"id", id}, {"expires", expires}, {"identity", QString(64, 'a')}, {"network", "mainnet"}, {"message", message}}).toJson(QJsonDocument::Compact);
          if (afterStart) afterStart();
        } else if (path == "/api/auth/wallet/prove") {
          ++proofs;
          const auto digest = QString::fromLatin1(QCryptographicHash::hash(input.value("handoff").toString().toLatin1(), QCryptographicHash::Sha256).toHex());
          require(digest == lastStart.value("handoffHash").toString(), "wallet must prove its own handoff preimage");
          require(input.value("signature").toString() == "test-signature", "explicit signer result");
          response = "{\"approved\":true}";
        } else { status = 404; response = "{}"; }
        if (mode == "offline") status = 503;
        if (mode == "oversize") response = QByteArray(33000, 'x');
        if (mode == "redirect") { socket->write("HTTP/1.1 302 Found\r\nLocation: http://127.0.0.1:1/stolen\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"); socket->disconnectFromHost(); return; }
        const auto send = [socket = QPointer<QTcpSocket>(socket), response, status] {
          if (!socket) return;
          socket->write("HTTP/1.1 " + QByteArray::number(status) + " Response\r\nContent-Type: application/json\r\nContent-Length: " + QByteArray::number(response.size()) + "\r\nConnection: close\r\n\r\n" + response); socket->disconnectFromHost();
        };
        if (mode == "delay") QTimer::singleShot(200, this, send); else send();
      });
    });
  }
};
int main(int argc, char** argv) {
  QApplication app(argc, argv); QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);
  BrowserSink browser; QDesktopServices::setUrlHandler("http", &browser, "capture");
  try {
    const QString intent(64, 'a');
    require(WebsiteLoginDialog::isValidLoginRequest("discrete:login?intent=" + intent), "login URI");
    require(WebsiteLoginDialog::isValidLoginRequest("discrete://login?intent=" + intent), "alternate URI");
    require(!WebsiteLoginDialog::isValidLoginRequest("discrete:login"), "require browser binding");
    require(!WebsiteLoginDialog::isValidLoginRequest("discrete:login?intent=" + intent + "&callback=evil"), "reject extra parameters");
    require(!WebsiteLoginDialog::isValidLoginRequest("discrete:login?intent=" + intent + "&intent=" + intent), "reject duplicate binding");
    require(!WebsiteLoginDialog::isValidLoginRequest("discrete:login?request=attacker"), "reject supplied challenge");
    require(!WebsiteLoginDialog::isValidLoginRequest("discrete:login#callback=https://attacker.example"), "reject supplied callback");
    require(!WebsiteLoginDialog::isLoginRequest("discrete:disc1payment"), "payment routing preserved");
    require(WebsiteLoginDialog::isReservedLoginMessage("Sign in | Discrete Services | Wallet-directed browser login v1 | message"), "manual sign protection");
    require(!WebsiteLoginDialog::isReservedLoginMessage("Ordinary signed message"), "ordinary messages preserved");
    Server server; QString address = "disc1synthetic"; int signedCount = 0;
    auto signer = [&](const QString& message) { ++signedCount; require(message.contains(server.lastStart.value("handoffHash").toString()), "signature binds own secret hash"); return QString("test-signature"); };
    auto attempt = [&](const QString& mode) {
      server.mode = mode;
      WebsiteLoginDialog dialog([&] { return address; }, signer, intent); dialog.show();
      const int signedBefore = signedCount, browserBefore = browser.count;
      auto* button = dialog.findChild<QPushButton*>("confirm-website-login"); require(button, "approval button");
      require(signedCount == signedBefore, "no signing before approval"); button->click();
      if (mode == "delay") { require(waitFor([&] { return server.lastStart.value("address").toString() == address; }), "request started"); dialog.reject(); QElapsedTimer timer; timer.start(); while (timer.elapsed() < 250) QApplication::processEvents(); }
      else require(waitFor([&] { return dialog.result() == QDialog::Accepted || button->isEnabled(); }), "approval completed or failed");
      if (mode.isEmpty()) {
        require(dialog.result() == QDialog::Accepted && signedCount == signedBefore + 1 && browser.count == browserBefore + 1, "successful GUI approval");
        require(browser.url.adjusted(QUrl::RemoveFragment).toString() == WebsiteLoginDialog::websiteOrigin() + "/account/", "fixed browser destination");
        const QString secret = browser.url.fragment().section('.', -1);
        require(secret.size() == 64, "handoff secret size");
        require(QString::fromLatin1(QCryptographicHash::hash(secret.toLatin1(), QCryptographicHash::Sha256).toHex()) == server.lastStart.value("handoffHash").toString(), "browser receives wallet-generated secret");
      } else require(signedCount == signedBefore && browser.count == browserBefore, "failure cannot sign or open browser");
    };
    { WebsiteLoginDialog dialog([&] { return address; }, signer, intent); const int starts = server.starts; dialog.reject(); require(server.starts == starts, "cancel before approval makes no request"); }
    attempt(""); const auto firstHash = server.lastStart.value("handoffHash");
    attempt(""); require(server.lastStart.value("handoffHash") != firstHash, "fresh secret per attempt");
    attempt("changed"); attempt("intentchanged"); attempt("redirect"); attempt("oversize"); attempt("offline");
    server.afterStart = [&] { address = "disc1changed"; }; attempt("walletchanged"); server.afterStart = {};
    address = "disc1synthetic"; server.lastStart = {}; attempt("delay");
    attempt("");
    std::cout << "PASS: URI isolation, explicit consent, fresh handoff, fixed destination, altered response, redirect, oversized response, offline/retry, cancellation and normal message policy\n";
  } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
  QDesktopServices::unsetUrlHandler("http"); return 0;
}
#include "WebsiteLoginTests.moc"
