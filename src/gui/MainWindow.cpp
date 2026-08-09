// Copyright (c) 2011-2016 The Cryptonote developers
// Copyright (c) 2011-2013 The Bitcoin Core developers
// Copyright (c) 2015-2016 XDN developers
// Copyright (c) 2016-2026 The Karbo developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QCloseEvent>
#include <QDialog>
#include <QFileDialog>
#include <QStandardPaths>
#include <QInputDialog>
#include <QMessageBox>
#include <QSystemTrayIcon>
#include <QDesktopServices>
#include <QTimer>
#include <QLocale>
#include <QDateTime>
#include <QTranslator>
#include <QToolButton>
#include <QPushButton>
#include <QFontDatabase>
#include <QIcon>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QHBoxLayout>

#include "MainWindow.h"

#include "Common/Base58.h"
#include "Common/StringTools.h"
#include "Common/Util.h"
#include "CryptoNoteCore/CryptoNoteTools.h"
#include "AboutDialog.h"
#include "AnimatedLabel.h"
#include "AddressBookModel.h"
#include "ChangePasswordDialog.h"
#include "ConnectionSettings.h"
#include "WalletRpcSettings.h"
#include "PrivateKeysDialog.h"
#include "ImportKeyDialog.h"
#include "ExportTrackingKeyDialog.h"
#include "ImportTrackingKeyDialog.h"
#include "RestoreFromMnemonicSeedDialog.h"
#include "SignMessageDialog.h"
#include "CurrencyAdapter.h"
#include "ExitWidget.h"
#include "NewPasswordDialog.h"
#include "NodeAdapter.h"
#include "PasswordDialog.h"
#include "Settings.h"
#include "WalletAdapter.h"
#include "WalletEvents.h"
#include "SendFrame.h"
#include "InfoDialog.h"
#include "ui_mainwindow.h"
#include "MnemonicSeedDialog.h"
#include "ConfirmSendDialog.h"
#include "TranslatorManager.h"

#ifdef Q_OS_MAC
#include "macdockiconhandler.h"
#endif

