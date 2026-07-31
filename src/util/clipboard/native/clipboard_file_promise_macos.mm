#include "util/clipboard/native/clipboard_file_promise.h"

#if defined(__APPLE__)

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#import <objc/runtime.h>

#include "common/logger_manager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIODevice>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QObject>
#include <QPointer>
#include <QThread>
#include <QTimer>
#include <QWidget>

#include <algorithm>
#include <atomic>
#include <memory>
#include <utility>
#include <vector>

static QString cleanDisplayName(const ClipboardFilePromiseItem &item)
{
    QString name = item.displayName.trimmed();
    if (name.isEmpty())
        name = QFileInfo(item.sourcePath).fileName();
    if (name.isEmpty())
        name = QStringLiteral("item");
    name.replace(QLatin1Char('/'), QLatin1Char('_'));
    name.replace(QLatin1Char('\\'), QLatin1Char('_'));
    return name;
}

struct MacFilePromiseState
{
    ClipboardFilePromise::ReadFileChunk reader;
    QList<ClipboardFilePromiseItem> items;
    QString cacheRoot;
    std::atomic_bool cancelled{false};
};

@interface AiranFilePromiseDelegate : NSObject <NSFilePromiseProviderDelegate, NSDraggingSource>
{
@public
    std::shared_ptr<MacFilePromiseState> state;
}
@end

static std::vector<std::weak_ptr<MacFilePromiseState>> g_macPromiseStates;
static const void *kAiranFilePromiseDelegateAssociationKey =
    &kAiranFilePromiseDelegateAssociationKey;

@implementation AiranFilePromiseDelegate

- (NSString *)filePromiseProvider:(NSFilePromiseProvider *)filePromiseProvider fileNameForType:(NSString *)fileType
{
    (void)fileType;
    NSNumber *index = (NSNumber *)filePromiseProvider.userInfo;
    const int itemIndex = index ? [index intValue] : 0;
    const std::shared_ptr<MacFilePromiseState> currentState = state;
    if (!currentState || currentState->cancelled.load() ||
        itemIndex < 0 || itemIndex >= currentState->items.size())
        return @"item";
    const QString name = cleanDisplayName(currentState->items.at(itemIndex));
    return [NSString stringWithUTF8String:name.toUtf8().constData()];
}

- (void)filePromiseProvider:(NSFilePromiseProvider *)filePromiseProvider
          writePromiseToURL:(NSURL *)url
          completionHandler:(void (^)(NSError * _Nullable error))completionHandler
{
    NSNumber *index = (NSNumber *)filePromiseProvider.userInfo;
    const int itemIndex = index ? [index intValue] : 0;
    const std::shared_ptr<MacFilePromiseState> currentState = state;
    if (!currentState || currentState->cancelled.load() ||
        itemIndex < 0 || itemIndex >= currentState->items.size())
    {
        if (completionHandler)
            completionHandler([NSError errorWithDomain:NSCocoaErrorDomain code:NSFileReadNoSuchFileError userInfo:nil]);
        return;
    }

    const ClipboardFilePromiseItem item = currentState->items.at(itemIndex);
    const QString destinationPath = QString::fromUtf8([[url path] UTF8String]);
    if (destinationPath.isEmpty())
    {
        if (completionHandler)
            completionHandler([NSError errorWithDomain:NSCocoaErrorDomain code:NSFileReadNoSuchFileError userInfo:nil]);
        return;
    }

    QFile file(destinationPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly))
    {
        if (completionHandler)
            completionHandler([NSError errorWithDomain:NSCocoaErrorDomain code:NSFileWriteFileExistsError userInfo:nil]);
        return;
    }

    constexpr qint64 kChunkSize = 64 * 1024;
    qint64 offset = 0;
    while (offset < item.size)
    {
        if (currentState->cancelled.load())
        {
            file.close();
            file.remove();
            if (completionHandler)
                completionHandler([NSError errorWithDomain:NSCocoaErrorDomain code:NSUserCancelledError userInfo:nil]);
            return;
        }
        const qint64 requestBytes = qMin<qint64>(kChunkSize, item.size - offset);
        bool ok = false;
        QString errorMessage;
        const QByteArray chunk = currentState->reader
                                     ? currentState->reader(item.sourcePath, offset, requestBytes, &ok, &errorMessage)
                                     : QByteArray();
        if (!ok || chunk.isEmpty() || chunk.size() > requestBytes)
        {
            LOG_WARN("macOS clipboard promise failed: path={}, offset={}, bytes={}, error={}",
                     item.sourcePath,
                     offset,
                     requestBytes,
                     errorMessage);
            file.close();
            file.remove();
            if (completionHandler)
                completionHandler([NSError errorWithDomain:NSCocoaErrorDomain code:NSFileReadUnknownError userInfo:nil]);
            return;
        }
        if (file.write(chunk) != chunk.size())
        {
            file.close();
            file.remove();
            if (completionHandler)
                completionHandler([NSError errorWithDomain:NSCocoaErrorDomain code:NSFileWriteUnknownError userInfo:nil]);
            return;
        }
        offset += chunk.size();
    }
    file.close();
    if (completionHandler)
        completionHandler(nil);
}

