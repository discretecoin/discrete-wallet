// Copyright (c) 2011-2015 The Cryptonote developers
// Copyright (c) 2016-2026 The Karbowanec developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <QApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QLocale>
#include <QTranslator>
#include <QLockFile>
#include <QMessageBox>
#include <QProcess>
#include <QRegularExpression>
#include <QSplashScreen>
#include <QSettings>
#include <QStyleFactory>
#include <QPalette>
#include <QColor>

#ifdef KARBO_USE_QLEMENTINE
#include <oclero/qlementine.hpp>

// Subclass to fix tooltip colors (Qlementine draws tooltips directly, ignoring palette)
class KarboStyle : public oclero::qlementine::QlementineStyle {
public:
  using QlementineStyle::QlementineStyle;

  QColor const& toolTipBackgroundColor() const override {
    static const QColor bg(35, 38, 41);
    return bg;
  }
  QColor const& toolTipBorderColor() const override {
    static const QColor border(74, 74, 74);
    return border;
  }
  QColor const& toolTipForegroundColor() const override {
    static const QColor fg(220, 220, 220);
    return fg;
  }

  // Qlementine's theme palette sets ToolTipText to secondaryColorForeground
  // (near-black), assuming a light ToolTipBase — but PE_PanelTipLabel paints
  // the tooltip background dark via toolTipBackgroundColor(), giving dark text
  // on a dark background.
  void polish(QPalette& palette) override {
    QlementineStyle::polish(palette);
    palette.setColor(QPalette::ToolTipBase, QColor(35, 38, 41));
    palette.setColor(QPalette::ToolTipText, QColor(0xF5, 0xF7, 0xF8));
  }

  // The palette override above isn't reliably picked up by the tooltip widget
  // on Windows (Qlementine only special-cases QTipLabel on non-Windows, and the
  // app palette ends up dark-on-dark regardless). Fix the actual tooltip widget
  // directly when the style polishes it. Targeted to QTipLabel, so it doesn't
  // trigger the global re-polish that an app-wide QToolTip stylesheet would
  // (which recurses through Qlementine's combobox filter and overflows the
  // stack on launch).
  void polish(QWidget* w) override {
    QlementineStyle::polish(w);
    if (w && w->inherits("QTipLabel")) {
      QPalette p = w->palette();
      const QColor fg(0xF5, 0xF7, 0xF8);
      p.setColor(QPalette::ToolTipText, fg);
      p.setColor(QPalette::Text, fg);
      p.setColor(QPalette::WindowText, fg);
      p.setColor(QPalette::ToolTipBase, QColor(35, 38, 41));
      w->setPalette(p);
    }
  }
};
#endif

#include "CommandLineParser.h"
#include "CurrencyAdapter.h"
#include "LoggerAdapter.h"
#include "NodeAdapter.h"
#include "Settings.h"
#include "SignalHandler.h"
#include "WalletAdapter.h"
#include "gui/MainWindow.h"
#include "Update.h"
#include "PaymentServer.h"
#include "TranslatorManager.h"
#include "LogFileWatcher.h"

#define DEBUG 1

using namespace WalletGui;

const QRegularExpression LOG_SPLASH_REG_EXP("\\] ");

QSplashScreen* splash(nullptr);

inline void newLogString(const QString& _string) {
  QRegularExpressionMatch match = LOG_SPLASH_REG_EXP.match(_string);
  if (match.hasMatch()) {
    QString message = _string.mid(match.capturedEnd());
    splash->showMessage(message, Qt::AlignLeft | Qt::AlignBottom, Qt::white);
  }
}

