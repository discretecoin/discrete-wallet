// Copyright (c) 2012-2016, The CryptoNote developers, The Bytecoin developers
// Copyright (c) 2016-2025, The Karbo developers
// Copyright (c) 2026, The Discrete developers
//
// This file is part of Karbo.
//
// Karbo is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Karbo is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with Karbo.  If not, see <http://www.gnu.org/licenses/>.

#include "Miner.h"

#include "NodeAdapter.h"
#include "WalletAdapter.h"
#include "crypto_pq/PqKem.h"
#include "crypto_pq/PqDsa.h"

#undef ERROR

namespace WalletGui {

Miner::Miner(QObject* _parent, Logging::ILogger& log) :
  QObject(_parent),
  m_logger(log, "Miner") {
}

Miner::~Miner() {
  stop();
}

bool Miner::is_mining() {
  return NodeAdapter::instance().isMining();
}

bool Miner::start(size_t threads_count) {
  if (is_mining()) {
    m_logger(Logging::DEBUGGING) << "Starting miner but it's already started";
    Q_EMIT miningErrorSignal(tr("Miner is already running"));
    return false;
  }

  if (NodeAdapter::instance().getPeerCount() == 0) {
    const QString errorMessage = tr("Mining was not started because there are no connected peers");
    m_logger(Logging::WARNING) << errorMessage.toStdString();
    Q_EMIT minerMessageSignal(errorMessage);
    Q_EMIT miningErrorSignal(errorMessage);
    return false;
  }

  CryptoPQ::KemPublicKey viewPub;
  CryptoPQ::DsaPublicKey spendPub;
  CryptoPQ::DsaSecretKey spendSk;
  if (!WalletAdapter::instance().getMiningKeys(viewPub, spendPub, spendSk)) {
    m_logger(Logging::ERROR) << "Unable to start miner because account keys are unavailable";
    Q_EMIT miningErrorSignal(tr("Unable to start miner because account keys are unavailable"));
    return false;
  }

  if (!NodeAdapter::instance().startMining(viewPub, spendPub, spendSk, threads_count)) {
    m_logger(Logging::ERROR) << "Failed to start mining";
    Q_EMIT miningErrorSignal(tr("Unable to start miner because there are active mining threads"));
    return false;
  }

  m_threads_total = threads_count;

  const quint64 difficulty = NodeAdapter::instance().getDifficulty();
  m_logger(Logging::INFO) << "Mining has started with " << threads_count << " thread(s), at difficulty " << difficulty << " good luck!";
  Q_EMIT minerMessageSignal(
      tr("Mining has started with %n thread(s) at difficulty %1, good luck!", nullptr, static_cast<int>(threads_count))
          .arg(difficulty));
  Q_EMIT minerStartedSignal(static_cast<quint32>(threads_count), difficulty);
  return true;
}

bool Miner::set_thread_count(size_t threads_count) {
  if (threads_count == 0) {
    threads_count = 1;
  }

  if (threads_count == m_threads_total) {
    return true;
  }

  // Core's built-in miner takes its thread count at startPq() time; there is
  // no live resize, so changing it means a stop+restart.
  const bool wasMining = is_mining();
  if (wasMining) {
    NodeAdapter::instance().stopMining();
  }

  m_threads_total = threads_count;

  if (wasMining) {
    CryptoPQ::KemPublicKey viewPub;
    CryptoPQ::DsaPublicKey spendPub;
    CryptoPQ::DsaSecretKey spendSk;
    if (!WalletAdapter::instance().getMiningKeys(viewPub, spendPub, spendSk) ||
        !NodeAdapter::instance().startMining(viewPub, spendPub, spendSk, threads_count)) {
      Q_EMIT miningErrorSignal(tr("Failed to change mining thread count"));
      return false;
    }
  }

  m_logger(Logging::INFO) << "Mining thread count changed to " << threads_count;
  Q_EMIT minerMessageSignal(tr("Mining thread count changed to %n thread(s)", nullptr, static_cast<int>(threads_count)));
  Q_EMIT minerThreadsChangedSignal(static_cast<quint32>(threads_count));
  return true;
}

double Miner::get_speed() {
  return is_mining() ? static_cast<double>(NodeAdapter::instance().getHashRate()) : 0;
}

void Miner::merge_hr() {
  // No-op: Core's miner tracks its own hash rate; get_speed() reads it directly.
}

bool Miner::stop() {
  const bool wasMining = is_mining();
  const quint32 threadsCount = static_cast<quint32>(m_threads_total);
  NodeAdapter::instance().stopMining();
  m_threads_total = 0;

  if (!wasMining) {
    return true;
  }

  m_logger(Logging::INFO) << "Mining stopped, " << threadsCount << " thread(s) finished";
  Q_EMIT minerMessageSignal(tr("Mining stopped, %n thread(s) finished", nullptr, static_cast<int>(threadsCount)));
  Q_EMIT minerStoppedSignal(threadsCount);
  return true;
}

bool Miner::on_block_chain_update() {
  if (!is_mining()) {
    return true;
  }

  if (NodeAdapter::instance().getPeerCount() == 0) {
    const QString errorMessage = tr("Mining stopped because there are no connected peers");
    m_logger(Logging::WARNING) << errorMessage.toStdString();
    Q_EMIT minerMessageSignal(errorMessage);
    Q_EMIT miningErrorSignal(errorMessage);
    stop();
    return false;
  }

  const quint64 height = NodeAdapter::instance().getLastLocalBlockHeight();
  const quint64 difficulty = NodeAdapter::instance().getDifficulty();
  Q_EMIT minerTemplateUpdatedSignal(height, difficulty);
  return true;
}

}
