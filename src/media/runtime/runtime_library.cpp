#include "media/runtime/runtime_library.h"

#include <filesystem>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__linux__)
#include <dlfcn.h>
#include <limits.h>
#include <unistd.h>
#endif

namespace airan::media
{
namespace
{

std::filesystem::path executableDirectory()
{
#if defined(_WIN32)
    std::wstring buffer(MAX_PATH, L'\0');
    DWORD size = 0;
    for (;;)
    {
        size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (size == 0)
            return {};
        if (size < buffer.size() - 1)
            break;
        buffer.resize(buffer.size() * 2);
    }
    buffer.resize(size);
    return std::filesystem::path(buffer).parent_path();
#elif defined(__linux__)
    std::string buffer(PATH_MAX, '\0');
    const ssize_t size = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
    if (size <= 0)
        return {};
    buffer.resize(static_cast<size_t>(size));
    return std::filesystem::path(buffer).parent_path();
#else
    return {};
#endif
}

std::filesystem::path moduleDirectory()
{
#if defined(_WIN32)
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&moduleDirectory),
                            &module))
    {
        return {};
    }

    std::wstring buffer(MAX_PATH, L'\0');
    DWORD size = 0;
    for (;;)
    {
        size = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (size == 0)
            return {};
        if (size < buffer.size() - 1)
            break;
        buffer.resize(buffer.size() * 2);
    }
    buffer.resize(size);
    return std::filesystem::path(buffer).parent_path();
#elif defined(__linux__)
    Dl_info info{};
    if (dladdr(reinterpret_cast<void *>(&moduleDirectory), &info) == 0 || !info.dli_fname)
        return {};
    return std::filesystem::path(info.dli_fname).parent_path();
#else
    return {};
#endif
}

void *openLibraryPath(const std::filesystem::path &path)
{
#if defined(_WIN32)
    const auto fullPath = path.wstring();
    HMODULE module = LoadLibraryExW(fullPath.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!module)
        module = LoadLibraryExW(fullPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!module)
        module = LoadLibraryW(fullPath.c_str());
    return module;
#elif defined(__linux__)
    return dlopen(path.string().c_str(), RTLD_NOW | RTLD_LOCAL);
#else
    (void)path;
    return nullptr;
#endif
}

} // namespace

RuntimeLibrary::RuntimeLibrary(RuntimeLibrary &&other) noexcept
    : m_handle(other.m_handle),
      m_loadedName(std::move(other.m_loadedName))
{
    other.m_handle = nullptr;
}

RuntimeLibrary &RuntimeLibrary::operator=(RuntimeLibrary &&other) noexcept
{
    if (this != &other)
    {
        close();
        m_handle = other.m_handle;
        m_loadedName = std::move(other.m_loadedName);
        other.m_handle = nullptr;
    }
    return *this;
}

RuntimeLibrary::~RuntimeLibrary()
{
    close();
}

bool RuntimeLibrary::openAny(const char *const *names, size_t count)
{
    close();
    const auto exeDir = executableDirectory();
    const auto moduleDir = moduleDirectory();
    for (size_t i = 0; i < count; ++i)
    {
        const char *name = names[i];
        if (!name || !name[0])
            continue;
        if (!moduleDir.empty())
            m_handle = openLibraryPath(moduleDir / name);
        if (!m_handle && !exeDir.empty())
            m_handle = openLibraryPath(exeDir / name);
        if (!m_handle && !moduleDir.empty())
            m_handle = openLibraryPath(moduleDir / std::filesystem::path(name).filename());
        if (!m_handle)
            m_handle = openLibraryPath(std::filesystem::path(name));
        if (m_handle)
        {
            m_loadedName = name;
            return true;
        }
    }
    return false;
}

void RuntimeLibrary::close()
{
    if (!m_handle)
        return;
#if defined(_WIN32)
    FreeLibrary(static_cast<HMODULE>(m_handle));
#elif defined(__linux__)
    dlclose(m_handle);
#endif
    m_handle = nullptr;
    m_loadedName.clear();
}

bool RuntimeLibrary::isOpen() const
{
    return m_handle != nullptr;
}

const std::string &RuntimeLibrary::loadedName() const
{
    return m_loadedName;
}

void *RuntimeLibrary::resolveSymbol(const char *name) const
{
    if (!m_handle || !name)
        return nullptr;
#if defined(_WIN32)
    return reinterpret_cast<void *>(GetProcAddress(static_cast<HMODULE>(m_handle), name));
#elif defined(__linux__)
    return dlsym(m_handle, name);
#else
    return nullptr;
#endif
}

} // namespace airan::media
