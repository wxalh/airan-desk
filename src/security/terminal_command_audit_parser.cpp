#include "terminal_command_audit_parser.h"

#include <QtGlobal>

namespace
{
constexpr int kMaxCommandBytes = 64 * 1024;

bool isLineBreak(unsigned char byte)
{
    return byte == '\r' || byte == '\n';
}

bool isPrintableInput(unsigned char byte)
{
    return byte == '\t' || byte >= 0x20;
}

void removeLastUtf8CodePoint(QByteArray *text)
{
    if (!text || text->isEmpty())
        return;
    int index = text->size() - 1;
    while (index > 0 && (static_cast<unsigned char>(text->at(index)) & 0xc0) == 0x80)
        --index;
    text->truncate(index);
}
} /* namespace */

TerminalCommandAuditParser::~TerminalCommandAuditParser()
{
    reset();
}

void TerminalCommandAuditParser::initialize(int cols, int rows, EchoMode mode)
{
    reset();
    m_cols = qMax(20, cols);
    m_rows = qMax(5, rows);
    m_echoMode = mode;
    m_vterm = vterm_new(m_rows, m_cols);
    vterm_set_utf8(m_vterm, 1);
    m_screen = vterm_obtain_screen(m_vterm);
    m_state = vterm_obtain_state(m_vterm);
    static const VTermScreenCallbacks callbacks = {
        nullptr,
        nullptr,
        &TerminalCommandAuditParser::cursorCallback,
        &TerminalCommandAuditParser::termPropCallback,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr};
    vterm_screen_set_callbacks(m_screen, &callbacks, this);
    vterm_screen_enable_altscreen(m_screen, 1);
    vterm_screen_enable_reflow(m_screen, true);
    vterm_screen_reset(m_screen, 1);
    vterm_state_get_cursorpos(m_state, &m_cursor);
}

void TerminalCommandAuditParser::resize(int cols, int rows)
{
    if (!m_vterm)
        return;
    m_cols = qMax(20, cols);
    m_rows = qMax(5, rows);
    vterm_set_size(m_vterm, m_rows, m_cols);
    vterm_state_get_cursorpos(m_state, &m_cursor);
}

void TerminalCommandAuditParser::setEchoMode(EchoMode mode)
{
    if (m_echoMode == mode)
        return;
    resetCommandState();
    m_echoMode = mode;
}

QVector<TerminalCommandAuditRecord> TerminalCommandAuditParser::noteInput(const QByteArray &data)
{
    QVector<TerminalCommandAuditRecord> records;
    if (!m_vterm || data.isEmpty() || m_alternateScreen)
        return records;

    for (const char raw : data)
    {
        const unsigned char byte = static_cast<unsigned char>(raw);
        if (isLineBreak(byte))
        {
            if (!m_inputActive && !m_secretInput && !m_unavailable)
                continue;
            if (m_echoMode == EchoMode::PipeFallback)
            {
                const TerminalCommandAuditRecord record = committedRecord();
                if (record.kind != TerminalCommandAuditRecord::Command ||
                    !record.command.trimmed().isEmpty())
                {
                    records.append(record);
                }
                resetCommandState();
            }
            else
            {
                m_submitPending = true;
            }
            continue;
        }

        if (byte == 0x03 || byte == 0x04)
        {
            resetCommandState();
            continue;
        }

        beginInputIfNeeded();
        if (m_echoMode == EchoMode::PipeFallback)
            applyFallbackByte(byte);
    }
    return records;
}

QVector<TerminalCommandAuditRecord> TerminalCommandAuditParser::processOutput(const QByteArray &data)
{
    QVector<TerminalCommandAuditRecord> records;
    if (!m_vterm || data.isEmpty())
        return records;

    for (const char raw : data)
    {
        const unsigned char byte = static_cast<unsigned char>(raw);
        if (m_submitPending && isLineBreak(byte))
        {
            if (!m_alternateScreen)
            {
                const TerminalCommandAuditRecord record = committedRecord();
                if (record.kind != TerminalCommandAuditRecord::Command ||
                    !record.command.trimmed().isEmpty())
                {
                    records.append(record);
                }
            }
            resetCommandState();
        }
        vterm_input_write(m_vterm, &raw, 1);
    }
    vterm_screen_flush_damage(m_screen);
    vterm_state_get_cursorpos(m_state, &m_cursor);
    return records;
}

void TerminalCommandAuditParser::setSecretInput(bool secret)
{
    m_secretInput = secret;
}

