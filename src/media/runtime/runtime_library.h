#pragma once

#include <string>

namespace airan::media
{

class RuntimeLibrary
{
public:
    RuntimeLibrary() = default;
    RuntimeLibrary(const RuntimeLibrary &) = delete;
    RuntimeLibrary &operator=(const RuntimeLibrary &) = delete;
    RuntimeLibrary(RuntimeLibrary &&other) noexcept;
    RuntimeLibrary &operator=(RuntimeLibrary &&other) noexcept;
    ~RuntimeLibrary();

    bool openAny(const char *const *names, size_t count);
    void close();
    bool isOpen() const;
    const std::string &loadedName() const;

    template <typename Fn>
    Fn resolve(const char *name) const
    {
        return reinterpret_cast<Fn>(resolveSymbol(name));
    }

private:
    void *resolveSymbol(const char *name) const;

    void *m_handle = nullptr;
    std::string m_loadedName;
};

} // namespace airan::media
