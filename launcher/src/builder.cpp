#include "builder.h"
#include "paths.h"
#include "tr.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

namespace {
QJsonObject readJson(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}
}

Builder::Builder(QObject *parent) : QObject(parent)
{
    connect(&proc_, &QProcess::readyReadStandardOutput, this, &Builder::readLines);
    connect(&proc_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus status) {
                readLines();
                const bool ok = status == QProcess::NormalExit && code == 0 && error_.isEmpty();
                QString msg = error_;
                if (cancelled_)
                    msg = T("Cancelled.");
                else if (!ok && msg.isEmpty())
                    msg = T("The build stopped (exit %1). Its log is %2/build.log.")
                              .arg(code)
                              .arg(QDir::toNativeSeparators(paths::releaseUserDir()));
                emit finished(ok, msg, unsupported_);
            });
    connect(&proc_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            emit finished(false, T("The builder could not be started (%1).").arg(proc_.errorString()), false);
    });
}

QString Builder::releaseVersion() const
{
    return readJson(paths::releaseJson()).value(QLatin1String("version")).toString();
}

QString Builder::source() const
{
    return readJson(paths::releaseUserDir() + QLatin1String("/source.json")).value(QLatin1String("path")).toString();
}

Builder::State Builder::state() const
{
    if (paths::releaseJson().isEmpty())
        return State::NotRelease;
    const QString dir = paths::releaseUserDir();
    const QString built = readJson(dir + QLatin1String("/build.json")).value(QLatin1String("release_version")).toString();
    if (built.isEmpty())
        return State::Unbuilt;
    if (built != releaseVersion() || !QFileInfo::exists(paths::gameExe()))
        return State::Stale;
    if (!QFileInfo::exists(dir + QLatin1String("/game/default.xbe")))
        return State::Moved;
    return State::Ready;
}

bool Builder::start(const QString &source, bool relink, QString *error)
{
    if (running())
        return false;
    const QString exe = paths::builderExe();
    if (!QFileInfo::exists(exe)) {
        if (error)
            *error = T("This is not a release: there is no builder at %1.").arg(QDir::toNativeSeparators(exe));
        return false;
    }
    error_.clear();
    pending_.clear();
    unsupported_ = cancelled_ = false;
    proc_.setProgram(exe);
    proc_.setArguments({relink ? QStringLiteral("relink") : QStringLiteral("build"), QStringLiteral("--source"),
                        source});
    proc_.setProcessChannelMode(QProcess::SeparateChannels);
    proc_.start();
    return true;
}

void Builder::cancel()
{
    if (!running())
        return;
    cancelled_ = true;
    proc_.terminate();
    if (!proc_.waitForFinished(3000))
        proc_.kill();
}

// One compact JSON object a line: start, progress, done, error (builder
// tools/builder/src/main.rs). Only progress and error matter here.
void Builder::readLines()
{
    pending_ += proc_.readAllStandardOutput();
    int nl;
    while ((nl = pending_.indexOf('\n')) >= 0) {
        const QByteArray line = pending_.left(nl).trimmed();
        pending_.remove(0, nl + 1);
        const QJsonObject o = QJsonDocument::fromJson(line).object();
        const QString ev = o.value(QLatin1String("event")).toString();
        if (ev == QLatin1String("progress"))
            emit progress(o.value(QLatin1String("overall")).toDouble(), o.value(QLatin1String("label")).toString(),
                          o.value(QLatin1String("detail")).toString());
        else if (ev == QLatin1String("error")) {
            error_ = o.value(QLatin1String("message")).toString();
            unsupported_ = o.value(QLatin1String("unsupported")).toBool();
        }
    }
}
