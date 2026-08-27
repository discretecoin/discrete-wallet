// Copyright (c) 2011-2016 The Cryptonote developers
// Copyright (c) 2015-2016 XDN developers
// Copyright (c) 2016-2026 The Karbo developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include <QByteArray>
#include <QMutex>
#include <QObject>
#include <QTime>
#include <QTimer>
#include <QPushButton>
#include <QStringList>
#include <QtGui/qwindowdefs.h>

#include <list>
#include <vector>
#include <atomic>
#include <fstream>
#include <thread>

#include <boost/program_options.hpp>

#include <memory>
#include <mutex>
#include <IWalletLegacy.h>
#include "System/Dispatcher.h"
#include "Wallet/WalletRpcServer.h"
#include "Wallet/PqWallet.h"
#include "Wallet/SentPaymentsStore.h"
#include "crypto_pq/PqKem.h"
#include "crypto_pq/PqDsa.h"

namespace WalletGui {

class ITransfersContainer;
struct YubiKeySeedMetadata;

class WalletAdapter : public QObject, public CryptoNote::IWalletLegacyObserver {
  Q_OBJECT
  Q_DISABLE_COPY(WalletAdapter)

public:
  static WalletAdapter& instance();

  void open(const QString& _password);
  void createWallet();
  void createWithKeys(const CryptoNote::AccountKeys& _keys);
  void createWithKeys(const CryptoNote::AccountKeys& _keys, const quint32 _sync_heigth);
  void createTrackingWallet(const CryptoNote::AccountKeys& _keys, const CryptoNote::PqTrackingKeys& _trackingKeys);
  void createTrackingWallet(const CryptoNote::AccountKeys& _keys, const CryptoNote::PqTrackingKeys& _trackingKeys, const quint32 _sync_heigth);
  void close();
  bool save(bool _details, bool _cache);
  void backup(const QString& _file);
  void autoBackup();
  void rescan();
  void reset();
  bool isResetInProgress() const;
  bool canRebuildWallet() const;
  void waitForRebuildWorker();

  QString getAddress() const;
  quint64 getActualBalance() const;
  quint64 getPendingBalance() const;
  quint64 getTransactionCount() const;
  quint64 getTransferCount() const;
  bool getTransaction(CryptoNote::TransactionId& _id, CryptoNote::WalletLegacyTransaction& _transaction);
  bool getTransfer(CryptoNote::TransferId& _id, CryptoNote::WalletLegacyTransfer& _transfer);
  // Payer-side recipients + off-chain payment proofs recorded for a sent transaction
  // (empty/false for incoming or pre-feature sends). Concrete WalletLegacy state, so
  // this reaches through to it; copies out so the caller never touches wallet internals.
  bool getPaymentProofs(const Crypto::Hash& _txid, CryptoNote::SentPaymentRecord& _record) const;
  bool getAccountKeys(CryptoNote::AccountKeys& _keys);
  size_t getUnlockedOutputsCount();

  std::vector<CryptoNote::TransactionOutputInformation> getOutputs();
  std::vector<CryptoNote::TransactionOutputInformation> getLockedOutputs();
  std::vector<CryptoNote::TransactionOutputInformation> getUnlockedOutputs();
  std::vector<CryptoNote::TransactionSpentOutputInformation> getSpentOutputs();

  void sendTransaction(const std::vector<CryptoNote::WalletLegacyTransfer>& _transfers, quint64 _fee);
  void sendTransactionWithSeed(const CryptoPQ::SeedMaster& _seedMaster,
                               const std::vector<CryptoNote::WalletLegacyTransfer>& _transfers,
                               quint64 _fee);
  QString prepareRawTransaction(const std::vector<CryptoNote::WalletLegacyTransfer>& _transfers,
                                quint64 _fee, QString* _errorText = nullptr);
  QString prepareRawTransactionWithSeed(const CryptoPQ::SeedMaster& _seedMaster,
                                        const std::vector<CryptoNote::WalletLegacyTransfer>& _transfers,
                                        quint64 _fee, QString* _errorText = nullptr);

  enum class AccountRegistrationMode {
    Free,
    Paid
  };

  // Registers this wallet's PQ identity on chain as a short H-I-A-C account
  // number. Free mode submits a zero-fee TX_FREE_REG with anti-spam PoW; paid
  // mode submits a normal fee-paying TX_PQ carrying the registration tag.
  void registerAccountNumber(AccountRegistrationMode _mode);

