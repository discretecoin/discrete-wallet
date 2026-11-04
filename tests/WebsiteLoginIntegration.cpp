#include "gui/WebsiteLoginDialog.h"
#include "Wallet/PqWallet.h"
#include "CryptoNoteCore/CryptoNoteFormatUtils.h"
#include "CryptoNoteConfig.h"
#include <QApplication>
#include <QDesktopServices>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPushButton>
#include <QSaveFile>
#include <QTimer>
#include <iostream>

// Synthetic, unfunded, process-local identity. This test program does not open
// a wallet file, register a URI handler or access an existing wallet process.
class HandoffSink : public QObject {
  Q_OBJECT
public:
  QString destination;
  bool captured = false;
public slots:
  void capture(const QUrl& url) {
    QSaveFile file(destination);
    if (!file.open(QIODevice::WriteOnly)) return;
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    file.write(QJsonDocument(QJsonObject{{"url", url.toString()}}).toJson(QJsonDocument::Compact));
    captured = file.commit();
  }
};
int main(int argc, char** argv) {
  QApplication app(argc, argv);
  if (argc < 3) return 2;
  auto seed = CryptoNote::generatePqSeedMaster();
  auto keys = CryptoNote::derivePqWalletKeys(seed);
  const QString address = QString::fromStdString(CryptoNote::encodePqAddress(CryptoNote::pqWalletAddress(keys, CryptoNote::parameters::CRYPTONOTE_PUBLIC_ADDRESS_BASE58_PREFIX)));
  HandoffSink sink; sink.destination = QString::fromLocal8Bit(argv[1]);
  QDesktopServices::setUrlHandler("http", &sink, "capture");
  WalletGui::WebsiteLoginDialog dialog([&] { return address; }, [&](const QString& message) {
    return QString::fromStdString(CryptoNote::signMessagePq(message.toStdString(), keys.spendSk));
  }, QString::fromLatin1(argv[2]));
  dialog.setWindowTitle("Synthetic wallet — website login qualification");
  const int delay = argc > 3 ? 20000 : 20;
  QTimer::singleShot(delay, &dialog, [&] { dialog.findChild<QPushButton*>("confirm-website-login")->click(); });
  QTimer::singleShot(delay + 20000, &dialog, &QDialog::reject);
  const int result = dialog.exec();
  QDesktopServices::unsetUrlHandler("http");
  if (result != QDialog::Accepted || !sink.captured) { std::cerr << "FAIL: real-crypto GUI approval\n"; return 1; }
  std::cout << "PASS: production GUI component signed with real Core ML-DSA and produced a browser handoff\n";
  return 0;
}
#include "WebsiteLoginIntegration.moc"
