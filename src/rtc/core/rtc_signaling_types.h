#pragma once

#include "rtc/core/rtc_base_types.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace rtc
{


enum class TransportPolicy
{
    All,
    Relay
};

enum class MediaTopology
{
    PeerToPeer,
    Sfu
};

struct IceServer
{
    enum class RelayType
    {
        TurnUdp,
        TurnTcp
    };

    std::string hostname;
    uint16_t port{0};
    std::string username;
    std::string password;
    bool relay{false};
    RelayType relayType{RelayType::TurnUdp};

    IceServer() = default;
    IceServer(const std::string &host, uint16_t p)
        : hostname(host), port(p)
    {
    }
    IceServer(const std::string &host, uint16_t p, const std::string &user, const std::string &pass, RelayType type)
        : hostname(host), port(p), username(user), password(pass), relay(true), relayType(type)
    {
    }

    std::string uri() const
    {
        if (!relay)
            return "stun:" + hostname + ":" + std::to_string(port);
        const char *transport = relayType == RelayType::TurnTcp ? "tcp" : "udp";
        return "turn:" + hostname + ":" + std::to_string(port) + "?transport=" + transport;
    }
};

struct Configuration
{
    std::vector<IceServer> iceServers;
    bool enableIceTcp{false};
    bool enableAudioDeviceModule{false};
    TransportPolicy iceTransportPolicy{TransportPolicy::All};
    MediaTopology mediaTopology{MediaTopology::PeerToPeer};
};

class Candidate
{
public:
    Candidate() = default;
    Candidate(std::string candidate, std::string mid)
        : m_candidate(std::move(candidate)), m_mid(std::move(mid))
    {
    }

    operator std::string() const { return m_candidate; }
    const std::string &mid() const { return m_mid; }

private:
    std::string m_candidate;
    std::string m_mid;
};

class Description
{
public:
    enum class Type
    {
        Offer,
        Answer
    };

    enum class Direction
    {
        SendOnly,
        RecvOnly,
        SendRecv
    };

    class Media
    {
    public:
        using Direction = Description::Direction;
    };

    class Video : public Media
    {
    public:
        explicit Video(std::string name) : m_name(std::move(name)) {}
        void addSSRC(uint32_t, const std::string &, const std::string &, const std::string &) {}
        void setDirection(Direction direction) { m_direction = direction; }
        void setDesktopSourceIndex(int index) { m_desktopSourceIndex = index; }
        void setDesktopSourceId(intptr_t sourceId)
        {
            m_desktopSourceId = sourceId;
            m_hasDesktopSourceId = true;
        }
        void setDesktopFps(int fps) { m_desktopFps = fps; }
        void setDesktopQualityProfile(std::string profile) { m_desktopQualityProfile = std::move(profile); }
        void setDesktopTargetResolution(int width, int height)
        {
            m_desktopTargetWidth = width;
            m_desktopTargetHeight = height;
        }
        void setDesktopSimulcastRequested(bool requested) { m_desktopSimulcastRequested = requested; }
        const std::string &name() const { return m_name; }
        Direction direction() const { return m_direction; }
        int desktopSourceIndex() const { return m_desktopSourceIndex; }
        bool hasDesktopSourceId() const { return m_hasDesktopSourceId; }
        intptr_t desktopSourceId() const { return m_desktopSourceId; }
        int desktopFps() const { return m_desktopFps; }
        const std::string &desktopQualityProfile() const { return m_desktopQualityProfile; }
        int desktopTargetWidth() const { return m_desktopTargetWidth; }
        int desktopTargetHeight() const { return m_desktopTargetHeight; }
        bool desktopSimulcastRequested() const { return m_desktopSimulcastRequested; }

    private:
        std::string m_name;
        Direction m_direction{Direction::SendRecv};
        int m_desktopSourceIndex{0};
        bool m_hasDesktopSourceId{false};
        intptr_t m_desktopSourceId{0};
        int m_desktopFps{30};
        std::string m_desktopQualityProfile{"balanced"};
        int m_desktopTargetWidth{0};
        int m_desktopTargetHeight{0};
        bool m_desktopSimulcastRequested{false};
    };

    class Audio : public Media
    {
    public:
        explicit Audio(std::string name) : m_name(std::move(name)) {}
        void addSSRC(uint32_t, const std::string &, const std::string &, const std::string &) {}
        void setDirection(Direction direction) { m_direction = direction; }
        void setSystemLoopback(bool enabled) { m_systemLoopback = enabled; }
        const std::string &name() const { return m_name; }
        Direction direction() const { return m_direction; }
        bool systemLoopback() const { return m_systemLoopback; }

    private:
        std::string m_name;
        Direction m_direction{Direction::SendRecv};
        bool m_systemLoopback{false};
    };

    Description() = default;
    Description(std::string sdp, Type type) : m_sdp(std::move(sdp)), m_type(type) {}
    Description(std::string sdp, std::string type)
        : m_sdp(std::move(sdp)), m_type(type == "answer" ? Type::Answer : Type::Offer)
    {
    }

    operator std::string() const { return m_sdp; }
    std::string typeString() const { return m_type == Type::Answer ? "answer" : "offer"; }
    Type type() const { return m_type; }

private:
    std::string m_sdp;
    Type m_type{Type::Offer};
};

struct Reliability
{
    bool unordered{false};
    int maxRetransmits{-1};
};

} // namespace rtc
