// Copyright (c) 2011-2016 The Cryptonote developers
// Copyright (c) 2015-2016 XDN developers
// Copyright (c) 2016-2026 The Karbo developers
// Copyright (c) 2026 The Discrete developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QApplication>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QInputDialog>
#include <QMessageBox>
#include <QGridLayout>
#include <QTextEdit>
#include <QDateTime>
#include <QEventLoop>
#include <QLocale>
#include <QStringList>
#include <QVector>
#include <QDebug>

#include <algorithm>
#include <chrono>
#include <future>
#include <thread>

#include <boost/filesystem.hpp>

#include "WalletAdapter.h"

#include "CryptoNoteConfig.h"
#include "AccountNumber.h"
#include "PqAddress.h"
#include "Wallet/PqRecipient.h"
#include "Wallet/PqSender.h"
#include "Wallet/PqTransactionBuilder.h"
#include "CryptoNoteCore/TransactionExtra.h"
#include "CryptoNoteCore/PqValidation.h"
#include "CryptoNoteCore/CryptoNoteFormatUtils.h"
#include "CryptoNoteCore/CryptoNoteTools.h"
#include "crypto/crypto.h"
#include "Common/StringTools.h"
#include "Wallet/WalletErrors.h"
#include "CryptoNoteCore/CryptoNoteBasic.h"
#include "ITransfersContainer.h"
#include "NodeAdapter.h"
#include "Settings.h"
#include "Mnemonics/electrum-words.h"
#include "gui/VerifyMnemonicSeedDialog.h"
#include "CurrencyAdapter.h"
#include "LoggerAdapter.h"
#include "WalletLegacy/WalletLegacy.h"
#include "Common/SecureMemory.h"
#include "security/WindowsWebAuthnPrf.h"
#include "security/YubiKeySeedStore.h"

#undef ERROR

