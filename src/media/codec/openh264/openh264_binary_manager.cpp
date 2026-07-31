#include "media/codec/openh264/openh264_binary_manager.h"

#include "media/codec/openh264/openh264_release_manifest.h"
#include "media/codec/openh264/openh264_staging_internal.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QtEndian>

#if defined(Q_OS_WIN)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <array>
#include <utility>

namespace airan::media::openh264
{
namespace
{

struct BinaryIdentity
{
    QString platform;
    QString architecture;
};

struct OpenH264Version
{
    quint32 major;
    quint32 minor;
    quint32 revision;
    quint32 reserved;
};

#if defined(Q_OS_WIN)
#define AIRAN_OPENH264_CALL __cdecl
#else
#define AIRAN_OPENH264_CALL
#endif

class NativeModule
{
public:
    NativeModule() = default;
    NativeModule(const NativeModule &) = delete;
    NativeModule &operator=(const NativeModule &) = delete;
    ~NativeModule() { close(); }

    bool open(const QString &absolutePath)
    {
        close();
#if defined(Q_OS_WIN)
        const std::wstring path = QDir::toNativeSeparators(absolutePath).toStdWString();
        m_handle = LoadLibraryExW(path.c_str(), nullptr,
                                  LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
                                      LOAD_LIBRARY_SEARCH_SYSTEM32);
#else
        const QByteArray path = QFile::encodeName(absolutePath);
        m_handle = dlopen(path.constData(), RTLD_NOW | RTLD_LOCAL);
#endif
        return m_handle != nullptr;
    }

