#include "terminal/emulator/native_terminal_widget.h"

#include <QApplication>
#include <QEvent>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QMetaObject>
#include <QMutexLocker>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStyle>
#include <QTimer>

namespace
{
constexpr int kScrollBarWidth = 10;
constexpr int kScrollBarActiveDurationMs = 850;
}


NativeTerminalWidget::NativeTerminalWidget(QWidget *parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_InputMethodEnabled);

    const QString preferredFont = preferredTerminalFontFamily();
    m_font = preferredFont.isEmpty()
                 ? QFontDatabase::systemFont(QFontDatabase::FixedFont)
                 : QFont(preferredFont);
    m_font.setPointSize(11);
    m_font.setStyleHint(QFont::Monospace);
    m_font.setFixedPitch(true);
    setFont(m_font);

    QFontMetrics metrics(m_font);
#if QT_VERSION >= QT_VERSION_CHECK(5, 11, 0)
    m_cellWidth = qMax(1, metrics.horizontalAdvance(QLatin1Char('M')));
#else
    m_cellWidth = qMax(1, metrics.width(QLatin1Char('M')));
#endif
    m_cellHeight = qMax(1, metrics.lineSpacing());
    m_ascent = metrics.ascent();

    m_scrollBar = new QScrollBar(Qt::Vertical, this);
    m_scrollBar->setObjectName(QStringLiteral("terminalScrollBar"));
    m_scrollBar->setFocusPolicy(Qt::NoFocus);
    m_scrollBar->setAttribute(Qt::WA_Hover);
    m_scrollBar->setContextMenuPolicy(Qt::NoContextMenu);
    m_scrollBar->installEventFilter(this);
    m_scrollBar->setFixedWidth(kScrollBarWidth);
    m_scrollBar->setStyleSheet(QStringLiteral(
        "QScrollBar#terminalScrollBar:vertical {"
        "  background: transparent; border: none; width: 10px; margin: 2px 0;"
        "}"
        "QScrollBar#terminalScrollBar::handle:vertical {"
        "  background: rgba(126, 147, 163, 72); border: none; border-radius: 3px;"
        "  min-height: 28px; margin: 0 2px;"
        "}"
        "QScrollBar#terminalScrollBar::handle:vertical:hover {"
        "  background: rgba(137, 184, 214, 205); margin: 0 1px; border-radius: 4px;"
        "}"
        "QScrollBar#terminalScrollBar[hovered=\"true\"]::handle:vertical {"
        "  background: rgba(137, 184, 214, 165); margin: 0 1px; border-radius: 4px;"
        "}"
        "QScrollBar#terminalScrollBar[active=\"true\"]::handle:vertical {"
        "  background: rgba(131, 193, 224, 172); margin: 0 1px; border-radius: 4px;"
        "}"
        "QScrollBar#terminalScrollBar::add-line:vertical,"
        "QScrollBar#terminalScrollBar::sub-line:vertical {"
        "  border: none; background: transparent; height: 0;"
        "}"
        "QScrollBar#terminalScrollBar::add-page:vertical,"
        "QScrollBar#terminalScrollBar::sub-page:vertical { background: transparent; }"));
    m_scrollBar->hide();

    m_scrollBarInteractionTimer = new QTimer(this);
    m_scrollBarInteractionTimer->setSingleShot(true);
    m_scrollBarInteractionTimer->setInterval(kScrollBarActiveDurationMs);
    connect(m_scrollBarInteractionTimer, &QTimer::timeout, this, [this]() {
        m_scrollBar->setProperty("active", false);
        m_scrollBar->style()->unpolish(m_scrollBar);
        m_scrollBar->style()->polish(m_scrollBar);
        m_scrollBar->update();
    });
    connect(m_scrollBar, &QScrollBar::valueChanged, this, [this](int value) {
        setScrollbackOffset(m_scrollBar->maximum() - value);
    });
    connect(m_scrollBar, &QScrollBar::sliderPressed, this, &NativeTerminalWidget::markScrollBarInteraction);

    startWorker();
    updateScrollBar();
    updateCursorBlink();
}


NativeTerminalWidget::~NativeTerminalWidget()
{
    shutdownWorker();
}