  // This wallet's own PQ identity, hex-encoded (view pub, spend pub) — used to
  // look up its account-number registration and to derive mining keys.
  bool getOwnPqIdentityHex(QString& _viewPubHex, QString& _spendPubHex) const;

  // The PQ identity used to mine: the same derivation the daemon's
  // start_mining path uses (CryptoNote::deriveMinerPqKeys), so a block mined by
  // this wallet is signed with the matching spend secret.
  bool getMiningKeys(CryptoPQ::KemPublicKey& _viewPub, CryptoPQ::DsaPublicKey& _spendPub, CryptoPQ::DsaSecretKey& _spendSk) const;

  bool isOpen() const;
  bool hasYubiKeyMetadata() const;
  bool isYubiKeyProtected() const;
  int yubiKeyCount() const;
  QStringList yubiKeyBypassFiles() const;
  bool removeYubiKeyBypassFiles(QStringList& _removedFiles,
                                QString& _errorText);
  bool enableYubiKeyProtection(WId _parentWindow, const QString& _label,
                               const QString& _walletPassword,
                               QString& _protectedBackupPath,
                               QStringList& _removedBypassFiles,
                               QString& _warningText,
                               QString& _errorText);
  bool addYubiKeyProtectionKey(WId _parentWindow, const QString& _label,
                              QString& _errorText);
  bool unlockYubiKeySeed(WId _parentWindow, CryptoPQ::SeedMaster& _seedMaster,
                        QString& _errorText,
                        const QString& _authorizationPurpose = QString(),
                        QString* _usedKeyLabel = nullptr) const;

  bool changePassword(const QString& _old_pass, const QString& _new_pass);
  void setWalletFile(const QString& _path);

  QString signMessage(const QString &data);
  QString getSpendSeedHex() const;
  // _destination accepts either a raw bech32m PQ address or an account number
  // (H-I-A-C / H-I-A-T-C); it is resolved to a spend public key the same way the
  // send path resolves a destination (see PqRecipient.h).
  bool verifyMessage(const QString &data, const QString &_destination, const QString &signature);

  void initCompleted(std::error_code _result) Q_DECL_OVERRIDE;
  void saveCompleted(std::error_code _result) Q_DECL_OVERRIDE;
  void synchronizationProgressUpdated(uint32_t _current, uint32_t _total) Q_DECL_OVERRIDE;
  void synchronizationCompleted(std::error_code _error) Q_DECL_OVERRIDE;
  void actualBalanceUpdated(uint64_t _actual_balance) Q_DECL_OVERRIDE;
  void pendingBalanceUpdated(uint64_t _pending_balance) Q_DECL_OVERRIDE;
  void externalTransactionCreated(CryptoNote::TransactionId _transaction_id) Q_DECL_OVERRIDE;
  void sendTransactionCompleted(CryptoNote::TransactionId _transaction_id, std::error_code _result) Q_DECL_OVERRIDE;
  void transactionUpdated(CryptoNote::TransactionId _transaction_id) Q_DECL_OVERRIDE;

  bool isTrackingWallet() const;
  QString getMnemonicSeed(QString _language, WId _parentWindow = 0) const;
  CryptoNote::AccountKeys getKeysFromMnemonicSeed(QString& _seed) const;
  bool tryOpen(const QString& _password);

private:
  std::fstream m_file;
  CryptoNote::IWalletLegacy* m_wallet;
  Tools::wallet_rpc_server* m_wallet_rpc;
  std::unique_ptr<System::Dispatcher> m_rpcDispatcher;
  QMutex m_mutex;
  std::atomic<bool> m_isBackupInProgress;
  std::atomic<quint64> m_saveGeneration;
  std::atomic<quint64> m_activeSaveGeneration;
  std::atomic<bool> m_isRebuildInProgress;
  std::atomic<bool> m_discardUncommittedRebuild;
  std::atomic<int> m_rebuildResult;
  std::mutex m_rebuildResultMutex;
  QString m_rebuildErrorText;
  bool m_rebuildInitObserved;
  bool m_rebuildCanResumeExistingWallet;
  int m_rebuildInitResult;
  QString m_rebuildInitErrorText;
  std::thread m_rebuildWorker;
  std::atomic<bool> m_isSynchronized;
  std::atomic<quint64> m_lastWalletTransactionId;
  QTimer m_newTransactionsNotificationTimer;
  QTimer* m_dispatcherTimer = nullptr;
  QPushButton* m_closeButton;
  Logging::LoggerRef m_logger;
  uint32_t m_syncSpeed;
  uint32_t m_syncPeriod;
  struct PerfType { uint32_t height; QTime time; };
  std::vector<PerfType> m_perfData;
  QString m_pendingYubiKeySidecarRemoval;
  QByteArray m_pendingYubiKeySidecarData;

