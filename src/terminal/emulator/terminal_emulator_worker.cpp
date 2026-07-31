#include "terminal/emulator/terminal_emulator_worker.h"

#include "terminal/emulator/native_terminal_widget_colors.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QMetaObject>
#include <QTimer>
#include <QUrl>

#include <utility>

namespace
{
constexpr int kOutputSliceBytes = 8 * 1024;
constexpr int kOutputMaxBytesPerDrain = 32 * 1024;
constexpr qint64 kOutputBudgetMs = 4;
constexpr int kSnapshotIntervalMs = 20;
constexpr int kSnapshotQuietPeriodMs = 4;
constexpr int kScrollbackPages = 10;
constexpr qint64 kMaxScrollbackBytes = 32LL * 1024 * 1024;

QString pathFromOsc7Payload(const QByteArray &payload)
{
    const QString text = QString::fromUtf8(payload);
    const QUrl url(text);
    if (url.scheme() == QStringLiteral("file"))
    {
        QString path;
        const QString host = url.host().toLower();
        if (host.isEmpty() || host == QStringLiteral("localhost"))
            path = QUrl::fromPercentEncoding(url.path().toUtf8());
        else
            path = url.toLocalFile();

        if (!path.isEmpty())
        {
            if (path.size() >= 3 && path.at(0) == QLatin1Char('/') && path.at(2) == QLatin1Char(':'))
                path.remove(0, 1);
            return QDir::fromNativeSeparators(path);
        }
    }

    const QString prefix = QStringLiteral("file:///");
    if (text.startsWith(prefix, Qt::CaseInsensitive))
    {
        QString path = QUrl::fromPercentEncoding(text.mid(prefix.size()).toUtf8());
        if (path.size() >= 2 && path.at(1) == QLatin1Char(':'))
            return QDir::fromNativeSeparators(path);
        return QDir::fromNativeSeparators(QStringLiteral("/") + path);
    }
    return QString();
}
}

TerminalEmulatorWorker::TerminalEmulatorWorker(QObject *parent)
    : QObject(parent)
{
}

TerminalEmulatorWorker::~TerminalEmulatorWorker()
{
    shutdown();
}

void TerminalEmulatorWorker::initialize(int cols, int rows)
{
    if (m_shuttingDown)
        return;
    if (!m_snapshotTimer)
    {
        m_snapshotTimer = new QTimer(this);
        m_snapshotTimer->setSingleShot(true);
        m_snapshotTimer->setInterval(kSnapshotIntervalMs);
        connect(m_snapshotTimer, &QTimer::timeout, this, &TerminalEmulatorWorker::publishSnapshot);
    }
    initializeTerminal(cols, rows);
    m_snapshotDirty = true;
    publishSnapshot();
}

void TerminalEmulatorWorker::initializeTerminal(int cols, int rows)
{
    if (m_vterm)
        vterm_free(m_vterm);

    m_gridSize = QSize(qMax(20, cols), qMax(5, rows));
    m_vterm = vterm_new(m_gridSize.height(), m_gridSize.width());
    vterm_set_utf8(m_vterm, 1);
    vterm_output_set_callback(m_vterm, &TerminalEmulatorWorker::outputCallback, this);

    m_screen = vterm_obtain_screen(m_vterm);
    static const VTermScreenCallbacks callbacks = {
        &TerminalEmulatorWorker::damageCallback,
        nullptr,
        &TerminalEmulatorWorker::cursorCallback,
        &TerminalEmulatorWorker::termPropCallback,
        &TerminalEmulatorWorker::bellCallback,
        &TerminalEmulatorWorker::resizeCallback,
        &TerminalEmulatorWorker::scrollbackPushCallback,
        &TerminalEmulatorWorker::scrollbackPopCallback,
        &TerminalEmulatorWorker::scrollbackClearCallback};
    vterm_screen_set_callbacks(m_screen, &callbacks, this);
    vterm_screen_enable_altscreen(m_screen, 1);
    vterm_screen_enable_reflow(m_screen, true);
    vterm_screen_set_damage_merge(m_screen, VTERM_DAMAGE_ROW);

    VTermColor fg;
    VTermColor bg;
    vterm_color_rgb(&fg, kDefaultForeground.red(), kDefaultForeground.green(), kDefaultForeground.blue());
    vterm_color_rgb(&bg, kDefaultBackground.red(), kDefaultBackground.green(), kDefaultBackground.blue());
    vterm_screen_set_default_colors(m_screen, &fg, &bg);

    VTermState *state = vterm_obtain_state(m_vterm);
    for (int i = 0; i < static_cast<int>(kAnsiPalette.size()); ++i)
    {
        const QColor &ansiColor = kAnsiPalette.at(i);
        VTermColor color;
        vterm_color_rgb(&color, ansiColor.red(), ansiColor.green(), ansiColor.blue());
        vterm_state_set_palette_color(state, i, &color);
    }

    m_scrollback.clear();
    m_scrollbackSequence = 0;
    m_alternateScreen = false;
    vterm_screen_reset(m_screen, 1);
    flushDamage();
}