namespace WalletGui {

MainWindow* MainWindow::m_instance = nullptr;

MainWindow& MainWindow::instance() {
  if (m_instance == nullptr) {
    m_instance = new MainWindow;
  }

  return *m_instance;
}

MainWindow::MainWindow() : QMainWindow(),
  m_ui(new Ui::MainWindow), m_trayIcon(nullptr), m_tabActionGroup(new QActionGroup(this)), m_isAboutToQuit(false), paymentServer(0),
  maxRecentFiles(10), trayIconMenu(0), toggleHideAction(0), maxProgressBar(100), m_statusBarText("") {
  m_ui->setupUi(this);
  m_connectionStateIconLabel = new QPushButton();
  m_connectionStateIconLabel->setFlat(true); // Make the button look like a label, but clickable
  m_connectionStateIconLabel->setAutoDefault(false);
  m_connectionStateIconLabel->setDefault(false);
  m_connectionStateIconLabel->setFocusPolicy(Qt::NoFocus);
  m_connectionStateIconLabel->setMaximumSize(20, 20);
  m_encryptionStateIconLabel = new QLabel(this);
  m_trackingModeIconLabel = new QLabel(this);
  m_remoteModeIconLabel = new QLabel(this);
  m_finalityWarningLabel = new QLabel(this);
  m_syncProgressBar = new QProgressBar();
  m_syncStatusLabel = new QLabel();
  m_synchronizationStateIconLabel = new AnimatedLabel(this);
  connectToSignals();
  createLanguageMenu();
  initUi();
  walletClosed();
}

MainWindow::~MainWindow() {
    delete paymentServer;
    paymentServer = 0;
    //if(m_trayIcon) // Hide tray icon, as deleting will let it linger until quit (on Ubuntu)
    //  m_trayIcon->hide();
    #ifdef Q_OS_MAC
      MacDockIconHandler::cleanup();
    #endif
}

void MainWindow::connectToSignals() {
  connect(&WalletAdapter::instance(), &WalletAdapter::openWalletWithPasswordSignal, this, &MainWindow::askForWalletPassword, Qt::QueuedConnection);
  connect(&WalletAdapter::instance(), &WalletAdapter::changeWalletPasswordSignal, this, &MainWindow::encryptWallet, Qt::QueuedConnection);
  connect(&WalletAdapter::instance(), &WalletAdapter::walletSynchronizationProgressUpdatedSignal,
    this, &MainWindow::walletSynchronizationInProgress, Qt::QueuedConnection);
  connect(&WalletAdapter::instance(), &WalletAdapter::walletSynchronizationCompletedSignal, this, &MainWindow::walletSynchronized
    , Qt::QueuedConnection);
  connect(&WalletAdapter::instance(), &WalletAdapter::walletStateChangedSignal, this, &MainWindow::setStatusBarText);
  connect(&WalletAdapter::instance(), &WalletAdapter::walletInitCompletedSignal, this, &MainWindow::walletOpened);
  connect(&WalletAdapter::instance(), &WalletAdapter::walletCloseCompletedSignal, this, &MainWindow::walletClosed);
  connect(&WalletAdapter::instance(), &WalletAdapter::walletTransactionCreatedSignal, this, [this]() {
      QApplication::alert(this);
  });
  connect(&WalletAdapter::instance(), &WalletAdapter::walletSendTransactionCompletedSignal, this, [this](CryptoNote::TransactionId _transactionId, int _error, const QString& _errorString) {
    if (_error == 0) {
      m_ui->m_transactionsAction->setChecked(true);
    }
  });
  connect(&NodeAdapter::instance(), &NodeAdapter::peerCountUpdatedSignal, this, &MainWindow::peerCountUpdated, Qt::QueuedConnection);
  connect(&NodeAdapter::instance(), &NodeAdapter::finalityForkStateChangedSignal, this, &MainWindow::finalityForkStateChanged, Qt::QueuedConnection);
  connect(m_ui->m_exitAction, &QAction::triggered, qApp, &QApplication::quit);
  connect(m_ui->m_yubiKeyProtectionAction, &QAction::triggered,
          this, &MainWindow::enableYubiKeyProtection);
  connect(m_ui->m_sendFrame, &SendFrame::uriOpenSignal, this, &MainWindow::onUriOpenSignal, Qt::QueuedConnection);
  connect(m_ui->m_noWalletFrame, &NoWalletFrame::createWalletClickedSignal, this, &MainWindow::createWallet, Qt::QueuedConnection);
  connect(m_ui->m_noWalletFrame, &NoWalletFrame::openWalletClickedSignal, this, &MainWindow::openWallet, Qt::QueuedConnection);
  connect(m_ui->m_addressBookFrame, &AddressBookFrame::payToSignal, this, &MainWindow::payTo);
  connect(m_connectionStateIconLabel, SIGNAL(clicked()), this, SLOT(showStatusInfo()));
}

void MainWindow::setMainWindowTitle() {
  setWindowTitle(QString(tr("Discrete Wallet %1")).arg(Settings::instance().getVersion()));
}

void MainWindow::initUi() {
  setMainWindowTitle();
#ifdef Q_OS_WIN32
  createTrayIcon();
#endif

  m_ui->m_aboutCryptonoteAction->setText(QString(tr("About %1 Wallet")).arg(CurrencyAdapter::instance().getCurrencyDisplayName()));

  m_tabActionGroup->addAction(m_ui->m_transactionsAction);
  m_tabActionGroup->addAction(m_ui->m_sendAction);
  m_tabActionGroup->addAction(m_ui->m_receiveAction);
  m_tabActionGroup->addAction(m_ui->m_addressBookAction);
  m_tabActionGroup->addAction(m_ui->m_miningAction);

  // Assemble the left sidebar (account card + vertical nav)
  // and the content area, replacing the former top toolbars.
  buildSidebar();

  m_ui->m_sendFrame->hide();
  m_ui->m_accountFrame->setVisible(false);
  m_ui->m_receiveFrame->hide();
  m_ui->m_transactionsFrame->hide();
  m_ui->m_addressBookFrame->hide();
  m_ui->m_miningFrame->hide();

  m_syncProgressBar->setMaximum(maxProgressBar);
  m_syncProgressBar->setMinimum(0);
  m_syncProgressBar->setValue(0);
  m_syncProgressBar->setTextVisible(false);
  m_syncProgressBar->setMaximumHeight(30);
  m_syncProgressBar->setFixedWidth(180);
  m_syncProgressBar->hide();

  m_syncStatusLabel->hide();

  m_synchronizationStateIconLabel->setFixedSize(20, 20);
  m_synchronizationStateIconLabel->setScaledContents( true );
  m_connectionStateIconLabel->setFixedSize(20, 20);
  m_encryptionStateIconLabel->setFixedSize(20, 20);
  m_encryptionStateIconLabel->setScaledContents( true );
  m_trackingModeIconLabel->setFixedSize(20, 20);
  m_trackingModeIconLabel->setScaledContents( true );
  m_remoteModeIconLabel->setFixedSize(20, 20);
  m_remoteModeIconLabel->setScaledContents( true );

  m_ui->m_transactionsAction->toggle();
  encryptedFlagChanged(false);

  // Render SVG status icons via QIcon at the target size (sharp) instead of
  // QPixmap(svg).scaled() (which rasterizes the SVG at its tiny default size
  // then upscales, producing blur).
  qobject_cast<AnimatedLabel*>(m_synchronizationStateIconLabel)->setSprite(QPixmap(":icons/sync_sprite"), QSize(16, 16), 5, 24);
  m_connectionStateIconLabel->setIcon(QIcon(":icons/disconnected"));
  m_connectionStateIconLabel->setIconSize(QSize(18, 18));
  m_trackingModeIconLabel->setPixmap(QIcon(":icons/tracking").pixmap(96, 96));
  m_remoteModeIconLabel->hide();
  m_trackingModeIconLabel->hide();
  m_trackingModeIconLabel->setToolTip(tr("Tracking wallet. Spending unavailable"));
  m_remoteModeIconLabel->setToolTip(tr("Wallet is connected through a remote node."));

  // First-seen finality: amber (not red) status-bar hint, shown only while the
  // node reports it ignored a deeper competing chain. Kept low-key on purpose.
  m_finalityWarningLabel->setText(QChar(0x26A0) + tr(" side chain "));  // U+26A0 warning sign
  m_finalityWarningLabel->setStyleSheet("color: #d18b00;");
  m_finalityWarningLabel->hide();

  m_syncStatusLabel->setMinimumWidth(0);
  m_syncStatusLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  statusBar()->setSizeGripEnabled(false);
  statusBar()->addWidget(m_syncStatusLabel, 1);
  statusBar()->addPermanentWidget(m_syncProgressBar);
  statusBar()->addPermanentWidget(m_synchronizationStateIconLabel);
  statusBar()->addPermanentWidget(m_connectionStateIconLabel);
  statusBar()->addPermanentWidget(m_encryptionStateIconLabel);
  statusBar()->addPermanentWidget(m_trackingModeIconLabel);
  statusBar()->addPermanentWidget(m_remoteModeIconLabel);
  statusBar()->addPermanentWidget(m_finalityWarningLabel);
  statusBar()->show();

  QString connection = Settings::instance().getConnection();
  if(connection.compare("remote") == 0) {
    m_remoteModeIconLabel->show();
    m_remoteModeIconLabel->setPixmap(QIcon(":icons/remote_mode").pixmap(96, 96));
  }

  m_ui->m_showMnemonicSeedAction->setEnabled(false);

  m_ui->m_miningOnLaunchAction->setChecked(Settings::instance().isMiningOnLaunchEnabled());
  m_ui->m_startOnLoginAction->setChecked(Settings::instance().isStartOnLoginEnabled());
  m_ui->m_hideEverythingOnLocked->setChecked(Settings::instance().hideEverythingOnLocked());
  m_ui->m_lockWalletAction->setEnabled(false);

  m_ui->menuRecent_wallets->setVisible(false);
  QAction* recentWalletAction = 0;
  for(int i = 0; i < maxRecentFiles; ++i){
    recentWalletAction = new QAction(this);
    recentWalletAction->setVisible(false);
    QObject::connect(recentWalletAction, SIGNAL(triggered()), this, SLOT(openRecent()));
    recentFileActionList.append(recentWalletAction);
  }
  for(int i = 0; i < maxRecentFiles; ++i)
     m_ui->menuRecent_wallets->addAction(recentFileActionList.at(i));
  updateRecentActionList();

#ifdef Q_OS_MAC
  installDockHandler();
#endif

#ifdef Q_OS_WIN
  m_ui->m_minimizeToTrayAction->setVisible(true);
  m_ui->m_closeToTrayAction->setVisible(true);
  m_ui->m_minimizeToTrayAction->setChecked(Settings::instance().isMinimizeToTrayEnabled());
  m_ui->m_closeToTrayAction->setChecked(Settings::instance().isCloseToTrayEnabled());
  toggleHideAction = new QAction(tr("&Show / Hide"), this);
  toggleHideAction->setStatusTip(tr("Show or hide the main window"));
  connect(toggleHideAction, SIGNAL(triggered()), this, SLOT(toggleHidden()));
#endif

#ifndef Q_OS_WIN
  m_ui->m_minimizeToTrayAction->deleteLater();
  m_ui->m_closeToTrayAction->deleteLater();
#endif

  createTrayIconMenu();
}

void MainWindow::scrollToTransaction(const QModelIndex& _index) {
  m_ui->m_transactionsAction->setChecked(true);
  m_ui->m_transactionsFrame->scrollToTransaction(_index);
}

void MainWindow::quit() {
  if (!m_isAboutToQuit) {
    ExitWidget* exitWidget = new ExitWidget(nullptr);
    exitWidget->show();
    m_isAboutToQuit = true;
    m_ui->m_miningFrame->stopMiningForShutdown();
    if(m_trayIcon) // Hide tray icon, as deleting will let it linger until quit (on Ubuntu)
       m_trayIcon->hide();
    close();
  }
}

#ifdef Q_OS_MAC
void MainWindow::restoreFromDock() {
  if (m_isAboutToQuit) {
    return;
  }

  showNormal();
}
#endif

void MainWindow::closeEvent(QCloseEvent* _event) {
#ifdef Q_OS_WIN
  if (m_isAboutToQuit) {
    QMainWindow::closeEvent(_event);
    return;
  } else if (Settings::instance().isCloseToTrayEnabled()) {
    minimizeToTray(true);
    _event->ignore();
  } else {
    QApplication::quit();
    return;
  }
#elif defined(Q_OS_LINUX) || defined(Q_OS_FREEBSD)
  if (!m_isAboutToQuit) {
    QApplication::quit();
    return;
  }
#endif
  QMainWindow::closeEvent(_event);

}

void MainWindow::changeEvent(QEvent* _event) {
#ifdef Q_OS_WIN
  if (!QSystemTrayIcon::isSystemTrayAvailable()) {
    QMainWindow::changeEvent(_event);
    return;
  }
#endif
  switch (_event->type()) {
  // this event is send if a translator is loaded
  case QEvent::LanguageChange:
  {
    //m_ui->retranslateUi(this);
    setMainWindowTitle();
    break;
  }
  // this event is send, if the system language changes
  case QEvent::LocaleChange:
  {
    QString locale = QLocale::system().name();
    locale.truncate(locale.lastIndexOf('_'));
    loadLanguage(locale);
  }
  case QEvent::WindowStateChange:
  {
#ifdef Q_OS_WIN
    if(Settings::instance().isMinimizeToTrayEnabled()) {
      minimizeToTray(isMinimized());
    }
    break;
#endif
  }
  default:
    break;
  }
  QWidget::changeEvent(_event);
  QMainWindow::changeEvent(_event);
}

void MainWindow::buildSidebar() {
  QWidget* central = m_ui->centralwidget;

  // Content area: every view frame stacked in one column. Exactly one is ever
  // visible at a time (driven by the tab actions / wallet state), so the
  // visible one fills the area and the hidden ones take no space.
  QWidget* content = new QWidget(central);
  content->setObjectName(QStringLiteral("m_content"));
  QVBoxLayout* contentLayout = new QVBoxLayout(content);
  contentLayout->setContentsMargins(0, 0, 0, 0);
  contentLayout->setSpacing(0);
  contentLayout->addWidget(m_ui->m_transactionsFrame);
  contentLayout->addWidget(m_ui->m_sendFrame);
  contentLayout->addWidget(m_ui->m_receiveFrame);
  contentLayout->addWidget(m_ui->m_addressBookFrame);
  contentLayout->addWidget(m_ui->m_miningFrame);
  contentLayout->addWidget(m_ui->m_noWalletFrame);

  // Sidebar column.
  m_sidebar = new QWidget(central);
  m_sidebar->setObjectName(QStringLiteral("m_sidebar"));
  m_sidebar->setFixedWidth(250);
  QVBoxLayout* side = new QVBoxLayout(m_sidebar);
  side->setContentsMargins(12, 12, 12, 12);
  side->setSpacing(10);

  side->addWidget(m_ui->m_accountFrame);

  // Vertical nav — QToolButtons bound to the existing (exclusive) tab actions,
  // so they inherit the check state and toggled->setVisible wiring already
  // connected in the .ui.
  QAction* navActions[] = { m_ui->m_transactionsAction, m_ui->m_sendAction,
                            m_ui->m_receiveAction, m_ui->m_addressBookAction,
                            m_ui->m_miningAction };
  QVBoxLayout* nav = new QVBoxLayout();
  nav->setContentsMargins(0, 4, 0, 0);
  nav->setSpacing(2);
  for (QAction* action : navActions) {
    // QToolButton has no icon-text gap property, so pad the label with leading
    // spaces. iconText is what the button shows; menus/tray use text(), so they
    // are unaffected.
    action->setIconText(QStringLiteral("   ") + action->text());
    QToolButton* button = new QToolButton(m_sidebar);
    button->setProperty("sidebarNavigation", true);
    button->setDefaultAction(action);
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    button->setIconSize(QSize(20, 20));
    button->setCursor(Qt::PointingHandCursor);
    nav->addWidget(button);
    m_navButtons.append(button);
  }
  side->addLayout(nav);

  side->addStretch(1);

  // Replace the central widget's old grid with [ sidebar | content ]. The
  // account + view frames were reparented out above, so the grid is now empty.
  delete central->layout();
  QHBoxLayout* root = new QHBoxLayout(central);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);
  root->addWidget(m_sidebar);
  root->addWidget(content, 1);

