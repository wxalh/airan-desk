#ifndef TERMINAL_COMMAND_AUDIT_PARSER_H
#define TERMINAL_COMMAND_AUDIT_PARSER_H

#include <QByteArray>
#include <QString>
#include <QVector>

extern "C"
{
#ifdef small
#undef small
#endif
#include <vterm.h>
}

struct TerminalCommandAuditRecord
{
    enum Kind
    {
        Command,
        Redacted,
        Unavailable
    };

    Kind kind{Unavailable};
    QString command;
};

class TerminalCommandAuditParser
{
public:
    enum class EchoMode
    {
        Pty,
        PipeFallback
    };

    TerminalCommandAuditParser() = default;
    ~TerminalCommandAuditParser();

    void initialize(int cols, int rows, EchoMode mode);
    void resize(int cols, int rows);
    void setEchoMode(EchoMode mode);
    QVector<TerminalCommandAuditRecord> noteInput(const QByteArray &data);
    QVector<TerminalCommandAuditRecord> processOutput(const QByteArray &data);
    void setSecretInput(bool secret);
    void reset();

private:
    void beginInputIfNeeded();
    void resetCommandState();
    void applyFallbackByte(unsigned char byte);
    QString commandFromScreen(bool *reliable) const;
    TerminalCommandAuditRecord committedRecord() const;

    static int cursorCallback(VTermPos pos, VTermPos oldPos, int visible, void *user);
    static int termPropCallback(VTermProp prop, VTermValue *value, void *user);

    VTerm *m_vterm{nullptr};
    VTermScreen *m_screen{nullptr};
    VTermState *m_state{nullptr};
    VTermPos m_cursor{0, 0};
    QByteArray m_fallbackInput;
    EchoMode m_echoMode{EchoMode::Pty};
    int m_cols{80};
    int m_rows{24};
    int m_promptColumn{0};
    bool m_inputActive{false};
    bool m_submitPending{false};
    bool m_secretInput{false};
    bool m_alternateScreen{false};
    bool m_unavailable{false};
};

#endif /* TERMINAL_COMMAND_AUDIT_PARSER_H */
