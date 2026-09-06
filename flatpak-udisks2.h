// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QDBusUnixFileDescriptor>
#include <QDebug>
#include <QMap>
#include <QVariantMap>
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace FlatpakUDisks {
using Interfaces = QMap<QString, QVariantMap>;
using Objects = QMap<QDBusObjectPath, Interfaces>;
inline constexpr auto service = "org.freedesktop.UDisks2";
inline constexpr auto blockInterface = "org.freedesktop.UDisks2.Block";
inline constexpr auto partitionInterface = "org.freedesktop.UDisks2.Partition";
inline constexpr auto filesystemInterface = "org.freedesktop.UDisks2.Filesystem";

inline bool active() {
    return ::access("/.flatpak-info", F_OK) == 0;
}

inline QDBusMessage call(const QString &path, const QString &interface,
                         const QString &method, const QVariantList &arguments = {}) {
    auto message = QDBusMessage::createMethodCall(service, path, interface, method);
    message.setArguments(arguments);
    message.setInteractiveAuthorizationAllowed(true);
    // Allow time for the host's polkit authentication dialog.
    return QDBusConnection::systemBus().call(message, QDBus::Block, 120000);
}

inline bool getObjects(Objects &objects) {
    qDBusRegisterMetaType<Interfaces>();
    qDBusRegisterMetaType<Objects>();
    QDBusReply<Objects> reply = call("/org/freedesktop/UDisks2",
        "org.freedesktop.DBus.ObjectManager", "GetManagedObjects");
    if (!reply.isValid()) {
        qWarning() << "UDisks2 enumeration failed:" << reply.error();
        errno = EIO;
        return false;
    }
    objects = reply.value();
    return true;
}

inline QString findDevice(const Objects &objects, dev_t number) {
    for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
        const auto properties = it.value().value(blockInterface);
        if (properties.contains("DeviceNumber") &&
            properties.value("DeviceNumber").toULongLong() == static_cast<qulonglong>(number))
            return it.key().path();
    }
    return {};
}

inline bool belongsToDevice(const QString &path, const Interfaces &interfaces,
                            const QString &device) {
    return path == device ||
        qvariant_cast<QDBusObjectPath>(interfaces.value(partitionInterface).value("Table")).path() == device;
}

inline int openObject(const QString &device, int flags) {
    // Keep exclusive access: never retry a denied/busy request as a shared open.
    QVariantMap options{{"flags", (flags & ~O_ACCMODE) | O_CLOEXEC}};
    QDBusReply<QDBusUnixFileDescriptor> reply = call(device, blockInterface,
        "OpenDevice", {QStringLiteral("rw"), options});
    if (!reply.isValid()) {
        qWarning() << "UDisks2 could not open" << device << reply.error();
        // Treat failed authorization as final, including when the user cancels.
        errno = ECANCELED;
        return -1;
    }
    // Own our descriptor independently of the lifetime of the D-Bus reply.
    const int fd = ::fcntl(reply.value().fileDescriptor(), F_DUPFD_CLOEXEC, 0);
    if (fd < 0)
        return -1;
    return fd;
}

inline int openDevice(const char *path, int flags) {
    struct stat st;
    if (::stat(path, &st) != 0)
        return -1;
    if (!S_ISBLK(st.st_mode)) {
        errno = ENOTBLK;
        return -1;
    }
    Objects objects;
    if (!getObjects(objects))
        return -1;
    const auto device = findDevice(objects, st.st_rdev);
    if (device.isEmpty()) {
        errno = ENODEV;
        return -1;
    }
    const int fd = openObject(device, flags);
    if (fd < 0)
        return -1;
    struct stat opened;
    if (::fstat(fd, &opened) != 0 || !S_ISBLK(opened.st_mode) || opened.st_rdev != st.st_rdev) {
        ::close(fd);
        errno = ENODEV;
        return -1;
    }
    return fd;
}

inline bool unmountObjects(const Objects &objects, const QString &device) {
    // Use host objects, since /proc/mounts only describes the sandbox's mounts.
    // Partition.Table avoids prefix collisions (sda/sdaa, mmcblk1/mmcblk10).
    for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
        if (!belongsToDevice(it.key().path(), it.value(), device) ||
            !it.value().contains(filesystemInterface))
            continue;
        const auto reply = call(it.key().path(), filesystemInterface,
            "Unmount", {QVariantMap{}});
        if (reply.type() == QDBusMessage::ErrorMessage &&
            reply.errorName() != "org.freedesktop.UDisks2.Error.NotMounted") {
            qWarning() << "UDisks2 unmount failed:" << reply.errorName() << reply.errorMessage();
            return false;
        }
    }
    return true;
}

inline bool unmountDevice(const QString &path) {
    struct stat st;
    if (::stat(path.toUtf8().constData(), &st) != 0)
        return false;
    // Image-file output needs no host unmount operation.
    if (S_ISREG(st.st_mode))
        return true;
    if (!S_ISBLK(st.st_mode))
        return false;
    Objects objects;
    if (!getObjects(objects))
        return false;
    const auto device = findDevice(objects, st.st_rdev);
    if (device.isEmpty())
        return false;
    return unmountObjects(objects, device);
}

} // namespace FlatpakUDisks