  m_sidebar->setStyleSheet(
    "#m_sidebar { background-color:#10171B; border-right:1px solid #1c262c; }"
    "#m_sidebar QToolButton[sidebarNavigation=\"true\"] { color:#c3cbd1; border:none; border-radius:8px;"
    "  padding:9px 12px; text-align:left; background:transparent; font-size:14px; }"
    "#m_sidebar QToolButton[sidebarNavigation=\"true\"]:hover { background:#17212a; }"
    "#m_sidebar QToolButton[sidebarNavigation=\"true\"]:checked { color:#5FE29F; background:#17251f; }");
}

bool MainWindow::event(QEvent* _event) {
  switch (static_cast<WalletEventType>(_event->type())) {
    case WalletEventType::ShowMessage:
    {
      showMessage(static_cast<ShowMessageEvent*>(_event)->messageText(), static_cast<ShowMessageEvent*>(_event)->messageType());
      return true;
    }
  }

  return QMainWindow::event(_event);
}

void MainWindow::createWallet() {
  QString filePath = QFileDialog::getSaveFileName(this, tr("New wallet file"),
  #ifdef Q_OS_WIN
      //QApplication::applicationDirPath(),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),

  #else
      QDir::homePath(),
  #endif
      tr("Wallets (*.wallet)")
      );

    if (!filePath.isEmpty() && !filePath.endsWith(".wallet")) {
      filePath.append(".wallet");
    }

    if (QFile::exists(filePath)) {
        QFile::remove(filePath);
    }

    if (!filePath.isEmpty()) {
      if (WalletAdapter::instance().isOpen()) {
        closeWallet();
      }

      WalletAdapter::instance().setWalletFile(filePath);
      WalletAdapter::instance().createWallet();
    }
}

