#pragma once

#include <QDialog>
#include <QByteArray>
#include <QUrl>
#include <functional>

class QLabel;
class QPushButton;
class QNetworkAccessManager;
class QNetworkReply;
class QJsonObject;

namespace WalletGui {
class WebsiteLoginDialog : public QDialog {
  Q_OBJECT
public:
  using Address = std::function<QString()>;
  using Signer = std::function<QString(const QString&)>;
  WebsiteLoginDialog(Address address, Signer signer, QString intentHash, QWidget* parent = nullptr);
  static bool isLoginRequest(const QString& uri);
  static bool isValidLoginRequest(const QString& uri);
  static QString loginIntent(const QString& uri);
  static bool isReservedLoginMessage(const QString& message);
  static QString websiteOrigin();
  void reject() override;

private:
  void approve();
  void post(const QString& path, const QJsonObject& input, std::function<void(const QJsonObject&)> done);
  void fail(const QString& message);
  void openWebsite();
  Address m_address;
  Signer m_signer;
  QNetworkAccessManager* m_network;
  QNetworkReply* m_reply = nullptr;
  QLabel* m_status;
  QPushButton* m_confirm;
  QByteArray m_response;
  QString m_walletAddress, m_nonce, m_handoff, m_id, m_intentHash;
  QUrl m_browserUrl;
  bool m_cancelled = false;
};
}