    void *resolve(const char *name) const
    {
        if (!m_handle || !name)
            return nullptr;
#if defined(Q_OS_WIN)
        return reinterpret_cast<void *>(GetProcAddress(m_handle, name));
#else
        return dlsym(m_handle, name);
#endif
    }

private:
    void close()
    {
        if (!m_handle)
            return;
#if defined(Q_OS_WIN)
        FreeLibrary(m_handle);
#else
        dlclose(m_handle);
#endif
        m_handle = nullptr;
    }

#if defined(Q_OS_WIN)
    HMODULE m_handle{nullptr};
#else
    void *m_handle{nullptr};
#endif
};

ValidationResult result(Availability availability,
                        QString reasonCode,
                        QString absolutePath = QString())
{
    ValidationResult value;
    value.availability = availability;
    value.reasonCode = std::move(reasonCode);
    value.absolutePath = std::move(absolutePath);
    return value;
}

QString currentPlatform()
{
#if defined(Q_OS_WIN)
    return QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("macos");
#elif defined(Q_OS_LINUX)
    return QStringLiteral("linux");
#else
    return QString();
#endif
}

QString currentArchitecture()
{
#if defined(Q_PROCESSOR_X86_64)
    return QStringLiteral("x64");
#elif defined(Q_PROCESSOR_X86_32)
    return QStringLiteral("x86");
#elif defined(Q_PROCESSOR_ARM_64)
    return QStringLiteral("arm64");
#elif defined(Q_PROCESSOR_ARM_32)
    return QStringLiteral("arm");
#else
    return QString();
#endif
}

quint16 readLe16(const QByteArray &data, int offset)
{
    return qFromLittleEndian<quint16>(
        reinterpret_cast<const uchar *>(data.constData() + offset));
}

quint32 readLe32(const QByteArray &data, int offset)
{
    return qFromLittleEndian<quint32>(
        reinterpret_cast<const uchar *>(data.constData() + offset));
}

BinaryIdentity inspectBinaryIdentity(const QByteArray &header)
{
    if (header.size() < 64)
        return {};

    if (header[0] == 'M' && header[1] == 'Z')
    {
        const quint32 peOffset = readLe32(header, 0x3c);
        if (peOffset > static_cast<quint32>(header.size() - 6) ||
            header.mid(static_cast<int>(peOffset), 4) != QByteArray("PE\0\0", 4))
        {
            return {};
        }
        const quint16 machine = readLe16(header, static_cast<int>(peOffset) + 4);
        const QString architecture = machine == 0x014c ? QStringLiteral("x86")
                                     : machine == 0x8664 ? QStringLiteral("x64")
                                     : machine == 0xaa64 ? QStringLiteral("arm64")
                                                        : QString();
        return {QStringLiteral("windows"), architecture};
    }

    if (static_cast<uchar>(header[0]) == 0x7f && header.mid(1, 3) == QByteArray("ELF", 3) &&
        static_cast<uchar>(header[5]) == 1)
    {
        const quint16 machine = readLe16(header, 18);
        const QString architecture = machine == 3 ? QStringLiteral("x86")
                                     : machine == 62 ? QStringLiteral("x64")
                                     : machine == 40 ? QStringLiteral("arm")
                                     : machine == 183 ? QStringLiteral("arm64")
                                                      : QString();
        return {QStringLiteral("linux"), architecture};
    }

    if (readLe32(header, 0) == 0xfeedfacf)
    {
        const quint32 cpu = readLe32(header, 4);
        const QString architecture = cpu == 0x01000007 ? QStringLiteral("x64")
                                     : cpu == 0x0100000c ? QStringLiteral("arm64")
                                                        : QString();
        return {QStringLiteral("macos"), architecture};
    }
    return {};
}

QByteArray sha256File(QFile &file)
{
    if (!file.seek(0))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    std::array<char, 64 * 1024> buffer{};
    while (!file.atEnd())
    {
        const qint64 count = file.read(buffer.data(), static_cast<qint64>(buffer.size()));
        if (count < 0)
            return {};
        if (count > 0)
            hash.addData(buffer.data(), count);
    }
    return hash.result();
}

bool hasRequiredExportsAndVersion(const QString &absolutePath)
{
    NativeModule module;
    if (!module.open(absolutePath))
        return false;

    constexpr std::array<const char *, 5> requiredExports{{
        "WelsCreateSVCEncoder",
        "WelsDestroySVCEncoder",
        "WelsCreateDecoder",
        "WelsDestroyDecoder",
        "WelsGetCodecVersion",
    }};
    for (const char *name : requiredExports)
    {
        if (!module.resolve(name))
            return false;
    }

    using GetVersion = OpenH264Version(AIRAN_OPENH264_CALL *)(void);
    const auto getVersion = reinterpret_cast<GetVersion>(module.resolve("WelsGetCodecVersion"));
    if (!getVersion)
        return false;
    const OpenH264Version version = getVersion();
    return version.major == 2 && version.minor == 6 && version.revision == 0;
}

bool isUnsafeLink(const QString &path)
{
    const QFileInfo info(path);
    if (info.isSymLink())
        return true;
#if defined(Q_OS_WIN)
    const std::wstring nativePath = QDir::toNativeSeparators(info.absoluteFilePath()).toStdWString();
    const DWORD attributes = GetFileAttributesW(nativePath.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT);
#else
    return false;
#endif
}

bool samePath(const QString &left, const QString &right)
{
    const QString cleanLeft = QDir::cleanPath(QFileInfo(left).absoluteFilePath());
    const QString cleanRight = QDir::cleanPath(QFileInfo(right).absoluteFilePath());
#if defined(Q_OS_WIN)
    return cleanLeft.compare(cleanRight, Qt::CaseInsensitive) == 0;
#else
    return cleanLeft == cleanRight;
#endif
}

bool writeAtomically(const QString &destination,
                     QFile &source,
                     QString *reasonCode)
{
    if (!source.seek(0))
    {
        *reasonCode = QStringLiteral("copy-failed");
        return false;
    }
    QSaveFile output(destination);
    output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly))
    {
        *reasonCode = QStringLiteral("copy-failed");
        return false;
    }
    std::array<char, 64 * 1024> buffer{};
    while (!source.atEnd())
    {
        const qint64 count = source.read(buffer.data(), static_cast<qint64>(buffer.size()));
        if (count < 0 || (count > 0 && output.write(buffer.data(), count) != count))
        {
            output.cancelWriting();
            *reasonCode = QStringLiteral("copy-failed");
            return false;
        }
    }
    if (!output.commit())
    {
        *reasonCode = QStringLiteral("copy-failed");
        return false;
    }
    return true;
}