void MainWindow::openWallet() {
  QString walletDirectory = "";
  QString lastWalletDir = QFileInfo(Settings::instance().getWalletFile()).absolutePath();
  if (!lastWalletDir.isEmpty()) {
    walletDirectory = lastWalletDir;
  } else {
#ifdef Q_OS_WIN
    walletDirectory = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
#else
    walletDirectory = QDir::homePath();
#endif
  }
  QString filePath = QFileDialog::getOpenFileName(this, tr("Open .wallet/.keys file"),
    walletDirectory,
    tr("Wallet (*.wallet *.keys)"));

  if (!filePath.isEmpty()) {

    if (QFile::exists(filePath) && (!filePath.endsWith(".keys") && !filePath.endsWith(".wallet") && !filePath.endsWith(".trackingwallet"))) {
      QMessageBox::warning(this, tr("Wrong wallet file extension"),
                                 tr("Wrong wallet file extension, wallet file should have \".wallet\", \".keys\" or \".trackingwallet\" extension."), QMessageBox::Ok);
      return;
    }

    if (WalletAdapter::instance().isOpen()) {
      closeWallet();
    }

    WalletAdapter::instance().setWalletFile(filePath);
    WalletAdapter::instance().open("");
  }
}

void MainWindow::openRecent(){
  QAction *action = qobject_cast<QAction *>(sender());
  if (action) {
    QString filePath = action->data().toString();
    if (!filePath.isEmpty() && QFile::exists(filePath)) {
      if (WalletAdapter::instance().isOpen()) {
          closeWallet();
      }
      WalletAdapter::instance().setWalletFile(filePath);
      WalletAdapter::instance().open("");
    } else {
       QMessageBox::warning(this, tr("Recent wallet file not found"), tr("The recent wallet file is missing. Probably it was removed."), QMessageBox::Ok);
    }
  }
}

void MainWindow::importKey() {
  ImportKeyDialog dlg(this);
  if (dlg.exec() == QDialog::Accepted) {
    QString filePath = dlg.getFilePath();
    if (filePath.isEmpty()) {
      return;
    }

    if (!filePath.endsWith(".wallet")) {
      filePath.append(".wallet");
    }

    CryptoNote::AccountKeys keys = dlg.getAccountKeys();

    if (WalletAdapter::instance().isOpen()) {
      closeWallet();
    }
    WalletAdapter::instance().setWalletFile(filePath);

    quint32 syncHeight = dlg.getSyncHeight();
    if (syncHeight != 0) {
      WalletAdapter::instance().createWithKeys(keys, syncHeight);
    } else {
      WalletAdapter::instance().createWithKeys(keys);
    }
  }
}

void MainWindow::importTrackingKey() {
  ImportTrackingKeyDialog dlg(this);
  if (dlg.exec() == QDialog::Accepted) {
    QString keyString = dlg.getKeyString().trimmed();
    QString filePath = dlg.getFilePath();
    if (keyString.isEmpty() || filePath.isEmpty()) {
      return;
    }

    if (!filePath.endsWith(".wallet")) {
      filePath.append(".wallet");
    }

    CryptoNote::AccountKeys keys = dlg.getAccountKeys();
    CryptoNote::PqTrackingKeys trackingKeys = dlg.getTrackingKeys();

    if (WalletAdapter::instance().isOpen()) {
      closeWallet();
    }
    Settings::instance().setTrackingMode(true);
    WalletAdapter::instance().setWalletFile(filePath);

    quint32 syncHeight = dlg.getSyncHeight();
    if (syncHeight != 0) {
      WalletAdapter::instance().createTrackingWallet(keys, trackingKeys, syncHeight);
    } else {
      WalletAdapter::instance().createTrackingWallet(keys, trackingKeys);
    }
  }
}

void MainWindow::isTrackingMode() {
  m_ui->m_sendFrame->hide();
  m_ui->m_transactionsAction->trigger();
  m_ui->m_sendAction->setEnabled(false);
  m_ui->m_openUriAction->setEnabled(false);
  m_ui->m_showMnemonicSeedAction->setEnabled(false);
  m_trackingModeIconLabel->show();
}

void MainWindow::restoreFromMnemonicSeed() {
  RestoreFromMnemonicSeedDialog dlg(this);
  if (dlg.exec() == QDialog::Accepted) {
    QString filePath = dlg.getFilePath();
    if (filePath.isEmpty()) {
      return;
    }

    if (!filePath.endsWith(".wallet")) {
      filePath.append(".wallet");
    }

    CryptoNote::AccountKeys keys = dlg.getAccountKeys();

    if (WalletAdapter::instance().isOpen()) {
      closeWallet();
    }
    WalletAdapter::instance().setWalletFile(filePath);

    quint32 syncHeight = dlg.getSyncHeight();
    if (syncHeight != 0) {
      WalletAdapter::instance().createWithKeys(keys, syncHeight);
    } else {
      WalletAdapter::instance().createWithKeys(keys);
    }
  }
}

void MainWindow::createLanguageMenu(void)
{
  QActionGroup* langGroup = new QActionGroup(m_ui->menuLanguage);
  langGroup->setExclusive(true);
  connect(langGroup, SIGNAL(triggered(QAction*)), this, SLOT(slotLanguageChanged(QAction*)));

  QString defaultLocale = Settings::instance().getLanguage();
  if (defaultLocale.isEmpty()){
    defaultLocale = QLocale::system().name().section('_', 0, 0);
  }

  // Get the path that we already detected in TranslatorManager
  m_langPath = TranslatorManager::instance()->getLangPath();
  // -----------------------

  QDir dir(m_langPath);

  // Use *.qm to ensure 'ua.qm' is caught even if the filter is picky
  QStringList fileNames = dir.entryList(QStringList("*.qm"), QDir::Files);

  for (int i = 0; i < fileNames.size(); ++i) {
    QString file = fileNames[i];
    QString locale = QFileInfo(file).baseName(); // Gets "ua" from "ua.qm"

    // Convert "ua" or "uk" to a readable name like "Українська"
    QString lang = QLocale(locale).nativeLanguageName();

    // Fallback if QLocale doesn't recognize "ua"
    if (lang.isEmpty()) lang = locale;

    QAction *action = new QAction(lang, this);
    action->setCheckable(true);
    action->setData(locale);
    m_ui->menuLanguage->addAction(action);
    langGroup->addAction(action);

    if (defaultLocale == locale) {
      action->setChecked(true);
    }
  }
}

void MainWindow::slotLanguageChanged(QAction* action)
{
    if (!action)
        return;

    QString lang = action->data().toString();
    TranslatorManager::instance()->switchLanguage(lang);
    loadLanguage(lang);
}

void MainWindow::loadLanguage(const QString& rLanguage)
{
    QLocale locale(rLanguage);
    QString languageName = QLocale::languageToString(locale.language());

    setStatusBarText(tr("Language changed to %1").arg(languageName));
    setStatusBarText(QString(tr("Language changed to %1").arg(languageName)));
    QMessageBox::information(this, tr("Language was changed"),
                             tr("Language changed to %1. The change will take effect after restarting the wallet.").arg(languageName), QMessageBox::Ok);
}