void TerminalEmulatorWorker::enqueueOutput(const QByteArray &data)
{
    if (m_shuttingDown || data.isEmpty())
        return;
    m_pendingOutput.enqueue(PendingOutput{data, true});
    scheduleOutputDrain();
}

void TerminalEmulatorWorker::enqueueLocalOutput(const QByteArray &data)
{
    if (m_shuttingDown || data.isEmpty())
        return;
    m_pendingOutput.enqueue(PendingOutput{data, false});
    scheduleOutputDrain();
}

void TerminalEmulatorWorker::scheduleOutputDrain()
{
    if (m_outputDrainScheduled || m_pendingOutput.isEmpty() || m_shuttingDown)
        return;
    m_outputDrainScheduled = true;
    QMetaObject::invokeMethod(this, "drainOutput", Qt::QueuedConnection);
}

void TerminalEmulatorWorker::drainOutput()
{
    m_outputDrainScheduled = false;
    if (m_shuttingDown || !m_vterm)
    {
        m_pendingOutput.clear();
        return;
    }

    QElapsedTimer budget;
    budget.start();
    qint64 consumedBytes = 0;
    qint64 remoteConsumedBytes = 0;
    while (!m_pendingOutput.isEmpty() &&
           consumedBytes < kOutputMaxBytesPerDrain &&
           (consumedBytes == 0 || budget.elapsed() < kOutputBudgetMs))
    {
        PendingOutput &pending = m_pendingOutput.head();
        const int sliceBytes = qMin(kOutputSliceBytes, pending.data.size());
        const QByteArray slice = pending.data.left(sliceBytes);
        const bool remoteOutput = pending.remoteOutput;
        if (sliceBytes == pending.data.size())
            m_pendingOutput.dequeue();
        else
            pending.data.remove(0, sliceBytes);

        processOutput(slice, remoteOutput);
        consumedBytes += sliceBytes;
        if (remoteOutput)
            remoteConsumedBytes += sliceBytes;
    }
    if (remoteConsumedBytes > 0)
        emit outputConsumed(remoteConsumedBytes);
    scheduleOutputDrain();
}

void TerminalEmulatorWorker::processOutput(const QByteArray &data, bool remoteOutput)
{
    if (!m_vterm || data.isEmpty())
        return;
    const QByteArray output = remoteOutput
                                  ? normalizePipeTerminalOutput(filterTerminalOutput(data))
                                  : data;
    if (output.isEmpty())
        return;
    parseOsc7Directory(output);
    vterm_input_write(m_vterm, output.constData(), static_cast<size_t>(output.size()));
    flushDamage();
    m_snapshotDirty = true;
    scheduleSnapshot();
    if (remoteOutput)
        emit processedOutput(output);
}

void TerminalEmulatorWorker::setGridSize(int cols, int rows)
{
    if (m_shuttingDown || !m_vterm)
        return;
    const QSize size(qMax(20, cols), qMax(5, rows));
    if (size == m_gridSize)
        return;
    m_gridSize = size;
    vterm_set_size(m_vterm, m_gridSize.height(), m_gridSize.width());
    trimScrollback();
    flushDamage();
    m_snapshotDirty = true;
    scheduleSnapshot();
}

void TerminalEmulatorWorker::setLocalEchoEnabled(bool enabled)
{
    m_localEchoEnabled = enabled;
}

void TerminalEmulatorWorker::setPipeMode(bool enabled)
{
    m_pipeMode = enabled;
    m_lastPipeOutputWasCr = false;
}

void TerminalEmulatorWorker::setPromptEchoFiltering(bool enabled)
{
    m_filterPromptEcho = enabled;
    if (!enabled)
        m_terminalFilterPending.clear();
}

