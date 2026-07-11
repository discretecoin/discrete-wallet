// Copyright (c) 2011-2015 The Cryptonote developers
// Copyright (c) 2016-2022 The Karbowanec developers
// Copyright (c) 2026 The Discrete developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <system_error>
#include <INode.h>
#include <Logging/LoggerRef.h>
#include <Rpc/RpcServerConfig.h>
#include <System/Dispatcher.h>
#include <crypto_pq/PqKem.h>
#include <crypto_pq/PqDsa.h>

namespace CryptoNote {

class INode;
class IWalletLegacy;
class Currency;
class CoreConfig;
class NetNodeConfig;
class RpcServerConfig;

}

namespace Logging {
  class LoggerManager;
}

namespace WalletGui {

enum class NodeType {
  UNKNOWN, IN_PROCESS, RPC
};

class Node {
public:
  virtual ~Node() = 0;
  virtual void init(const std::function<void(std::error_code)>& callback) = 0;
  virtual void deinit() = 0;

  virtual uint64_t getLastKnownBlockHeight() const = 0;
  virtual uint64_t getLastLocalBlockHeight() const = 0;
  virtual uint64_t getLastLocalBlockTimestamp() const = 0;
  virtual uint64_t getPeerCount() = 0;
  virtual uint64_t getDifficulty() = 0;
  virtual uint64_t getNextReward() = 0;
  virtual uint64_t getTxCount() = 0;
  virtual uint64_t getTxPoolSize() = 0;
  virtual uint64_t getAltBlocksCount() = 0;
  // First-seen finality: true while the node has ignored a deeper competing chain
  // (usually a brief connectivity hiccup that left it on a side chain).
  virtual bool isFinalityForkActive() = 0;
  virtual uint64_t getConnectionsCount() = 0;
  virtual uint64_t getOutgoingConnectionsCount() = 0;
  virtual uint64_t getIncomingConnectionsCount() = 0;
  virtual uint64_t getWhitePeerlistSize() = 0;
  virtual uint64_t getGreyPeerlistSize() = 0;
  virtual uint64_t getMinimalFee() = 0;
  virtual uint8_t getCurrentBlockMajorVersion() = 0;
  virtual uint64_t getAlreadyGeneratedCoins() = 0;
  virtual CryptoNote::BlockHeaderInfo getLastLocalBlockHeaderInfo() = 0;
  virtual std::vector<CryptoNote::p2pConnection> getConnections() = 0;

  // Built-in (in-process) mining. Only the in-process node can mine: it shares
  // the GUI's address space, so it can hold the wallet's derived ML-DSA spend
  // secret in memory for the lifetime of the mining session and sign each
  // candidate block itself (mining is identity-bound — the coinbase reward is
  // only spendable by the same key that signed the block). An RPC-connected
  // remote node has no such access and reports mining as unsupported.
  virtual bool startMining(const CryptoPQ::KemPublicKey& viewPub, const CryptoPQ::DsaPublicKey& spendPub,
                           const CryptoPQ::DsaSecretKey& spendSk, size_t threadCount) = 0;
  virtual bool stopMining() = 0;
  virtual bool isMining() = 0;
  virtual uint64_t getHashRate() = 0;

  virtual NodeType getNodeType() const = 0;

  virtual CryptoNote::IWalletLegacy* createWallet() = 0;

  virtual CryptoNote::INode* getNode() = 0;
  virtual System::Dispatcher& getDispatcher() = 0;

  // PQ account-number registry (see include/AccountNumber.h and
  // src/Wallet/PqRecipient.h). getPqAccount: a full PQ identity (view+spend
  // public keys, hex) -> its on-chain registration coordinates, if registered.
  // resolvePqAccount: coordinates -> the registered view+spend public keys.
  virtual void getPqAccount(const std::string& viewPubHex, const std::string& spendPubHex, bool& registered,
                            uint32_t& blockHeight, uint32_t& txIndex, const std::function<void(std::error_code)>& callback) = 0;
  virtual void resolvePqAccount(uint32_t blockHeight, uint32_t txIndex, bool& found,
                                std::string& viewPubHex, std::string& spendPubHex, const std::function<void(std::error_code)>& callback) = 0;

};

class INodeCallback {
public:
  virtual void peerCountUpdated(Node& node, size_t count) = 0;
  virtual void localBlockchainUpdated(Node& node, uint64_t height) = 0;
  virtual void lastKnownBlockHeightUpdated(Node& node, uint64_t height) = 0;
  virtual void connectionStatusUpdated(bool _connected) = 0;
  virtual void poolChanged(Node& node) = 0;
  virtual void blockFoundByMiner(Node& node, uint64_t reward) = 0;
};

Node* createRpcNode(const CryptoNote::Currency& currency, INodeCallback& callback, Logging::LoggerManager& logManager, const std::string& nodeHost, unsigned short nodePort, bool enableSSL);
Node* createInprocessNode(const CryptoNote::Currency& currency, Logging::LoggerManager& logManager,
  const CryptoNote::CoreConfig& coreConfig, const CryptoNote::NetNodeConfig& netNodeConfig, const CryptoNote::RpcServerConfig& rpcServerConfig, INodeCallback& callback);

}
