// Copyright (c) 2016-2020 The Karbowanec developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <iostream>
#include <QRegularExpression>
#include <QHeaderView>
#include <QMessageBox>
#include "ui_connectionsettingsdialog.h"
#include "ConnectionSettings.h"
#include "CurrencyAdapter.h"
#include "NewNodeDialog.h"
#include "MainWindow.h"
#include "NodeModel.h"

namespace Ui {
class ConnectionSettingsDialog;
}

namespace WalletGui {

void RemoteNodesView::showEvent(QShowEvent *e) {
  if (e->type() == QShowEvent::Show)
    this->selectRow(this->currentIndex().row());
}

void RemoteNodesDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const {
  QStyleOptionViewItem cellOption = option;
  cellOption.state &= QStyle::State_Active;
  QItemDelegate::paint(painter, cellOption, index);
}

QSize RemoteNodesDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const {
  return QItemDelegate::sizeHint(option, index);
}

ConnectionSettingsDialog::ConnectionSettingsDialog(QWidget *_parent) : QDialog(_parent),
                                                       m_nodeModel(new NodeModel(this)),
                                                       m_ui(new Ui::ConnectionSettingsDialog),
                                                       m_nodesCurrentIndex(0) {
  m_ui->setupUi(this);
  m_remoteNodesView = new RemoteNodesView(this);
  setupRemoteNodesView(m_remoteNodesView);
  m_ui->remoteNodesComboBox->setModel(m_nodeModel);
  m_ui->remoteNodesComboBox->setView(m_remoteNodesView);
  m_ui->remoteNodesComboBox->setModelColumn(1);
  m_ui->remoteNodesComboBox->view()->selectionModel()->hasSelection();
  m_ui->remoteNodesComboBox->setItemDelegate(new RemoteNodesDelegate);
  connect(m_ui->remoteNodesComboBox, SIGNAL(currentIndexChanged(int)), this, SLOT(nodesCurrentIndex(int)));
  connect(m_ui->radioButton_1, &QRadioButton::toggled, this, &ConnectionSettingsDialog::connectionModeChanged);
  connect(m_ui->radioButton_2, &QRadioButton::toggled, this, &ConnectionSettingsDialog::connectionModeChanged);
  connect(m_ui->radioButton_3, &QRadioButton::toggled, this, &ConnectionSettingsDialog::connectionModeChanged);
  connect(m_ui->radioButton_4, &QRadioButton::toggled, this, &ConnectionSettingsDialog::connectionModeChanged);
  updateTrustState();
}

ConnectionSettingsDialog::~ConnectionSettingsDialog() {
}

void ConnectionSettingsDialog::setupRemoteNodesView(QTableView *view) {
  view->setSelectionMode(QAbstractItemView::SingleSelection);
  view->setSelectionBehavior(QAbstractItemView::SelectRows); 
  view->setColumnWidth(0, 30);
  view->horizontalHeader()->setStretchLastSection(true);
  view->verticalHeader()->setStretchLastSection(true);
  view->horizontalHeader()->hide();
  view->verticalHeader()->hide();
  view->setShowGrid(false);
}

void ConnectionSettingsDialog::nodesCurrentIndex(int currentIndex) {
  m_nodesCurrentIndex = currentIndex;
  if (m_nodeModel) m_nodeModel->pushCurrentIndex(m_nodesCurrentIndex);
  updateTrustState();
}

void ConnectionSettingsDialog::connectionModeChanged() {
  updateTrustState();
}

void ConnectionSettingsDialog::updateTrustState() {
  const bool remoteMode = m_ui->radioButton_4->isChecked();
  const int index = m_ui->remoteNodesComboBox->currentIndex();
  const bool haveNode = m_nodeModel != nullptr && index >= 0;

  // Only a remote node has a trust decision to make. The other three modes reach
  // a daemon on this machine (127.0.0.1) or the built-in node, both of which the
  // core already treats as trusted resolvers, so the box would be misleading
  // rather than merely inert.
  m_ui->m_trustRemoteNode->setEnabled(remoteMode && haveNode);
  m_ui->m_trustRemoteNode->setChecked(haveNode && m_nodeModel->isTrusted(index));
  if (!remoteMode) {
    m_ui->m_trustRemoteNode->setToolTip(
      tr("Applies to remote nodes only. The built-in node and a daemon on this "
         "computer resolve account numbers without this setting."));
  } else {
    m_ui->m_trustRemoteNode->setToolTip(
      tr("Allow this node to resolve account numbers into recipient keys. Only "
         "tick this for a node you run or whose operator you trust: an account "
         "number is a locator, so whoever answers the lookup decides who gets "
         "paid. Sending to a full address is unaffected."));
  }
}

void ConnectionSettingsDialog::updateNodeSelect() {
  const NodeSetting currentRemoteNode = Settings::instance().getCurrentRemoteNode();
  int index = m_nodeModel->getIndexByData(currentRemoteNode);
  if ( index != -1 ) {
    m_ui->remoteNodesComboBox->setCurrentIndex(index);
  }
}

void ConnectionSettingsDialog::initConnectionSettings() {
  QString connection = Settings::instance().getConnection();
  if (connection.compare("auto") == 0) {
    m_ui->radioButton_1->setChecked(true);
  } else if (connection.compare("embedded") == 0) {
    m_ui->radioButton_2->setChecked(true);
  } else if (connection.compare("local") == 0) {
    m_ui->radioButton_3->setChecked(true);
  } else if (connection.compare("remote") == 0) {
    m_ui->radioButton_4->setChecked(true);
  }

  quint16 localDaemonPort = Settings::instance().getCurrentLocalDaemonPort();
  if (localDaemonPort == 0) localDaemonPort = CryptoNote::RPC_DEFAULT_PORT;
  m_ui->m_localDaemonPort->setValue(localDaemonPort);

  updateNodeSelect();
  updateTrustState();

  quint16 connections = Settings::instance().getConnectionsCount();
  m_ui->m_connectionsCount->setValue(connections);
}

QString ConnectionSettingsDialog::getConnectionMode() const {
  QString connectionMode;
  if (m_ui->radioButton_1->isChecked()) {
    connectionMode = "auto";
  } else if (m_ui->radioButton_2->isChecked()) {
    connectionMode = "embedded";
  } else if (m_ui->radioButton_3->isChecked()) {
    connectionMode = "local";
  } else if(m_ui->radioButton_4->isChecked()) {
    connectionMode = "remote";
  }
  return connectionMode;
}

NodeSetting ConnectionSettingsDialog::getRemoteNode() const {
  return m_nodeModel->getDataByIndex(m_ui->remoteNodesComboBox->currentIndex());
}

quint16 ConnectionSettingsDialog::getLocalDaemonPort() const {
  quint16 localDaemonPort = m_ui->m_localDaemonPort->value();
  return localDaemonPort;
}

quint16 ConnectionSettingsDialog::getConnectionsCount() const {
  quint16 count = m_ui->m_connectionsCount->value();
  return count;
}

void ConnectionSettingsDialog::addNodeClicked() {
  NewNodeDialog dlg(&MainWindow::instance());
  NodeSetting nodeSetting;
  if (dlg.exec() == QDialog::Accepted) {
    nodeSetting.host = dlg.getHost();
    nodeSetting.port = dlg.getPort();
    nodeSetting.path = dlg.getPath();
    nodeSetting.ssl = dlg.getEnableSSL();
    nodeSetting.trusted = dlg.getTrusted();
    QRegularExpression hostRegex("^([a-zA-Z0-9]|[a-zA-Z0-9]-[a-zA-Z0-9]|[a-zA-Z0-9]\\.)+$");
    QRegularExpressionMatch host_match = hostRegex.match(nodeSetting.host);
    bool hostMatch = host_match.hasMatch();
    QRegularExpression pathRegex("^(/([\\w|-]+/)+|/)$");
    QRegularExpressionMatch path_match = pathRegex.match(nodeSetting.path);
    bool pathMatch = path_match.hasMatch();
    if (hostMatch && (nodeSetting.port > 0 && nodeSetting.port < 65535) && pathMatch) {
      m_nodeModel->addNode(nodeSetting);
    }
  }
  updateNodeSelect();
}

void ConnectionSettingsDialog::removeNodeClicked() {
  m_nodeModel->delNode(m_ui->remoteNodesComboBox->currentIndex());
  updateNodeSelect();
}

void ConnectionSettingsDialog::accept() {
  const int index = m_ui->remoteNodesComboBox->currentIndex();
  if (getConnectionMode().compare("remote") == 0 && index < 0) {
    QMessageBox::warning(this, tr("No remote node selected"), tr("Add or select a remote node before using remote daemon mode."), QMessageBox::Ok);
    return;
  }

  // Committed here rather than on toggle so that Cancel leaves the stored trust
  // alone. Must run before the caller reads getRemoteNode(), which takes the
  // node straight from the model.
  if (m_nodeModel != nullptr && index >= 0 && m_ui->m_trustRemoteNode->isEnabled()) {
    m_nodeModel->setTrusted(index, m_ui->m_trustRemoteNode->isChecked());
  }

  QDialog::accept();
}

}
