// The controls (xboxrecomp src/input/input_bindings.c): one JSON file per
// person, shared by every game. Controller 1 is edited here; the other
// ports, unknown keys and any third or fourth source of a control are kept
// as the file had them.
//
// Each control has at most one pad source ("pad:a", "pad:rx+", ...) and one
// keyboard-or-mouse source ("key:W", "mouse:left") as far as this editor is
// concerned, which is what the runtime reads first of each.
#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace bind {

struct Control {
    const char *name;        // in the file
    const char *label;       // English; tr.cpp has the German
    const char *pad;         // the built-in pad source
    const char *key;         // the built-in keyboard source of controller 1, or ""
};
const QVector<Control> &controls();     // the 24, in the file's order

// Every pad source the runtime knows, "none" first.
QStringList padSources();
// What a source reads as in the window ("Left mouse button", "W", ...).
QString sourceLabel(const QString &source);

class File {
public:
    bool load(const QString &path);     // false: no file (defaults apply)
    bool save(QString *error) const;
    void setJson(const QJsonObject &o) { root_ = o; }
    QJsonObject json() const { return root_; }

    // Controller 1.
    QString device() const;             // "gamepad:0".."gamepad:3", "keyboard", "none"
    void setDevice(const QString &d);
    QString padApi() const;             // "sdl" or "xinput"
    void setPadApi(const QString &api);
    QString padSource(const QString &control) const;
    QString keySource(const QString &control) const;
    void setPadSource(const QString &control, const QString &src);
    void setKeySource(const QString &control, const QString &src);

    void resetController1();            // the built-in mapping
    // Keyboard and mouse for a PC shooter on top of the pad's mapping:
    // WASD on the left stick, the mouse buttons on the triggers (fire,
    // aim/zoom), the rest on keys near them. The aim itself is the mouse.
    void applyPcPreset();

    const QString &path() const { return path_; }

private:
    QJsonObject controller1() const;
    void setController1(const QJsonObject &c);
    QStringList sources(const QString &control) const;
    void setSources(const QString &control, const QStringList &s);

    QString path_;
    QJsonObject root_;
};

}