void MainWindow::DisplayCmdLineHelp() {
    CommandLineParser cmdLineParser(nullptr);
//  QMessageBox::information(nullptr, QObject::tr("Help"), cmdLineParser.getHelpText());
    QMessageBox *msg = new QMessageBox(QMessageBox::Information, QObject::tr("Help"),
                       cmdLineParser.getHelpText(),
                       QMessageBox::Ok, this);
    msg->setInformativeText(tr("More info can be found at discrete.cash in Documentation section"));
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    msg->setFont(font);
    QSpacerItem* horizontalSpacer = new QSpacerItem(650, 0, QSizePolicy::Minimum, QSizePolicy::Expanding);
        QGridLayout* layout = (QGridLayout*)msg->layout();
        layout->addItem(horizontalSpacer, layout->rowCount(), 0, 1, layout->columnCount());
    msg->exec();
}

void MainWindow::openConnectionSettings() {
  ConnectionSettingsDialog dlg(&MainWindow::instance());
  dlg.initConnectionSettings();
  if (dlg.exec() == QDialog::Accepted) {
    QString connection = dlg.getConnectionMode();
    Settings::instance().setConnection(connection);

    if (connection.compare("remote") == 0) {
      NodeSetting remoteNode = dlg.getRemoteNode();
      Settings::instance().setCurrentRemoteNode(remoteNode);
    }

    quint16 daemonPort = dlg.getLocalDaemonPort();
    Settings::instance().setCurrentLocalDaemonPort(daemonPort);

    quint16 connCount = dlg.getConnectionsCount();
    Settings::instance().setConnectionsCount(connCount);

    QMessageBox::information(this, tr("Connection settings changed"), tr("Connection mode will be changed after restarting the wallet."), QMessageBox::Ok);
  }
}

void MainWindow::openWalletRpcSettings() {
  WalletRpcSettingsDialog dlg(&MainWindow::instance());
  if (dlg.exec() == QDialog::Accepted) {
    QMessageBox::information(this, tr("Wallet RPC settings changed"), tr("Changes will take effect when you restart the wallet."), QMessageBox::Ok);
  }
}

void MainWindow::showStatusInfo() {
  InfoDialog dlg(this);
  dlg.exec();
}

void MainWindow::backupWallet() {
  QString filePath = QFileDialog::getSaveFileName(this, tr("Backup wallet to..."),
  #ifdef Q_OS_WIN
      //QApplication::applicationDirPath(),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
  #else
      QDir::homePath(),
  #endif
      tr("Wallets (*.wallet)")
      );
    if (!filePath.isEmpty() && !filePath.endsWith(".wallet")) {
      filePath.append(".wallet");
    }

    if (!filePath.isEmpty() && !QFile::exists(filePath)) {
      WalletAdapter::instance().backup(filePath);
    }
}

void MainWindow::resetWallet() {
  Q_ASSERT(WalletAdapter::instance().isOpen());
  if (QMessageBox::warning(this, tr("Warning"), tr("Your wallet will be reset and restored from blockchain.\n"
    "Are you sure?"), QMessageBox::Ok, QMessageBox::Cancel) == QMessageBox::Ok) {
    WalletAdapter::instance().reset();
    WalletAdapter::instance().open("");
  }
}

void MainWindow::openLogFile() {
  QString pathLog = Settings::instance().getDataDir().absoluteFilePath(QApplication::applicationName() + ".log");
  if (!pathLog.isEmpty()) {
    QDesktopServices::openUrl(QUrl::fromLocalFile(pathLog));
  }
}

void MainWindow::showPrivateKeys() {
  if (!confirmWithPassword()) {
    return;
  }

  PrivateKeysDialog dlg(this);
  dlg.walletOpened();
  dlg.exec();
}

void MainWindow::showMnemonicSeed() {
  if (!confirmWithPassword()) {
    return;
  }

  MnemonicSeedDialog dlg(this);
  // Start WebAuthn only after exec() has made this dialog visible and modal.
  // Windows can then keep its security UI in front of the correct owner.
  QTimer::singleShot(0, &dlg, &MnemonicSeedDialog::walletOpened);
  dlg.exec();
}

void MainWindow::exportTrackingKey() {
  if (!confirmWithPassword()) {
    return;
  }

  ExportTrackingKeyDialog dlg(this);
  dlg.walletOpened();
  dlg.exec();
}

void MainWindow::signMessage() {
  if (!confirmWithPassword()) {
    return;
  }

  SignMessageDialog dlg(this);
  dlg.walletOpened();
  dlg.sign();
  dlg.exec();
}

void MainWindow::verifyMessage() {
  SignMessageDialog dlg(this);
  dlg.walletOpened();
  dlg.verify();
  dlg.exec();
}

void MainWindow::handlePaymentRequest(QString _request) {
  if (WalletAdapter::instance().isOpen() && Settings::instance().isTrackingMode()) {
    m_pendingPaymentRequest.clear();
    isTrackingMode();
    showNormal();
    raise();
    activateWindow();
    showMessage(tr("A tracking wallet cannot send payments."), QtWarningMsg);
    return;
  }

  if (!WalletAdapter::instance().isOpen() || !m_ui->m_sendAction->isEnabled()) {
    // A protocol handler can deliver the URI before the last wallet has
    // finished opening. Keep the latest request and apply it once Send is
    // actually available instead of parsing it into a hidden, disabled frame.
    m_pendingPaymentRequest = _request;
    showNormal();
    raise();
    activateWindow();
    return;
  }

  if (QWidget* modalWidget = QApplication::activeModalWidget()) {
    m_pendingPaymentRequest = _request;
    if (QDialog* modalDialog = qobject_cast<QDialog*>(modalWidget)) {
      connect(modalDialog, &QDialog::finished, this, [this](int) {
        if (m_pendingPaymentRequest.isEmpty()) {
          return;
        }

        const QString pendingPaymentRequest = m_pendingPaymentRequest;
        m_pendingPaymentRequest.clear();
        QTimer::singleShot(0, this, [this, pendingPaymentRequest]() {
          handlePaymentRequest(pendingPaymentRequest);
        });
      }, Qt::SingleShotConnection);
    }
    return;
  }

  m_pendingPaymentRequest.clear();
  m_ui->m_sendAction->trigger();
  m_ui->m_sendFrame->parsePaymentRequest(_request);
  showNormal();
  raise();
  activateWindow();
}

void MainWindow::onUriOpenSignal() {
  if (Settings::instance().isTrackingMode()) {
      isTrackingMode();
      return;
  }
  m_ui->m_sendAction->trigger();
}

void MainWindow::encryptWallet() {
  if (Settings::instance().isEncrypted()) {
    bool error = false;
    do {
      ChangePasswordDialog dlg(this);
      if (dlg.exec() == QDialog::Rejected) {
        return;
      }

      QString oldPassword = dlg.getOldPassword();
      QString newPassword = dlg.getNewPassword();
      error = !WalletAdapter::instance().changePassword(oldPassword, newPassword);
    } while (error);
  } else {
    NewPasswordDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) {
      QString password = dlg.getPassword();
      if (password.isEmpty()) {
        return;
      }

      encryptedFlagChanged(WalletAdapter::instance().changePassword("", password));
    }
  }
}