namespace WalletGui {

const quint32 MSECS_IN_HOUR = 60 * 60 * 1000;
const quint32 MSECS_IN_MINUTE = 60 * 1000;

const quint32 LAST_BLOCK_INFO_UPDATING_INTERVAL = 1 * MSECS_IN_MINUTE;
const quint32 LAST_BLOCK_INFO_WARNING_INTERVAL = 1 * MSECS_IN_HOUR;
constexpr std::chrono::seconds ACCOUNT_REGISTRATION_RELAY_TIMEOUT(60);

uint32_t freeRegPowWorkerCount() {
  return std::max(1u, std::thread::hardware_concurrency());
}

WalletAdapter& WalletAdapter::instance() {
  static WalletAdapter inst;
  return inst;
}

WalletAdapter::WalletAdapter() : QObject(), m_wallet(nullptr), m_mutex(), m_isBackupInProgress(false),
  m_syncSpeed(0), m_syncPeriod(0), m_isSynchronized(false), m_newTransactionsNotificationTimer(),
  m_lastWalletTransactionId(std::numeric_limits<quint64>::max()),
  m_logger(LoggerAdapter::instance().getLoggerManager(), "WalletAdapter")
{
  connect(this, &WalletAdapter::walletInitCompletedSignal, this, &WalletAdapter::onWalletInitCompleted, Qt::QueuedConnection);
  connect(this, &WalletAdapter::walletSendTransactionCompletedSignal, this, &WalletAdapter::onWalletSendTransactionCompleted, Qt::QueuedConnection);
  connect(this, &WalletAdapter::updateBlockStatusTextSignal, this, &WalletAdapter::updateBlockStatusText, Qt::QueuedConnection);
  connect(this, &WalletAdapter::updateBlockStatusTextWithDelaySignal, this, &WalletAdapter::updateBlockStatusTextWithDelay, Qt::QueuedConnection);
  connect(&m_newTransactionsNotificationTimer, &QTimer::timeout, this, &WalletAdapter::notifyAboutLastTransaction);
  connect(this, &WalletAdapter::walletSynchronizationProgressUpdatedSignal, this, [&]() {
    if (!m_newTransactionsNotificationTimer.isActive()) {
      m_newTransactionsNotificationTimer.start();
    }
  }, Qt::QueuedConnection);

  connect(this, &WalletAdapter::walletSynchronizationCompletedSignal, this, [&]() {
    m_newTransactionsNotificationTimer.stop();
    notifyAboutLastTransaction();
  }, Qt::QueuedConnection);

  m_newTransactionsNotificationTimer.setInterval(500);

  // init wallet rpc config
  bool no = false;
  std::string dummy = "";
  std::string wrpcBindIp = Settings::instance().getWalletRpcBindIp().toStdString();
  uint16_t wrpcBindPort = static_cast<uint16_t>(Settings::instance().getWalletRpcBindPort());
  uint16_t wrpcBindSslPort = CryptoNote::WALLET_RPC_DEFAULT_SSL_PORT;
  std::string wrpcUser = Settings::instance().getWalletRpcUser().toStdString();
  std::string wrpcPassword = Settings::instance().getWalletRpcPassword().toStdString();

  m_wrpcOptions.insert(std::make_pair("rpc-bind-ip", boost::program_options::variable_value(wrpcBindIp, false)));
  m_wrpcOptions.insert(std::make_pair("rpc-bind-port", boost::program_options::variable_value(wrpcBindPort, false)));
  m_wrpcOptions.insert(std::make_pair("rpc-bind-ssl-port", boost::program_options::variable_value(wrpcBindSslPort, false)));
  m_wrpcOptions.insert(std::make_pair("rpc-bind-ssl-enable", boost::program_options::variable_value(no, false)));
  m_wrpcOptions.insert(std::make_pair("rpc-chain-file", boost::program_options::variable_value(dummy, false)));
  m_wrpcOptions.insert(std::make_pair("rpc-key-file", boost::program_options::variable_value(dummy, false)));
  m_wrpcOptions.insert(std::make_pair("rpc-user", boost::program_options::variable_value(wrpcUser, false)));
  m_wrpcOptions.insert(std::make_pair("rpc-password", boost::program_options::variable_value(wrpcPassword, false)));
}

WalletAdapter::~WalletAdapter() {
}

QString WalletAdapter::getAddress() const {
  try {
    return m_wallet == nullptr ? QString() : QString::fromStdString(m_wallet->getAddress());
  } catch (std::system_error&) {
  }
  return QString();
}

quint64 WalletAdapter::getActualBalance() const {
  try {
    return m_wallet == nullptr ? 0 : m_wallet->actualBalance();
  } catch (std::system_error&) {
  }
  return 0;
}

quint64 WalletAdapter::getPendingBalance() const {
  try {
    return m_wallet == nullptr ? 0 : m_wallet->pendingBalance();
  } catch (std::system_error&) {
  }
  return 0;
}

void WalletAdapter::open(const QString& _password) {
  Q_ASSERT(m_wallet == nullptr);
  Settings::instance().setEncrypted(!_password.isEmpty());
  Q_EMIT walletStateChangedSignal(tr("Opening wallet"));

  m_wallet = NodeAdapter::instance().createWallet();
  m_wallet->addObserver(this);

  if (QFile::exists(Settings::instance().getWalletFile())) {
    if (Settings::instance().getWalletFile().endsWith(".wallet")) {
      if (openFile(Settings::instance().getWalletFile(), true)) {
        try {
          m_wallet->initAndLoad(m_file, _password.toStdString());
        } catch (std::system_error&) {
          closeFile();
          delete m_wallet;
          m_wallet = nullptr;
        }
      }
    }

  } else {
    //createWallet();
  }
}

bool WalletAdapter::tryOpen(const QString& _password) {
  Q_ASSERT(m_wallet != nullptr);
  if (Settings::instance().getWalletFile().endsWith(".wallet")) {
    if (openFile(Settings::instance().getWalletFile(), true)) {
      try {
        if (m_wallet->tryLoadWallet(m_file, _password.toStdString())) {
          closeFile();
          return true;
        }
        else {
          closeFile();
          return false;
        }
      }
      catch (std::system_error&) {
        closeFile();
        return false;
      }
    }
  }
  return false;
}

void WalletAdapter::createWallet() {
  Q_ASSERT(m_wallet == nullptr);
  Settings::instance().setEncrypted(false);
  Q_EMIT walletStateChangedSignal(tr("Creating wallet"));
  m_wallet = NodeAdapter::instance().createWallet();
  m_wallet->addObserver(this);

  try {
    m_wallet->initAndGenerateDeterministic("");

    VerifyMnemonicSeedDialog dlg(nullptr);
    if (!dlg.exec() == QDialog::Accepted) {
      return;
    }
  } catch (std::system_error&) {
    delete m_wallet;
    m_wallet = nullptr;
  }
}

void WalletAdapter::createWithKeys(const CryptoNote::AccountKeys& _keys) {
  m_wallet = NodeAdapter::instance().createWallet();
  m_wallet->addObserver(this);
  Settings::instance().setEncrypted(false);
  Q_EMIT walletStateChangedSignal(tr("Importing keys"));
  m_wallet->initWithKeys(_keys, "");
}

void WalletAdapter::createWithKeys(const CryptoNote::AccountKeys& _keys, const quint32 _sync_heigth) {
  m_wallet = NodeAdapter::instance().createWallet();
  m_wallet->addObserver(this);
  Settings::instance().setEncrypted(false);
  Q_EMIT walletStateChangedSignal(tr("Importing keys"));
  m_wallet->initWithKeys(_keys, "", _sync_heigth);
}

void WalletAdapter::createTrackingWallet(const CryptoNote::AccountKeys& _keys, const CryptoNote::PqTrackingKeys& _trackingKeys) {
  m_wallet = NodeAdapter::instance().createWallet();
  m_wallet->addObserver(this);
  Settings::instance().setEncrypted(false);
  Q_EMIT walletStateChangedSignal(tr("Importing tracking key"));
  // initWithPqTrackingKeys is concrete-only (see SimpleWallet::new_tracking_wallet
  // for the same pattern), so go through the concrete class rather than IWalletLegacy.
  auto* wallet = dynamic_cast<CryptoNote::WalletLegacy*>(m_wallet);
  Q_ASSERT(wallet);
  wallet->initWithPqTrackingKeys(_keys, _trackingKeys, "");
}

void WalletAdapter::createTrackingWallet(const CryptoNote::AccountKeys& _keys, const CryptoNote::PqTrackingKeys& _trackingKeys, const quint32 _sync_heigth) {
  m_wallet = NodeAdapter::instance().createWallet();
  m_wallet->addObserver(this);
  Settings::instance().setEncrypted(false);
  Q_EMIT walletStateChangedSignal(tr("Importing tracking key"));
  auto* wallet = dynamic_cast<CryptoNote::WalletLegacy*>(m_wallet);
  Q_ASSERT(wallet);
  wallet->initWithPqTrackingKeys(_keys, _trackingKeys, "", _sync_heigth);
}

bool WalletAdapter::isOpen() const {
  return m_wallet != nullptr;
}

bool WalletAdapter::hasYubiKeyMetadata() const {
  return YubiKeySeedStore::exists(Settings::instance().getWalletFile());
}

bool WalletAdapter::isYubiKeyProtected() const {
  return m_wallet != nullptr && m_wallet->isTrackingWallet() &&
         hasYubiKeyMetadata();
}

int WalletAdapter::yubiKeyCount() const {
  YubiKeySeedMetadata metadata;
  QString error;
  return YubiKeySeedStore::load(Settings::instance().getWalletFile(), metadata, error)
      ? metadata.keys.size() : 0;
}

bool WalletAdapter::enableYubiKeyProtection(WId _parentWindow, QString& _backupPath,
                                            QString& _errorText) {
  _backupPath.clear();
  _errorText.clear();
  if (m_wallet == nullptr || m_wallet->isTrackingWallet()) {
    _errorText = tr("Only an open full wallet can enable YubiKey protected spending.");
    return false;
  }
  if (YubiKeySeedStore::exists(Settings::instance().getWalletFile())) {
    _errorText = tr("This wallet already has YubiKey protection metadata.");
    return false;
  }

  auto* wallet = dynamic_cast<CryptoNote::WalletLegacy*>(m_wallet);
  if (wallet == nullptr) {
    _errorText = tr("This wallet backend does not support protected spending.");
    return false;
  }

  CryptoNote::AccountKeys accountKeys;
  m_wallet->getAccountKeys(accountKeys);
  if (accountKeys.spendSecretKey == CryptoNote::NULL_SECRET_KEY) {
    _errorText = tr("The open wallet has no spend seed.");
    return false;
  }
  CryptoPQ::SeedMaster seedMaster = CryptoNote::pqSeedMasterFromSpendSecret(accountKeys.spendSecretKey);
  Tools::SecretLock scrubSeed(seedMaster.data(), seedMaster.size());
  sodium_memzero(accountKeys.spendSecretKey.data, sizeof(accountKeys.spendSecretKey.data));

  CryptoNote::PqTrackingKeys tracking;
  if (!wallet->getPqTrackingKeys(tracking)) {
    _errorText = tr("Could not derive the wallet tracking identity.");
    return false;
  }
  const QByteArray binding = YubiKeySeedStore::walletBinding(
      QByteArray::fromStdString(CryptoNote::encodePqTrackingKey(tracking)));

  WindowsWebAuthnPrf::Enrollment enrollment;
  if (!WindowsWebAuthnPrf::enroll(_parentWindow, binding, enrollment, _errorText)) {
    return false;
  }
  Tools::SecretLock scrubPrf(enrollment.prfSecret.data(), enrollment.prfSecret.size());

  YubiKeySeedEnvelope primaryEnvelope;
  if (!YubiKeySeedStore::seal(seedMaster, tr("Primary YubiKey"),
                              enrollment.credentialId, enrollment.prfSalt,
                              enrollment.prfSecret, binding, primaryEnvelope,
                              _errorText)) {
    return false;
  }
  YubiKeySeedMetadata metadata;
  metadata.walletBinding = binding;
  metadata.keys.append(primaryEnvelope);

  const QString walletPath = Settings::instance().getWalletFile();
  const QFileInfo walletInfo(walletPath);
  _backupPath = walletInfo.dir().absoluteFilePath(
      walletInfo.completeBaseName() + QStringLiteral(".pre-yubikey-") +
      QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss")) +
      QStringLiteral(".wallet"));
  if (!QFile::copy(walletPath, _backupPath)) {
    _errorText = tr("Could not create the mandatory pre-YubiKey backup: %1").arg(_backupPath);
    _backupPath.clear();
    return false;
  }
  if (!YubiKeySeedStore::save(walletPath, metadata, _errorText)) {
    return false;
  }

  CryptoPQ::SeedMaster detached{};
  Tools::SecretLock scrubDetached(detached.data(), detached.size());
  if (!wallet->detachPqSpendSeed(detached) ||
      sodium_compare(detached.data(), seedMaster.data(), seedMaster.size()) != 0) {
    _errorText = tr("The wallet refused to detach its spend seed after the protected copy was written. The original wallet backup is intact.");
    return false;
  }
  if (!save(true, true)) {
    _errorText = tr("The protected tracking wallet could not be saved. The original wallet backup and encrypted sidecar are intact.");
    return false;
  }
  return true;
}

bool WalletAdapter::addYubiKeyProtectionKey(WId _parentWindow,
                                             const QString& _label,
                                             QString& _errorText) {
  _errorText.clear();
  if (!isYubiKeyProtected()) {
    _errorText = tr("This wallet is not in YubiKey protected-spending mode.");
    return false;
  }

  YubiKeySeedMetadata metadata;
  const QString walletPath = Settings::instance().getWalletFile();
  if (!YubiKeySeedStore::load(walletPath, metadata, _errorText)) {
    return false;
  }
  const QString label = _label.trimmed();
  if (label.isEmpty() || label.size() > 64) {
    _errorText = tr("The YubiKey label must contain between 1 and 64 characters.");
    return false;
  }
  for (const YubiKeySeedEnvelope& envelope : metadata.keys) {
    if (envelope.label.compare(label, Qt::CaseInsensitive) == 0) {
      _errorText = tr("Another enrolled YubiKey already uses that label.");
      return false;
    }
  }
  if (metadata.keys.size() >= YubiKeySeedStore::MAX_KEY_COUNT) {
    _errorText = tr("This wallet already has the maximum of %1 enrolled YubiKeys.")
        .arg(YubiKeySeedStore::MAX_KEY_COUNT);
    return false;
  }

  CryptoPQ::SeedMaster seedMaster{};
  Tools::SecretLock scrubSeed(seedMaster.data(), seedMaster.size());
  if (!unlockYubiKeySeed(_parentWindow, seedMaster, _errorText)) {
    return false;
  }

  QWidget* parent = _parentWindow == 0 ? QApplication::activeWindow()
                                       : QWidget::find(_parentWindow);
  // Let Windows Security and its temporary native owner finish closing before
  // showing the key-switch instruction.
  QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  if (!isYubiKeyProtected()) {
    _errorText = tr("The protected wallet was closed before the backup key could be enrolled.");
    return false;
  }
  QMessageBox switchPrompt(
      QMessageBox::Warning,
      tr("Insert a different backup YubiKey"),
      tr("The enrolled key has authorized this change. Remove it now and insert "
         "the different physical YubiKey that will serve as the backup.\n\n"
         "The wallet cannot detect whether two credentials were created on the "
         "same physical key. Using the same key again does not create a backup.\n\n"
         "Windows Security will ask for the backup key twice: first to create "
         "its credential, then to verify the PRF secret. Keep the backup key inserted."),
      QMessageBox::NoButton,
      parent);
  QPushButton* enrollButton = switchPrompt.addButton(
      tr("Enroll backup key"), QMessageBox::AcceptRole);
  switchPrompt.addButton(QMessageBox::Cancel);
  switchPrompt.setDefaultButton(QMessageBox::Cancel);
  switchPrompt.exec();
  if (switchPrompt.clickedButton() != enrollButton) {
    _errorText.clear();
    return false;
  }

  WindowsWebAuthnPrf::Enrollment enrollment;
  if (!WindowsWebAuthnPrf::enroll(
          _parentWindow, metadata.walletBinding, enrollment, _errorText)) {
    return false;
  }
  Tools::SecretLock scrubPrf(
      enrollment.prfSecret.data(), enrollment.prfSecret.size());

  for (const YubiKeySeedEnvelope& envelope : metadata.keys) {
    if (envelope.credentialId == enrollment.credentialId) {
      _errorText = tr("That WebAuthn credential is already enrolled for this wallet.");
      return false;
    }
  }

  YubiKeySeedEnvelope backupEnvelope;
  if (!YubiKeySeedStore::seal(seedMaster, label, enrollment.credentialId,
                              enrollment.prfSalt, enrollment.prfSecret,
                              metadata.walletBinding, backupEnvelope,
                              _errorText)) {
    return false;
  }
  metadata.keys.append(backupEnvelope);
  return YubiKeySeedStore::save(walletPath, metadata, _errorText);
}

bool WalletAdapter::unlockYubiKeySeed(WId _parentWindow,
                                      CryptoPQ::SeedMaster& _seedMaster,
                                      QString& _errorText) const {
  const auto totalStarted = std::chrono::steady_clock::now();
  _errorText.clear();
  if (!isYubiKeyProtected()) {
    _errorText = tr("This wallet is not in YubiKey protected-spending mode.");
    return false;
  }
  auto* wallet = dynamic_cast<CryptoNote::WalletLegacy*>(m_wallet);
  if (wallet == nullptr) {
    _errorText = tr("This wallet backend does not support protected spending.");
    return false;
  }

  YubiKeySeedMetadata metadata;
  if (!YubiKeySeedStore::load(Settings::instance().getWalletFile(), metadata, _errorText)) {
    return false;
  }
  CryptoNote::PqTrackingKeys tracking;
  if (!wallet->getPqTrackingKeys(tracking)) {
    _errorText = tr("The open wallet has no tracking identity.");
    return false;
  }
  const QByteArray currentBinding = YubiKeySeedStore::walletBinding(
      QByteArray::fromStdString(CryptoNote::encodePqTrackingKey(tracking)));
  if (currentBinding != metadata.walletBinding) {
    _errorText = tr("The YubiKey sidecar belongs to a different wallet.");
    return false;
  }

  QWidget* parent = _parentWindow == 0 ? QApplication::activeWindow()
                                       : QWidget::find(_parentWindow);
  int selectedIndex = 0;
  const auto selectionStarted = std::chrono::steady_clock::now();
  if (metadata.keys.size() > 1) {
    QStringList choices;
    choices.reserve(metadata.keys.size());
    for (const YubiKeySeedEnvelope& envelope : metadata.keys) {
      choices.append(tr("%1 [%2]").arg(
          envelope.label, YubiKeySeedStore::keyFingerprint(envelope)));
    }
    bool accepted = false;
    const QString selected = QInputDialog::getItem(
        parent, tr("Select YubiKey"), tr("Security key:"), choices,
        0, false, &accepted);
    if (!accepted) {
      _errorText = tr("YubiKey selection was cancelled.");
      return false;
    }
    selectedIndex = choices.indexOf(selected);
    if (selectedIndex < 0 || selectedIndex >= metadata.keys.size()) {
      _errorText = tr("The selected YubiKey entry is invalid.");
      return false;
    }
  }
  const auto selectionFinished = std::chrono::steady_clock::now();
  const YubiKeySeedEnvelope& selectedKey = metadata.keys.at(selectedIndex);

  QByteArray prfSecret;
  const auto webAuthnStarted = std::chrono::steady_clock::now();
  const bool authorized = WindowsWebAuthnPrf::unlock(
      _parentWindow, selectedKey.credentialId, selectedKey.prfSalt,
      prfSecret, _errorText);
  const auto webAuthnFinished = std::chrono::steady_clock::now();
  if (!authorized) {
    return false;
  }
  Tools::SecretLock scrubPrf(prfSecret.data(), prfSecret.size());
  const auto unsealStarted = std::chrono::steady_clock::now();
  if (!YubiKeySeedStore::unseal(selectedKey, metadata.walletBinding,
                                prfSecret, _seedMaster, _errorText)) {
    return false;
  }
  const auto unsealFinished = std::chrono::steady_clock::now();
  const bool seedMatches = CryptoNote::pqTrackingKeysMatchSeed(tracking, _seedMaster);
  const auto verificationFinished = std::chrono::steady_clock::now();

  const auto milliseconds = [](auto from, auto to) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(to - from).count();
  };
  m_logger(Logging::INFO)
      << "YubiKey unlock timing: precheck="
      << milliseconds(totalStarted, selectionStarted)
      << "ms, selection=" << milliseconds(selectionStarted, selectionFinished)
      << "ms, webauthn=" << milliseconds(webAuthnStarted, webAuthnFinished)
      << "ms, decrypt=" << milliseconds(unsealStarted, unsealFinished)
      << "ms, seed-check=" << milliseconds(unsealFinished, verificationFinished)
      << "ms, total=" << milliseconds(totalStarted, verificationFinished) << "ms";

