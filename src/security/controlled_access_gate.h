#ifndef CONTROLLED_ACCESS_GATE_H
#define CONTROLLED_ACCESS_GATE_H

#include <QByteArray>
#include <QJsonObject>
#include <QString>

#include <memory>

class AuditSession;

struct ControlledAccessPolicySnapshot
{
    bool allowRemote{false};
    QByteArray expectedPasswordDigest;
};

struct ControlledAccessDecision
{
    bool accepted{false};
    quint64 policyGeneration{0};
    QString reason;
    QString peerId;
    QString sourceIp;
    QString sessionId;
    std::shared_ptr<AuditSession> auditSession;
};

namespace ControlledAccessGate
{
void setRuntimePrerequisiteReady(bool ready);
bool runtimePrerequisiteReady();
ControlledAccessPolicySnapshot policySnapshot();
ControlledAccessDecision evaluate(const QString &sender, const QJsonObject &object,
                                  bool notificationReady = true);
ControlledAccessDecision evaluate(const QString &sender,
                                  const QJsonObject &object,
                                  const ControlledAccessPolicySnapshot &policy,
                                  bool notificationReady = true);
}

#endif /* CONTROLLED_ACCESS_GATE_H */