void TerminalEmulatorWorker::sendKey(int key, int modifiers)
{
    if (!m_vterm || m_shuttingDown)
        return;
    vterm_keyboard_key(m_vterm, static_cast<VTermKey>(key), static_cast<VTermModifier>(modifiers));
    scheduleSnapshot();
}

void TerminalEmulatorWorker::sendText(const QString &text, int modifiers)
{
    if (!m_vterm || m_shuttingDown || text.isEmpty())
        return;
    const VTermModifier vtermModifiers = static_cast<VTermModifier>(modifiers);
    if (vtermModifiers == VTERM_MOD_NONE || vtermModifiers == VTERM_MOD_SHIFT)
    {
        const QByteArray bytes = terminalInputBytesFromText(text);
        handleOutputBytes(bytes.constData(), static_cast<size_t>(bytes.size()));
        return;
    }

    for (const uint ucs4 : text.toUcs4())
    {
        if (ucs4 < 0x20 || ucs4 == 0x7f)
        {
            const QByteArray bytes = terminalInputBytesFromText(text);
            handleOutputBytes(bytes.constData(), static_cast<size_t>(bytes.size()));
            return;
        }
    }
    for (const uint ucs4 : text.toUcs4())
        vterm_keyboard_unichar(m_vterm, ucs4, vtermModifiers);
    scheduleSnapshot();
}

void TerminalEmulatorWorker::sendInputBytes(const QByteArray &data)
{
    if (m_shuttingDown || data.isEmpty())
        return;
    handleOutputBytes(data.constData(), static_cast<size_t>(data.size()));
}

void TerminalEmulatorWorker::sendClipboardPaste(const QString &text)
{
    if (!m_vterm || m_shuttingDown || text.isEmpty())
        return;
    const QByteArray bytes = terminalInputBytesFromText(text);
    vterm_keyboard_start_paste(m_vterm);
    handleOutputBytes(bytes.constData(), static_cast<size_t>(bytes.size()));
    vterm_keyboard_end_paste(m_vterm);
    scheduleSnapshot();
}

void TerminalEmulatorWorker::sendMouseMove(int row, int col, int modifiers)
{
    if (!m_vterm || m_shuttingDown)
        return;
    vterm_mouse_move(m_vterm, row, col, static_cast<VTermModifier>(modifiers));
}

void TerminalEmulatorWorker::sendMouseButton(int button, bool pressed, int modifiers)
{
    if (!m_vterm || m_shuttingDown)
        return;
    vterm_mouse_button(m_vterm, button, pressed, static_cast<VTermModifier>(modifiers));
}

void TerminalEmulatorWorker::clearScreenAndScrollback(bool notifyRemote)
{
    if (m_shuttingDown || !m_vterm)
        return;
    clearScrollback();
    vterm_input_write(m_vterm, "\x1b[2J\x1b[H", 7);
    flushDamage();
    if (notifyRemote)
        emit inputGenerated(QByteArray(1, '\x0c'));
    emit screenCleared();
    m_snapshotDirty = true;
    publishSnapshot();
}

void TerminalEmulatorWorker::handleOutputBytes(const char *data, size_t len)
{
    if (!data || len == 0 || m_shuttingDown)
        return;
    if (m_localEchoEnabled && m_vterm)
    {
        const QByteArray echo = localEchoBytes(data, len);
        if (!echo.isEmpty())
        {
            vterm_input_write(m_vterm, echo.constData(), static_cast<size_t>(echo.size()));
            flushDamage();
            m_snapshotDirty = true;
            scheduleSnapshot();
        }
    }
    emit inputGenerated(QByteArray(data, static_cast<int>(len)));
}

QByteArray TerminalEmulatorWorker::terminalInputBytesFromText(const QString &text) const
{
    QByteArray bytes;
    QString pendingText;
    for (int i = 0; i < text.size(); ++i)
    {
        const QChar ch = text.at(i);
        if (ch == QLatin1Char('\r') || ch == QLatin1Char('\n'))
        {
            if (!pendingText.isEmpty())
            {
                bytes.append(pendingText.toUtf8());
                pendingText.clear();
            }
            if (ch == QLatin1Char('\r') && i + 1 < text.size() && text.at(i + 1) == QLatin1Char('\n'))
                ++i;
            bytes.append('\r');
        }
        else
        {
            pendingText.append(ch);
        }
    }
    if (!pendingText.isEmpty())
        bytes.append(pendingText.toUtf8());
    return bytes;
}