- (NSDragOperation)draggingSession:(NSDraggingSession *)session
 sourceOperationMaskForDraggingContext:(NSDraggingContext)context
{
    (void)session;
    (void)context;
    return NSDragOperationCopy;
}

@end

static bool onGuiThread()
{
    QCoreApplication *app = QCoreApplication::instance();
    return !app || QThread::currentThread() == app->thread();
}

static bool installNativeMacPromise(const QList<ClipboardFilePromiseItem> &items,
                                    const QString &cacheRoot,
                                    const ClipboardFilePromise::ReadFileChunk &readFileChunk,
                                    QString *errorMessage);

static QObject *macPromiseThreadInvoker()
{
    static QObject *invoker = nullptr;
    static QMutex mutex;
    QMutexLocker locker(&mutex);
    if (!invoker)
    {
        invoker = new QObject();
        if (QCoreApplication::instance())
            invoker->moveToThread(QCoreApplication::instance()->thread());
    }
    return invoker;
}

static bool validateMacPromiseItems(const QList<ClipboardFilePromiseItem> &items,
                                    const QString &cacheRoot,
                                    const ClipboardFilePromise::ReadFileChunk &readFileChunk,
                                    QString *errorMessage)
{
    if (items.isEmpty())
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("No file promises were provided.");
        return false;
    }
    if (cacheRoot.isEmpty() || !readFileChunk)
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("macOS clipboard file promise prerequisites are missing.");
        return false;
    }
    for (const ClipboardFilePromiseItem &item : items)
    {
        if (item.isDirectory)
        {
            if (errorMessage)
                *errorMessage = QStringLiteral("macOS clipboard file promise does not support directory promises yet.");
            return false;
        }
        if (item.sourcePath.isEmpty() || item.size < 0)
        {
            if (errorMessage)
                *errorMessage = QStringLiteral("A macOS clipboard file promise has invalid metadata.");
            return false;
        }
    }
    return true;
}

static AiranFilePromiseDelegate *createMacPromiseDelegate(
    const QList<ClipboardFilePromiseItem> &items,
    const QString &cacheRoot,
    const ClipboardFilePromise::ReadFileChunk &readFileChunk)
{
    auto promiseState = std::make_shared<MacFilePromiseState>();
    promiseState->reader = readFileChunk;
    promiseState->items = items;
    promiseState->cacheRoot = QDir::cleanPath(cacheRoot);

    AiranFilePromiseDelegate *delegate = [[AiranFilePromiseDelegate alloc] init];
    delegate->state = std::move(promiseState);
    g_macPromiseStates.erase(
        std::remove_if(g_macPromiseStates.begin(),
                       g_macPromiseStates.end(),
                       [](const std::weak_ptr<MacFilePromiseState> &entry) {
                           return entry.expired();
                       }),
        g_macPromiseStates.end());
    g_macPromiseStates.push_back(delegate->state);
    return delegate;
}

static NSMutableArray *createMacPromiseProviders(AiranFilePromiseDelegate *delegate, int itemCount)
{
    NSMutableArray *providers = [NSMutableArray arrayWithCapacity:itemCount];
    for (int i = 0; i < itemCount; ++i)
    {
        NSFilePromiseProvider *provider = [[NSFilePromiseProvider alloc] initWithFileType:@"public.data" delegate:delegate];
        provider.userInfo = @(i);
        objc_setAssociatedObject(provider,
                                 kAiranFilePromiseDelegateAssociationKey,
                                 delegate,
                                 OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        [providers addObject:provider];
        [provider release];
    }
    return providers;
}

static void cancelNativeMacPromise(const QString &cacheRoot)
{
    const QString key = QDir::cleanPath(cacheRoot);
    if (key.isEmpty())
        return;

    auto it = g_macPromiseStates.begin();
    while (it != g_macPromiseStates.end())
    {
        const std::shared_ptr<MacFilePromiseState> promiseState = it->lock();
        if (!promiseState)
        {
            it = g_macPromiseStates.erase(it);
            continue;
        }
        if (promiseState->cacheRoot == key)
        {
            promiseState->cancelled.store(true);
            it = g_macPromiseStates.erase(it);
            continue;
        }
        ++it;
    }
}

static bool installNativeMacPromise(const QList<ClipboardFilePromiseItem> &items,
                                    const QString &cacheRoot,
                                    const ClipboardFilePromise::ReadFileChunk &readFileChunk,
                                    QString *errorMessage)
{
    if (!validateMacPromiseItems(items, cacheRoot, readFileChunk, errorMessage))
        return false;

    NSPasteboard *pasteboard = [NSPasteboard generalPasteboard];
    if (!pasteboard)
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("macOS pasteboard is unavailable.");
        return false;
    }

    AiranFilePromiseDelegate *delegate = createMacPromiseDelegate(items, cacheRoot, readFileChunk);
    NSMutableArray *objects = createMacPromiseProviders(delegate, items.size());
    [delegate release];

    [pasteboard clearContents];
    const BOOL ok = [pasteboard writeObjects:objects];
    if (!ok)
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("Failed to write file promise to macOS pasteboard.");
        return false;
    }
    LOG_INFO("Installed macOS clipboard file promise for {} file(s)", items.size());
    return true;
}