ValidationResult validateKnownBinary(const QString &path)
{
    const ReleaseBinary *release = currentReleaseBinary();
    if (!release)
        return result(Availability::Rejected, QStringLiteral("unsupported-platform"));

    const QFileInfo info(path);
    if (!info.exists())
        return result(Availability::Missing, QStringLiteral("missing"));
    if (!info.isFile() || info.isSymLink())
        return result(Availability::Rejected, QStringLiteral("not-regular-file"));

#if defined(Q_OS_WIN)
    constexpr Qt::CaseSensitivity fileNameCase = Qt::CaseInsensitive;
#else
    constexpr Qt::CaseSensitivity fileNameCase = Qt::CaseSensitive;
#endif
    if (info.fileName().compare(release->fileName, fileNameCase) != 0)
        return result(Availability::Rejected, QStringLiteral("unexpected-file-name"));
    if (info.size() != release->fileSize)
        return result(Availability::Rejected, QStringLiteral("size-mismatch"));
    if (isUnsafeLink(info.absoluteFilePath()))
        return result(Availability::Rejected, QStringLiteral("symlink-rejected"));

    QFile file(info.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly))
        return result(Availability::Rejected, QStringLiteral("unreadable"));
    const BinaryIdentity identity = inspectBinaryIdentity(file.read(4096));
    if (identity.platform.isEmpty() || identity.architecture.isEmpty())
        return result(Availability::Rejected, QStringLiteral("invalid-binary-format"));
    if (identity.platform != release->platform || identity.architecture != release->architecture)
        return result(Availability::Rejected, QStringLiteral("wrong-architecture"));
    if (sha256File(file) != release->sha256)
        return result(Availability::Rejected, QStringLiteral("hash-mismatch"));
    file.close();

    const QString canonical = info.canonicalFilePath();
    if (canonical.isEmpty())
        return result(Availability::Rejected, QStringLiteral("canonical-path-failed"));
    if (!hasRequiredExportsAndVersion(canonical))
        return result(Availability::Rejected, QStringLiteral("runtime-incompatible"));
    return result(Availability::Ready, QStringLiteral("ready"), canonical);
}

struct StagedOfficialBinary
{
    detail::StagedSourceFile snapshot;
    ValidationResult validation;
};