void TerminalCommandAuditParser::reset()
{
    if (m_vterm)
        vterm_free(m_vterm);
    m_vterm = nullptr;
    m_screen = nullptr;
    m_state = nullptr;
    m_cursor = VTermPos{0, 0};
    m_alternateScreen = false;
    resetCommandState();
}

void TerminalCommandAuditParser::beginInputIfNeeded()
{
    if (m_inputActive)
        return;
    vterm_state_get_cursorpos(m_state, &m_cursor);
    m_promptColumn = qBound(0, m_cursor.col, m_cols - 1);
    m_inputActive = true;
}

void TerminalCommandAuditParser::resetCommandState()
{
    m_fallbackInput.clear();
    m_promptColumn = 0;
    m_inputActive = false;
    m_submitPending = false;
    m_secretInput = false;
    m_unavailable = false;
}

void TerminalCommandAuditParser::applyFallbackByte(unsigned char byte)
{
    if (byte == 0x08 || byte == 0x7f)
    {
        removeLastUtf8CodePoint(&m_fallbackInput);
        return;
    }
    if (!isPrintableInput(byte) || byte == '\t')
    {
        m_unavailable = true;
        return;
    }
    if (m_fallbackInput.size() >= kMaxCommandBytes)
    {
        m_unavailable = true;
        return;
    }
    m_fallbackInput.append(static_cast<char>(byte));
}

QString TerminalCommandAuditParser::commandFromScreen(bool *reliable) const
{
    if (reliable)
        *reliable = false;
    if (!m_screen || !m_state)
        return QString();

    VTermPos cursor = m_cursor;
    vterm_state_get_cursorpos(m_state, &cursor);
    int firstRow = qBound(0, cursor.row, m_rows - 1);
    while (firstRow > 0)
    {
        const VTermLineInfo *lineInfo = vterm_state_get_lineinfo(m_state, firstRow);
        if (!lineInfo || !lineInfo->continuation)
            break;
        --firstRow;
    }
    if (firstRow == 0)
    {
        const VTermLineInfo *firstLineInfo = vterm_state_get_lineinfo(m_state, 0);
        if (firstLineInfo && firstLineInfo->continuation)
            return QString();
    }

    QString command;
    for (int row = firstRow; row <= cursor.row && row < m_rows; ++row)
    {
        const int startCol = row == firstRow ? qBound(0, m_promptColumn, m_cols) : 0;
        const int endCol = row == cursor.row ? qBound(0, cursor.col, m_cols) : m_cols;
        for (int col = startCol; col < endCol; ++col)
        {
            VTermScreenCell cell{};
            if (!vterm_screen_get_cell(m_screen, VTermPos{row, col}, &cell))
                return QString();
            if (cell.chars[0] == 0)
                continue;
            int count = 0;
            while (count < VTERM_MAX_CHARS_PER_CELL && cell.chars[count] != 0)
                ++count;
            command.append(QString::fromUcs4(cell.chars, count));
            if (command.toUtf8().size() > kMaxCommandBytes)
                return QString();
        }
    }

    while (command.endsWith(QLatin1Char(' ')))
        command.chop(1);
    if (reliable)
        *reliable = true;
    return command;
}

TerminalCommandAuditRecord TerminalCommandAuditParser::committedRecord() const
{
    if (m_secretInput)
        return TerminalCommandAuditRecord{TerminalCommandAuditRecord::Redacted, QString()};
    if (m_unavailable)
        return TerminalCommandAuditRecord{TerminalCommandAuditRecord::Unavailable, QString()};

    bool reliable = true;
    const QString command = m_echoMode == EchoMode::PipeFallback
                                ? QString::fromUtf8(m_fallbackInput)
                                : commandFromScreen(&reliable);
    if (!reliable || command.contains(QChar::ReplacementCharacter) ||
        command.toUtf8().size() > kMaxCommandBytes)
    {
        return TerminalCommandAuditRecord{TerminalCommandAuditRecord::Unavailable, QString()};
    }
    return TerminalCommandAuditRecord{TerminalCommandAuditRecord::Command, command};
}

int TerminalCommandAuditParser::cursorCallback(VTermPos pos, VTermPos, int, void *user)
{
    static_cast<TerminalCommandAuditParser *>(user)->m_cursor = pos;
    return 1;
}

int TerminalCommandAuditParser::termPropCallback(VTermProp prop, VTermValue *value, void *user)
{
    if (prop != VTERM_PROP_ALTSCREEN)
        return 1;
    auto *parser = static_cast<TerminalCommandAuditParser *>(user);
    parser->m_alternateScreen = value && value->boolean;
    if (parser->m_alternateScreen)
        parser->resetCommandState();
    return 1;
}