int main(int argc, char* argv[]) {
  QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
  QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
  QApplication app(argc, argv);
  app.setApplicationName(CurrencyAdapter::instance().getCurrencyName() + "wallet");
  app.setApplicationVersion(Settings::instance().getVersion());
  app.setQuitOnLastWindowClosed(false);

#ifndef Q_OS_MAC
#ifndef KARBO_USE_QLEMENTINE
  QApplication::setStyle(QStyleFactory::create("Fusion"));
#endif
#endif

  CommandLineParser cmdLineParser(nullptr);
  Settings::instance().setCommandLineParser(&cmdLineParser);
  bool cmdLineParseResult = cmdLineParser.process(app.arguments());
  Settings::instance().load();

  //Translator must be created before the application's widgets.
  TranslatorManager* tmanager = TranslatorManager::instance();
  Q_UNUSED(tmanager)

  setlocale(LC_ALL, "");

#ifdef KARBO_USE_QLEMENTINE
  auto* style = new KarboStyle(&app);
  style->setThemeJsonPath(QStringLiteral(":/themes/qlementine-dark.json"));
  QApplication::setStyle(style);
  // QApplication::setStyle() doesn't push the style's palette onto
  // QApplication::palette() by itself — widgets only pick up the theme's
  // colors once Qt lazily polishes each of them individually. Any label
  // whose text is set from static .ui content before that polish pass
  // catches up (e.g. AccountFrame's captions, reparented into a QToolBar)
  // keeps rendering with the pre-theme default palette: dark text on the
  // theme's dark background. Set it explicitly, immediately.
  // Qlementine paints the tooltip background via KarboStyle's colour overrides
  // (above), but the tooltip *text* is drawn by the QLabel using the palette's
  // ToolTipText role, which was dark-on-dark. Fix it in the palette rather than
  // with an app-wide QToolTip stylesheet: setting a global stylesheet forces a
  // re-polish of every widget, and Qlementine's combobox filter recurses during
  // that re-polish (QComboBox::view() rebuild ↔ event filter), overflowing the
  // stack on launch.
  QPalette pal = style->standardPalette();
  pal.setColor(QPalette::ToolTipBase, QColor(0x23, 0x26, 0x29));
  pal.setColor(QPalette::ToolTipText, QColor(0xF5, 0xF7, 0xF8));
  QApplication::setPalette(pal);
#endif

  if (PaymentServer::ipcSendCommandLine())
  exit(0);

  PaymentServer* paymentServer = new PaymentServer(&app);

#ifdef Q_OS_WIN
  if(!cmdLineParseResult) {
    QMessageBox::critical(nullptr, QObject::tr("Error"), cmdLineParser.getErrorText());
    return app.exec();
  } else if (cmdLineParser.hasHelpOption()) {
    QMessageBox::information(nullptr, QObject::tr("Help"), cmdLineParser.getHelpText());
    return app.exec();
  }

  //Create registry entries for URL execution
  QSettings discreteKey("HKEY_CLASSES_ROOT\\discrete", QSettings::NativeFormat);
  discreteKey.setValue(".", "Discrete Wallet");
  discreteKey.setValue("URL Protocol", "");
  QSettings discreteOpenKey("HKEY_CLASSES_ROOT\\discrete\\shell\\open\\command", QSettings::NativeFormat);
  discreteOpenKey.setValue(".", "\"" + QCoreApplication::applicationFilePath().replace("/", "\\") + "\" \"%1\"");
#endif

#if defined(Q_OS_LINUX)
  QStringList args;
  QProcess exec;

  //as root
  args << "-c" << "printf '[Desktop Entry]\\nName = Discrete URL Handler\\nGenericName = Discrete\\nComment = Handle URL Sheme discrete://\\nExec = " + QCoreApplication::applicationFilePath() + " %%u\\nTerminal = false\\nType = Application\\nMimeType = x-scheme-handler/discrete;\\nIcon = Discrete-Wallet' | tee /usr/share/applications/discrete-handler.desktop";
  exec.start("/bin/sh", args);
  exec.waitForFinished();

  args.clear();
  args << "-c" << "update-desktop-database";
  exec.start("/bin/sh", args);
  exec.waitForFinished();
#endif

  LoggerAdapter::instance().init();

  QString dataDirPath = Settings::instance().getDataDir().absolutePath();

  if (!QDir().exists(dataDirPath)) {
    QDir().mkpath(dataDirPath);
  }

  QLockFile lockFile(Settings::instance().getDataDir().absoluteFilePath(QApplication::applicationName() + ".lock"));
  if (!lockFile.tryLock()) {
    QMessageBox::warning(nullptr, QObject::tr("Fail"), QObject::tr("%1 wallet already running or cannot create lock file %2. Check your permissions.").arg(CurrencyAdapter::instance().getCurrencyDisplayName()).arg(Settings::instance().getDataDir().absoluteFilePath(QApplication::applicationName() + ".lock")));
    return 0;
  }

  SignalHandler::instance().init();
  QObject::connect(&SignalHandler::instance(), &SignalHandler::quitSignal, &app, &QApplication::quit);

  if (splash == nullptr) {
    splash = new QSplashScreen(QPixmap(":images/splash"), Qt::X11BypassWindowManagerHint);
  }

  if (!splash->isVisible()) {
    splash->show();
  }

  splash->showMessage(QObject::tr("Loading blockchain..."), Qt::AlignLeft | Qt::AlignBottom, Qt::white);

  LogFileWatcher* logWatcher(nullptr);
  if (logWatcher == nullptr) {
    logWatcher = new LogFileWatcher(Settings::instance().getDataDir().absoluteFilePath(QCoreApplication::applicationName() + ".log"), &app);
    QObject::connect(logWatcher, &LogFileWatcher::newLogStringSignal, &app, &newLogString);
  }

  app.processEvents();
  qRegisterMetaType<CryptoNote::TransactionId>("CryptoNote::TransactionId");
  qRegisterMetaType<QList<CryptoNote::TransactionOutputInformation>>("QList<CryptoNote::TransactionOutputInformation>");
  qRegisterMetaType<quintptr>("quintptr");
  if (!NodeAdapter::instance().init()) {
    return 0;
  }

  splash->finish(&MainWindow::instance());

  if (logWatcher != nullptr) {
    logWatcher->deleteLater();
    logWatcher = nullptr;
  }

  splash->deleteLater();
  splash = nullptr;

  // Update checks are disabled: DISCRETE_UPDATE_URL/DISCRETE_DOWNLOAD_URL in
  // Update.h point at this repo, but it doesn't publish tagged releases yet.
  // Re-enable once it does.
  // Updater *d = new Updater();
  // d->checkForUpdate();

  MainWindow::instance().show();
  QString lastWallet = Settings::instance().getWalletFile();
  if (!lastWallet.isEmpty()) {
    WalletAdapter::instance().setWalletFile(lastWallet);
    WalletAdapter::instance().open("");
  }

  QTimer::singleShot(1000, paymentServer, SLOT(uiReady()));
  QObject::connect(paymentServer, &PaymentServer::receivedURI, &MainWindow::instance(), &MainWindow::handlePaymentRequest, Qt::QueuedConnection);

  QObject::connect(QApplication::instance(), &QApplication::aboutToQuit, []() {
    MainWindow::instance().quit();
    if (WalletAdapter::instance().isOpen()) {
      WalletAdapter::instance().close();
    }

    NodeAdapter::instance().deinit();
  });

  return app.exec();
}
