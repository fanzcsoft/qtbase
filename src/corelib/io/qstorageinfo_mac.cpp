// Copyright (C) 2014 Ivan Komissarov <ABBAPOH@gmail.com>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#include "qstorageinfo_p.h"

#include <QtCore/qfileinfo.h>
#include <QtCore/private/qcore_mac_p.h>

#include <CoreFoundation/CoreFoundation.h>
#include <CoreFoundation/CFURLEnumerator.h>

#include <sys/mount.h>

#define QT_STATFSBUF struct statfs
#define QT_STATFS    ::statfs

QT_BEGIN_NAMESPACE

void QStorageInfoPrivate::initRootPath()
{
    rootPath = QFileInfo(rootPath).canonicalFilePath();

    if (rootPath.isEmpty())
        return;

    retrieveUrlProperties(true);
}

void QStorageInfoPrivate::doStat()
{
    initRootPath();

    if (rootPath.isEmpty())
        return;

    retrieveLabel();
    retrievePosixInfo();
    retrieveUrlProperties();
}

void QStorageInfoPrivate::retrievePosixInfo()
{
    QT_STATFSBUF statfs_buf;
    int result = QT_STATFS(QFile::encodeName(rootPath).constData(), &statfs_buf);
    if (result == 0) {
        device = QByteArray(statfs_buf.f_mntfromname);
        readOnly = (statfs_buf.f_flags & MNT_RDONLY) != 0;
        fileSystemType = QByteArray(statfs_buf.f_fstypename);
        blockSize = statfs_buf.f_bsize;
    }
}

static inline qint64 CFDictionaryGetInt64(CFDictionaryRef dictionary, const void *key)
{
    CFNumberRef cfNumber = (CFNumberRef)CFDictionaryGetValue(dictionary, key);
    if (!cfNumber)
        return -1;
    qint64 result;
    bool ok = CFNumberGetValue(cfNumber, kCFNumberSInt64Type, &result);
    if (!ok)
        return -1;
    return result;
}

void QStorageInfoPrivate::retrieveUrlProperties(bool initRootPath)
{
    static const void *rootPathKeys[] = { kCFURLVolumeURLKey };
    static const void *propertyKeys[] = {
        // kCFURLVolumeNameKey, // 10.7
        // kCFURLVolumeLocalizedNameKey, // 10.7
        kCFURLVolumeTotalCapacityKey,
        kCFURLVolumeAvailableCapacityKey,
        // kCFURLVolumeIsReadOnlyKey // 10.7
    };
    size_t size = (initRootPath ? sizeof(rootPathKeys) : sizeof(propertyKeys)) / sizeof(void*);
    QCFType<CFArrayRef> keys = CFArrayCreate(kCFAllocatorDefault,
                                             initRootPath ? rootPathKeys : propertyKeys,
                                             size,
                                             nullptr);

    if (!keys)
        return;

    const QCFString cfPath = rootPath;
    if (initRootPath)
        rootPath.clear();

    QCFType<CFURLRef> url = CFURLCreateWithFileSystemPath(kCFAllocatorDefault,
                                                          cfPath,
                                                          kCFURLPOSIXPathStyle,
                                                          true);
    if (!url)
        return;

    CFErrorRef error;
    QCFType<CFDictionaryRef> map = CFURLCopyResourcePropertiesForKeys(url, keys, &error);

    if (!map)
        return;

    if (initRootPath) {
        const CFURLRef rootUrl = (CFURLRef)CFDictionaryGetValue(map, kCFURLVolumeURLKey);
        if (!rootUrl)
            return;

        rootPath = QCFString(CFURLCopyFileSystemPath(rootUrl, kCFURLPOSIXPathStyle));
        valid = true;
        ready = true;

        return;
    }

    bytesTotal = CFDictionaryGetInt64(map, kCFURLVolumeTotalCapacityKey);
    bytesAvailable = CFDictionaryGetInt64(map, kCFURLVolumeAvailableCapacityKey);
    bytesFree = bytesAvailable;
}

void QStorageInfoPrivate::retrieveLabel()
{
    QCFString path = CFStringCreateWithFileSystemRepresentation(0,
        QFile::encodeName(rootPath).constData());
    if (!path)
        return;

    QCFType<CFURLRef> url = CFURLCreateWithFileSystemPath(0, path, kCFURLPOSIXPathStyle, true);
    if (!url)
        return;

    QCFType<CFURLRef> volumeUrl;
    if (!CFURLCopyResourcePropertyForKey(url, kCFURLVolumeURLKey, &volumeUrl, NULL))
        return;

    QCFString volumeName;
    if (!CFURLCopyResourcePropertyForKey(url, kCFURLNameKey, &volumeName, NULL))
        return;

    name = volumeName;
}

QList<QStorageInfo> QStorageInfoPrivate::mountedVolumes()
{
    QList<QStorageInfo> volumes;

    QCFType<CFURLEnumeratorRef> enumerator;
    enumerator = CFURLEnumeratorCreateForMountedVolumes(nullptr,
                                                        kCFURLEnumeratorSkipInvisibles,
                                                        nullptr);

    CFURLEnumeratorResult result = kCFURLEnumeratorSuccess;
    do {
        CFURLRef url;
        CFErrorRef error;
        result = CFURLEnumeratorGetNextURL(enumerator, &url, &error);
        if (result == kCFURLEnumeratorSuccess) {
            const QCFString urlString = CFURLCopyFileSystemPath(url, kCFURLPOSIXPathStyle);
            volumes.append(QStorageInfo(urlString));
        }
    } while (result != kCFURLEnumeratorEnd);

    return volumes;
}

QT_END_NAMESPACE