void MainWindow::enableYubiKeyProtection() {
#ifndef Q_OS_WIN
  QMessageBox::critical(this, tr("YubiKey protected spending"),
      tr("This feature is currently available only on Windows."));
  return;
#else
  if (m_ui->m_miningFrame->isSoloRunning()) {
    QMessageBox::critical(this, tr("YubiKey protected spending"),
        tr("Stop mining before enabling protection. The miner holds a signing key in memory for its entire session."));
    return;
  }
  if (!confirmWithPassword()) {
    return;
  }

  const QMessageBox::StandardButton decision = QMessageBox::warning(
      this,
      tr("Enable YubiKey protected spending"),
      tr("This converts the current wallet into a tracking-only wallet and encrypts its 32-byte spend seed with a credential stored on a FIDO2 security key.\n\n"
         "You will need the YubiKey, its PIN, and a touch for every spend, seed export, or message signature. Mining and account registration are not supported in this first version because they would keep or reuse spend authority outside the one-operation boundary.\n\n"
         "A full pre-migration wallet backup will be created automatically. Move that backup offline: anyone who obtains it and its wallet password can bypass the YubiKey. Your mnemonic remains the final recovery method.\n\n"
         "Continue?"),
      QMessageBox::Yes | QMessageBox::Cancel,
      QMessageBox::Cancel);
  if (decision != QMessageBox::Yes) {
    return;
  }

  QString backupPath;
  QString error;
  if (!WalletAdapter::instance().enableYubiKeyProtection(winId(), backupPath, error)) {
    QMessageBox::critical(this, tr("YubiKey protected spending"), error);
    return;
  }

  Settings::instance().setTrackingMode(false);
  m_ui->m_yubiKeyProtectionAction->setEnabled(false);
  m_ui->m_yubiKeyProtectionAction->setText(tr("YubiKey protected spending enabled"));
  m_ui->m_miningAction->setEnabled(false);
  m_trackingModeIconLabel->show();
  m_trackingModeIconLabel->setToolTip(tr("YubiKey protected spending. The wallet file contains no spend seed."));
  m_ui->m_showMnemonicSeedAction->setEnabled(true);
  QMessageBox::information(
      this,
      tr("YubiKey protected spending enabled"),
      tr("The spend seed has been removed from the active wallet and the protected wallet save has started.\n\n"
         "Mandatory full-wallet backup:\n%1\n\nMove that backup offline after verifying your mnemonic recovery. Keep the .wallet and .yubikey.json files together when backing up the protected wallet.")
          .arg(backupPath));
#endif
}

void MainWindow::closeWallet() {
  if (WalletAdapter::instance().isOpen()) {
    m_ui->m_miningFrame->stopMiningForShutdown();
    WalletAdapter::instance().close();
  }
}

void MainWindow::updateRecentActionList(){
  QStringList recentFilePaths = Settings::instance().getRecentWalletsList();
  if(recentFilePaths.isEmpty())
    m_ui->menuRecent_wallets->setVisible(false);

  if(recentFilePaths.size() != 0) {
    int itEnd = 0;
    if(recentFilePaths.size() <= maxRecentFiles)
        itEnd = recentFilePaths.size();
    else
        itEnd = maxRecentFiles;

    for (int i = 0; i < itEnd; ++i) {
        QString strippedName = QFileInfo(recentFilePaths.at(i)).absoluteFilePath();
        recentFileActionList.at(i)->setText(strippedName);
        recentFileActionList.at(i)->setData(recentFilePaths.at(i));
        recentFileActionList.at(i)->setVisible(true);
    }
    for (int i = itEnd; i < maxRecentFiles; ++i)
        recentFileActionList.at(i)->setVisible(false);
  } else {
      m_ui->menuRecent_wallets->setVisible(false);
  }
}

void MainWindow::aboutQt() {
  QMessageBox::aboutQt(this);
}

void MainWindow::setStartOnLogin(bool _on) {
  Settings::instance().setStartOnLoginEnabled(_on);
  m_ui->m_startOnLoginAction->setChecked(Settings::instance().isStartOnLoginEnabled());
}

void MainWindow::setMiningOnLaunch(bool _on) {
  Settings::instance().setMiningOnLaunchEnabled(_on);
  m_ui->m_miningOnLaunchAction->setChecked(Settings::instance().isMiningOnLaunchEnabled());
}

void MainWindow::setMinimizeToTray(bool _on) {
#ifdef Q_OS_WIN
  Settings::instance().setMinimizeToTrayEnabled(_on);
  m_ui->m_minimizeToTrayAction->setChecked(Settings::instance().isMinimizeToTrayEnabled());
#endif
}

void MainWindow::setCloseToTray(bool _on) {
#ifdef Q_OS_WIN
  Settings::instance().setCloseToTrayEnabled(_on);
  m_ui->m_closeToTrayAction->setChecked(Settings::instance().isCloseToTrayEnabled());
#endif
}

void MainWindow::hideEverythingOnLocked(bool _on) {
  Settings::instance().setHideEverythingOnLocked(_on);
  m_ui->m_hideEverythingOnLocked->setChecked(Settings::instance().hideEverythingOnLocked());
}

void MainWindow::about() {
  AboutDialog dlg(this);
  dlg.exec();
}

void MainWindow::setStatusBarText(const QString& _text) {
  m_statusBarText = _text;
  m_syncStatusLabel->setText(m_statusBarText);
  m_syncStatusLabel->setVisible(!m_statusBarText.isEmpty());
}

void MainWindow::finalityForkStateChanged(bool _active) {
  // Deliberately low-key: this is usually just a brief connectivity hiccup that
  // left the node on a side chain. No funds are at risk; the out-of-band explorer
  // check is what tells the user whether their node is genuinely behind.
  const QString message = tr(
    "Your node ignored a deeper competing chain - most likely just a brief connectivity "
    "hiccup that left it on a side chain. Nothing to worry about and your funds are safe.\n\n"
    "If your balance looks off, your node may be a little behind: check the current chain on "
    "the official block explorer. A node that stays behind can be returned to the main chain "
    "with the resync recovery step (see the recovery guide).");

  m_finalityWarningLabel->setToolTip(message);
  m_finalityWarningLabel->setVisible(_active);

  if (_active && m_trayIcon != nullptr && QSystemTrayIcon::supportsMessages()) {
    // A gentle, non-modal toast rather than a modal popup — keep it casual.
    m_trayIcon->showMessage(tr("Discrete"), message, QSystemTrayIcon::Information, 12000);
  }
}

void MainWindow::showMessage(const QString& _text, QtMsgType _type) {
  switch (_type) {
  case QtCriticalMsg:
    QMessageBox::critical(this, tr("Wallet error"), _text);
    break;
  case QtDebugMsg:
    QMessageBox::information(this, tr("Wallet"), _text);
    break;
  case QtWarningMsg:
    QMessageBox::warning(this, tr("Wallet warning"), _text);
    break;
  default:
    break;
  }
}

