#include "controlled_access_gate.h"

#include "common/constant.h"
#include "security/audit_logger.h"
#include "security/audit_session.h"
#include "util/config/config_util.h"
#include "util/json/json_util.h"

#include <QUuid>

#include <atomic>

namespace
{
std::atomic_bool g_runtimePrerequisiteReady{true};
bool constantTimeEqual(const QByteArray &left, const QByteArray &right)
{
    if (left.size() != right.size())
        return false;
    unsigned char difference = 0;
    for (int i = 0; i < left.size(); ++i)
        difference |= static_cast<unsigned char>(left.at(i) ^ right.at(i));
    return difference == 0;
}

bool appendConnectionRequest(const ControlledAccessDecision &decision, bool success, const QString &reason)
{
    QJsonObject fields;
    fields.insert(QStringLiteral("session_id"), decision.sessionId);
    fields.insert(QStringLiteral("peer_id"), decision.peerId);
    fields.insert(QStringLiteral("source_ip"), decision.sourceIp);
    fields.insert(QStringLiteral("auth_method"), QStringLiteral("password"));
    fields.insert(QStringLiteral("success"), success);
    if (!reason.isEmpty())
        fields.insert(QStringLiteral("reason"), reason);
    return AuditLogger::instance().append(QStringLiteral("connection_request"), fields);
}
}

void ControlledAccessGate::setRuntimePrerequisiteReady(bool ready)
{
    g_runtimePrerequisiteReady.store(ready);
}

bool ControlledAccessGate::runtimePrerequisiteReady()
{
    return g_runtimePrerequisiteReady.load();
}

ControlledAccessDecision ControlledAccessGate::evaluate(const QString &sender, const QJsonObject &object,
                                                        bool notificationReady)
{
    ControlledAccessDecision decision;
    decision.peerId = sender.trimmed();
    decision.sourceIp = JsonUtil::getString(object, QStringLiteral("source_ip")).trimmed();
    decision.sessionId = JsonUtil::getString(object, Constant::KEY_SESSION_ID).trimmed();
    if (decision.sessionId.isEmpty())
        decision.sessionId = QUuid::createUuid().toString().remove(QLatin1Char('{')).remove(QLatin1Char('}'));

    if (!ConfigUtil->allow_remote)
    {
        decision.reason = QStringLiteral("controlled_access_disabled");
        if (AuditLogger::instance().isReady())
            appendConnectionRequest(decision, false, decision.reason);
        return decision;
    }
    if (!AuditLogger::instance().isReady())
    {
        decision.reason = QStringLiteral("audit_unavailable");
        return decision;
    }
    if (!runtimePrerequisiteReady())
    {
        decision.reason = QStringLiteral("runtime_prerequisite_unavailable");
        appendConnectionRequest(decision, false, decision.reason);
        return decision;
    }
    if (decision.peerId.isEmpty() || decision.sourceIp.isEmpty())
    {
        decision.reason = QStringLiteral("invalid_peer_identity");
        appendConnectionRequest(decision, false, decision.reason);
        return decision;
    }
    if (!notificationReady)
    {
        decision.reason = QStringLiteral("notification_unavailable");
        appendConnectionRequest(decision, false, decision.reason);
        return decision;
    }

    const QString receiverPassword = JsonUtil::getString(object, Constant::KEY_RECEIVER_PWD).trimmed();
    const QByteArray expected = ConfigUtil->local_pwd_md5.trimmed().toUtf8();
    const QByteArray supplied = receiverPassword.toUtf8();
    if (expected.isEmpty() || supplied.isEmpty() || !constantTimeEqual(expected, supplied))
    {
        decision.reason = QStringLiteral("authentication_failed");
        appendConnectionRequest(decision, false, decision.reason);
        return decision;
    }

    if (!appendConnectionRequest(decision, true, QString()))
    {
        decision.reason = QStringLiteral("audit_write_failed");
        return decision;
    }

    const bool isOnlyFile = JsonUtil::getBool(object, Constant::KEY_IS_ONLY_FILE, false);
    decision.auditSession = std::make_shared<AuditSession>(decision.sessionId,
                                                           decision.peerId,
                                                           decision.sourceIp,
                                                           isOnlyFile ? QStringLiteral("file") : QStringLiteral("desktop"));
    decision.accepted = true;
    return decision;
}