QByteArray TerminalEmulatorWorker::localEchoBytes(const char *data, size_t len) const
{
    const QByteArray source(data, static_cast<int>(len));
    if (source.contains('\x1b'))
        return QByteArray();

    const QString text = QString::fromUtf8(source);
    QByteArray echo;
    for (const QChar ch : text)
    {
        if (ch == QLatin1Char('\r') || ch == QLatin1Char('\n'))
            echo.append("\r\n");
        else if (ch == QLatin1Char('\b') || ch.unicode() == 0x7f)
            echo.append("\b \b");
        else if (ch == QLatin1Char('\t') || ch.unicode() >= 0x20)
            echo.append(QString(ch).toUtf8());
    }
    return echo;
}

QByteArray TerminalEmulatorWorker::filterTerminalOutput(const QByteArray &data)
{
    if (data.isEmpty())
        return data;

    QByteArray output = m_terminalFilterPending + data;
    m_terminalFilterPending.clear();
    const QByteArray markers[] = {
        QByteArrayLiteral("prompt $E]7;file:"),
        QByteArrayLiteral("$E]7;file:///$P$E\\$G$S$P$G"),
        QByteArrayLiteral("function global:prompt"),
        QByteArrayLiteral("export AIRAN_OLD_PROMPT_COMMAND="),
        QByteArrayLiteral("export PROMPT_COMMAND="),
        QByteArrayLiteral("file://localhost%s")};

    for (;;)
    {
        int marker = -1;
        for (const QByteArray &candidate : markers)
        {
            const int index = output.indexOf(candidate);
            if (index >= 0 && (marker < 0 || index < marker))
                marker = index;
        }
        if (marker < 0)
            break;

        const int lineStart = qMax(output.lastIndexOf('\r', marker), output.lastIndexOf('\n', marker)) + 1;
        int lineEnd = output.indexOf('\r', marker);
        const int lf = output.indexOf('\n', marker);
        if (lineEnd < 0 || (lf >= 0 && lf < lineEnd))
            lineEnd = lf;
        if (lineEnd < 0)
        {
            m_terminalFilterPending = output.mid(lineStart);
            output.truncate(lineStart);
            break;
        }

        int removeEnd = lineEnd + 1;
        while (removeEnd < output.size() &&
               (output.at(removeEnd) == '\r' || output.at(removeEnd) == '\n'))
            ++removeEnd;
        output.remove(lineStart, removeEnd - lineStart);
        m_filterPromptEcho = false;
    }

    if (m_filterPromptEcho)
    {
        constexpr int kMarkerLookbehindBytes = 96;
        const int keepBytes = qMin(output.size(), kMarkerLookbehindBytes);
        const QByteArray tail = output.right(keepBytes);
        if (tail.contains("prompt ") || tail.contains("$E]7;") || tail.contains("function global"))
        {
            m_terminalFilterPending = tail;
            output.chop(keepBytes);
        }
    }
    return output;
}

QByteArray TerminalEmulatorWorker::normalizePipeTerminalOutput(const QByteArray &data)
{
    if (data.isEmpty() || !m_pipeMode)
        return data;

    QByteArray output;
    output.reserve(data.size() + 16);
    for (const char ch : data)
    {
        if (ch == '\n' && !m_lastPipeOutputWasCr)
            output.append('\r');
        output.append(ch);
        m_lastPipeOutputWasCr = ch == '\r';
        if (ch == '\n')
            m_lastPipeOutputWasCr = false;
    }
    return output;
}

void TerminalEmulatorWorker::scheduleSnapshot()
{
    if (m_shuttingDown || !m_snapshotDirty || !m_snapshotTimer)
        return;

    if (!m_snapshotBurstActive)
    {
        m_snapshotBurstTimer.start();
        m_snapshotBurstActive = true;
    }

    const int elapsedMs = static_cast<int>(qMin<qint64>(m_snapshotBurstTimer.elapsed(), kSnapshotIntervalMs));
    const int remainingMs = qMax(0, kSnapshotIntervalMs - elapsedMs);
    m_snapshotTimer->start(qMin(kSnapshotQuietPeriodMs, remainingMs));
}