void MainWindow::askForWalletPassword(bool _error) {
  PasswordDialog dlg(_error, this);
  if (dlg.exec() == QDialog::Accepted) {
    QString password = dlg.getPassword();
    WalletAdapter::instance().open(password);
  }
}

void MainWindow::lockWalletWithPassword() {
  bool hide = Settings::instance().hideEverythingOnLocked();

  if (hide) {
    m_ui->m_accountFrame->setVisible(false);
    m_ui->m_receiveFrame->hide();
    m_ui->m_sendFrame->hide();
    m_ui->m_transactionsFrame->hide();
    m_ui->m_addressBookFrame->hide();
  }
  bool keep_asking = true;
  bool wrong_pass = false;
  do {
    PasswordDialog dlg(wrong_pass, this);
    if (dlg.exec() == QDialog::Accepted) {
      QString password = dlg.getPassword();
      keep_asking = !WalletAdapter::instance().tryOpen(password);
      wrong_pass = keep_asking;
    }
    else {
      closeWallet();
      return;
    }
  } while (keep_asking);

  if (hide) {
    m_ui->m_accountFrame->setVisible(true);
    m_ui->m_transactionsAction->trigger();
  }
}

bool MainWindow::confirmWithPassword() {
  if (!Settings::instance().isEncrypted() && WalletAdapter::instance().tryOpen(""))
    return true;

  PasswordDialog dlg(false, this);
  if (dlg.exec() == QDialog::Accepted) {
    QString password = dlg.getPassword();
    if (!WalletAdapter::instance().tryOpen(password)) {
      QMessageBox::critical(nullptr, tr("Incorrect password"), tr("Wrong password."), QMessageBox::Ok);
      return false;
    } else {
      return true;
    }
  }

  return false;
}

void MainWindow::encryptedFlagChanged(bool _encrypted) {
  m_ui->m_encryptWalletAction->setEnabled(!_encrypted);
  m_ui->m_changePasswordAction->setEnabled(_encrypted);
  QString encryptionIconPath = _encrypted ? ":icons/encrypted" : ":icons/decrypted";
  m_encryptionStateIconLabel->setPixmap(QIcon(encryptionIconPath).pixmap(96, 96));
  QString encryptionLabelTooltip = _encrypted ? tr("Encrypted") : tr("Not encrypted");
  m_encryptionStateIconLabel->setToolTip(encryptionLabelTooltip);
  m_ui->m_lockWalletAction->setEnabled(_encrypted);
}

void MainWindow::peerCountUpdated(quint64 _peerCount) {
  QString connectionIconPath = _peerCount > 0 ? ":icons/connected" : ":icons/disconnected";
  m_connectionStateIconLabel->setIcon(QIcon(connectionIconPath));
  m_connectionStateIconLabel->setToolTip(QString(tr("%n active connection(s)", "", _peerCount)));
}

void MainWindow::walletSynchronizationInProgress(uint32_t _current, uint32_t _total) {
  const uint32_t progressActOffset = 500;
  bool progressAct = false;
  uint32_t syncProgress = 0;
  qobject_cast<AnimatedLabel*>(m_synchronizationStateIconLabel)->startAnimation();
  m_synchronizationStateIconLabel->setToolTip(tr("Synchronization in progress"));
  if (_total > 0 && _current <= _total) {
    syncProgress = static_cast<uint32_t>(static_cast<float>(_current) /
                   static_cast<float>(_total) *
                   static_cast<float>(maxProgressBar));
    if (_total > progressActOffset && _total - _current > progressActOffset) progressAct = true;
  } else {
    syncProgress = maxProgressBar;
  }
  if (m_syncProgressBar->isHidden() && progressAct) {
    m_syncProgressBar->show();
    m_syncStatusLabel->show();
  }
  m_syncProgressBar->setValue(syncProgress);
}

void MainWindow::walletSynchronized(int _error, const QString& _error_text) {
  qobject_cast<AnimatedLabel*>(m_synchronizationStateIconLabel)->stopAnimation();
  m_synchronizationStateIconLabel->setPixmap(QIcon(":icons/synced").pixmap(96, 96));
  QString syncLabelTooltip = _error > 0 ? tr("Not synchronized") : tr("Synchronized");
  m_synchronizationStateIconLabel->setToolTip(syncLabelTooltip);
  m_syncProgressBar->hide();
}

void MainWindow::walletOpened(bool _error, const QString& _error_text) {
  if (!_error) {
    const bool yubiKeyProtected = WalletAdapter::instance().isYubiKeyProtected();
    const bool incompleteYubiKeyMigration =
        WalletAdapter::instance().hasYubiKeyMetadata() &&
        !WalletAdapter::instance().isTrackingWallet();
    m_ui->m_noWalletFrame->hide();
    m_ui->m_closeWalletAction->setEnabled(true);
    m_ui->m_exportTrackingKeyAction->setEnabled(true);
    m_encryptionStateIconLabel->show();
    m_synchronizationStateIconLabel->show();
    m_ui->m_backupWalletAction->setEnabled(true);
    m_ui->m_showPrivateKey->setEnabled(true);
    m_ui->m_resetAction->setEnabled(true);
    m_ui->m_openUriAction->setEnabled(true);
    m_ui->m_signMessageAction->setEnabled(true);
    m_ui->m_verifySignedMessageAction->setEnabled(true);
    if (!WalletAdapter::instance().isTrackingWallet() || yubiKeyProtected) {
       m_ui->m_showMnemonicSeedAction->setEnabled(true);
    }
#ifdef Q_OS_WIN
    m_ui->m_yubiKeyProtectionAction->setEnabled(
        !WalletAdapter::instance().isTrackingWallet() &&
        !yubiKeyProtected && !incompleteYubiKeyMigration);
#endif
    m_ui->m_yubiKeyProtectionAction->setText(
        yubiKeyProtected
            ? tr("YubiKey protected spending enabled")
            : incompleteYubiKeyMigration
                ? tr("YubiKey migration incomplete")
                : tr("Enable YubiKey protected spending..."));
    encryptedFlagChanged(Settings::instance().isEncrypted());

    QList<QAction*> tabActions = m_tabActionGroup->actions();
    Q_FOREACH(auto action, tabActions) {
      action->setEnabled(true);
    }
    if (yubiKeyProtected) {
      m_ui->m_miningAction->setEnabled(false);
      m_trackingModeIconLabel->show();
      m_trackingModeIconLabel->setToolTip(
          tr("YubiKey protected spending. The wallet file contains no spend seed."));
    } else if (incompleteYubiKeyMigration) {
      QMessageBox::critical(
          this,
          tr("YubiKey migration incomplete"),
          tr("A YubiKey sidecar exists, but this wallet file still contains its spend seed. "
             "This wallet is NOT YubiKey protected. Restore or preserve the mandatory pre-migration backup, close the wallet, and resolve the orphan .yubikey.json file before retrying."));
    }

    setWindowTitle(QString(tr("%1 - Discrete Wallet %2")).arg(Settings::instance().getWalletFile()).arg(Settings::instance().getVersion()));

    m_ui->m_transactionsAction->trigger();
    m_ui->m_accountFrame->setVisible(true);
    m_ui->m_transactionsFrame->show();

    checkTrackingMode();
    updateRecentActionList();

    if (Settings::instance().isTrackingMode()) {
      isTrackingMode();
    }

    if (!m_pendingPaymentRequest.isEmpty()) {
      const QString pendingPaymentRequest = m_pendingPaymentRequest;
      m_pendingPaymentRequest.clear();
      QTimer::singleShot(0, this, [this, pendingPaymentRequest]() {
        handlePaymentRequest(pendingPaymentRequest);
      });
    }

    WalletAdapter::instance().autoBackup();

  } else {
    walletClosed();
  }
}

