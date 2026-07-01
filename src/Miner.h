// Copyright (c) 2012-2016, The CryptoNote developers, The Bytecoin developers
// Copyright (c) 2016-2022, The Karbo developers
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

#pragma once

#include <QObject>
#include <QString>

#include "Logging/LoggerRef.h"

namespace WalletGui {

// Thin Qt-facing wrapper around the in-process node's built-in PQ miner
// (Core::get_miner(), reached through Node::startMining/stopMining/isMining/
// getHashRate — see CryptoNoteWrapper.h and CryptoNoteCore/Miner.cpp). Unlike
// the old ECC-era miner, the nonce search, threading and per-block ML-DSA
// signing all happen inside Core itself; this class only starts/stops it and
// polls its state, but keeps the same public surface and signals the GUI
// (MiningFrame) already used, so the rest of the mining UI is unaffected.
//
// One real behavior change: Core's internal miner does not report individual
// found-block events back up to the GUI, so blockFoundSignal no longer fires.
// Faking it from a "local height advanced while mining" heuristic would be
// actively misleading (it can't tell a self-mined block from a peer's), so
// that feature is cut rather than approximated — see MiningFrame for the
// corresponding UI removal.
class Miner : public QObject {
  Q_OBJECT

public:
  Miner(QObject* _parent, Logging::ILogger& log);
  ~Miner();

  bool on_block_chain_update();
  bool start(size_t threads_count);
  bool set_thread_count(size_t threads_count);
  double get_speed();
  bool stop();
  bool is_mining();
  void merge_hr();

private:
  size_t m_threads_total = 0;
  Logging::LoggerRef m_logger;

Q_SIGNALS:
  void minerMessageSignal(const QString& _message);
  void minerStartedSignal(quint32 _threads, quint64 _difficulty);
  void minerStoppedSignal(quint32 _threads);
  void minerThreadsChangedSignal(quint32 _threads);
  void minerTemplateUpdatedSignal(quint64 _height, quint64 _difficulty);
  void miningErrorSignal(const QString& _message);

};
}