  if (!seedMatches) {
    sodium_memzero(_seedMaster.data(), _seedMaster.size());
    _errorText = tr("The decrypted seed does not match the open wallet.");
    return false;
  }
  return true;
}

void WalletAdapter::close() {
  Q_CHECK_PTR(m_wallet);
  save(true, true);
  lock();
  m_wallet->removeObserver(this);
  m_isSynchronized = false;
  m_newTransactionsNotificationTimer.stop();
  m_lastWalletTransactionId = std::numeric_limits<quint64>::max();
  Q_EMIT walletCloseCompletedSignal();
  QCoreApplication::processEvents();

  stopWalletRpc();

  delete m_wallet;
  m_wallet = nullptr;
  unlock();
}

bool WalletAdapter::save(bool _details, bool _cache) {
  return save(Settings::instance().getWalletFile() + ".temp", _details, _cache);
}

bool WalletAdapter::save(const QString& _file, bool _details, bool _cache) {
  Q_CHECK_PTR(m_wallet);
  if (openFile(_file, false)) {
    Q_EMIT walletStateChangedSignal(tr("Saving data"));
    try {
      m_wallet->save(m_file, _details, _cache);
    } catch (std::system_error&) {
      closeFile();
      return false;
    }
  } else {
    return false;
  }

  return true;
}

void WalletAdapter::backup(const QString& _file) {
  const QString target = _file.endsWith(".wallet") ? _file : _file + ".wallet";
  if (save(target, true, false)) {
    if (isYubiKeyProtected()) {
      QFile::copy(YubiKeySeedStore::sidecarPath(Settings::instance().getWalletFile()),
                  YubiKeySeedStore::sidecarPath(target));
    }
    m_isBackupInProgress = true;
  }
}