void MainWindow::walletClosed() {
  m_ui->m_backupWalletAction->setEnabled(false);
  m_ui->m_encryptWalletAction->setEnabled(false);
  m_ui->m_changePasswordAction->setEnabled(false);
  m_ui->m_yubiKeyProtectionAction->setEnabled(false);
  m_ui->m_closeWalletAction->setEnabled(false);
  m_ui->m_openUriAction->setEnabled(false);
  m_ui->m_exportTrackingKeyAction->setEnabled(false);
  m_ui->m_showPrivateKey->setEnabled(false);
  m_ui->m_resetAction->setEnabled(false);
  m_ui->m_showMnemonicSeedAction->setEnabled(false);
  m_ui->m_signMessageAction->setEnabled(false);
  m_ui->m_verifySignedMessageAction->setEnabled(false);
  m_ui->m_lockWalletAction->setEnabled(false);
  m_ui->m_accountFrame->setVisible(false);
  m_ui->m_receiveFrame->hide();
  m_ui->m_sendFrame->hide();
  m_ui->m_transactionsFrame->hide();
  m_ui->m_addressBookFrame->hide();
  if (!m_ui->m_miningFrame->isSoloRunning()) {
    m_ui->m_noWalletFrame->show();
    m_ui->m_miningFrame->hide();
  } else {
    m_ui->m_miningFrame->show();
  }
  m_encryptionStateIconLabel->hide();
  m_trackingModeIconLabel->hide();
  m_synchronizationStateIconLabel->hide();

  setWindowTitle(QString(tr("Discrete Wallet %1")).arg(Settings::instance().getVersion()));

  QList<QAction*> tabActions = m_tabActionGroup->actions();
  Q_FOREACH(auto action, tabActions) {
    action->setEnabled(false);
  }
  if (m_ui->m_miningFrame->isSoloRunning()) {
    m_ui->m_miningAction->setEnabled(true);
    m_ui->m_miningAction->setChecked(true);
  }
  Settings::instance().setTrackingMode(false);
  updateRecentActionList();
}

void MainWindow::checkTrackingMode() {
  Settings::instance().setTrackingMode(
      WalletAdapter::instance().isTrackingWallet() &&
      !WalletAdapter::instance().isYubiKeyProtected());
}

void MainWindow::createTrayIcon()
{
#ifdef Q_OS_WIN
    m_trayIcon = new QSystemTrayIcon(QPixmap(":images/discrete"), this);
    QString toolTip = QString(tr("Discrete Wallet %1")).arg(Settings::instance().getVersion());
    m_trayIcon->setToolTip(toolTip);
    m_trayIcon->show();
#endif
}

void MainWindow::createTrayIconMenu()
{
#ifndef Q_OS_MAC
    // return if trayIcon is unset (only on non-Mac OSes)
    if (!m_trayIcon)
        return;

    trayIconMenu = new QMenu(this);
    m_trayIcon->setContextMenu(trayIconMenu);

    connect(m_trayIcon, SIGNAL(activated(QSystemTrayIcon::ActivationReason)),
            this, SLOT(trayActivated(QSystemTrayIcon::ActivationReason)));
#endif
#ifdef Q_OS_MAC
    // Note: On Mac, the dock icon is used to provide the tray's functionality.
    MacDockIconHandler *dockIconHandler = MacDockIconHandler::instance();
    dockIconHandler->setMainWindow((QMainWindow *)this);
    trayIconMenu = dockIconHandler->dockMenu();
#endif

    // Configuration of the tray icon (or dock icon) icon menu

#ifndef Q_OS_MAC // This is built-in on Mac
    trayIconMenu->addAction(toggleHideAction);
    trayIconMenu->addSeparator();
#endif
    trayIconMenu->addAction(m_ui->m_sendAction);
    trayIconMenu->addAction(m_ui->m_receiveAction);
    trayIconMenu->addAction(m_ui->m_transactionsAction);
    trayIconMenu->addAction(m_ui->m_addressBookAction);
    trayIconMenu->addAction(m_ui->m_miningAction);
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(m_ui->m_openWalletAction);
    trayIconMenu->addAction(m_ui->m_closeWalletAction);
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(m_ui->actionHelp);
#ifndef Q_OS_MAC // This is built-in on Mac
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(m_ui->m_exitAction);
#endif
}

void MainWindow::payTo(const QModelIndex& _index) {
  m_ui->m_sendFrame->setAddress(_index.data(AddressBookModel::ROLE_ADDRESS).toString());
  m_ui->m_sendAction->trigger();
}

#ifdef Q_OS_WIN
void MainWindow::trayActivated(QSystemTrayIcon::ActivationReason reason)
{
    if(reason == QSystemTrayIcon::Trigger)
    {
        // Click on system tray icon triggers show/hide of the main window
        toggleHidden();
    }
}
#endif

#ifdef Q_OS_WIN
void MainWindow::minimizeToTray(bool _on) {
  if (_on) {
    hide();
  } else {
    showNormal();
    activateWindow();
  }
}
#endif

void MainWindow::showNormalIfMinimized(bool fToggleHidden)
{
    if (isHidden() || isMinimized())
    {
        showNormal();
        activateWindow();
    }
    else if (isObscured(this))
    {
        raise();
        activateWindow();
    }
    else if (fToggleHidden) {
        hide();
    }
    else {
        showNormal();
        activateWindow();
    }
}

void MainWindow::toggleHidden()
{
    showNormalIfMinimized(true);
}

bool MainWindow::checkPoint(const QPoint &p, const QWidget *w)
{
    QWidget *atW = QApplication::widgetAt(w->mapToGlobal(p));
    if (!atW) return false;
    return atW->topLevelWidget() == w;
}

bool MainWindow::isObscured(QWidget *w)
{
    return !(checkPoint(QPoint(0, 0), w)
        && checkPoint(QPoint(w->width() - 1, 0), w)
        && checkPoint(QPoint(0, w->height() - 1), w)
        && checkPoint(QPoint(w->width() - 1, w->height() - 1), w)
        && checkPoint(QPoint(w->width() / 2, w->height() / 2), w));
}

}
