#ifndef CONTROLLED_ACCESS_GATE_H
#define CONTROLLED_ACCESS_GATE_H

#include <QJsonObject>
#include <QString>

#include <memory>

class AuditSession;

struct ControlledAccessDecision
{
    bool accepted{false};
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
ControlledAccessDecision evaluate(const QString &sender, const QJsonObject &object,
                                  bool notificationReady = true);
}

#endif /* CONTROLLED_ACCESS_GATE_H */