  boost::program_options::variables_map m_wrpcOptions;

  WalletAdapter();
  ~WalletAdapter();

  void onWalletInitCompleted(int _error, const QString& _error_text);
  void onWalletSendTransactionCompleted(CryptoNote::TransactionId _transaction_id, int _error, const QString& _error_text);

  bool save(const QString& _file, bool _details, bool _cache,
            quint64* _saveGeneration = nullptr, bool _backupMode = false,
            bool _waitForFile = true);
  bool saveAndWait(const QString& _file, bool _details, bool _cache,
                   bool _backupMode, QString& _errorText);
  bool verifyProtectedWalletSnapshot(
      const QString& _file, const QString& _password,
      const CryptoNote::PqTrackingKeys& _expectedTracking,
      const QByteArray& _expectedMetadata, QString& _errorText);
  bool loadYubiKeyMetadata(YubiKeySeedMetadata& _metadata,
                           QString& _errorText) const;
  bool storeEmbeddedYubiKeyMetadata(const YubiKeySeedMetadata& _metadata,
                                    QString& _errorText,
                                    QByteArray* _serialized = nullptr);
  QByteArray embeddedYubiKeyMetadata() const;
  void migrateLegacyYubiKeySidecar();
  void removeMigratedYubiKeySidecar();
  void lock();
  void unlock();
  bool openFile(const QString& _file, bool _read_only,
                bool _waitForLock = true);
  void closeFile();
  void rebuildWallet(bool _destructive);
  void completeResetAfterWalletRebuild();
  void notifyAboutLastTransaction();
  QString walletErrorMessage(int _error_code);
  void runWalletRpc();
  void stopWalletRpc();
  void doRegisterAccountNumber(AccountRegistrationMode _mode);
  void sendTransactionImpl(const CryptoPQ::SeedMaster* _seedMaster,
                           const std::vector<CryptoNote::WalletLegacyTransfer>& _transfers,
                           quint64 _fee);
  QString prepareRawTransactionImpl(const CryptoPQ::SeedMaster* _seedMaster,
                                    const std::vector<CryptoNote::WalletLegacyTransfer>& _transfers,
                                    quint64 _fee, QString* _errorText);

  static bool renameFile(const QString& _old_name, const QString& _new_name);
  Q_SLOT void updateBlockStatusText();
  Q_SLOT void updateBlockStatusTextWithDelay();

Q_SIGNALS:
  void walletInitCompletedSignal(int _error, const QString& _error_text);
  void walletCloseCompletedSignal();
  void walletSaveCompletedSignal(int _error, const QString& _error_text);
  void walletSaveCompletedGenerationSignal(quint64 _generation, int _error,
                                           const QString& _error_text);
  void walletRebuildCompletedSignal(int _error, const QString& _error_text);
  void walletSynchronizationProgressUpdatedSignal(quint64 _current, quint64 _total);
  void walletSynchronizationCompletedSignal(int _error, const QString& _error_text);
  void walletActualBalanceUpdatedSignal(quint64 _actual_balance);
  void walletPendingBalanceUpdatedSignal(quint64 _pending_balance);
  void walletTransactionCreatedSignal(CryptoNote::TransactionId _transaction_id);
  void walletSendTransactionCompletedSignal(CryptoNote::TransactionId _transaction_id, int _error, const QString& _error_text);
  void walletTransactionUpdatedSignal(CryptoNote::TransactionId _transaction_id);
  void accountRegistrationCompletedSignal(int _error, const QString& _error_text, const QString& _transaction_hash);
  void walletStateChangedSignal(const QString &_state_text);

  void openWalletWithPasswordSignal(bool _error);
  void changeWalletPasswordSignal();
  void updateWalletAddressSignal(const QString& _address);
  void reloadWalletTransactionsSignal();
  void updateBlockStatusTextSignal();
  void updateBlockStatusTextWithDelaySignal();
};

}
