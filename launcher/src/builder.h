// A self-building release (xboxrecomp docs/technical/self-build-release.md):
// the release carries no game code, and its builder recompiles the
// player's own disc once. This runs that builder and reads its JSON lines.
#pragma once

#include <QObject>
#include <QProcess>
#include <QString>

class Builder : public QObject {
    Q_OBJECT
public:
    enum class State { NotRelease, Unbuilt, Stale, Moved, Ready };

    explicit Builder(QObject *parent = nullptr);

    // From release.json and the player's build folder.
    State state() const;
    QString releaseVersion() const;
    QString source() const;             // the disc image or folder last built from

    bool running() const { return proc_.state() != QProcess::NotRunning; }
    // `relink` when the game's files moved: nothing is rebuilt.
    bool start(const QString &source, bool relink, QString *error);
    void cancel();

signals:
    void progress(double overall, const QString &label, const QString &detail);
    void finished(bool ok, const QString &message, bool unsupported);

private:
    void readLines();
    QProcess proc_;
    QByteArray pending_;
    QString error_;
    bool unsupported_ = false, cancelled_ = false;
};