StagedOfficialBinary stageAndValidateOfficialBinary(const QString &path,
                                                    const QString &stagingRoot)
{
    StagedOfficialBinary staged;
    const ReleaseBinary *release = currentReleaseBinary();
    if (!release)
    {
        staged.validation =
            result(Availability::Rejected, QStringLiteral("unsupported-platform"));
        return staged;
    }

    const QFileInfo info(path);
    if (!info.exists())
    {
        staged.validation = result(Availability::Missing, QStringLiteral("missing"));
        return staged;
    }
    if (!info.isFile() || info.isSymLink())
    {
        staged.validation =
            result(Availability::Rejected, QStringLiteral("not-regular-file"));
        return staged;
    }
#if defined(Q_OS_WIN)
    constexpr Qt::CaseSensitivity fileNameCase = Qt::CaseInsensitive;
#else
    constexpr Qt::CaseSensitivity fileNameCase = Qt::CaseSensitive;
#endif
    if (info.fileName().compare(release->fileName, fileNameCase) != 0)
    {
        staged.validation =
            result(Availability::Rejected, QStringLiteral("unexpected-file-name"));
        return staged;
    }
    if (isUnsafeLink(info.absoluteFilePath()))
    {
        staged.validation =
            result(Availability::Rejected, QStringLiteral("symlink-rejected"));
        return staged;
    }

    staged.snapshot = detail::stageSourceFile(
        info.absoluteFilePath(), stagingRoot, {}, release->fileName);
    if (!staged.snapshot.file)
    {
        staged.validation =
            result(Availability::Rejected, staged.snapshot.reasonCode);
        return staged;
    }
    if (staged.snapshot.size != release->fileSize)
    {
        staged.validation =
            result(Availability::Rejected, QStringLiteral("size-mismatch"));
        return staged;
    }
    const BinaryIdentity identity =
        inspectBinaryIdentity(staged.snapshot.header);
    if (identity.platform.isEmpty() || identity.architecture.isEmpty())
    {
        staged.validation =
            result(Availability::Rejected, QStringLiteral("invalid-binary-format"));
        return staged;
    }
    if (identity.platform != release->platform ||
        identity.architecture != release->architecture)
    {
        staged.validation =
            result(Availability::Rejected, QStringLiteral("wrong-architecture"));
        return staged;
    }
    if (staged.snapshot.sha256 != release->sha256)
    {
        staged.validation =
            result(Availability::Rejected, QStringLiteral("hash-mismatch"));
        return staged;
    }

    const QString stagedPath = staged.snapshot.file->fileName();
    staged.snapshot.file->close();
    if (!hasRequiredExportsAndVersion(stagedPath))
    {
        staged.validation =
            result(Availability::Rejected, QStringLiteral("runtime-incompatible"));
        return staged;
    }
    staged.validation = result(Availability::Ready,
                               QStringLiteral("ready"),
                               info.absoluteFilePath());
    return staged;
}

} // namespace

const ReleaseBinary *currentReleaseBinary()
{
    static const ReleaseBinary selected = []() {
        ReleaseBinary value;
        const QString platform = currentPlatform();
        const QString architecture = currentArchitecture();
        for (const detail::ReleaseManifestEntry &entry : detail::kReleaseManifest)
        {
            if (platform != QLatin1String(entry.platform) ||
                architecture != QLatin1String(entry.architecture))
            {
                continue;
            }
            value.version = QLatin1String(entry.version);
            value.platform = QLatin1String(entry.platform);
            value.architecture = QLatin1String(entry.architecture);
            value.fileName = QLatin1String(entry.fileName);
            value.fileSize = entry.fileSize;
            value.sha256 = QByteArray::fromHex(entry.sha256);
            value.downloadUrl = QLatin1String(entry.downloadUrl);
            value.releaseUrl = QLatin1String(detail::kReleaseUrl);
            break;
        }
        return value;
    }();
    return selected.version.isEmpty() ? nullptr : &selected;
}

QString defaultStorageRoot()
{
    return QDir(QDir::homePath()).filePath(QStringLiteral(".wxalh/airan-desk/codecs/openh264"));
}

QString managedLibraryPath(const QString &storageRoot)
{
    const ReleaseBinary *release = currentReleaseBinary();
    if (!release)
        return QString();
    const QString root = storageRoot.isEmpty() ? defaultStorageRoot() : storageRoot;
    const QString immutableVersion =
        release->version + QLatin1Char('-') + QString::fromLatin1(release->sha256.toHex());
    return QDir(root).filePath(immutableVersion + QLatin1Char('/') + release->fileName);
}

bool isManagedLibraryPath(const QString &path, const QString &storageRoot)
{
    const QString expected = managedLibraryPath(storageRoot);
    return !path.trimmed().isEmpty() && !expected.isEmpty() &&
           samePath(path, expected);
}

