#include "device_vault_credentials.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QProcess>
#include <QStandardPaths>
#include <QUuid>

#if defined(Q_OS_WIN)
#include <windows.h>
#include <wincrypt.h>
#include <sddl.h>
#elif defined(Q_OS_MACOS) || defined(Q_OS_MAC)
#include <Security/Security.h>
#endif

namespace DeviceVaultCredentials
{
namespace
{
QString credentialPath(const QString &path)
{
    return path + QStringLiteral(".key");
}

QString account(const QString &path)
{
    return QString::fromLatin1(QCryptographicHash::hash(
        QFileInfo(path).absoluteFilePath().toUtf8(), QCryptographicHash::Sha256).toHex());
}

void setError(QString *error, const char *text)
{
    if (error)
        *error = QCoreApplication::translate("DeviceVault", text);
}

bool writeProtected(const QString &path, const QByteArray &data, QString *error)
{
#if defined(Q_OS_WIN)
    // Create the temporary file with its final private DACL. Qt's QSaveFile
    // handle does not grant the ACL access required to secure it afterwards.
    HANDLE token = nullptr;
    DWORD required = 0;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
    {
        GetTokenInformation(token, TokenUser, nullptr, 0, &required);
        QByteArray tokenData(int(required), '\0');
        if (required && GetTokenInformation(token, TokenUser, tokenData.data(), required, &required))
        {
            LPWSTR sid = nullptr;
            const auto *user = reinterpret_cast<const TOKEN_USER *>(tokenData.constData());
            if (ConvertSidToStringSidW(user->User.Sid, &sid))
            {
                const QString sddl = QStringLiteral("D:P(A;;FA;;;%1)").arg(QString::fromWCharArray(sid));
                ConvertStringSecurityDescriptorToSecurityDescriptorW(
                    reinterpret_cast<LPCWSTR>(sddl.utf16()), SDDL_REVISION_1, &descriptor, nullptr);
                LocalFree(sid);
            }
        }
        CloseHandle(token);
    }
    if (!descriptor)
    {
        setError(error, QT_TRANSLATE_NOOP("DeviceVault", "Failed to restrict access to the remembered device key."));
        return false;
    }
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE};
    const QString temporary = credentialPath(path) + QStringLiteral(".tmp-") +
                              QUuid::createUuid().toString().remove(QLatin1Char('{')).remove(QLatin1Char('}'));
    const HANDLE file = CreateFileW(reinterpret_cast<LPCWSTR>(temporary.utf16()),
        GENERIC_WRITE, 0, &attributes, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    LocalFree(descriptor);
    if (file == INVALID_HANDLE_VALUE)
    {
        setError(error, QT_TRANSLATE_NOOP("DeviceVault", "Failed to save the remembered device key."));
        return false;
    }
    DWORD written = 0;
    bool success = WriteFile(file, data.constData(), DWORD(data.size()), &written, nullptr) &&
                   written == DWORD(data.size()) && FlushFileBuffers(file);
    if (!CloseHandle(file))
        success = false;
    if (success)
    {
        success = MoveFileExW(reinterpret_cast<LPCWSTR>(temporary.utf16()),
            reinterpret_cast<LPCWSTR>(credentialPath(path).utf16()),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
    }
    if (!success)
    {
        DeleteFileW(reinterpret_cast<LPCWSTR>(temporary.utf16()));
        setError(error, QT_TRANSLATE_NOOP("DeviceVault", "Failed to save the remembered device key."));
        return false;
    }
#else
    QSaveFile file(credentialPath(path));
    if (!file.open(QIODevice::WriteOnly) ||
        !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) ||
        file.write(data) != data.size() || !file.commit())
    {
        setError(error, QT_TRANSLATE_NOOP("DeviceVault", "Failed to save the remembered device key."));
        return false;
    }
#endif
    return true;
}
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
CFMutableDictionaryRef keychainQuery(const QString &path)
{
    CFMutableDictionaryRef query = CFDictionaryCreateMutable(
        nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(query, kSecClass, kSecClassGenericPassword);
    CFDictionarySetValue(query, kSecAttrService, CFSTR("com.wxalh.airan-desk.my-devices"));
    const QByteArray name = account(path).toUtf8();
    CFStringRef accountName = CFStringCreateWithBytes(
        nullptr, reinterpret_cast<const UInt8 *>(name.constData()), name.size(), kCFStringEncodingUTF8, false);
    CFDictionarySetValue(query, kSecAttrAccount, accountName);
    CFRelease(accountName);
    return query;
}
#elif !defined(Q_OS_WIN)
bool secretTool(const QStringList &arguments, const QByteArray &input, QByteArray *output, QString *error)
{
    const QString executable = QStandardPaths::findExecutable(QStringLiteral("secret-tool"));
    if (executable.isEmpty())
    {
        setError(error, QT_TRANSLATE_NOOP("DeviceVault", "Permanent password remembering requires secret-tool and an unlocked system keyring."));
        return false;
    }
    QProcess process;
    process.start(executable, arguments);
    if (!process.waitForStarted(3000))
    {
        setError(error, QT_TRANSLATE_NOOP("DeviceVault", "The system credential store could not be started."));
        return false;
    }
    if (!input.isEmpty())
        process.write(input);
    process.closeWriteChannel();
    if (!process.waitForFinished(10000))
    {
        process.kill();
        process.waitForFinished(1000);
        setError(error, QT_TRANSLATE_NOOP("DeviceVault", "The system credential store did not respond. Unlock the system keyring and retry."));
        return false;
    }
    const bool missingOnClear = !arguments.isEmpty() && arguments.first() == QStringLiteral("clear") &&
                                process.exitCode() == 1 && process.readAllStandardError().isEmpty();
    if (process.exitStatus() != QProcess::NormalExit || (process.exitCode() != 0 && !missingOnClear))
    {
        setError(error, QT_TRANSLATE_NOOP("DeviceVault", "The system credential store is unavailable or the remembered key is missing."));
        return false;
    }
    if (output)
        *output = process.readAllStandardOutput().trimmed();
    return true;
}

QStringList attributes(const QString &path)
{
    return {QStringLiteral("application"), QStringLiteral("airan-desk-my-devices"),
            QStringLiteral("vault"), account(path)};
}
#endif
}

bool exists(const QString &vaultPath)
{
    return QFileInfo::exists(credentialPath(vaultPath));
}

bool store(const QString &vaultPath, const QByteArray &key, QString *error)
{
    // Password rotation temporarily keeps the new and previous keys together.
    // A crash on either side of the vault commit must leave a usable key.
    if (key.size() != 32 && key.size() != 64)
    {
        setError(error, QT_TRANSLATE_NOOP("DeviceVault", "The device encryption key is invalid."));
        return false;
    }
#if defined(Q_OS_WIN)
    DATA_BLOB plain{DWORD(key.size()), reinterpret_cast<BYTE *>(const_cast<char *>(key.constData()))};
    QByteArray entropy = account(vaultPath).toUtf8();
    DATA_BLOB extra{DWORD(entropy.size()), reinterpret_cast<BYTE *>(entropy.data())};
    DATA_BLOB encrypted{};
    if (!CryptProtectData(&plain, L"Airan Desk My Devices", &extra, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &encrypted))
    {
        setError(error, QT_TRANSLATE_NOOP("DeviceVault", "Windows could not protect the remembered device key."));
        return false;
    }
    const QByteArray protectedKey(reinterpret_cast<const char *>(encrypted.pbData), int(encrypted.cbData));
    LocalFree(encrypted.pbData);
    return writeProtected(vaultPath, protectedKey, error);
#elif defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    CFMutableDictionaryRef query = keychainQuery(vaultPath);
    CFDataRef data = CFDataCreate(nullptr, reinterpret_cast<const UInt8 *>(key.constData()), key.size());
    CFMutableDictionaryRef update = CFDictionaryCreateMutable(
        nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(update, kSecValueData, data);
    OSStatus result = SecItemUpdate(query, update);
    if (result == errSecItemNotFound)
    {
        CFDictionarySetValue(query, kSecValueData, data);
        CFDictionarySetValue(query, kSecAttrAccessible, kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly);
        result = SecItemAdd(query, nullptr);
    }
    CFRelease(update);
    CFRelease(data);
    CFRelease(query);
    if (result != errSecSuccess)
    {
        setError(error, QT_TRANSLATE_NOOP("DeviceVault", "The device key could not be saved in macOS Keychain."));
        return false;
    }
    if (exists(vaultPath))
        return true;
    if (!writeProtected(vaultPath, QByteArrayLiteral("system-keychain\n"), error))
    {
        CFMutableDictionaryRef cleanup = keychainQuery(vaultPath);
        SecItemDelete(cleanup);
        CFRelease(cleanup);
        return false;
    }
    return true;
#else
    QStringList args{QStringLiteral("store"), QStringLiteral("--label=Airan Desk My Devices")};
    args.append(attributes(vaultPath));
    if (!secretTool(args, key.toBase64() + '\n', nullptr, error))
        return false;
    if (exists(vaultPath))
        return true;
    if (!writeProtected(vaultPath, QByteArrayLiteral("system-keyring\n"), error))
    {
        QStringList cleanup{QStringLiteral("clear")};
        cleanup.append(attributes(vaultPath));
        QString ignored;
        secretTool(cleanup, {}, nullptr, &ignored);
        return false;
    }
    return true;
#endif
}

bool load(const QString &vaultPath, QByteArray *key, QString *error)
{
    if (!key || !exists(vaultPath))
    {
        setError(error, QT_TRANSLATE_NOOP("DeviceVault", "No device password has been remembered on this computer."));
        return false;
    }
#if defined(Q_OS_WIN)
    QFile file(credentialPath(vaultPath));
    if (!file.open(QIODevice::ReadOnly) || file.size() > 16384)
    {
        setError(error, QT_TRANSLATE_NOOP("DeviceVault", "The remembered device key could not be read."));
        return false;
    }
    QByteArray protectedKey = file.readAll();
    QByteArray entropy = account(vaultPath).toUtf8();
    DATA_BLOB encrypted{DWORD(protectedKey.size()), reinterpret_cast<BYTE *>(protectedKey.data())};
    DATA_BLOB extra{DWORD(entropy.size()), reinterpret_cast<BYTE *>(entropy.data())};
    DATA_BLOB plain{};
    if (!CryptUnprotectData(&encrypted, nullptr, &extra, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &plain))
    {
        setError(error, QT_TRANSLATE_NOOP("DeviceVault", "The remembered device key cannot be unlocked by this Windows user."));
        return false;
    }
    *key = QByteArray(reinterpret_cast<const char *>(plain.pbData), int(plain.cbData));
    SecureZeroMemory(plain.pbData, plain.cbData);
    LocalFree(plain.pbData);
#elif defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    CFMutableDictionaryRef query = keychainQuery(vaultPath);
    CFDictionarySetValue(query, kSecReturnData, kCFBooleanTrue);
    CFDictionarySetValue(query, kSecMatchLimit, kSecMatchLimitOne);
    CFTypeRef result = nullptr;
    const OSStatus status = SecItemCopyMatching(query, &result);
    CFRelease(query);
    if (status != errSecSuccess || !result || CFGetTypeID(result) != CFDataGetTypeID())
    {
        if (result)
            CFRelease(result);
        setError(error, QT_TRANSLATE_NOOP("DeviceVault", "The remembered device key could not be read from macOS Keychain."));
        return false;
    }
    const CFDataRef data = static_cast<CFDataRef>(result);
    *key = QByteArray(reinterpret_cast<const char *>(CFDataGetBytePtr(data)), int(CFDataGetLength(data)));
    CFRelease(result);
#else
    QStringList args{QStringLiteral("lookup")};
    args.append(attributes(vaultPath));
    QByteArray encoded;
    if (!secretTool(args, {}, &encoded, error))
        return false;
    *key = QByteArray::fromBase64(encoded);
    encoded.fill('\0');
#endif
    if (key->size() != 32 && key->size() != 64)
    {
        key->fill('\0');
        key->clear();
        setError(error, QT_TRANSLATE_NOOP("DeviceVault", "The remembered device key is invalid. Enter the device management password again."));
        return false;
    }
    return true;
}

bool remove(const QString &vaultPath, QString *error)
{
    if (!exists(vaultPath))
        return true;
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    CFMutableDictionaryRef query = keychainQuery(vaultPath);
    const OSStatus result = SecItemDelete(query);
    CFRelease(query);
    if (result != errSecSuccess && result != errSecItemNotFound)
    {
        setError(error, QT_TRANSLATE_NOOP("DeviceVault", "The remembered device key could not be removed from macOS Keychain."));
        return false;
    }
#elif !defined(Q_OS_WIN)
    QStringList args{QStringLiteral("clear")};
    args.append(attributes(vaultPath));
    if (!secretTool(args, {}, nullptr, error))
        return false;
#endif
    if (!QFile::remove(credentialPath(vaultPath)))
    {
        setError(error, QT_TRANSLATE_NOOP("DeviceVault", "The remembered device key file could not be removed."));
        return false;
    }
    return true;
}
}