void WalletAdapter::autoBackup(){
  QString source = Settings::instance().getWalletFile();
  source.append(QString(".backup"));

  if (!source.isEmpty() && !QFile::exists(source)) {
    if (save(source, true, false)) {
      if (isYubiKeyProtected()) {
        QFile::copy(YubiKeySeedStore::sidecarPath(Settings::instance().getWalletFile()),
                    YubiKeySeedStore::sidecarPath(source));
      }
      m_isBackupInProgress = true;
    }
  }
}

void WalletAdapter::reset() {
  Q_CHECK_PTR(m_wallet);
  save(false, false);
  lock();
  m_wallet->removeObserver(this);
  m_isSynchronized = false;
  m_newTransactionsNotificationTimer.stop();
  m_lastWalletTransactionId = std::numeric_limits<quint64>::max();
  Q_EMIT walletCloseCompletedSignal();
  QCoreApplication::processEvents();
  delete m_wallet;
  m_wallet = nullptr;
  unlock();
}

quint64 WalletAdapter::getTransactionCount() const {
  Q_CHECK_PTR(m_wallet);
  try {
    return m_wallet->getTransactionCount();
  } catch (std::system_error&) {
  }

  return 0;
}

quint64 WalletAdapter::getTransferCount() const {
  Q_CHECK_PTR(m_wallet);
  try {
    return m_wallet->getTransferCount();
  } catch (std::system_error&) {
  }

  return 0;
}

bool WalletAdapter::getTransaction(CryptoNote::TransactionId& _id, CryptoNote::WalletLegacyTransaction& _transaction) {
  Q_CHECK_PTR(m_wallet);
  try {
    return m_wallet->getTransaction(_id, _transaction);
  } catch (std::system_error&) {
  }

  return false;
}

bool WalletAdapter::getTransfer(CryptoNote::TransferId& _id, CryptoNote::WalletLegacyTransfer& _transfer) {
  Q_CHECK_PTR(m_wallet);
  try {
    return m_wallet->getTransfer(_id, _transfer);
  } catch (std::system_error&) {
  }

  return false;
}

bool WalletAdapter::getAccountKeys(CryptoNote::AccountKeys& _keys) {
  Q_CHECK_PTR(m_wallet);
  try {
    m_wallet->getAccountKeys(_keys);
    return true;
  } catch (std::system_error&) {
  }

  return false;
}

bool WalletAdapter::getPaymentProofs(const Crypto::Hash& _txid, CryptoNote::SentPaymentRecord& _record) const {
  // copyPaymentProofs is concrete WalletLegacy state (not on IWalletLegacy), so reach
  // the concrete class the same way createTrackingWallet does. copyPaymentProofs takes
  // the cache lock and copies out, so this is safe to call from the GUI thread.
  auto* wallet = dynamic_cast<CryptoNote::WalletLegacy*>(m_wallet);
  if (wallet == nullptr) {
    return false;
  }
  return wallet->copyPaymentProofs(_txid, _record);
}

std::vector<CryptoNote::TransactionOutputInformation> WalletAdapter::getOutputs() {
  Q_CHECK_PTR(m_wallet);
  try {
    return m_wallet->getOutputs();
  } catch (std::system_error&) {
  }
  return {};
}

std::vector<CryptoNote::TransactionOutputInformation> WalletAdapter::getLockedOutputs() {
  Q_CHECK_PTR(m_wallet);
  try {
    return m_wallet->getLockedOutputs();
  } catch (std::system_error&) {
  }
  return {};
}

std::vector<CryptoNote::TransactionOutputInformation> WalletAdapter::getUnlockedOutputs() {
  Q_CHECK_PTR(m_wallet);
  try {
    return m_wallet->getLockedOutputs();
  } catch (std::system_error&) {
  }
  return {};
}

std::vector<CryptoNote::TransactionSpentOutputInformation> WalletAdapter::getSpentOutputs() {
  Q_CHECK_PTR(m_wallet);
  try {
    return m_wallet->getSpentOutputs();
  } catch (std::system_error&) {
  }
  return {};
}

void WalletAdapter::sendTransaction(const std::vector<CryptoNote::WalletLegacyTransfer>& _transfers, quint64 _fee) {
  sendTransactionImpl(nullptr, _transfers, _fee);
}

void WalletAdapter::sendTransactionWithSeed(
    const CryptoPQ::SeedMaster& _seedMaster,
    const std::vector<CryptoNote::WalletLegacyTransfer>& _transfers,
    quint64 _fee) {
  sendTransactionImpl(&_seedMaster, _transfers, _fee);
}

void WalletAdapter::sendTransactionImpl(
    const CryptoPQ::SeedMaster* _seedMaster,
    const std::vector<CryptoNote::WalletLegacyTransfer>& _transfers,
    quint64 _fee) {
  Q_CHECK_PTR(m_wallet);
  try {
    lock();
    Q_EMIT walletStateChangedSignal(tr("Sending transaction"));
    // The destination string (raw PQ address or account number) is resolved
    // inside WalletLegacy::sendTransaction itself (see Wallet/PqRecipient.h);
    // the GUI does not need to pre-resolve it. Mixin and payment IDs no longer
    // exist in the PQ design, so both trailing parameters stay at 0.
    if (_seedMaster != nullptr) {
      auto* wallet = dynamic_cast<CryptoNote::WalletLegacy*>(m_wallet);
      if (wallet == nullptr) {
        throw std::runtime_error("protected spending requires WalletLegacy");
      }
      wallet->sendTransactionWithSeed(*_seedMaster, _transfers, _fee, "", 0, 0);
    } else {
      m_wallet->sendTransaction(_transfers, _fee, "", 0, 0);
    }
  } catch (const CryptoNote::PqSendError& _error) {
    int code = CryptoNote::error::INTERNAL_WALLET_ERROR;
    switch (_error.code) {
      case CryptoNote::PqSendErrorCode::InsufficientFunds:
        code = CryptoNote::error::INSUFFICIENT_FUNDS;
        break;
      case CryptoNote::PqSendErrorCode::TooLarge:
        code = CryptoNote::error::AMOUNT_TOO_LARGE_FOR_ONE_TRANSACTION;
        break;
      case CryptoNote::PqSendErrorCode::ZeroAmount:
        code = CryptoNote::error::WRONG_AMOUNT;
        break;
      case CryptoNote::PqSendErrorCode::NoRecipients:
      case CryptoNote::PqSendErrorCode::UnsupportedUnlockHeight:
        code = CryptoNote::error::WRONG_PARAMETERS;
        break;
    }

    m_logger(Logging::WARNING) << "PQ transaction could not be sent: " << _error.what();
    unlock();
    Q_EMIT walletSendTransactionCompletedSignal(
      CryptoNote::WALLET_LEGACY_INVALID_TRANSACTION_ID, code, walletErrorMessage(code));
    Q_EMIT updateBlockStatusTextWithDelaySignal();
  } catch (std::system_error& _error) {
    // A synchronous failure (an unresolvable account number -> BAD_ADDRESS,
    // insufficient funds, a relay error, …) is thrown here rather than delivered
    // through the completion observer. Surface it exactly like a completed-with-
    // error send so the Send frame reports the reason; otherwise the status bar is
    // left stuck on "Sending transaction" with nothing logged.
    unlock();
    const int code = _error.code().value();
    Q_EMIT walletSendTransactionCompletedSignal(
      CryptoNote::WALLET_LEGACY_INVALID_TRANSACTION_ID, code, walletErrorMessage(code));
    Q_EMIT updateBlockStatusTextWithDelaySignal();
  } catch (const std::exception& _error) {
    // Keep unexpected builder/signing failures inside the GUI boundary. These are
    // synchronous, so without this guard a runtime_error would unwind through the
    // Qt event handler and terminate the application.
    m_logger(Logging::ERROR) << "Unexpected error while sending transaction: " << _error.what();
    unlock();
    const int code = CryptoNote::error::INTERNAL_WALLET_ERROR;
    Q_EMIT walletSendTransactionCompletedSignal(
      CryptoNote::WALLET_LEGACY_INVALID_TRANSACTION_ID, code,
      tr("Failed to send transaction: %1").arg(QString::fromUtf8(_error.what())));
    Q_EMIT updateBlockStatusTextWithDelaySignal();
  }
}

QString WalletAdapter::prepareRawTransaction(
    const std::vector<CryptoNote::WalletLegacyTransfer>& _transfers,
    quint64 _fee, QString* _errorText) {
  return prepareRawTransactionImpl(nullptr, _transfers, _fee, _errorText);
}