void TerminalEmulatorWorker::publishSnapshot()
{
    if (m_shuttingDown || !m_snapshotDirty || !m_screen)
    {
        m_snapshotBurstActive = false;
        return;
    }
    m_snapshotDirty = false;
    m_snapshotBurstActive = false;
    emit snapshotReady(buildSnapshot());
}

TerminalEmulatorSnapshot TerminalEmulatorWorker::buildSnapshot() const
{
    TerminalEmulatorSnapshot snapshot;
    snapshot.gridSize = m_gridSize;
    snapshot.scrollbackLines = static_cast<int>(m_scrollback.size());
    snapshot.scrollbackSequence = m_scrollbackSequence;
    snapshot.cursorPosition = QPoint(m_cursorPos.col, m_cursorPos.row);
    snapshot.cursorVisible = m_cursorVisible;
    snapshot.cursorBlink = m_cursorBlink;
    snapshot.cursorShape = m_cursorShape;
    snapshot.mouseMode = m_mouseMode;
    snapshot.alternateScreen = m_alternateScreen;
    snapshot.focusReport = m_focusReport;

    const int cols = m_gridSize.width();
    const int rows = m_gridSize.height();
    snapshot.cells.resize((snapshot.scrollbackLines + rows) * cols);
    int targetRow = 0;
    for (const QVector<VTermScreenCell> &line : m_scrollback)
    {
        for (int col = 0; col < cols; ++col)
            snapshot.cells[targetRow * cols + col] = col < line.size() ? line.at(col) : blankCell();
        ++targetRow;
    }
    for (int row = 0; row < rows; ++row, ++targetRow)
    {
        for (int col = 0; col < cols; ++col)
        {
            VTermScreenCell cell{};
            const VTermPos pos{row, col};
            if (!vterm_screen_get_cell(m_screen, pos, &cell))
                cell = blankCell();
            else
                cell = resolvedCell(cell);
            snapshot.cells[targetRow * cols + col] = cell;
        }
    }
    return snapshot;
}

VTermScreenCell TerminalEmulatorWorker::resolvedCell(VTermScreenCell cell) const
{
    if (m_screen)
    {
        vterm_screen_convert_color_to_rgb(m_screen, &cell.fg);
        vterm_screen_convert_color_to_rgb(m_screen, &cell.bg);
    }
    return cell;
}

VTermScreenCell TerminalEmulatorWorker::blankCell() const
{
    VTermScreenCell cell{};
    cell.width = 1;
    vterm_color_rgb(&cell.fg, kDefaultForeground.red(), kDefaultForeground.green(), kDefaultForeground.blue());
    vterm_color_rgb(&cell.bg, kDefaultBackground.red(), kDefaultBackground.green(), kDefaultBackground.blue());
    return cell;
}

int TerminalEmulatorWorker::maxScrollbackLines() const
{
    const int cols = qMax(1, m_gridSize.width());
    const int pageLines = qMax(1, m_gridSize.height()) * kScrollbackPages;
    const qint64 bytesPerLine = static_cast<qint64>(cols) * sizeof(VTermScreenCell);
    const int memoryLines = static_cast<int>(qMax<qint64>(1, kMaxScrollbackBytes / qMax<qint64>(1, bytesPerLine)));
    return qMin(pageLines, memoryLines);
}

void TerminalEmulatorWorker::trimScrollback()
{
    const int limit = maxScrollbackLines();
    while (static_cast<int>(m_scrollback.size()) > limit)
        m_scrollback.pop_front();
}

void TerminalEmulatorWorker::clearScrollback()
{
    m_scrollback.clear();
}

void TerminalEmulatorWorker::flushDamage()
{
    if (m_screen)
        vterm_screen_flush_damage(m_screen);
}

void TerminalEmulatorWorker::parseOsc7Directory(const QByteArray &data)
{
    int pos = 0;
    while ((pos = data.indexOf("\x1b]7;", pos)) >= 0)
    {
        const int start = pos + 4;
        int end = data.indexOf('\x07', start);
        int terminatorLength = 1;
        const int stEnd = data.indexOf("\x1b\\", start);
        if (end < 0 || (stEnd >= 0 && stEnd < end))
        {
            end = stEnd;
            terminatorLength = 2;
        }
        if (end < 0)
            return;
        const QString path = pathFromOsc7Payload(data.mid(start, end - start));
        if (!path.isEmpty())
            emit currentDirectoryChanged(path);
        pos = end + terminatorLength;
    }
}