ValidationResult validateOfficialBinary(const QString &path)
{
    if (path.trimmed().isEmpty())
        return result(Availability::Missing, QStringLiteral("missing"));
    QTemporaryDir validationRoot(
        QDir::temp().filePath(QStringLiteral("airan-openh264-validation-XXXXXX")));
    if (!validationRoot.isValid())
        return result(Availability::Rejected, QStringLiteral("storage-unavailable"));
    return stageAndValidateOfficialBinary(
               QFileInfo(path).absoluteFilePath(),
               QDir(validationRoot.path()).filePath(QStringLiteral("staging")))
        .validation;
}

ValidationResult validateManagedBinary(const QString &path, const QString &storageRoot)
{
    const QString root = storageRoot.isEmpty() ? defaultStorageRoot() : storageRoot;
    const QString expected = managedLibraryPath(root);
    if (path.trimmed().isEmpty() || expected.isEmpty())
        return result(Availability::Missing, QStringLiteral("missing"));
    if (!isManagedLibraryPath(path, root))
        return result(Availability::Rejected, QStringLiteral("unmanaged-path"));

    const QFileInfo expectedInfo(expected);
    if (!expectedInfo.exists())
        return result(Availability::Missing, QStringLiteral("missing"));
    const QString versionDir = expectedInfo.dir().absolutePath();
    if (isUnsafeLink(root) || isUnsafeLink(versionDir))
        return result(Availability::Rejected, QStringLiteral("symlink-rejected"));

    return validateKnownBinary(expected);
}

ValidationResult installOfficialBinary(const QString &sourcePath, const QString &storageRoot)
{
    const QString root = storageRoot.isEmpty() ? defaultStorageRoot() : storageRoot;
    QTemporaryDir validationRoot(
        QDir::temp().filePath(QStringLiteral("airan-openh264-install-XXXXXX")));
    if (!validationRoot.isValid())
        return result(Availability::Rejected, QStringLiteral("storage-unavailable"));
    StagedOfficialBinary sourceValidation = stageAndValidateOfficialBinary(
        QFileInfo(sourcePath).absoluteFilePath(),
        QDir(validationRoot.path()).filePath(QStringLiteral("staging")));
    if (sourceValidation.validation.availability != Availability::Ready)
        return sourceValidation.validation;

    const ReleaseBinary *release = currentReleaseBinary();
    if (!release)
        return result(Availability::Rejected, QStringLiteral("unsupported-platform"));
    const QString destination = managedLibraryPath(root);
    const QString versionDir = QFileInfo(destination).dir().absolutePath();
    if (!QDir().mkpath(versionDir) || isUnsafeLink(root) || isUnsafeLink(versionDir))
        return result(Availability::Rejected, QStringLiteral("storage-unavailable"));

    if (QFileInfo::exists(destination))
    {
        const ValidationResult existing = validateManagedBinary(destination, root);
        if (existing.availability == Availability::Ready)
        {
            return result(Availability::RestartRequired,
                          QStringLiteral("already-installed"),
                          existing.absolutePath);
        }
    }
    QFile stagedSource(sourceValidation.snapshot.file->fileName());
    if (!stagedSource.open(QIODevice::ReadOnly))
        return result(Availability::Rejected, QStringLiteral("unreadable"));
    QString writeReason;
    if (!writeAtomically(destination, stagedSource, &writeReason))
        return result(Availability::Rejected, writeReason);
    stagedSource.close();

    const ValidationResult installed = validateManagedBinary(destination, root);
    if (installed.availability != Availability::Ready)
    {
        QFile::remove(destination);
        return installed;
    }
    return result(Availability::RestartRequired,
                  QStringLiteral("restart-required"),
                  installed.absolutePath);
}

ValidationResult currentAvailability(bool enabled,
                                     const QString &configuredPath,
                                     const QString &storageRoot)
{
    if (!enabled)
        return result(Availability::Disabled, QStringLiteral("disabled"));
    if (configuredPath.trimmed().isEmpty())
        return result(Availability::Missing, QStringLiteral("missing"));
    return validateManagedBinary(configuredPath, storageRoot);
}

} // namespace airan::media::openh264