QString WalletAdapter::prepareRawTransactionWithSeed(
    const CryptoPQ::SeedMaster& _seedMaster,
    const std::vector<CryptoNote::WalletLegacyTransfer>& _transfers,
    quint64 _fee, QString* _errorText) {
  return prepareRawTransactionImpl(&_seedMaster, _transfers, _fee, _errorText);
}

QString WalletAdapter::prepareRawTransactionImpl(
    const CryptoPQ::SeedMaster* _seedMaster,
    const std::vector<CryptoNote::WalletLegacyTransfer>& _transfers,
    quint64 _fee, QString* _errorText) {
  Q_CHECK_PTR(m_wallet);
  if (_errorText != nullptr) {
    _errorText->clear();
  }

  lock();
  Q_EMIT walletStateChangedSignal(tr("Preparing transaction"));
  try {
    CryptoNote::TransactionId transactionId = CryptoNote::WALLET_LEGACY_INVALID_TRANSACTION_ID;
    std::string raw;
    if (_seedMaster != nullptr) {
      auto* wallet = dynamic_cast<CryptoNote::WalletLegacy*>(m_wallet);
      if (wallet == nullptr) {
        throw std::runtime_error("protected spending requires WalletLegacy");
      }
      raw = wallet->prepareRawTransactionWithSeed(
          *_seedMaster, transactionId, _transfers, _fee, "", 0, 0);
    } else {
      raw = m_wallet->prepareRawTransaction(transactionId, _transfers, _fee, "", 0, 0);
    }
    unlock();
    Q_EMIT updateBlockStatusTextWithDelaySignal();
    return QString::fromStdString(raw);
  } catch (const CryptoNote::PqSendError& _error) {
    m_logger(Logging::WARNING) << "PQ transaction could not be prepared: " << _error.what();
    if (_errorText != nullptr) {
      *_errorText = tr("Failed to prepare transaction: %1")
          .arg(QString::fromUtf8(_error.what()));
    }
  } catch (const std::system_error& _error) {
    m_logger(Logging::WARNING) << "Transaction could not be prepared: " << _error.what();
    if (_errorText != nullptr) {
      *_errorText = tr("Failed to prepare transaction: %1")
          .arg(QString::fromUtf8(_error.what()));
    }
  } catch (const std::exception& _error) {
    m_logger(Logging::ERROR) << "Unexpected error while preparing transaction: " << _error.what();
    if (_errorText != nullptr) {
      *_errorText = tr("Failed to prepare transaction: %1")
          .arg(QString::fromUtf8(_error.what()));
    }
  }

  unlock();
  Q_EMIT updateBlockStatusTextWithDelaySignal();
  return QString();
}

bool WalletAdapter::getOwnPqIdentityHex(QString& _viewPubHex, QString& _spendPubHex) const {
  if (auto* wallet = dynamic_cast<CryptoNote::WalletLegacy*>(m_wallet)) {
    CryptoNote::PqTrackingKeys tracking;
    if (wallet->getPqTrackingKeys(tracking)) {
      _viewPubHex = QString::fromStdString(Common::toHex(tracking.viewPub.data(), tracking.viewPub.size()));
      _spendPubHex = QString::fromStdString(Common::toHex(tracking.spendPub.data(), tracking.spendPub.size()));
      return true;
    }
  }

  CryptoNote::AccountKeys keys;
  if (!const_cast<WalletAdapter*>(this)->getAccountKeys(keys)) {
    return false;
  }

  CryptoNote::PqWalletKeys pq = CryptoNote::derivePqWalletKeys(keys.spendSecretKey);
  _viewPubHex = QString::fromStdString(Common::toHex(pq.viewPub.data(), pq.viewPub.size()));
  _spendPubHex = QString::fromStdString(Common::toHex(pq.spendPub.data(), pq.spendPub.size()));
  return true;
}

bool WalletAdapter::getMiningKeys(CryptoPQ::KemPublicKey& _viewPub, CryptoPQ::DsaPublicKey& _spendPub, CryptoPQ::DsaSecretKey& _spendSk) const {
  CryptoNote::AccountKeys keys;
  if (!const_cast<WalletAdapter*>(this)->getAccountKeys(keys)) {
    return false;
  }
  // Protected-spending wallets deliberately never release the seed to the
  // long-running miner. That would keep spend authority in RAM indefinitely
  // and defeat the protection model.
  if (keys.spendSecretKey == CryptoNote::NULL_SECRET_KEY) {
    return false;
  }

  // Same derivation the daemon's start_mining path uses, so a block this
  // wallet mines is signed with the identity that also receives the reward.
  CryptoNote::deriveMinerPqKeys(keys.spendSecretKey, _viewPub, _spendPub, _spendSk);
  return true;
}

void WalletAdapter::registerAccountNumber(AccountRegistrationMode _mode) {
  if (m_wallet == nullptr) {
    Q_EMIT accountRegistrationCompletedSignal(
      static_cast<int>(CryptoNote::error::WalletErrorCodes::NOT_INITIALIZED),
      tr("Object was not initialized"),
      QString());
    return;
  }

  Q_EMIT walletStateChangedSignal(tr("Registering account number"));
  std::thread([this, _mode]() {
    doRegisterAccountNumber(_mode);
  }).detach();
}

void WalletAdapter::doRegisterAccountNumber(AccountRegistrationMode _mode) {
  try {
    auto finishRegistration = [this](int error, const QString& message, const QString& transactionHash = QString()) {
      Q_EMIT accountRegistrationCompletedSignal(error, message, transactionHash);
      Q_EMIT walletStateChangedSignal(tr("Ready"));
      Q_EMIT updateBlockStatusTextWithDelaySignal();
    };

    if (m_wallet == nullptr) {
      finishRegistration(
        static_cast<int>(CryptoNote::error::WalletErrorCodes::NOT_INITIALIZED),
        tr("Object was not initialized"));
      return;
    }

    CryptoNote::AccountKeys keys;
    m_wallet->getAccountKeys(keys);
    if (keys.spendSecretKey == CryptoNote::NULL_SECRET_KEY) {
      finishRegistration(
        static_cast<int>(CryptoNote::error::WalletErrorCodes::TRACKING_MODE),
        tr("Cannot register account number from a tracking wallet."));
      return;
    }

    CryptoNote::PqWalletKeys pq = CryptoNote::derivePqWalletKeys(keys.spendSecretKey);
    if (_mode == AccountRegistrationMode::Paid) {
      Q_EMIT walletStateChangedSignal(tr("Sending paid registration"));

      std::vector<uint8_t> extra;
      CryptoNote::addPqAccountRegistrationToExtra(extra, pq.viewPub, pq.spendPub);

      std::vector<CryptoNote::WalletLegacyTransfer> transfers;
      transfers.push_back(CryptoNote::WalletLegacyTransfer{
        m_wallet->getAddress(),
        static_cast<int64_t>(CryptoNote::parameters::DEFAULT_DUST_THRESHOLD)
      });

      QString transactionHash;
      try {
        lock();
        const CryptoNote::TransactionId transactionId =
          m_wallet->sendTransaction(transfers, 0, std::string(extra.begin(), extra.end()), 0, 0);

        CryptoNote::WalletLegacyTransaction transaction;
        if (transactionId != CryptoNote::WALLET_LEGACY_INVALID_TRANSACTION_ID &&
            m_wallet->getTransaction(transactionId, transaction)) {
          transactionHash = QString::fromStdString(Common::podToHex(transaction.hash));
        }
      } catch (...) {
        unlock();
        throw;
      }

      finishRegistration(0, QString(), transactionHash);
      return;
    }

    const CryptoNote::BlockHeaderInfo headerInfo = NodeAdapter::instance().getLastLocalBlockHeaderInfo();
    if (headerInfo.hash == Crypto::Hash{}) {
      finishRegistration(
        static_cast<int>(CryptoNote::error::WalletErrorCodes::INTERNAL_WALLET_ERROR),
        tr("Node has no known block to reference yet; try again once it is synchronized."));
      return;
    }

    const uint32_t workerCount = freeRegPowWorkerCount();
    Q_EMIT walletStateChangedSignal(tr("Solving registration proof-of-work (%1 threads)").arg(workerCount));
    const uint64_t nonce = CryptoNote::grindFreeRegPow(pq.viewPub, pq.spendPub, headerInfo.hash);
    const CryptoNote::Transaction tx = CryptoNote::buildFreeRegTransaction(pq.viewPub, pq.spendPub, headerInfo.hash, nonce);

    Q_EMIT walletStateChangedSignal(tr("Relaying free registration"));
    auto relayCompleted = std::make_shared<std::promise<std::error_code>>();
    auto relayFuture = relayCompleted->get_future();
    NodeAdapter::instance().getNode()->relayTransaction(tx, [relayCompleted](std::error_code ec) {
      try {
        relayCompleted->set_value(ec);
      } catch (const std::future_error&) {
      }
    });

    if (relayFuture.wait_for(ACCOUNT_REGISTRATION_RELAY_TIMEOUT) != std::future_status::ready) {
      finishRegistration(
        static_cast<int>(CryptoNote::error::WalletErrorCodes::INTERNAL_WALLET_ERROR),
        tr("The wallet solved the anti-spam proof-of-work, but the daemon did not answer the relay request within 60 seconds. "
           "Check that the daemon is reachable and synchronized, then try again."));
      return;
    }

    const std::error_code relayError = relayFuture.get();
    if (relayError) {
      finishRegistration(
        relayError.value(),
        tr("The wallet solved the anti-spam proof-of-work, but the daemon did not accept the free registration relay: %1. "
           "Make sure the daemon is current and synchronized, then try again.")
          .arg(QString::fromStdString(relayError.message())));
      return;
    }

    finishRegistration(
      0,
      QString(),
      QString::fromStdString(Common::podToHex(CryptoNote::getObjectHash(tx))));
  } catch (const std::system_error& e) {
    Q_EMIT accountRegistrationCompletedSignal(
      e.code().value(),
      QString::fromStdString(e.code().message()),
      QString());
    Q_EMIT walletStateChangedSignal(tr("Ready"));
    Q_EMIT updateBlockStatusTextWithDelaySignal();
  } catch (const std::exception& e) {
    Q_EMIT accountRegistrationCompletedSignal(
      static_cast<int>(CryptoNote::error::WalletErrorCodes::INTERNAL_WALLET_ERROR),
      QString::fromLocal8Bit(e.what()),
      QString());
    Q_EMIT walletStateChangedSignal(tr("Ready"));
    Q_EMIT updateBlockStatusTextWithDelaySignal();
  }
}

