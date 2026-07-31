#ifndef QT_CALLBACK_UTIL_H
#define QT_CALLBACK_UTIL_H

#include <QObject>
#include <QPointer>

#include <functional>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <utility>
#include <memory>

class QtCallbackDispatcherPrivate;

class QtCallbackDispatcher : public QObject
{
    Q_OBJECT

public:
    explicit QtCallbackDispatcher(QObject *parent = nullptr);
    ~QtCallbackDispatcher() override;

    void post(std::function<void()> callback);

private slots:
    void drain();

private:
    QtCallbackDispatcherPrivate *d = nullptr;
};

class CallbackLifetime
{
public:
    class Permit
    {
    public:
        explicit Permit(CallbackLifetime *owner)
            : m_owner(owner)
        {
        }

        Permit(const Permit &) = delete;
        Permit &operator=(const Permit &) = delete;

        Permit(Permit &&other) noexcept
            : m_owner(std::exchange(other.m_owner, nullptr))
        {
        }

        Permit &operator=(Permit &&other) noexcept
        {
            if (this != &other)
            {
                release();
                m_owner = std::exchange(other.m_owner, nullptr);
            }
            return *this;
        }

        ~Permit()
        {
            release();
        }

    private:
        void release()
        {
            if (m_owner)
            {
                m_owner->leave();
                m_owner = nullptr;
            }
        }

        CallbackLifetime *m_owner = nullptr;
    };

    std::optional<Permit> tryEnter()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_closed)
            return std::nullopt;
        ++m_active;
        ++m_activeByThread[std::this_thread::get_id()];
        return std::optional<Permit>(std::in_place, this);
    }

    void closeAndWait()
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_closed = true;
        const auto currentThread = std::this_thread::get_id();
        // A callback can pump the owner event loop (for example during
        // backpressure waits) and re-enter shutdown. Do not deadlock that
        // thread; the owning thread must keep the object alive until it
        // returns from the callback stack.
        m_condition.wait(lock, [this, currentThread]() {
            const auto it = m_activeByThread.find(currentThread);
            const size_t ownActive = it == m_activeByThread.end() ? 0 : it->second;
            return m_active <= ownActive;
        });
    }

private:
    void leave()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_active > 0)
            --m_active;
        const auto currentThread = std::this_thread::get_id();
        const auto it = m_activeByThread.find(currentThread);
        if (it != m_activeByThread.end())
        {
            if (it->second > 1)
                --it->second;
            else
                m_activeByThread.erase(it);
        }
        m_condition.notify_all();
    }

    std::mutex m_mutex;
    std::condition_variable m_condition;
    std::unordered_map<std::thread::id, size_t> m_activeByThread;
    size_t m_active = 0;
    bool m_closed = false;
};

template <typename T, typename... Args>
class WeakMemberCallback
{
public:
    using Method = void (T::*)(Args...);

    WeakMemberCallback(T *target, Method method, std::shared_ptr<CallbackLifetime> lifetime = {})
        : m_target(target), m_method(method), m_lifetime(std::move(lifetime))
    {
    }

    void operator()(Args... args) const
    {
        std::optional<CallbackLifetime::Permit> permit;
        if (m_lifetime)
        {
            permit = m_lifetime->tryEnter();
            if (!permit)
                return;
        }
        T *target = m_target.data();
        if (target)
            (target->*m_method)(std::forward<Args>(args)...);
    }

private:
    QPointer<T> m_target;
    Method m_method = nullptr;
    std::shared_ptr<CallbackLifetime> m_lifetime;
};

template <typename T, typename... Args>
WeakMemberCallback<T, Args...> makeWeakCallback(T *target,
                                                void (T::*method)(Args...),
                                                std::shared_ptr<CallbackLifetime> lifetime = {})
{
    return WeakMemberCallback<T, Args...>(target, method, std::move(lifetime));
}

#endif /* QT_CALLBACK_UTIL_H */
