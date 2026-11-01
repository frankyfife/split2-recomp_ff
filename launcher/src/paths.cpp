#include "paths.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

namespace paths {

namespace {
QString env(const char *name)
{
    return qEnvironmentVariable(name);
}

QString appDir()
{
    return QCoreApplication::instance() ? QCoreApplication::applicationDirPath() : QDir::currentPath();
}

bool hasXbe(const QString &dir)
{
    return !dir.isEmpty() && QFileInfo::exists(dir + QLatin1String("/default.xbe"));
}
}

QString titleIdHex()
{
    return QStringLiteral("%1").arg(kTitleId, 8, 16, QLatin1Char('0')).toUpper();
}

QString userDir()
{
    const QString over = env("RECOMP_USER_DIR");
    if (!over.isEmpty())
        return QDir::cleanPath(over);
#if defined(Q_OS_WIN)
    const QString appdata = env("APPDATA");
    return appdata.isEmpty() ? QString() : QDir::cleanPath(appdata + QLatin1String("/xboxrecomp"));
#elif defined(Q_OS_MACOS)
    return QDir::homePath() + QLatin1String("/Library/Application Support/xboxrecomp");
#else
    const QString xdg = env("XDG_CONFIG_HOME");
    return (xdg.isEmpty() ? QDir::homePath() + QLatin1String("/.config") : xdg) +
           QLatin1String("/xboxrecomp");
#endif
}

QString settingsFile()
{
    const QString over = env("RECOMP_DISPLAY_CONFIG");
    if (!over.isEmpty())
        return over;
    const QString u = userDir();
    return u.isEmpty() ? QString() : u + QLatin1String("/titles/") + titleIdHex() + QLatin1String(".conf");
}

QString bindingsFile()
{
    const QString over = env("RECOMP_INPUT_CONFIG");
    if (!over.isEmpty())
        return over;
    const QString u = userDir();
    return u.isEmpty() ? QString() : u + QLatin1String("/input_bindings.json");
}

QString gameExeName()
{
#if defined(Q_OS_WIN)
    return QStringLiteral("split2_recomp.exe");
#else
    return QStringLiteral("split2_recomp");
#endif
}

QString releaseJson()
{
    const QString here = appDir() + QLatin1String("/release.json");
    if (QFileInfo::exists(here))
        return here;
#if defined(Q_OS_MACOS)
    const QString res = QDir::cleanPath(appDir() + QLatin1String("/../Resources/release.json"));
    if (QFileInfo::exists(res))
        return res;
#endif
    return {};
}

QString releaseUserDir()
{
    const QString over = env("XBOXRECOMP_BUILD_DIR");
    if (!over.isEmpty())
        return QDir::cleanPath(over + QLatin1Char('/') + titleIdHex());
#if defined(Q_OS_WIN)
    const QString local = env("LOCALAPPDATA");
    if (local.isEmpty())
        return {};
    return QDir::cleanPath(local + QLatin1String("/xboxrecomp/builds/") + titleIdHex());
#elif defined(Q_OS_MACOS)
    return QDir::homePath() + QLatin1String("/Library/Application Support/xboxrecomp/builds/") + titleIdHex();
#else
    const QString xdg = env("XDG_DATA_HOME");
    return (xdg.isEmpty() ? QDir::homePath() + QLatin1String("/.local/share") : xdg) +
           QLatin1String("/xboxrecomp/builds/") + titleIdHex();
#endif
}

QString builderExe()
{
    const QString rel = releaseJson();
    if (rel.isEmpty())
        return {};
#if defined(Q_OS_WIN)
    return QFileInfo(rel).absolutePath() + QLatin1String("/bin/xboxrecomp-build.exe");
#else
    return QFileInfo(rel).absolutePath() + QLatin1String("/bin/xboxrecomp-build");
#endif
}

QString gameDir()
{
    if (!releaseJson().isEmpty()) {
        const QString d = releaseUserDir() + QLatin1String("/game");
        return hasXbe(d) ? d : QString();
    }
    const QString over = env("RECOMP_GAME_DIR");
    if (!over.isEmpty())
        return hasXbe(over) ? QDir::cleanPath(over) : QString();
    const QString base = appDir();
    for (const char *rel : {"/game", "/../../game", "/../game"}) {
        const QString d = QDir::cleanPath(base + QLatin1String(rel));
        if (hasXbe(d))
            return d;
    }
    return {};
}

QString gameExe()
{
    if (!releaseJson().isEmpty())
        return releaseUserDir() + QLatin1Char('/') + gameExeName();
    return appDir() + QLatin1Char('/') + gameExeName();
}

QString gameWorkDir()
{
    return !releaseJson().isEmpty() ? releaseUserDir() : appDir();
}

QString xbeRelease(const QString &gameFolder, QString *sha1)
{
    QFile f(gameFolder + QLatin1String("/default.xbe"));
    if (gameFolder.isEmpty() || !f.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash h(QCryptographicHash::Sha1);
    h.addData(&f);
    const QString hex = QString::fromLatin1(h.result().toHex());
    if (sha1)
        *sha1 = hex;
    // CLAUDE.md, "The title": the two releases every address belongs to.
    if (hex == QLatin1String("2809eb147385723eaa90c425be89ee4db033fc5a"))
        return QStringLiteral("PAL");
    if (hex == QLatin1String("3ec95fe3ae9e7794d83a5b489ad8583f352e0c18"))
        return QStringLiteral("USA");
    return {};
}

QString savesDir(const QString &gameFolder)
{
    return gameFolder.isEmpty() ? QString() : gameFolder + QLatin1String("/UDATA/4553000a");
}

}
