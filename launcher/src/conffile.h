// The game's settings file (xboxrecomp src/config/recomp_config.c), edited
// line by line: `key = value`, `#` or `;` comments, the last of a key
// wins. What the launcher does not set -- keys without a row, comments a
// player wrote, the toolkit's own explanations -- stays where it is.
//
// The toolkit writes an "auto" or empty value as a commented-out line
// (`# hor_plus = auto`) rather than as the word, so that a build from
// before the word reads the file as it read a missing key; unset() does
// the same.
#pragma once

#include <QString>
#include <QStringList>

class ConfFile {
public:
    ConfFile() = default;
    explicit ConfFile(QString path) : path_(std::move(path)) {}

    bool load();                        // false: no file yet (not an error)
    bool save(QString *error) const;    // creates the folders above it
    void setText(const QString &text);  // for tests
    QString text() const { return lines_.join(QLatin1Char('\n')) + QLatin1Char('\n'); }

    bool has(const QString &key) const;
    QString get(const QString &key, const QString &fallback = {}) const;
    int getInt(const QString &key, int fallback, int lo, int hi) const;
    bool getBool(const QString &key, bool fallback) const;

    // Replaces the last active line of the key, or appends one after a
    // comment line (`help`, may be empty) when the file has none.
    void set(const QString &key, const QString &value, const QString &help = {});
    void set(const QString &key, int value, const QString &help = {})
    {
        set(key, QString::number(value), help);
    }
    void setBool(const QString &key, bool on, const QString &help = {})
    {
        set(key, on ? 1 : 0, help);
    }
    // Every active line of the key becomes `# key =`: the game's default.
    void unset(const QString &key);

    const QString &path() const { return path_; }

private:
    int find(const QString &key) const;  // the last active line, or -1
    QString path_;
    QStringList lines_;
};

// "0", "off", "no", "false" are off, as the toolkit reads them; anything
// else is on.
bool confOffWord(const QString &v);
