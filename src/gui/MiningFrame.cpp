// Copyright (c) 2011-2016 The Cryptonote developers
// Copyright (c) 2015-2016 XDN developers
// Copyright (c) 2016-2022 The Karbowanec developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <algorithm>
#include <cmath>
#include <QCoreApplication>
#include <QDebug>
#include <QThread>
#include <QTime>
#include <QUrl>
#include <QtConcurrent/QtConcurrent>
#include <QtConcurrent/QtConcurrentRun>

#include "MiningFrame.h"
#include "MainWindow.h"
#include "WalletAdapter.h"
#include "NodeAdapter.h"
#include "CryptoNoteWrapper.h"
#include "CurrencyAdapter.h"
#include "Settings.h"
#include "Logging/LoggerManager.h"
#include "LoggerAdapter.h"
#include "LogFileWatcher.h"

#ifdef _WIN32
#include <windows.h>
#include <processthreadsapi.h>
#include <winbase.h>
#endif

#include "ui_miningframe.h"

namespace WalletGui {

const quint32 HASHRATE_TIMER_INTERVAL = 1000;
const quint32 MINER_ROUTINE_TIMER_INTERVAL = 60000;

namespace {

QColor withAlpha(QColor color, int alpha) {
  color.setAlpha(alpha);
  return color;
}

bool isDarkColor(const QColor& color) {
  return color.lightness() < 128;
}

QColor blendColors(const QColor& backgroundColor, const QColor& foregroundColor, double foregroundRatio) {
  const double backgroundRatio = 1.0 - foregroundRatio;
  return QColor(
      static_cast<int>(backgroundColor.red() * backgroundRatio + foregroundColor.red() * foregroundRatio),
      static_cast<int>(backgroundColor.green() * backgroundRatio + foregroundColor.green() * foregroundRatio),
      static_cast<int>(backgroundColor.blue() * backgroundRatio + foregroundColor.blue() * foregroundRatio));
}

QPen eventMarkerPen(const QColor& backgroundColor, bool highlight) {
  const QColor markerColor = highlight ?
      (isDarkColor(backgroundColor) ? QColor(255, 203, 112) : QColor(181, 107, 0)) :
      (isDarkColor(backgroundColor) ? QColor(132, 157, 184) : QColor(84, 102, 122));
  return QPen(withAlpha(markerColor, highlight ? 205 : 130), highlight ? 1.6 : 1.0, Qt::SolidLine);
}

QString formatMagnitude(double value) {
  const QStringList suffixes = {QString(), QStringLiteral("K"), QStringLiteral("M"), QStringLiteral("B"), QStringLiteral("T"), QStringLiteral("P")};
  double scaledValue = value;
  double absoluteValue = scaledValue < 0 ? -scaledValue : scaledValue;
  int suffixIndex = 0;

  while (absoluteValue >= 1000 && suffixIndex < suffixes.size() - 1) {
    scaledValue /= 1000;
    absoluteValue /= 1000;
    ++suffixIndex;
  }

  int precision = 0;
  if (absoluteValue < 10 && suffixIndex > 0) {
    precision = 2;
  } else if (absoluteValue < 100 && suffixIndex > 0) {
    precision = 1;
  }

  return QStringLiteral("%1%2").arg(QString::number(scaledValue, 'f', precision), suffixes[suffixIndex]);
}

QString formatHashRate(double hashRate) {
  return QStringLiteral("%1 H/s").arg(formatMagnitude(hashRate));
}

QString formatDuration(qint64 seconds) {
  if (seconds < 0) {
    return QStringLiteral("-");
  }

  const qint64 days = seconds / 86400;
  seconds %= 86400;
  const qint64 hours = seconds / 3600;
  seconds %= 3600;
  const qint64 minutes = seconds / 60;
  seconds %= 60;

  if (days > 0) {
    return QStringLiteral("%1d %2h").arg(days).arg(hours, 2, 10, QLatin1Char('0'));
  }

  return QStringLiteral("%1:%2:%3")
      .arg(hours, 2, 10, QLatin1Char('0'))
      .arg(minutes, 2, 10, QLatin1Char('0'))
      .arg(seconds, 2, 10, QLatin1Char('0'));
}

QString eventColor(const QString& kind) {
  if (kind == QStringLiteral("START")) {
    return QStringLiteral("#1f8f4d");
  }
  if (kind == QStringLiteral("STOP")) {
    return QStringLiteral("#6a737d");
  }
  if (kind == QStringLiteral("CHAIN")) {
    return QStringLiteral("#0b6e99");
  }
  if (kind == QStringLiteral("DIFF")) {
    return QStringLiteral("#7b61b8");
  }
  if (kind == QStringLiteral("PEAK")) {
    return QStringLiteral("#b86b00");
  }
  if (kind == QStringLiteral("CPU")) {
    return QStringLiteral("#2f7d6d");
  }
  if (kind == QStringLiteral("PEERS")) {
    return QStringLiteral("#b86b00");
  }
  if (kind == QStringLiteral("BLOCK")) {
    return QStringLiteral("#d7ff3f");
  }
  if (kind == QStringLiteral("ERROR")) {
    return QStringLiteral("#b00020");
  }

  return QStringLiteral("#5f6b7a");
}

}

MiningFrame::MiningFrame(QWidget* _parent) :
    QFrame(_parent), m_ui(new Ui::MiningFrame),
    m_soloHashRateTimerId(-1),
    m_minerRoutineTimerId(-1),
    m_miner(new Miner(this, LoggerAdapter::instance().getLoggerManager())),
    m_coreLogWatcher(new LogFileWatcher(Settings::instance().getDataDir().absoluteFilePath(QCoreApplication::applicationName() + ".log"), this)) {
  m_ui->setupUi(this);
  setMiningStatusBadge(tr("Stopped"), QStringLiteral("rgba(191, 92, 92, 70)"), QStringLiteral("#7f3030"));
  initCpuCoreList();

  QFont fixedFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
  fixedFont.setStyleHint(QFont::TypeWriter);
  m_ui->m_minerLog->setFont(fixedFont);

  m_ui->m_startSolo->setEnabled(false);
  m_ui->m_stopSolo->setEnabled(false);

  NodeType node = NodeAdapter::instance().getNodeType();
  if (node != NodeType::IN_PROCESS) {
    m_ui->m_startSolo->setDisabled(true);
  }

  m_ui->m_hashRateChart->addGraph();
  m_ui->m_hashRateChart->graph(0)->setScatterStyle(QCPScatterStyle::ssDot);
  m_ui->m_hashRateChart->graph(0)->setLineStyle(QCPGraph::lsLine);
  initHashRateChartItems();

  QSharedPointer<QCPAxisTickerDateTime> dateTicker(new QCPAxisTickerDateTime);
  dateTicker->setDateTimeFormat("hh:mm:ss");
  m_ui->m_hashRateChart->xAxis->setTicker(dateTicker);
  m_ui->m_hashRateChart->yAxis->setRange(0, m_maxHr);
  m_ui->m_hashRateChart->yAxis->setLabel(tr("Hashrate"));

  // make top and right axes visible but without ticks and labels
  m_ui->m_hashRateChart->xAxis2->setVisible(true);
  m_ui->m_hashRateChart->yAxis2->setVisible(true);
  m_ui->m_hashRateChart->xAxis2->setTicks(false);
  m_ui->m_hashRateChart->yAxis2->setTicks(false);
  m_ui->m_hashRateChart->xAxis2->setTickLabels(false);
  m_ui->m_hashRateChart->yAxis2->setTickLabels(false);

  initDifficultyChart();
  applyChartPalette();
  updateCpuIntensity();

  // Neutral context for the Luck read-out: above 100% only means this search is
  // running longer than average, which is ordinary variance for solo mining — not
  // an error. Kept as a tooltip with default (non-alarming) colouring.
  const QString luckTooltip = tr("Luck compares the work done to the difficulty. Above 100% simply means "
      "this search is taking longer than average — that is normal variance for solo mining, not a problem.");
  m_ui->m_luckTitle->setToolTip(luckTooltip);
  m_ui->m_luckValue->setToolTip(luckTooltip);
  m_ui->m_blockProgressBar->setToolTip(tr("How much of the average time-to-a-block has elapsed since your last "
      "find. It is an average, not a probability: each hash is independent, so this never means a block is 'due'."));

  updateSessionStats();

  addPoint(QDateTime::currentDateTime().toSecsSinceEpoch(), 0);
  plot();

  connect(m_ui->m_cpuEcoPreset, &QPushButton::clicked, this, [this]() { applyCpuPreset(0.25); });
  connect(m_ui->m_cpuBalancedPreset, &QPushButton::clicked, this, [this]() { applyCpuPreset(0.5); });
  connect(m_ui->m_cpuMaxPreset, &QPushButton::clicked, this, [this]() { applyCpuPreset(1); });
  connect(m_ui->m_cpuCoresSpin, static_cast<void(QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, [this](int) { updateCpuIntensity(); });
  m_threadResizeTimer.setSingleShot(true);
  connect(&m_threadResizeTimer, &QTimer::timeout, this, &MiningFrame::applyPendingMiningThreads);

  connect(&WalletAdapter::instance(), &WalletAdapter::walletCloseCompletedSignal, this, &MiningFrame::walletClosed, Qt::QueuedConnection);
  connect(&WalletAdapter::instance(), &WalletAdapter::walletInitCompletedSignal, this, &MiningFrame::walletOpened, Qt::QueuedConnection);
  connect(&WalletAdapter::instance(), &WalletAdapter::walletActualBalanceUpdatedSignal, this, &MiningFrame::updateBalance, Qt::QueuedConnection);
  connect(&WalletAdapter::instance(), &WalletAdapter::walletPendingBalanceUpdatedSignal, this, &MiningFrame::updatePendingBalance, Qt::QueuedConnection);
  connect(&WalletAdapter::instance(), &WalletAdapter::walletSynchronizationCompletedSignal, this, &MiningFrame::onSynchronizationCompleted, Qt::QueuedConnection);
  connect(&NodeAdapter::instance(), &NodeAdapter::localBlockchainUpdatedSignal, this, &MiningFrame::onBlockHeightUpdated, Qt::QueuedConnection);
  connect(&NodeAdapter::instance(), &NodeAdapter::peerCountUpdatedSignal, this, &MiningFrame::onPeerCountUpdated, Qt::QueuedConnection);
  connect(&NodeAdapter::instance(), &NodeAdapter::poolChangedSignal, this, &MiningFrame::poolChanged,Qt::QueuedConnection);
  connect(&*m_miner, &Miner::minerMessageSignal, this, &MiningFrame::updateMinerLog, Qt::QueuedConnection);
  connect(&*m_miner, &Miner::minerStartedSignal, this, &MiningFrame::onMinerStarted, Qt::QueuedConnection);
  connect(&*m_miner, &Miner::minerStoppedSignal, this, &MiningFrame::onMinerStopped, Qt::QueuedConnection);
  connect(&*m_miner, &Miner::minerThreadsChangedSignal, this, &MiningFrame::onMinerThreadsChanged, Qt::QueuedConnection);
  connect(&*m_miner, &Miner::minerTemplateUpdatedSignal, this, &MiningFrame::onMinerTemplateUpdated, Qt::QueuedConnection);
  connect(&*m_miner, &Miner::miningErrorSignal, this, &MiningFrame::onMinerError, Qt::QueuedConnection);
  connect(m_coreLogWatcher, &LogFileWatcher::newLogStringSignal, this, &MiningFrame::updateCoreLog, Qt::QueuedConnection);

  // Lifetime mining stats are derived from the wallet's own coinbase transactions,
  // so we refresh them when the transaction set changes rather than on a timer.
  connect(&WalletAdapter::instance(), &WalletAdapter::walletTransactionCreatedSignal, this, &MiningFrame::onWalletTransactionsChanged, Qt::QueuedConnection);
  connect(&WalletAdapter::instance(), &WalletAdapter::walletTransactionUpdatedSignal, this, &MiningFrame::onWalletTransactionsChanged, Qt::QueuedConnection);
  connect(&WalletAdapter::instance(), &WalletAdapter::reloadWalletTransactionsSignal, this, &MiningFrame::onWalletTransactionsReset, Qt::QueuedConnection);

  if (WalletAdapter::instance().isOpen()) {
    scanLifetimeStats(false);
  }
  updateForecast();
}

MiningFrame::~MiningFrame() {
  stopSolo();
}

void MiningFrame::changeEvent(QEvent* _event) {
  QFrame::changeEvent(_event);
  if (_event->type() == QEvent::PaletteChange || _event->type() == QEvent::StyleChange) {
    applyChartPalette();
    m_ui->m_hashRateChart->replot();
    m_ui->m_difficultyChart->replot();
  }
}

void MiningFrame::applyChartPalette() {
  const QPalette chartPalette = palette();
  const QColor textColor = chartPalette.color(QPalette::WindowText);
  const QColor axisColor = chartPalette.color(QPalette::Mid);
  const QColor subTickColor = chartPalette.color(QPalette::Midlight);
  const QColor backgroundColor = chartPalette.color(QPalette::Window);
  const QColor accentColor = chartPalette.color(QPalette::Highlight);
  const QColor gridColor = withAlpha(textColor, isDarkColor(backgroundColor) ? 38 : 28);

  m_ui->m_hashRateChart->graph(0)->setPen(QPen(accentColor));
  m_ui->m_hashRateChart->graph(0)->setBrush(QBrush(withAlpha(accentColor, 48)));
  m_ui->m_hashRateChart->graph(1)->setVisible(false);
  m_ui->m_hashRateChart->graph(1)->setPen(QPen(Qt::NoPen));
  m_ui->m_hashRateChart->graph(1)->setBrush(Qt::NoBrush);
  if (m_peakHashRateLine != nullptr) {
    m_peakHashRateLine->setVisible(false);
  }
  for (int markerIndex = 0; markerIndex < m_hashRateEventMarkers.size(); ++markerIndex) {
    const bool markerHighlight = markerIndex < m_hashRateEventMarkerHighlights.size() && m_hashRateEventMarkerHighlights.at(markerIndex);
    m_hashRateEventMarkers.at(markerIndex)->setPen(eventMarkerPen(backgroundColor, markerHighlight));
    m_hashRateEventMarkers.at(markerIndex)->setVisible(true);
  }

  m_ui->m_difficultyChart->graph()->setPen(QPen(accentColor, 1.0));
  m_ui->m_difficultyChart->graph()->setBrush(Qt::NoBrush);

  m_ui->m_hashRateChart->xAxis->setLabelColor(textColor);
  m_ui->m_hashRateChart->yAxis->setLabelColor(textColor);
  m_ui->m_hashRateChart->xAxis->setTickLabelColor(textColor);
  m_ui->m_hashRateChart->yAxis->setTickLabelColor(textColor);

  m_ui->m_hashRateChart->xAxis->setTickPen(QPen(axisColor));
  m_ui->m_hashRateChart->yAxis->setTickPen(QPen(axisColor));

  m_ui->m_hashRateChart->xAxis->setSubTickPen(QPen(subTickColor));
  m_ui->m_hashRateChart->yAxis->setSubTickPen(QPen(subTickColor));
  m_ui->m_hashRateChart->xAxis->grid()->setPen(QPen(gridColor, 1, Qt::SolidLine));
  m_ui->m_hashRateChart->yAxis->grid()->setPen(QPen(gridColor, 1, Qt::SolidLine));
  m_ui->m_hashRateChart->xAxis->grid()->setSubGridVisible(false);
  m_ui->m_hashRateChart->yAxis->grid()->setSubGridVisible(false);

  m_ui->m_hashRateChart->xAxis->setBasePen(QPen(axisColor));
  m_ui->m_hashRateChart->yAxis->setBasePen(QPen(axisColor));
  m_ui->m_hashRateChart->xAxis2->setBasePen(QPen(axisColor));
  m_ui->m_hashRateChart->yAxis2->setBasePen(QPen(axisColor));

  if (m_ui->m_hashRateChart->legend) {
    m_ui->m_hashRateChart->legend->setTextColor(textColor);
    m_ui->m_hashRateChart->legend->setBrush(QBrush(backgroundColor));
    m_ui->m_hashRateChart->legend->setBorderPen(QPen(axisColor));
  }

  m_ui->m_hashRateChart->setBackground(QBrush(backgroundColor));
  m_ui->m_difficultyChart->setBackground(QBrush(backgroundColor));

  // Match wrapper frame background to chart background so margins don't show a different color
  m_ui->m_chartFrame->setAutoFillBackground(true);
  QPalette framePalette = m_ui->m_chartFrame->palette();
  framePalette.setColor(QPalette::Window, backgroundColor);
  m_ui->m_chartFrame->setPalette(framePalette);
}

void MiningFrame::initHashRateChartItems() {
  m_ui->m_hashRateChart->addGraph();
  m_ui->m_hashRateChart->graph(1)->setScatterStyle(QCPScatterStyle::ssNone);
  m_ui->m_hashRateChart->graph(1)->setLineStyle(QCPGraph::lsLine);
  m_ui->m_hashRateChart->graph(1)->setName(tr("Average"));

  m_peakHashRateLine = new QCPItemStraightLine(m_ui->m_hashRateChart);
  m_peakHashRateLine->point1->setCoords(0, 0);
  m_peakHashRateLine->point2->setCoords(1, 0);
  m_peakHashRateLine->setVisible(false);
}

void MiningFrame::initDifficultyChart() {
  m_ui->m_difficultyChart->addGraph();
  m_ui->m_difficultyChart->graph(0)->setLineStyle(QCPGraph::lsLine);
  m_ui->m_difficultyChart->graph(0)->setScatterStyle(QCPScatterStyle::ssNone);
  m_ui->m_difficultyChart->setInteractions(QCP::iNone);
  m_ui->m_difficultyChart->axisRect()->setAutoMargins(QCP::msNone);
  m_ui->m_difficultyChart->axisRect()->setMargins(QMargins(2, 2, 2, 2));

  m_ui->m_difficultyChart->xAxis->setVisible(false);
  m_ui->m_difficultyChart->yAxis->setVisible(false);
  m_ui->m_difficultyChart->xAxis2->setVisible(false);
  m_ui->m_difficultyChart->yAxis2->setVisible(false);
}

void MiningFrame::updateDifficulty(quint64 _difficulty) {
  const double previousDifficulty = m_currentDifficulty;
  const bool difficultyChanged = previousDifficulty > 0 && previousDifficulty != static_cast<double>(_difficulty);
  m_currentDifficulty = static_cast<double>(_difficulty);
  m_ui->m_difficulty->setText(QString::number(_difficulty));

  if (!m_difficultyY.isEmpty() && m_difficultyY.last() == static_cast<double>(_difficulty)) {
    updateSessionStats();
    return;
  }

  m_difficultyX.append(m_difficultyX.size());
  m_difficultyY.append(static_cast<double>(_difficulty));
  plotDifficulty();

  if (difficultyChanged && m_solo_mining) {
    const double percentChange = previousDifficulty > 0 ? ((m_currentDifficulty - previousDifficulty) / previousDifficulty) * 100 : 0;
    const QString sign = percentChange >= 0 ? QStringLiteral("+") : QString();
    const QString direction = m_currentDifficulty > previousDifficulty ? tr("rose") : tr("dropped");
    appendMiningEvent(QStringLiteral("DIFF"), tr("Difficulty %1 to %2 (%3%4%)")
        .arg(direction)
        .arg(formatMagnitude(_difficulty))
        .arg(sign)
        .arg(QString::number(percentChange, 'f', 1)));
    addHashRateEventMarker(false);
  }

  updateSessionStats();
}

void MiningFrame::plotDifficulty() {
  if (m_difficultyY.isEmpty()) {
    return;
  }

  m_ui->m_difficultyChart->graph(0)->setData(m_difficultyX, m_difficultyY);

  double minDifficulty = m_difficultyY.first();
  double maxDifficulty = m_difficultyY.first();
  for (double difficulty : m_difficultyY) {
    minDifficulty = std::min(minDifficulty, difficulty);
    maxDifficulty = std::max(maxDifficulty, difficulty);
  }

  const double xStart = m_difficultyX.first();
  const double xEnd = m_difficultyX.last();
  m_ui->m_difficultyChart->xAxis->setRange(xStart, xStart == xEnd ? xStart + 1 : xEnd);

  double padding = 1;
  if (minDifficulty != maxDifficulty) {
    padding = (maxDifficulty - minDifficulty) * 0.08;
  } else if (maxDifficulty > 0) {
    padding = maxDifficulty * 0.08;
  }

  m_ui->m_difficultyChart->yAxis->setRange(std::max(0.0, minDifficulty - padding), maxDifficulty + padding);
  m_ui->m_difficultyChart->replot();
}

void MiningFrame::addHashRateEventMarker(bool _highlight) {
  if (m_hX.isEmpty()) {
    return;
  }

  const double x = QDateTime::currentDateTime().toSecsSinceEpoch();
  const QColor backgroundColor = palette().color(QPalette::Window);
  QCPItemLine* marker = new QCPItemLine(m_ui->m_hashRateChart);
  marker->start->setCoords(x, 0);
  marker->end->setCoords(x, std::max<double>(10, m_maxHr * 1.15));
  marker->setPen(eventMarkerPen(backgroundColor, _highlight));
  m_hashRateEventMarkers.append(marker);
  m_hashRateEventMarkerHighlights.append(_highlight);

  while (m_hashRateEventMarkers.size() > 40) {
    m_ui->m_hashRateChart->removeItem(m_hashRateEventMarkers.takeFirst());
    m_hashRateEventMarkerHighlights.takeFirst();
  }
}

void MiningFrame::appendMiningEvent(const QString& _kind, const QString& _message) {
  const QString trimmedMessage = _message.trimmed();
  if (trimmedMessage.isEmpty()) {
    return;
  }

  const QString time = QTime::currentTime().toString(QStringLiteral("hh:mm:ss"));
  const QString kind = _kind.trimmed().toUpper();
  const QString color = eventColor(kind);
  QString paddedKind = kind.leftJustified(5).toHtmlEscaped();
  paddedKind.replace(QLatin1Char(' '), QStringLiteral("&nbsp;"));
  QString rowStyle = QStringLiteral("margin:2px 0; white-space:nowrap;");
  if (kind == QStringLiteral("BLOCK")) {
    const QColor baseColor = m_ui->m_eventLog->palette().color(QPalette::Base);
    const QColor highlightColor = blendColors(baseColor, QColor(color), isDarkColor(baseColor) ? 0.28 : 0.16);
    rowStyle += QStringLiteral(" background-color:%1; padding-left:4px; border-radius:3px;")
        .arg(highlightColor.name(QColor::HexRgb));
  }

  m_event_log.append(QStringLiteral(
      "<div style=\"%1\">"
      "<span style=\"color:#7a7f85;\">%2</span>&nbsp;&nbsp;"
      "<span style=\"color:%3; font-weight:700;\">%4</span>&nbsp;&nbsp;"
      "<span>%5</span>"
      "</div>")
      .arg(rowStyle)
      .arg(time.toHtmlEscaped())
      .arg(color)
      .arg(paddedKind)
      .arg(trimmedMessage.toHtmlEscaped()));
  while (m_event_log.size() > 160) {
    m_event_log.removeFirst();
  }

  m_ui->m_eventLog->setHtml(QStringLiteral(
      "<html><body style=\"font-family:'Consolas','Courier New',monospace; font-size:9pt;\">%1</body></html>")
      .arg(m_event_log.join(QString())));
  QScrollBar* scrollBar = m_ui->m_eventLog->verticalScrollBar();
  scrollBar->setValue(scrollBar->maximum());
}

void MiningFrame::appendRawLogLine(const QString& _line) {
  const QString message = _line.trimmed();
  if (message.isEmpty()) {
    return;
  }

  m_miner_log += message + QStringLiteral("\n");

  const int maxRawLogChars = 240000;
  if (m_miner_log.size() > maxRawLogChars) {
    const int trimLength = m_miner_log.size() - maxRawLogChars;
    const int nextLine = m_miner_log.indexOf(QLatin1Char('\n'), trimLength);
    m_miner_log.remove(0, nextLine >= 0 ? nextLine + 1 : trimLength);
  }

  m_ui->m_minerLog->setPlainText(m_miner_log);

  QScrollBar* scrollBar = m_ui->m_minerLog->verticalScrollBar();
  scrollBar->setValue(scrollBar->maximum());
}

void MiningFrame::resetSessionStats() {
  m_sessionStartedAt = QDateTime::currentDateTime();
  m_sessionBlocks = 0;
  // Anchor the expected-time progress bar to the start of this mining stretch;
  // it advances toward D/h and is re-anchored whenever a block is found.
  m_lastBlockFoundAt = m_sessionStartedAt;
  m_sessionTotalHashes = 0;
  m_roundHashes = 0;
  m_sessionPeakHashRate = 0;
  m_lastAnnouncedPeakHashRate = 0;
  m_lastHashRate = 0;
  m_hX.clear();
  m_hY.clear();
  m_averageHashRateY.clear();
  m_maxHr = 10;
  m_ui->m_hashRateChart->graph(0)->data()->clear();
  m_ui->m_hashRateChart->graph(1)->data()->clear();
  if (m_peakHashRateLine != nullptr) {
    m_peakHashRateLine->setVisible(false);
  }

  while (!m_hashRateEventMarkers.isEmpty()) {
    m_ui->m_hashRateChart->removeItem(m_hashRateEventMarkers.takeFirst());
  }
  m_hashRateEventMarkerHighlights.clear();

  addPoint(m_sessionStartedAt.toSecsSinceEpoch(), 0);
  updateSessionStats();
  plot();
}

void MiningFrame::updateSessionStats() {
  qint64 elapsedSeconds = 0;
  if (m_sessionStartedAt.isValid()) {
    elapsedSeconds = std::max<qint64>(0, m_sessionStartedAt.secsTo(QDateTime::currentDateTime()));
  }

  const double averageHashRate = elapsedSeconds > 0 ? m_sessionTotalHashes / elapsedSeconds : 0;
  m_ui->m_sessionTimeValue->setText(formatDuration(elapsedSeconds));
  m_ui->m_totalHashesValue->setText(formatMagnitude(m_sessionTotalHashes));
  m_ui->m_averageHashRateValue->setText(formatHashRate(averageHashRate));
  m_ui->m_peakHashRateValue->setText(formatHashRate(m_sessionPeakHashRate));

  if (m_lastHashRate > 0 && m_currentDifficulty > 0) {
    const qint64 etaSeconds = static_cast<qint64>(m_currentDifficulty / m_lastHashRate);
    m_ui->m_estimatedBlockTimeValue->setText(formatDuration(etaSeconds));
  } else {
    m_ui->m_estimatedBlockTimeValue->setText(QStringLiteral("-"));
  }

  const double luck = m_currentDifficulty > 0 ? (m_roundHashes / m_currentDifficulty) * 100 : 0;
  const int luckPrecision = luck < 10 ? 1 : 0;
  m_ui->m_luckValue->setText(QStringLiteral("%1%").arg(QString::number(luck, 'f', luckPrecision)));

  updateForecast();
}

QString MiningFrame::formatExpectedDuration(double _seconds) const {
  if (!std::isfinite(_seconds) || _seconds <= 0) {
    return QStringLiteral("-");
  }

  qint64 totalSeconds = static_cast<qint64>(std::llround(_seconds));
  if (totalSeconds < 60) {
    return tr("%1s").arg(totalSeconds);
  }

  const qint64 days = totalSeconds / 86400;
  totalSeconds %= 86400;
  const qint64 hours = totalSeconds / 3600;
  totalSeconds %= 3600;
  const qint64 minutes = totalSeconds / 60;
  const qint64 seconds = totalSeconds % 60;

  if (days > 0) {
    return tr("%1d %2h").arg(days).arg(hours);
  }
  if (hours > 0) {
    return tr("%1h %2m").arg(hours).arg(minutes);
  }
  return tr("%1m %2s").arg(minutes).arg(seconds);
}

void MiningFrame::updateForecast() {
  // Expected solo time to a block is D / h seconds. This is the same relationship
  // the "Est. block" read-out uses (see above) and is kept as the single source
  // of truth for both the ETA and this forward-progress element.
  const bool haveRate = m_solo_mining && m_lastHashRate > 0 && m_currentDifficulty > 0;
  const double expectedSeconds = haveRate ? (m_currentDifficulty / m_lastHashRate) : 0;

  // Plain-language expectation line, updated live from the current difficulty and
  // hashrate. Degrades gracefully to a neutral prompt when h == 0 / not mining so
  // it never divides by zero.
  if (haveRate) {
    m_ui->m_expectationLabel->setText(
        tr("At ~%1 you'll find a block about every %2 on average. Solo mining is a lottery — "
           "long gaps are normal and don't mean anything is wrong.")
            .arg(formatHashRate(m_lastHashRate), formatExpectedDuration(expectedSeconds)));
  } else if (m_solo_mining) {
    m_ui->m_expectationLabel->setText(tr("Measuring your hashrate to estimate how often you'll find a block…"));
  } else {
    m_ui->m_expectationLabel->setText(tr("Start mining to see how often you can expect to find a block."));
  }

  // Between-block progress: the fraction of the *expected* time that has elapsed
  // since the last find. Explicitly an average, not a probability — solo mining is
  // a memoryless Poisson process, so effort never accumulates toward a guaranteed
  // block. Past 100% we switch to a calm "overdue but normal" state, never an alarm.
  if (haveRate && m_lastBlockFoundAt.isValid()) {
    const double elapsedSeconds = std::max<double>(0, m_lastBlockFoundAt.secsTo(QDateTime::currentDateTime()));
    const double fraction = elapsedSeconds / expectedSeconds;
    const int barValue = static_cast<int>(std::min(fraction, 1.0) * m_ui->m_blockProgressBar->maximum());
    m_ui->m_blockProgressBar->setValue(barValue);
    if (fraction <= 1.0) {
      m_ui->m_blockProgressCaption->setText(tr("~%1% of an average find").arg(static_cast<int>(fraction * 100 + 0.5)));
    } else {
      m_ui->m_blockProgressCaption->setText(tr("Longer than average — normal for solo mining"));
    }
  } else {
    m_ui->m_blockProgressBar->setValue(0);
    m_ui->m_blockProgressCaption->setText(m_solo_mining ? tr("Measuring hashrate…") : QStringLiteral("—"));
  }

  // Session and all-time blocks are always shown together, so a fresh restart
  // never presents a lonely "0".
  m_ui->m_blocksSummaryLabel->setText(
      tr("Blocks: %1 this session · %2 all time").arg(m_sessionBlocks).arg(m_lifetimeBlocks));

  if (m_lifetimeStatsReady) {
    m_ui->m_rewardSummaryLabel->setText(
        tr("Mined all time: %1 %2")
            .arg(CurrencyAdapter::instance().formatAmount(m_lifetimeReward),
                 CurrencyAdapter::instance().getCurrencyTicker()));
  } else {
    m_ui->m_rewardSummaryLabel->setText(tr("Mined all time: —"));
  }

  // Rolling recent view: a short dry spell looks ordinary next to the longer
  // 24h / 7d picture.
  const qint64 now = QDateTime::currentSecsSinceEpoch();
  int last24h = 0;
  int last7d = 0;
  for (qint64 timestamp : m_recentBlockTimes) {
    if (timestamp >= now - 24 * 3600) {
      ++last24h;
    }
    if (timestamp >= now - 7 * 24 * 3600) {
      ++last7d;
    }
  }
  m_ui->m_recentBlocksLabel->setText(tr("Recent finds — %1 in 24h, %2 in 7d").arg(last24h).arg(last7d));
}

void MiningFrame::resetLifetimeStats() {
  m_lifetimeBlocks = 0;
  m_lifetimeReward = 0;
  m_lifetimeStatsReady = false;
  m_lifetimeScanCursor = 0;
  m_recentBlockTimes.clear();
}

void MiningFrame::scanLifetimeStats(bool _announceNewBlocks) {
  if (!WalletAdapter::instance().isOpen()) {
    return;
  }

  // Walk only the transaction ids we have not seen yet. Ids are append-only and a
  // coinbase transaction's isCoinbase flag and amount never change, so an
  // incremental scan is correct and keeps this off the hot path — the 1s
  // hash-rate timer never calls it, only wallet transaction/open events do.
  const quint64 transactionCount = WalletAdapter::instance().getTransactionCount();
  const bool wasReady = m_lifetimeStatsReady;
  QList<quint64> newFinds;

  for (quint64 id = m_lifetimeScanCursor; id < transactionCount; ++id) {
    CryptoNote::TransactionId transactionId = static_cast<CryptoNote::TransactionId>(id);
    CryptoNote::WalletLegacyTransaction transaction;
    if (!WalletAdapter::instance().getTransaction(transactionId, transaction) || !transaction.isCoinbase) {
      continue;
    }

    const quint64 reward = transaction.totalAmount > 0 ? static_cast<quint64>(transaction.totalAmount) : 0;
    ++m_lifetimeBlocks;
    m_lifetimeReward += reward;
    if (transaction.timestamp > 0) {
      m_recentBlockTimes.append(static_cast<qint64>(transaction.timestamp));
    }

    // A coinbase we discover while actively solo mining is a block we just found.
    // Never announce the historical blocks read on first open (baseline), nor
    // coinbases that merely sync in while we are not mining.
    if (wasReady && _announceNewBlocks && m_solo_mining) {
      newFinds.append(reward);
    }
  }

  m_lifetimeScanCursor = transactionCount;
  m_lifetimeStatsReady = true;

  for (quint64 reward : newFinds) {
    onBlockFoundByMiner(reward);
  }

  updateForecast();
}

void MiningFrame::onBlockFoundByMiner(quint64 _reward) {
  ++m_sessionBlocks;
  // Re-anchor the expected-time progress bar: the search for the next block
  // starts now.
  m_lastBlockFoundAt = QDateTime::currentDateTime();

  const QString reward = CurrencyAdapter::instance().formatAmount(_reward);
  const QString ticker = CurrencyAdapter::instance().getCurrencyTicker();
  appendMiningEvent(QStringLiteral("BLOCK"), tr("Block found! Reward %1 %2 — nice work").arg(reward, ticker));
  addHashRateEventMarker(true);
}

void MiningFrame::updateCpuIntensity() {
  const int maxCores = std::max(1, m_ui->m_cpuCoresSpin->maximum());
  const int selectedCores = m_ui->m_cpuCoresSpin->value();
  m_ui->m_cpuIntensityBar->setValue((selectedCores * 100 + maxCores / 2) / maxCores);
}

void MiningFrame::applyCpuPreset(double _fraction) {
  const int maxCores = std::max(1, m_ui->m_cpuCoresSpin->maximum());
  const int cores = std::max(1, static_cast<int>(maxCores * _fraction + 0.5));
  m_ui->m_cpuCoresSpin->setValue(std::min(cores, maxCores));
}

void MiningFrame::setMiningStatusBadge(const QString& _text, const QString& _backgroundColor, const QString& _textColor) {
  m_ui->m_soloLabel->setText(_text);
  m_ui->m_soloLabel->setStyleSheet(QStringLiteral(
      "QLabel#m_soloLabel {"
      "  background: %1;"
      "  color: %2;"
      "  border: 1px solid %2;"
      "  border-radius: 8px;"
      "  padding: 2px 4px;"
      "  font-weight: bold;"
      "}")
      .arg(_backgroundColor, _textColor));
}

void MiningFrame::scheduleMiningThreadsChange(int _threads) {
  m_pendingMiningThreads = _threads;
  m_threadResizeTimer.start(m_solo_mining ? 900 : 500);
}

void MiningFrame::applyPendingMiningThreads() {
  if (m_pendingMiningThreads <= 0) {
    return;
  }

  const int threads = m_pendingMiningThreads;
  m_pendingMiningThreads = 0;
  Settings::instance().setMiningThreads(threads);

  if (!m_solo_mining || !m_miner->is_mining()) {
    return;
  }

  if (!m_miner->set_thread_count(static_cast<size_t>(threads))) {
    appendMiningEvent(QStringLiteral("ERROR"), tr("Failed to change mining thread count"));
  }
}

void MiningFrame::addPoint(double x, double y)
{
  m_hX.append(x);
  m_hY.append(y);
  qint64 elapsedSeconds = 0;
  if (m_sessionStartedAt.isValid()) {
    elapsedSeconds = std::max<qint64>(0, m_sessionStartedAt.secsTo(QDateTime::currentDateTime()));
  }

  m_averageHashRateY.append(elapsedSeconds > 0 ? m_sessionTotalHashes / elapsedSeconds : 0);

  // scroll plot
  if (m_hX.size() > 1000 || m_hY.size() > 1000 || m_averageHashRateY.size() > 1000) {
    m_hX.pop_front();
    m_hY.pop_front();
    m_averageHashRateY.pop_front();
  }
}

void MiningFrame::plot()
{
  m_maxHr = std::max<double>(m_maxHr, m_hY.at(m_hY.size()-1));
  m_ui->m_hashRateChart->graph(0)->setData(m_hX, m_hY);
  m_ui->m_hashRateChart->graph(1)->setData(m_hX, m_averageHashRateY);
  const double yRangeMax = std::max<double>(10, m_maxHr * 1.15);
  if (m_peakHashRateLine != nullptr && m_sessionPeakHashRate > 0) {
    m_peakHashRateLine->point1->setCoords(0, m_sessionPeakHashRate);
    m_peakHashRateLine->point2->setCoords(1, m_sessionPeakHashRate);
    m_peakHashRateLine->setVisible(false);
  }
  for (QCPItemLine* marker : m_hashRateEventMarkers) {
    marker->end->setCoords(marker->start->key(), yRangeMax);
  }
  m_ui->m_hashRateChart->xAxis->setRange(m_hX.at(0), m_hX.at(m_hX.size()-1));
  m_ui->m_hashRateChart->yAxis->setRange(0, yRangeMax);
  m_ui->m_hashRateChart->replot();
  m_ui->m_hashRateChart->update();
}

void MiningFrame::timerEvent(QTimerEvent* _event) {
  if (_event->timerId() == m_soloHashRateTimerId) {
    m_miner->merge_hr();
    double hashRate = m_miner->get_speed();
    m_lastHashRate = hashRate;
    m_sessionTotalHashes += hashRate * (HASHRATE_TIMER_INTERVAL / 1000.0);
    m_roundHashes += hashRate * (HASHRATE_TIMER_INTERVAL / 1000.0);
    const double previousPeakHashRate = m_sessionPeakHashRate;
    m_sessionPeakHashRate = std::max(m_sessionPeakHashRate, hashRate);
    if (hashRate > 0 && hashRate > previousPeakHashRate &&
        (m_lastAnnouncedPeakHashRate == 0 || hashRate >= m_lastAnnouncedPeakHashRate * 1.1)) {
      m_lastAnnouncedPeakHashRate = hashRate;
      appendMiningEvent(QStringLiteral("PEAK"), tr("New session peak %1").arg(formatHashRate(hashRate)));
    }

    setMiningStatusBadge(tr("Mining"), QStringLiteral("rgba(91, 171, 118, 65)"), QStringLiteral("#246d3f"));
    m_ui->m_hashratelcdNumber->display(hashRate);
    addPoint(QDateTime::currentDateTime().toSecsSinceEpoch(), hashRate);
    updateSessionStats();
    plot();

    return;
  }
  if (_event->timerId() == m_minerRoutineTimerId) {
    m_miner->on_block_chain_update();
  }

  QFrame::timerEvent(_event);
}

int getCpuCoreCount() {
#ifdef _WIN32
#if (_WIN32_WINNT >= 0x0601)
    DWORD total = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    return static_cast<int>(total);
#else
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    return static_cast<int>(sysinfo.dwNumberOfProcessors);
#endif
#else
    int count = QThread::idealThreadCount();
    return count > 0 ? count : 2;
#endif
}

void MiningFrame::initCpuCoreList() {
  quint16 threads = Settings::instance().getMiningThreads();
  int cpuCoreCount = getCpuCoreCount();
  if (threads > 0) {
    m_ui->m_cpuCoresSpin->setValue(threads);
  } else {
    m_ui->m_cpuCoresSpin->setValue((cpuCoreCount - 1) / 2);
  }

  m_ui->m_cpuCoresSpin->setMaximum(cpuCoreCount);
  m_ui->m_cpuCoresSpin->setMinimum(1);
  m_ui->m_cpuDial->setMaximum(cpuCoreCount);
  m_ui->m_cpuDial->setMinimum(1);
  updateCpuIntensity();
}

void MiningFrame::walletOpened() {
  if(m_solo_mining)
    stopSolo();

  m_wallet_closed = false;

  // Re-derive lifetime blocks/reward from this wallet's coinbase history. The
  // first scan just captures the baseline (historical blocks are not announced).
  resetLifetimeStats();
  scanLifetimeStats(false);

  quint64 difficulty = NodeAdapter::instance().getDifficulty();
  updateDifficulty(difficulty);
}

void MiningFrame::walletClosed() {
  //stopSolo();
  m_wallet_closed = true;
  resetLifetimeStats();
  updateForecast();
  //m_ui->m_startSolo->setEnabled(false);
  //m_ui->m_stopSolo->isChecked();

  // TODO: consider setting all dials to zeroes
}

bool MiningFrame::isSoloRunning() const {
  return m_solo_mining;
}

void MiningFrame::stopMiningForShutdown() {
  m_wallet_closed = true;
  m_threadResizeTimer.stop();
  m_pendingMiningThreads = 0;

  if (m_soloHashRateTimerId != -1) {
    killTimer(m_soloHashRateTimerId);
    m_soloHashRateTimerId = -1;
  }

  if (m_minerRoutineTimerId != -1) {
    killTimer(m_minerRoutineTimerId);
    m_minerRoutineTimerId = -1;
  }

  m_miner->stop();
  m_solo_mining = false;
  m_mining_was_stopped = true;
}

void MiningFrame::startSolo() {
  if (NodeAdapter::instance().getPeerCount() == 0) {
    setMiningStatusBadge(tr("No peers"), QStringLiteral("rgba(219, 178, 83, 75)"), QStringLiteral("#7a5a16"));
    m_ui->m_startSolo->setChecked(false);
    m_ui->m_startSolo->setEnabled(false);
    m_ui->m_stopSolo->setEnabled(false);
    appendMiningEvent(QStringLiteral("PEERS"), tr("Mining was not started because there are no connected peers"));
    return;
  }

  quint64 difficulty = NodeAdapter::instance().getDifficulty();
  updateDifficulty(difficulty);

  resetSessionStats();
  if (!m_miner->start(m_ui->m_cpuCoresSpin->value())) {
    setMiningStatusBadge(tr("Failed"), QStringLiteral("rgba(191, 92, 92, 70)"), QStringLiteral("#7f3030"));
    m_ui->m_startSolo->setChecked(false);
    m_ui->m_startSolo->setEnabled(true);
    m_ui->m_stopSolo->setEnabled(false);
    appendMiningEvent(QStringLiteral("ERROR"), tr("Mining failed to start"));
    return;
  }

  setMiningStatusBadge(tr("Starting..."), QStringLiteral("rgba(219, 178, 83, 75)"), QStringLiteral("#7a5a16"));
  m_soloHashRateTimerId = startTimer(HASHRATE_TIMER_INTERVAL);
  m_minerRoutineTimerId = startTimer(MINER_ROUTINE_TIMER_INTERVAL);
  m_ui->m_startSolo->setChecked(true);
  m_ui->m_startSolo->setEnabled(false);
  m_ui->m_stopSolo->setEnabled(true);
  m_solo_mining = true;
  m_miningStoppedByNoPeers = false;
}

void MiningFrame::stopSolo(bool _stoppedByNoPeers) {
  if(m_solo_mining) {
    killTimer(m_soloHashRateTimerId);
    m_soloHashRateTimerId = -1;
    killTimer(m_minerRoutineTimerId);
    m_minerRoutineTimerId = -1;
    m_miner->stop();
    addPoint(QDateTime::currentDateTime().toSecsSinceEpoch(), 0);
    setMiningStatusBadge(tr("Stopped"), QStringLiteral("rgba(191, 92, 92, 70)"), QStringLiteral("#7f3030"));
    m_ui->m_hashratelcdNumber->display(0.0);
    m_lastHashRate = 0;
    updateSessionStats();
    plot();
    if (!m_wallet_closed) {
      m_ui->m_startSolo->setEnabled(true);
      m_ui->m_stopSolo->setEnabled(false);
      m_ui->m_cpuCoresSpin->setEnabled(true);
      m_ui->m_cpuDial->setEnabled(true);
      m_ui->m_cpuEcoPreset->setEnabled(true);
      m_ui->m_cpuBalancedPreset->setEnabled(true);
      m_ui->m_cpuMaxPreset->setEnabled(true);
    }
    m_solo_mining = false;
    m_mining_was_stopped = !_stoppedByNoPeers;
    m_miningStoppedByNoPeers = _stoppedByNoPeers;
    updateForecast();
  }
}

void MiningFrame::enableSolo() {
  m_sychronized = true;
  if (!m_solo_mining && !m_miner->is_mining()) {
    m_ui->m_startSolo->setEnabled(true);
    m_ui->m_stopSolo->setEnabled(false);
    m_ui->m_cpuEcoPreset->setEnabled(true);
    m_ui->m_cpuBalancedPreset->setEnabled(true);
    m_ui->m_cpuMaxPreset->setEnabled(true);
    if (!m_mining_was_stopped && Settings::instance().isMiningOnLaunchEnabled() && m_sychronized) {
      startSolo();
    }
  }
}

void MiningFrame::startStopSoloClicked(QAbstractButton* _button) {
  if (_button == m_ui->m_startSolo && m_ui->m_startSolo->isChecked() && m_wallet_closed != true) {
    startSolo();
  } else if (m_wallet_closed == true && _button == m_ui->m_stopSolo && m_ui->m_stopSolo->isChecked()) {
    m_ui->m_startSolo->setEnabled(false);
    stopSolo();
  } else if (_button == m_ui->m_stopSolo && m_ui->m_stopSolo->isChecked()) {
    stopSolo();
  }
}

void MiningFrame::setMiningThreads() {
  Settings::instance().setMiningThreads(m_ui->m_cpuCoresSpin->value());
}

void MiningFrame::onBlockHeightUpdated(quint64 _height) {
  if (m_miner->is_mining()) {
    m_miner->on_block_chain_update();
  }

  if (m_solo_mining) {
    appendMiningEvent(QStringLiteral("CHAIN"), tr("New block %1, refreshing template").arg(_height));
    addHashRateEventMarker(false);
  }

  quint64 difficulty = NodeAdapter::instance().getDifficulty();
  updateDifficulty(difficulty);
}

void MiningFrame::onPeerCountUpdated(quintptr _count) {
  if (NodeAdapter::instance().getNodeType() != NodeType::IN_PROCESS) {
    return;
  }

  if (_count == 0) {
    m_sychronized = false;
    if (m_solo_mining) {
      appendMiningEvent(QStringLiteral("PEERS"), tr("Mining stopped because there are no connected peers"));
      stopSolo(true);
    }

    setMiningStatusBadge(tr("No peers"), QStringLiteral("rgba(219, 178, 83, 75)"), QStringLiteral("#7a5a16"));
    m_ui->m_startSolo->setEnabled(false);
    m_ui->m_stopSolo->setEnabled(false);
    return;
  }

  if (m_sychronized && !m_wallet_closed) {
    enableSolo();
  }
}

void MiningFrame::onSynchronizationCompleted() {
  NodeType node = NodeAdapter::instance().getNodeType();
  if (node != NodeType::IN_PROCESS) {
    m_ui->m_startSolo->setEnabled(false);
    return;
  }
  // A full sync may have brought in coinbase transactions (blocks we found);
  // refresh lifetime stats and announce any found during this mining session.
  scanLifetimeStats(true);

  const bool resumeMining = m_miningStoppedByNoPeers && NodeAdapter::instance().getPeerCount() > 0;
  enableSolo();
  if (resumeMining && !m_solo_mining && !m_miner->is_mining()) {
    m_miningStoppedByNoPeers = false;
    startSolo();
  }
}

void MiningFrame::updateBalance(quint64 _balance) {
  Q_UNUSED(_balance);
}

void MiningFrame::updatePendingBalance(quint64 _balance) {
  Q_UNUSED(_balance);
}

void MiningFrame::onWalletTransactionsChanged() {
  // A new/updated transaction may be a coinbase (a block we mined). Announce it
  // if it appears while we are solo mining.
  scanLifetimeStats(true);
}

void MiningFrame::onWalletTransactionsReset() {
  // The whole transaction set was rebuilt (e.g. wallet reset/rescan): recount the
  // baseline from scratch without announcing historical blocks.
  resetLifetimeStats();
  scanLifetimeStats(false);
  updateForecast();
}

void MiningFrame::updateMinerLog(const QString& _message) {
  appendRawLogLine(_message);
}

void MiningFrame::updateCoreLog(const QString& _message) {
  appendRawLogLine(_message);
}

void MiningFrame::onMinerStarted(quint32 _threads, quint64 _difficulty) {
  updateDifficulty(_difficulty);
  setMiningStatusBadge(tr("Mining"), QStringLiteral("rgba(91, 171, 118, 65)"), QStringLiteral("#246d3f"));
  appendMiningEvent(QStringLiteral("START"), tr("Mining started with %n thread(s) at difficulty %1", nullptr, _threads).arg(formatMagnitude(_difficulty)));
}

void MiningFrame::onMinerStopped(quint32 _threads) {
  appendMiningEvent(QStringLiteral("STOP"), tr("Mining stopped, %n thread(s) finished", nullptr, _threads));
  if (!m_solo_mining || m_miner->is_mining()) {
    return;
  }

  if (m_soloHashRateTimerId != -1) {
    killTimer(m_soloHashRateTimerId);
    m_soloHashRateTimerId = -1;
  }

  if (m_minerRoutineTimerId != -1) {
    killTimer(m_minerRoutineTimerId);
    m_minerRoutineTimerId = -1;
  }

  addPoint(QDateTime::currentDateTime().toSecsSinceEpoch(), 0);
  setMiningStatusBadge(tr("Stopped"), QStringLiteral("rgba(191, 92, 92, 70)"), QStringLiteral("#7f3030"));
  m_ui->m_hashratelcdNumber->display(0.0);
  m_lastHashRate = 0;
  updateSessionStats();
  plot();

  if (!m_wallet_closed) {
    m_ui->m_startSolo->setEnabled(true);
    m_ui->m_stopSolo->setEnabled(false);
    m_ui->m_cpuCoresSpin->setEnabled(true);
    m_ui->m_cpuDial->setEnabled(true);
    m_ui->m_cpuEcoPreset->setEnabled(true);
    m_ui->m_cpuBalancedPreset->setEnabled(true);
    m_ui->m_cpuMaxPreset->setEnabled(true);
  }

  m_solo_mining = false;
  m_mining_was_stopped = true;
  m_miningStoppedByNoPeers = false;
  updateForecast();
}

void MiningFrame::onMinerThreadsChanged(quint32 _threads) {
  appendMiningEvent(QStringLiteral("CPU"), tr("CPU threads changed to %n", nullptr, _threads));
}

void MiningFrame::onMinerTemplateUpdated(quint64 _height, quint64 _difficulty) {
  Q_UNUSED(_height);
  updateDifficulty(_difficulty);
}

void MiningFrame::onMinerError(const QString& _message) {
  setMiningStatusBadge(tr("Error"), QStringLiteral("rgba(191, 92, 92, 70)"), QStringLiteral("#7f3030"));
  appendMiningEvent(QStringLiteral("ERROR"), _message);
}

void MiningFrame::coreDealTurned(int _cores) {
  updateCpuIntensity();
  scheduleMiningThreadsChange(_cores);
}

void MiningFrame::poolChanged() {
  if (m_miner->is_mining()) {
    m_miner->on_block_chain_update();
  }
}

}