bool WalletAdapter::changePassword(const QString& _oldPassword, const QString& _newPassword) {
  Q_CHECK_PTR(m_wallet);
  try {
    if (m_wallet->changePassword(_oldPassword.toStdString(), _newPassword.toStdString()).value() == CryptoNote::error::WRONG_PASSWORD) {
      return false;
    }
  } catch (std::system_error&) {
    return false;
  }

  Settings::instance().setEncrypted(!_newPassword.isEmpty());

  QString source = Settings::instance().getWalletFile();
  source.append(QString(".backup"));
  if (!source.isEmpty()) {
    // remove old unencrypted backup
    if(QFile::exists(source)) {
      QFile::remove(source);
    }
    // create new encrypted backup
    if (save(source, true, false)) {
      m_isBackupInProgress = true;
    }
  }

  return save(true, true);
}

void WalletAdapter::setWalletFile(const QString& _path) {
  Q_ASSERT(m_wallet == nullptr);
  Settings::instance().setWalletFile(_path);
}

void WalletAdapter::initCompleted(std::error_code _error) {
  if (m_file.is_open()) {
    closeFile();
  }

  Q_EMIT walletInitCompletedSignal(_error.value(), QString::fromStdString(_error.message()));
}

void WalletAdapter::runWalletRpc() {
  m_logger(Logging::INFO) << "Initialize wallet RPC server";

  // Use a dedicated dispatcher owned by the GUI thread so that the QTimer-driven
  // yield() calls are always on the same thread that created the dispatcher.
  // Using the InprocessNode's dispatcher here would be wrong: that dispatcher is
  // created on m_nodeInitializerThread, and calling yield() on it from the GUI
  // thread causes the hang observed with InprocessNode.
  m_rpcDispatcher = std::make_unique<System::Dispatcher>();

  const std::string walletFilename = Settings::instance().getWalletFile().toStdString();
  m_wallet_rpc = new Tools::wallet_rpc_server(*m_rpcDispatcher,
                                              LoggerAdapter::instance().getLoggerManager(),
                                              *m_wallet,
                                              *NodeAdapter::instance().getNode(),
                                              CurrencyAdapter::instance().getCurrency(),
                                              walletFilename);
  if (!m_wallet_rpc->init(m_wrpcOptions))
    m_logger(Logging::ERROR) << "Failed to initialize wallet RPC server";
  bool enable_ssl;
  std::string bind_address, bind_address_ssl, ssl_info;
  m_wallet_rpc->getServerConf(bind_address, bind_address_ssl, enable_ssl);
  if (enable_ssl) ssl_info += std::string(", SSL on address ") + bind_address_ssl;
  m_logger(Logging::INFO) << "Starting wallet RPC server on address " << bind_address << ssl_info;

  m_wallet_rpc->run();

  m_dispatcherTimer = new QTimer(this);
  connect(m_dispatcherTimer, &QTimer::timeout, [this]() {
      m_rpcDispatcher->yield();  // Drive the RPC server's event loop
  });
  m_dispatcherTimer->start(1);  // Run every 1ms
}

void WalletAdapter::stopWalletRpc() {
  if (!m_wallet_rpc) {
    return;
  }

  m_logger(Logging::INFO) << "Stopping wallet RPC server";

  // Stop the timer first
  if (m_dispatcherTimer) {
      m_dispatcherTimer->stop();
      delete m_dispatcherTimer;
      m_dispatcherTimer = nullptr;
  }

  // Stop the RPC server
  if (m_wallet_rpc) {
      m_wallet_rpc->stop();
      delete m_wallet_rpc;
      m_wallet_rpc = nullptr;
  }

  m_rpcDispatcher.reset();

  m_logger(Logging::INFO) << "Wallet RPC server stopped";
}

void WalletAdapter::onWalletInitCompleted(int _error, const QString& _errorText) {
  switch(_error) {
  case 0: {
    Q_EMIT walletActualBalanceUpdatedSignal(m_wallet->actualBalance());
    Q_EMIT walletPendingBalanceUpdatedSignal(m_wallet->pendingBalance());
    Q_EMIT updateWalletAddressSignal(QString::fromStdString(m_wallet->getAddress()));
    Q_EMIT reloadWalletTransactionsSignal();
    Q_EMIT walletStateChangedSignal(tr("Ready"));
    QTimer::singleShot(5000, this, SLOT(updateBlockStatusText()));
    if (!QFile::exists(Settings::instance().getWalletFile())) {
      save(true, true);
    }

    if (Settings::instance().runWalletRpc()) {
      runWalletRpc();
    }

    break;
  }
  case CryptoNote::error::WRONG_PASSWORD:
    Q_EMIT openWalletWithPasswordSignal(Settings::instance().isEncrypted());
    Settings::instance().setEncrypted(true);
    delete m_wallet;
    m_wallet = nullptr;
    break;
  default: {
    delete m_wallet;
    m_wallet = nullptr;
    break;
  }
  }
}

void WalletAdapter::saveCompleted(std::error_code _error) {
  if (!_error && !m_isBackupInProgress) {
    closeFile();
    renameFile(Settings::instance().getWalletFile() + ".temp", Settings::instance().getWalletFile());
    Q_EMIT walletStateChangedSignal(tr("Ready"));
    Q_EMIT updateBlockStatusTextWithDelaySignal();
  } else if (m_isBackupInProgress) {
    m_isBackupInProgress = false;
    closeFile();
  } else {
    closeFile();
  }

  Q_EMIT walletSaveCompletedSignal(_error.value(), QString::fromStdString(_error.message()));
}