void TerminalEmulatorWorker::shutdown()
{
    if (m_shuttingDown)
        return;
    m_shuttingDown = true;
    if (m_snapshotTimer)
        m_snapshotTimer->stop();
    m_snapshotBurstActive = false;
    m_pendingOutput.clear();
    m_terminalFilterPending.clear();
    m_scrollback.clear();
    if (m_vterm)
    {
        vterm_free(m_vterm);
        m_vterm = nullptr;
        m_screen = nullptr;
    }
}

void TerminalEmulatorWorker::outputCallback(const char *data, size_t len, void *user)
{
    static_cast<TerminalEmulatorWorker *>(user)->handleOutputBytes(data, len);
}

int TerminalEmulatorWorker::damageCallback(VTermRect, void *user)
{
    static_cast<TerminalEmulatorWorker *>(user)->m_snapshotDirty = true;
    return 1;
}

int TerminalEmulatorWorker::cursorCallback(VTermPos pos, VTermPos, int visible, void *user)
{
    auto *worker = static_cast<TerminalEmulatorWorker *>(user);
    worker->m_cursorPos = pos;
    worker->m_cursorVisible = visible;
    worker->m_snapshotDirty = true;
    return 1;
}

int TerminalEmulatorWorker::termPropCallback(VTermProp prop, VTermValue *value, void *user)
{
    auto *worker = static_cast<TerminalEmulatorWorker *>(user);
    switch (prop)
    {
    case VTERM_PROP_CURSORVISIBLE:
        worker->m_cursorVisible = value->boolean;
        break;
    case VTERM_PROP_CURSORBLINK:
        worker->m_cursorBlink = value->boolean;
        break;
    case VTERM_PROP_ALTSCREEN:
        worker->m_alternateScreen = value->boolean;
        break;
    case VTERM_PROP_CURSORSHAPE:
        worker->m_cursorShape = value->number;
        break;
    case VTERM_PROP_MOUSE:
        worker->m_mouseMode = value->number;
        break;
    case VTERM_PROP_FOCUSREPORT:
        worker->m_focusReport = value->boolean;
        break;
    case VTERM_PROP_TITLE:
        emit worker->terminalTitleChanged(QString::fromUtf8(value->string.str, static_cast<int>(value->string.len)));
        break;
    default:
        break;
    }
    worker->m_snapshotDirty = true;
    return 1;
}

int TerminalEmulatorWorker::bellCallback(void *user)
{
    emit static_cast<TerminalEmulatorWorker *>(user)->bell();
    return 1;
}

int TerminalEmulatorWorker::resizeCallback(int rows, int cols, void *user)
{
    auto *worker = static_cast<TerminalEmulatorWorker *>(user);
    worker->m_gridSize = QSize(cols, rows);
    worker->trimScrollback();
    worker->m_snapshotDirty = true;
    return 1;
}

int TerminalEmulatorWorker::scrollbackPushCallback(int cols, const VTermScreenCell *cells, void *user)
{
    auto *worker = static_cast<TerminalEmulatorWorker *>(user);
    if (!worker || cols <= 0 || !cells)
        return 1;
    QVector<VTermScreenCell> line;
    line.reserve(cols);
    for (int col = 0; col < cols; ++col)
        line.append(worker->resolvedCell(cells[col]));
    worker->m_scrollback.push_back(std::move(line));
    ++worker->m_scrollbackSequence;
    worker->trimScrollback();
    worker->m_snapshotDirty = true;
    return 1;
}

int TerminalEmulatorWorker::scrollbackPopCallback(int cols, VTermScreenCell *cells, void *user)
{
    auto *worker = static_cast<TerminalEmulatorWorker *>(user);
    if (!worker || cols <= 0 || !cells || worker->m_scrollback.empty())
        return 0;
    const QVector<VTermScreenCell> line = std::move(worker->m_scrollback.back());
    worker->m_scrollback.pop_back();
    for (int col = 0; col < cols; ++col)
        cells[col] = col < line.size() ? line.at(col) : worker->blankCell();
    worker->m_snapshotDirty = true;
    return 1;
}

int TerminalEmulatorWorker::scrollbackClearCallback(void *user)
{
    auto *worker = static_cast<TerminalEmulatorWorker *>(user);
    if (worker)
    {
        worker->clearScrollback();
        worker->m_snapshotDirty = true;
    }
    return 1;
}