void NativeTerminalWidget::startWorker()
{
    m_workerThread = new QThread();
    m_workerThread->setObjectName(QStringLiteral("NativeTerminalWidget-EmulatorThread"));
    m_worker = new TerminalEmulatorWorker();
    m_worker->moveToThread(m_workerThread);
    connect(m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &TerminalEmulatorWorker::snapshotReady,
            this, &NativeTerminalWidget::enqueueSnapshot, Qt::DirectConnection);
    connect(m_worker, &TerminalEmulatorWorker::outputConsumed,
            this, &NativeTerminalWidget::outputConsumed, Qt::DirectConnection);
    connect(m_worker, &TerminalEmulatorWorker::processedOutput,
            this, &NativeTerminalWidget::processedOutput, Qt::DirectConnection);
    connect(m_worker, &TerminalEmulatorWorker::inputGenerated,
            this, &NativeTerminalWidget::inputGenerated, Qt::DirectConnection);
    connect(m_worker, &TerminalEmulatorWorker::terminalTitleChanged,
            this, &NativeTerminalWidget::terminalTitleChanged);
    connect(m_worker, &TerminalEmulatorWorker::currentDirectoryChanged,
            this, &NativeTerminalWidget::currentDirectoryChanged);
    connect(m_worker, &TerminalEmulatorWorker::screenCleared,
            this, &NativeTerminalWidget::screenCleared);
    connect(m_worker, &TerminalEmulatorWorker::bell, this, []() {
        QApplication::beep();
    });
    m_workerThread->start();
    QMetaObject::invokeMethod(m_worker, "initialize", Qt::QueuedConnection,
                              Q_ARG(int, m_gridSize.width()),
                              Q_ARG(int, m_gridSize.height()));
}


void NativeTerminalWidget::shutdownWorker()
{
    if (m_workerClosing.exchange(true))
        return;
    QMutexLocker workerLocker(&m_workerMutex);
    QThread *workerThread = m_workerThread;
    TerminalEmulatorWorker *worker = m_worker;
    m_workerThread = nullptr;
    m_worker = nullptr;
    if (worker)
    {
        QObject::disconnect(worker, nullptr, this, nullptr);
        if (workerThread && workerThread->isRunning())
            QMetaObject::invokeMethod(worker, "shutdown", Qt::QueuedConnection);
    }
    if (workerThread)
    {
        workerThread->quit();
        QObject::connect(workerThread, &QThread::finished,
                         workerThread, &QObject::deleteLater);
    }
    QMutexLocker locker(&m_snapshotMutex);
    m_pendingSnapshot.reset();
    m_snapshot.reset();
    m_snapshotApplyScheduled = false;
}


bool NativeTerminalWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_scrollBar && event)
    {
        if (event->type() == QEvent::Enter || event->type() == QEvent::Leave)
        {
            m_scrollBar->setProperty("hovered", event->type() == QEvent::Enter);
            m_scrollBar->style()->unpolish(m_scrollBar);
            m_scrollBar->style()->polish(m_scrollBar);
            m_scrollBar->update();
        }
    }
    return QWidget::eventFilter(watched, event);
}


QSize NativeTerminalWidget::gridSize() const
{
    return m_gridSize;
}


void NativeTerminalWidget::setLocalEchoEnabled(bool enabled)
{
    if (m_worker && !m_workerClosing.load())
        QMetaObject::invokeMethod(m_worker, "setLocalEchoEnabled", Qt::QueuedConnection,
                                  Q_ARG(bool, enabled));
}

void NativeTerminalWidget::setPipeMode(bool enabled)
{
    if (m_worker && !m_workerClosing.load())
        QMetaObject::invokeMethod(m_worker, "setPipeMode", Qt::QueuedConnection,
                                  Q_ARG(bool, enabled));
}

void NativeTerminalWidget::setPromptEchoFiltering(bool enabled)
{
    if (m_worker && !m_workerClosing.load())
        QMetaObject::invokeMethod(m_worker, "setPromptEchoFiltering", Qt::QueuedConnection,
                                  Q_ARG(bool, enabled));
}


void NativeTerminalWidget::setScrollbackOffset(int offset)
{
    const int scrollbackLines = m_snapshot ? m_snapshot->scrollbackLines : 0;
    const int boundedOffset = qBound(0, offset, scrollbackLines);
    if (boundedOffset == m_scrollbackOffset)
    {
        updateScrollBar();
        return;
    }

    m_scrollbackOffset = boundedOffset;
    updateScrollBar();
    markScrollBarInteraction();
    update();
}


void NativeTerminalWidget::updateScrollBar()
{
    if (!m_scrollBar)
        return;

    const int maximum = m_snapshot ? m_snapshot->scrollbackLines : 0;
    const QSignalBlocker blocker(m_scrollBar);
    m_scrollBar->setRange(0, maximum);
    m_scrollBar->setPageStep(qMax(1, m_gridSize.height()));
    m_scrollBar->setSingleStep(3);
    m_scrollBar->setValue(maximum - qBound(0, m_scrollbackOffset, maximum));
    m_scrollBar->setVisible(maximum > 0);
    if (maximum > 0)
        m_scrollBar->raise();
}


void NativeTerminalWidget::markScrollBarInteraction()
{
    if (!m_scrollBar || !m_scrollBarInteractionTimer)
        return;

    if (!m_scrollBar->property("active").toBool())
    {
        m_scrollBar->setProperty("active", true);
        m_scrollBar->style()->unpolish(m_scrollBar);
        m_scrollBar->style()->polish(m_scrollBar);
        m_scrollBar->update();
    }
    m_scrollBarInteractionTimer->start();
}