void WalletAdapter::synchronizationProgressUpdated(uint32_t _current, uint32_t _total) {
  if (m_isSynchronized) {
    m_syncSpeed = 0;
    m_syncPeriod = 0;
    m_perfData.clear();
  }
  m_isSynchronized = false;

  if (NodeAdapter::instance().isOffline()) {
    Q_EMIT walletStateChangedSignal(QString(tr("Offline")));
    Q_EMIT walletSynchronizationProgressUpdatedSignal(_current, _total);
    return;
  }

  const uint32_t speedCalcPeriod = 10;
  const uint32_t periodDay = 60 * 60 * 24;
  const uint32_t syncPeriodMax = std::numeric_limits<uint32_t>::max();
  bool calcReady = false;
  uint32_t totalDeltaTime = 0;
  uint32_t totalDeltaHeight= 0;
  uint32_t indexElements = m_perfData.empty() ? 0 : m_perfData.size() - 1;
  for (uint32_t i = indexElements; i > 0; i--) {
    totalDeltaTime += m_perfData[i - 1].time.secsTo(m_perfData[i].time);
    totalDeltaHeight += m_perfData[i].height - m_perfData[i - 1].height;
    if (totalDeltaTime >= speedCalcPeriod) {
      m_perfData.erase(m_perfData.begin(), m_perfData.begin() + i - 1);
      calcReady = true;
      break;
    }
  }
  if (calcReady && _total >= _current) {
    m_syncSpeed = static_cast<uint32_t>(totalDeltaHeight / totalDeltaTime);
    m_syncPeriod = m_syncSpeed > 0 ? static_cast<uint32_t>((_total - _current) / m_syncSpeed) : syncPeriodMax;
  }
  PerfType perfData = {_current, QTime::currentTime()};
  m_perfData.push_back(std::move(perfData));
  QString perfMess = "";
  if (m_syncPeriod > 0) {
    QDateTime leftTime = QDateTime::fromSecsSinceEpoch(m_syncPeriod).toUTC();
    perfMess += "(";
    perfMess += QString(tr("%n blocks per second", "", m_syncSpeed));
    if (m_syncPeriod < syncPeriodMax) {
      perfMess += " | ";
      perfMess += QString(tr("est. completion in")) + " ";
      if (m_syncPeriod >= periodDay) {
        perfMess += QString(tr("%n day(s) and", "", static_cast<uint32_t>(m_syncPeriod / periodDay))) + " ";
        perfMess += leftTime.toString("hh:mm");
      } else {
        perfMess += leftTime.toString("hh:mm:ss");
      }
    }
    perfMess += ")";
  }
  Q_EMIT walletStateChangedSignal(QString("%1 %2/%3 %4").arg(tr("Synchronizing")).arg(_current).arg(_total).arg(perfMess));
  Q_EMIT walletSynchronizationProgressUpdatedSignal(_current, _total);
}

void WalletAdapter::synchronizationCompleted(std::error_code _error) {
  if (!_error) {
    m_isSynchronized = true;
    Q_EMIT updateBlockStatusTextSignal();
    Q_EMIT walletSynchronizationCompletedSignal(_error.value(), QString::fromStdString(_error.message()));
  }
}

void WalletAdapter::actualBalanceUpdated(uint64_t _actual_balance) {
  Q_EMIT walletActualBalanceUpdatedSignal(_actual_balance);
}

void WalletAdapter::pendingBalanceUpdated(uint64_t _pending_balance) {
  Q_EMIT walletPendingBalanceUpdatedSignal(_pending_balance);
}

void WalletAdapter::externalTransactionCreated(CryptoNote::TransactionId _transactionId) {
  if (!m_isSynchronized) {
    m_lastWalletTransactionId = _transactionId;
  } else {
    Q_EMIT walletTransactionCreatedSignal(_transactionId);
  }
}

void WalletAdapter::sendTransactionCompleted(CryptoNote::TransactionId _transaction_id, std::error_code _error) {
  unlock();
  Q_EMIT walletSendTransactionCompletedSignal(_transaction_id, _error.value(), walletErrorMessage(_error.value()));
  Q_EMIT updateBlockStatusTextWithDelaySignal();
}

QString WalletAdapter::walletErrorMessage(int _error_code) {
  switch (_error_code) {
    case CryptoNote::error::WalletErrorCodes::NOT_INITIALIZED:               return tr("Object was not initialized");
    case CryptoNote::error::WalletErrorCodes::WRONG_PASSWORD:                return tr("The password is wrong");
    case CryptoNote::error::WalletErrorCodes::ALREADY_INITIALIZED:           return tr("The object is already initialized");
    case CryptoNote::error::WalletErrorCodes::INTERNAL_WALLET_ERROR:         return tr("Internal error occurred");
    case CryptoNote::error::WalletErrorCodes::BAD_ADDRESS:                   return tr("Bad address or account number");
    case CryptoNote::error::WalletErrorCodes::TRANSACTION_SIZE_TOO_BIG:      return tr("Transaction size is too big");
    case CryptoNote::error::WalletErrorCodes::WRONG_AMOUNT:                  return tr("Wrong amount");
    case CryptoNote::error::WalletErrorCodes::SUM_OVERFLOW:                  return tr("Sum overflow");
    case CryptoNote::error::WalletErrorCodes::ZERO_DESTINATION:              return tr("The destination is empty");
    case CryptoNote::error::WalletErrorCodes::TX_CANCEL_IMPOSSIBLE:          return tr("Impossible to cancel transaction");
    case CryptoNote::error::WalletErrorCodes::TX_CANCELLED:                  return tr("The transaction was cancelled");
    case CryptoNote::error::WalletErrorCodes::WRONG_STATE:                   return tr("The wallet is in wrong state (maybe loading or saving), try again later");
    case CryptoNote::error::WalletErrorCodes::OPERATION_CANCELLED:           return tr("The operation you've requested has been cancelled");
    case CryptoNote::error::WalletErrorCodes::TX_TRANSFER_IMPOSSIBLE:        return tr("Transaction transfer impossible");
    case CryptoNote::error::WalletErrorCodes::WRONG_VERSION:                 return tr("Wrong version");
    case CryptoNote::error::WalletErrorCodes::FEE_TOO_SMALL:                 return tr("Transaction fee is too small");
    case CryptoNote::error::WalletErrorCodes::KEY_GENERATION_ERROR:          return tr("Cannot generate new key");
    case CryptoNote::error::WalletErrorCodes::INDEX_OUT_OF_RANGE:            return tr("Index is out of range");
    case CryptoNote::error::WalletErrorCodes::ADDRESS_ALREADY_EXISTS:        return tr("Address already exists");
    case CryptoNote::error::WalletErrorCodes::TRACKING_MODE:                 return tr("The wallet is in tracking mode");
    case CryptoNote::error::WalletErrorCodes::WRONG_PARAMETERS:              return tr("Wrong parameters passed");
    case CryptoNote::error::WalletErrorCodes::OBJECT_NOT_FOUND:              return tr("Object not found");
    case CryptoNote::error::WalletErrorCodes::WALLET_NOT_FOUND:              return tr("Requested wallet not found");
    case CryptoNote::error::WalletErrorCodes::CHANGE_ADDRESS_REQUIRED:       return tr("Change address required");
    case CryptoNote::error::WalletErrorCodes::CHANGE_ADDRESS_NOT_FOUND:      return tr("Change address not found");
    case CryptoNote::error::WalletErrorCodes::DESTINATION_ADDRESS_REQUIRED:  return tr("Destination address required");
    case CryptoNote::error::WalletErrorCodes::DESTINATION_ADDRESS_NOT_FOUND: return tr("Destination address not found");
    case CryptoNote::error::WalletErrorCodes::BAD_PAYMENT_ID:                return tr("Wrong transaction extra format");
    case CryptoNote::error::WalletErrorCodes::BAD_TRANSACTION_EXTRA:         return tr("Wrong transaction extra format");
    case CryptoNote::error::WalletErrorCodes::INSUFFICIENT_FUNDS:            return tr("Insufficient available balance. Some funds may still be awaiting confirmation or coinbase maturity.");
    case CryptoNote::error::WalletErrorCodes::AMOUNT_TOO_LARGE_FOR_ONE_TRANSACTION: return tr("Amount is too large for one transaction (too many inputs or the transaction-size limit). Send a smaller amount or consolidate your outputs first.");
    case CryptoNote::error::WalletErrorCodes::ACCOUNT_NOT_REGISTERED:        return tr("That account number is not registered on chain");
    default:                                                                 return tr("Unknown error");
  }
}

void WalletAdapter::onWalletSendTransactionCompleted(CryptoNote::TransactionId _transactionId, int _error, const QString& _errorText) {
  if (_error) {
    return;
  }

  CryptoNote::WalletLegacyTransaction transaction;
  if (!this->getTransaction(_transactionId, transaction)) {
    return;
  }

  // PQ sends are announced by WalletLegacy::notifyExternalTransactions(), which
  // also advances the backend notification cursor. Classical rows still arrive
  // here with explicit transfer rows.
  if (transaction.transferCount > 0) {
    Q_EMIT walletTransactionCreatedSignal(_transactionId);
  }

  save(true, true);
}

