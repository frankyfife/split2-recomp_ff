#include "conffile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTextStream>

bool confOffWord(const QString &v)
{
    const QString t = v.trimmed().toLower();
    return t == QLatin1String("0") || t == QLatin1String("off") || t == QLatin1String("no") ||
           t == QLatin1String("false");
}

namespace {
// The key of an active line, or empty for a comment, a blank line or a line
// without '='.
QString lineKey(const QString &line, QString *value = nullptr)
{
    const QString t = line.trimmed();
    if (t.isEmpty() || t.startsWith(QLatin1Char('#')) || t.startsWith(QLatin1Char(';')))
        return {};
    const int eq = t.indexOf(QLatin1Char('='));
    if (eq <= 0)
        return {};
    if (value)
        *value = t.mid(eq + 1).trimmed();
    return t.left(eq).trimmed();
}
}

bool ConfFile::load()
{
    lines_.clear();
    QFile f(path_);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    setText(QString::fromUtf8(f.readAll()));
    return true;
}

void ConfFile::setText(const QString &text)
{
    QString t = text;
    t.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    lines_ = t.split(QLatin1Char('\n'));
    while (!lines_.isEmpty() && lines_.last().isEmpty())
        lines_.removeLast();
}

bool ConfFile::save(QString *error) const
{
    QDir().mkpath(QFileInfo(path_).absolutePath());
    QSaveFile f(path_);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    f.write(text().toUtf8());
    if (!f.commit()) {
        if (error)
            *error = f.errorString();
        return false;
    }
    return true;
}

int ConfFile::find(const QString &key) const
{
    for (int i = lines_.size() - 1; i >= 0; i--)
        if (lineKey(lines_[i]) == key)
            return i;
    return -1;
}

bool ConfFile::has(const QString &key) const
{
    QString v;
    const int i = find(key);
    return i >= 0 && (lineKey(lines_[i], &v), !v.isEmpty());
}

QString ConfFile::get(const QString &key, const QString &fallback) const
{
    const int i = find(key);
    if (i < 0)
        return fallback;
    QString v;
    lineKey(lines_[i], &v);
    return v.isEmpty() ? fallback : v;   // an empty value counts as absent
}

int ConfFile::getInt(const QString &key, int fallback, int lo, int hi) const
{
    bool ok = false;
    const int v = get(key).toInt(&ok);
    if (!ok)
        return fallback;
    return v < lo ? lo : v > hi ? hi : v;
}

bool ConfFile::getBool(const QString &key, bool fallback) const
{
    return has(key) ? !confOffWord(get(key)) : fallback;
}

void ConfFile::set(const QString &key, const QString &value, const QString &help)
{
    const QString line = key + QLatin1String(" = ") + value;
    const int i = find(key);
    if (i >= 0) {
        lines_[i] = line;
        return;
    }
    // A commented-out `# key =` the toolkit wrote: take its place, so the
    // explanation above it stays with it.
    for (int j = lines_.size() - 1; j >= 0; j--) {
        QString t = lines_[j].trimmed();
        if (!t.startsWith(QLatin1Char('#')))
            continue;
        t = t.mid(1).trimmed();
        const int eq = t.indexOf(QLatin1Char('='));
        if (eq > 0 && t.left(eq).trimmed() == key) {
            lines_[j] = line;
            return;
        }
    }
    if (!lines_.isEmpty() && !lines_.last().trimmed().isEmpty())
        lines_ << QString();
    if (!help.isEmpty())
        for (const QString &h : help.split(QLatin1Char('\n')))
            lines_ << QLatin1String("# ") + h;
    lines_ << line;
}

void ConfFile::unset(const QString &key)
{
    for (QString &l : lines_)
        if (lineKey(l) == key)
            l = QLatin1String("# ") + key + QLatin1String(" =");
}