bool ClipboardFilePromise::isSupported()
{
    return QGuiApplication::instance() != nullptr;
}

void ClipboardFilePromise::cancelCacheRoot(const QString &cacheRoot)
{
    if (cacheRoot.isEmpty())
        return;
    if (onGuiThread())
    {
        cancelNativeMacPromise(cacheRoot);
        return;
    }
    QTimer::singleShot(0, macPromiseThreadInvoker(), [cacheRoot]() {
        cancelNativeMacPromise(cacheRoot);
    });
}

bool ClipboardFilePromise::install(const QList<ClipboardFilePromiseItem> &items,
                                   const QString &cacheRoot,
                                   const ReadFileChunk &readFileChunk,
                                   QString *errorMessage)
{
    if (!onGuiThread())
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("macOS clipboard file promise installation must run on the GUI thread; use installAsync().");
        return false;
    }
    return installNativeMacPromise(items, cacheRoot, readFileChunk, errorMessage);
}

void ClipboardFilePromise::installAsync(QObject *context,
                                        const QList<ClipboardFilePromiseItem> &items,
                                        const QString &cacheRoot,
                                        const ReadFileChunk &readFileChunk,
                                        const InstallCompletion &completion)
{
    const QPointer<QObject> guard(context);
    const bool hasContext = context != nullptr;
    QTimer::singleShot(0, macPromiseThreadInvoker(),
                       [guard, hasContext, items, cacheRoot, readFileChunk, completion]() {
                           QString errorMessage;
                           const bool ok = installNativeMacPromise(items, cacheRoot, readFileChunk, &errorMessage);
                           if (!completion)
                               return;
                           if (guard)
                           {
                               QTimer::singleShot(0, guard.data(),
                                                  [guard, completion, ok, errorMessage]() {
                                                      if (guard)
                                                          completion(ok, errorMessage);
                                                  });
                           }
                           else if (!hasContext)
                           {
                               completion(ok, errorMessage);
                           }
                       });
}

bool ClipboardFilePromise::startDrag(QWidget *dragSource,
                                     const QList<ClipboardFilePromiseItem> &items,
                                     const QString &cacheRoot,
                                     const ReadFileChunk &readFileChunk,
                                     QString *errorMessage)
{
    if (!onGuiThread())
    {
        if (errorMessage)
            *errorMessage = QCoreApplication::translate("ClipboardFilePromise", "macOS file promise drag must start on the GUI thread.");
        return false;
    }
    if (!dragSource || !validateMacPromiseItems(items, cacheRoot, readFileChunk, errorMessage))
    {
        if (!dragSource && errorMessage)
            *errorMessage = QCoreApplication::translate("ClipboardFilePromise", "macOS file promise drag has no source widget.");
        return false;
    }

    NSView *view = reinterpret_cast<NSView *>(dragSource->winId());
    NSEvent *event = [NSApp currentEvent];
    if (!view || !event)
    {
        if (errorMessage)
            *errorMessage = QCoreApplication::translate("ClipboardFilePromise", "macOS file promise drag has no active native drag event.");
        return false;
    }

    AiranFilePromiseDelegate *delegate = createMacPromiseDelegate(items, cacheRoot, readFileChunk);
    NSArray *providers = createMacPromiseProviders(delegate, items.size());
    NSMutableArray *draggingItems = [NSMutableArray arrayWithCapacity:items.size()];
    const NSPoint location = [view convertPoint:event.locationInWindow fromView:nil];
    NSImage *icon = [NSImage imageNamed:NSImageNameMultipleDocuments];
    const NSSize iconSize = icon ? icon.size : NSMakeSize(32.0, 32.0);

    for (NSUInteger i = 0; i < providers.count; ++i)
    {
        NSDraggingItem *draggingItem = [[NSDraggingItem alloc] initWithPasteboardWriter:providers[i]];
        const NSRect frame = NSMakeRect(location.x + static_cast<CGFloat>(i * 4),
                                        location.y - iconSize.height - static_cast<CGFloat>(i * 4),
                                        iconSize.width,
                                        iconSize.height);
        [draggingItem setDraggingFrame:frame contents:icon];
        [draggingItems addObject:draggingItem];
        [draggingItem release];
    }

    [view beginDraggingSessionWithItems:draggingItems event:event source:delegate];
    [delegate release];
    LOG_INFO("Started macOS file promise drag for {} file(s)", items.size());
    return true;
}

#endif