void WalletAdapter::transactionUpdated(CryptoNote::TransactionId _transactionId) {
  Q_EMIT walletTransactionUpdatedSignal(_transactionId);
}

void WalletAdapter::lock() {
  m_mutex.lock();
}

void WalletAdapter::unlock() {
  if (m_mutex.try_lock()) {
    m_mutex.unlock();
  } else {
    m_mutex.unlock();
  }
}

bool WalletAdapter::openFile(const QString& _file, bool _readOnly) {
  lock();


#ifdef Q_OS_WIN
  m_file.open(_file.toStdWString(), std::ios::binary | (_readOnly ? std::ios::in : (std::ios::out | std::ios::trunc)));
#else
  m_file.open(_file.toStdString(), std::ios::binary | (_readOnly ? std::ios::in : (std::ios::out | std::ios::trunc)));
#endif


  if (!m_file.is_open()) {
    unlock();
  }

  return m_file.is_open();
}

void WalletAdapter::closeFile() {
  m_file.close();
  unlock();
}

void WalletAdapter::notifyAboutLastTransaction() {
  if (m_lastWalletTransactionId != std::numeric_limits<quint64>::max()) {
    Q_EMIT walletTransactionCreatedSignal(m_lastWalletTransactionId);
    m_lastWalletTransactionId = std::numeric_limits<quint64>::max();
  }
}

void WalletAdapter::renameFile(const QString& _oldName, const QString& _newName) {
  Q_ASSERT(QFile::exists(_oldName));
  QFile::remove(_newName);
  QFile::rename(_oldName, _newName);
}

void WalletAdapter::updateBlockStatusText() {
  if (m_wallet == nullptr) {
    return;
  }

  const QDateTime currentTime = QDateTime::currentDateTimeUtc();
  const QDateTime blockTime = NodeAdapter::instance().getLastLocalBlockTimestamp();
  quint64 blockAge = blockTime.msecsTo(currentTime);
  const QString warningString = blockTime.msecsTo(currentTime) < LAST_BLOCK_INFO_WARNING_INTERVAL ? "" :
    QString(tr("  Warning: last block was received %1 hours %2 minutes ago")).arg(blockAge / MSECS_IN_HOUR).arg(blockAge % MSECS_IN_HOUR / MSECS_IN_MINUTE);
  Q_EMIT walletStateChangedSignal(QString(tr("Wallet synchronized. Height: %1  |  Time (UTC): %2%3")).
    arg(NodeAdapter::instance().getLastLocalBlockHeight()).
    arg(QLocale(QLocale::English).toString(blockTime, "dd.MM.yyyy, HH:mm:ss")).
    arg(warningString));

  QTimer::singleShot(LAST_BLOCK_INFO_UPDATING_INTERVAL, this, SLOT(updateBlockStatusText()));
}

void WalletAdapter::updateBlockStatusTextWithDelay() {
  QTimer::singleShot(5000, this, SLOT(updateBlockStatusText()));
}

bool WalletAdapter::isTrackingWallet() const {
  Q_CHECK_PTR(m_wallet);
  try {
    return m_wallet->isTrackingWallet();
  } catch (std::system_error&) {
  }
  return false;
}

QString WalletAdapter::getMnemonicSeed(QString _language, WId _parentWindow) const {
  Q_UNUSED(_language);
  if (isYubiKeyProtected()) {
    CryptoPQ::SeedMaster seed{};
    Tools::SecretLock scrub(seed.data(), seed.size());
    QString error;
    QWidget* parent = _parentWindow == 0 ? QApplication::activeWindow() : QWidget::find(_parentWindow);
    const WId parentWindow = _parentWindow != 0 ? _parentWindow : (parent == nullptr ? 0 : parent->winId());
    if (!unlockYubiKeySeed(parentWindow, seed, error)) {
      QMessageBox::critical(parent, tr("Failed to reveal spend seed"), error, QMessageBox::Ok);
      return QString();
    }
    Crypto::SecretKey secret{};
    Tools::SecretLock scrubSecret(secret.data, sizeof(secret.data));
    std::copy(seed.begin(), seed.end(), secret.data);
    std::string words;
    std::string language = "English";
    Crypto::ElectrumWords::bytes_to_words(secret, words, language);
    return QString::fromStdString(words);
  }
  if (isTrackingWallet()) {
    return "Wallet is watch-only and has no seed";
  }

  Q_CHECK_PTR(m_wallet);
  std::string electrum_words;
  if (!m_wallet->getSeed(electrum_words)) {
    return "This wallet has no seed";
  }
  return QString::fromStdString(electrum_words);
}

CryptoNote::AccountKeys WalletAdapter::getKeysFromMnemonicSeed(QString& _seed) const {
  CryptoNote::AccountKeys keys;
  std::string seedLanguage;
  if (!Crypto::ElectrumWords::words_to_bytes(_seed.toStdString(), keys.spendSecretKey, seedLanguage)) {
    QMessageBox::critical(nullptr, tr("Mnemonic seed is not correct"), tr("There must be an error in mnemonic seed. Make sure you entered it correctly."), QMessageBox::Ok);
  }
  return keys;
}

QString WalletAdapter::signMessage(const QString &data) {
  Q_CHECK_PTR(m_wallet);
  if (isYubiKeyProtected()) {
    CryptoPQ::SeedMaster seed{};
    Tools::SecretLock scrub(seed.data(), seed.size());
    QString error;
    QWidget* parent = QApplication::activeWindow();
    if (!unlockYubiKeySeed(parent == nullptr ? 0 : parent->winId(), seed, error)) {
      QMessageBox::critical(nullptr, tr("Failed to sign message"), error, QMessageBox::Ok);
      return QString();
    }
    auto* wallet = dynamic_cast<CryptoNote::WalletLegacy*>(m_wallet);
    if (wallet == nullptr) return QString();
    const std::string signature = wallet->signMessageWithSeed(seed, data.toStdString());
    return QString::fromUtf8(signature.data(), signature.size());
  }
  if (isTrackingWallet()) {
    QMessageBox::critical(nullptr, tr("Failed to sign message"), tr("This is a watch-only wallet. The message can be signed only by a full wallet."), QMessageBox::Ok);
    return QString();
  }

  std::string sig_str = m_wallet->sign_message(data.toStdString());
  return QString::fromUtf8(sig_str.data(), sig_str.size());
}

QString WalletAdapter::getSpendSeedHex() const {
  Q_CHECK_PTR(m_wallet);
  if (isYubiKeyProtected()) {
    CryptoPQ::SeedMaster seed{};
    Tools::SecretLock scrub(seed.data(), seed.size());
    QString error;
    QWidget* parent = QApplication::activeWindow();
    if (!unlockYubiKeySeed(parent == nullptr ? 0 : parent->winId(), seed, error)) {
      QMessageBox::critical(parent, tr("Failed to reveal spend seed"), error, QMessageBox::Ok);
      return QString();
    }
    return QString::fromStdString(Common::toHex(seed.data(), seed.size()));
  }

  CryptoNote::AccountKeys keys;
  if (!const_cast<WalletAdapter*>(this)->getAccountKeys(keys) ||
      keys.spendSecretKey == CryptoNote::NULL_SECRET_KEY) {
    return QString();
  }
  Tools::SecretLock scrub(keys.spendSecretKey.data, sizeof(keys.spendSecretKey.data));
  return QString::fromStdString(Common::podToHex(keys.spendSecretKey));
}

bool WalletAdapter::verifyMessage(const QString &data, const QString &_destination, const QString &signature) {
  Q_CHECK_PTR(m_wallet);

  CryptoPQ::KemPublicKey viewPub;
  CryptoPQ::DsaPublicKey spendPub;
  uint64_t subaddrIndexT = 0;
  if (!CryptoNote::resolvePqRecipient(*NodeAdapter::instance().getNode(), CurrencyAdapter::instance().isTestnet(),
                                      _destination.toStdString(), viewPub, spendPub, subaddrIndexT)) {
    return false;
  }

  return CryptoNote::verifyMessagePq(data.toStdString(), spendPub, signature.toStdString());
}

size_t WalletAdapter::getUnlockedOutputsCount() {
  Q_CHECK_PTR(m_wallet);
  try {
    return m_wallet->getUnlockedOutputsCount();
  } catch (std::system_error&) {
    return 0;
  }
}

}
