// Copyright (c) 2011-2016 The Cryptonote developers
// Copyright (c) 2015-2016 XDN developers
// Copyright (c) 2016-2022 The Karbowanec developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include <QDateTime>
#include <QFrame>
#include <QList>
#include <QStringList>
#include <QTimer>
#include "qcustomplot.h"
#include "Miner.h"
#include <Logging/LoggerMessage.h>

class QAbstractButton;
class QEvent;

namespace Ui {
class MiningFrame;
}

class Miner;

namespace WalletGui {

class LogFileWatcher;

class MiningFrame : public QFrame {
  Q_OBJECT

public:
  MiningFrame(QWidget* _parent);
  ~MiningFrame();

  void addPoint(double x, double y);
  void plot();

  bool isSoloRunning() const;
  void stopMiningForShutdown();

protected:
  void changeEvent(QEvent* _event) override;
  void timerEvent(QTimerEvent* _event) Q_DECL_OVERRIDE;

private:
  QScopedPointer<Ui::MiningFrame> m_ui;
  int m_soloHashRateTimerId;
  int m_minerRoutineTimerId;
  QVector<double> m_hX, m_hY, m_averageHashRateY;
  QVector<double> m_difficultyX, m_difficultyY;
  QList<QCPItemLine*> m_hashRateEventMarkers;
  QList<bool> m_hashRateEventMarkerHighlights;
  std::unique_ptr<Miner> m_miner;
  LogFileWatcher* m_coreLogWatcher = nullptr;
  QString m_miner_log;
  QStringList m_event_log;
  QTimer m_threadResizeTimer;
  int m_pendingMiningThreads = 0;

  void initCpuCoreList();
  void startSolo();
  void stopSolo(bool _stoppedByNoPeers = false);
  // Resume solo mining that was suspended because peers dropped, once the node is
  // reconnected and synced. Mirrors the CLI daemon, whose built-in miner
  // auto-restarts on re-synchronization; safe to call from any reconnect/sync
  // event as it no-ops unless a resume is actually due.
  void tryResumeAfterNoPeers();
  bool nodeSynchronized() const;

  bool m_wallet_closed = false;
  bool m_solo_mining = false;
  bool m_sychronized = false;
  bool m_mining_was_stopped = false;
  bool m_miningStoppedByNoPeers = false;
  QDateTime m_sessionStartedAt;
  double m_sessionTotalHashes = 0;
  double m_roundHashes = 0;
  double m_sessionPeakHashRate = 0;
  double m_lastAnnouncedPeakHashRate = 0;
  double m_lastHashRate = 0;
  double m_currentDifficulty = 0;

  // Lifetime / persistent mining stats, derived from the wallet's own coinbase
  // (mined) transactions so they survive restarts and reinstalls without any
  // extra persisted state. Scanned incrementally (see scanLifetimeStats), never
  // on the 1s hash-rate timer.
  quint64 m_lifetimeBlocks = 0;
  quint64 m_lifetimeReward = 0;          // atomic units
  bool m_lifetimeStatsReady = false;     // baseline captured after the first scan
  quint64 m_lifetimeScanCursor = 0;      // next wallet transaction id to scan
  QList<qint64> m_recentBlockTimes;      // epoch seconds of coinbase txns (24h/7d)
  quint64 m_sessionBlocks = 0;           // blocks found in the current session
  QDateTime m_lastBlockFoundAt;          // anchor for the expected-time progress bar

  void applyChartPalette();
  void initDifficultyChart();
  void initHashRateChartItems();
  void updateDifficulty(quint64 _difficulty);
  void plotDifficulty();
  void addHashRateEventMarker(bool _highlight);
  void appendMiningEvent(const QString& _kind, const QString& _message);
  void appendRawLogLine(const QString& _line);
  void resetSessionStats();
  void updateSessionStats();
  void resetLifetimeStats();
  void scanLifetimeStats(bool _announceNewBlocks);
  void onBlockFoundByMiner(quint64 _reward);
  void updateForecast();
  QString formatExpectedDuration(double _seconds) const;
  void updateCpuIntensity();
  void applyCpuPreset(double _fraction);
  void setMiningStatusBadge(const QString& _text, const QString& _backgroundColor, const QString& _textColor);
  void scheduleMiningThreadsChange(int _threads);
  void applyPendingMiningThreads();
  void walletOpened();
  void walletClosed();
  quint32 getHashRate() const;
  double m_maxHr = 10;
  QCPItemStraightLine* m_peakHashRateLine = nullptr;

  Q_SLOT void startStopSoloClicked(QAbstractButton* _button);
  Q_SLOT void enableSolo();
  Q_SLOT void setMiningThreads();
  Q_SLOT void onBlockHeightUpdated(quint64 _height);
  Q_SLOT void onPeerCountUpdated(quintptr _count);
  Q_SLOT void onSynchronizationCompleted();
  Q_SLOT void updateBalance(quint64 _balance);
  Q_SLOT void updatePendingBalance(quint64 _balance);
  Q_SLOT void onWalletTransactionsChanged();
  Q_SLOT void onWalletTransactionsReset();
  Q_SLOT void updateMinerLog(const QString& _message);
  Q_SLOT void updateCoreLog(const QString& _message);
  Q_SLOT void onMinerStarted(quint32 _threads, quint64 _difficulty);
  Q_SLOT void onMinerStopped(quint32 _threads);
  Q_SLOT void onMinerThreadsChanged(quint32 _threads);
  Q_SLOT void onMinerTemplateUpdated(quint64 _height, quint64 _difficulty);
  Q_SLOT void onMinerError(const QString& _message);
  Q_SLOT void coreDealTurned(int _cores);
  Q_SLOT void poolChanged();
};

}
