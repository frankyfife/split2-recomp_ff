#include "bindings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

namespace bind {

// xboxrecomp src/launcher/launcher_bindings.c: the names, the labels and the
// built-in mapping, in the file's order.
const QVector<Control> &controls()
{
    static const QVector<Control> k = {
        {"a", "A", "pad:a", "key:Z"},
        {"b", "B", "pad:b", "key:X"},
        {"x", "X", "pad:x", "key:A"},
        {"y", "Y", "pad:y", "key:S"},
        {"black", "Black", "pad:lshoulder", "key:W"},
        {"white", "White", "pad:rshoulder", "key:Q"},
        {"start", "Start", "pad:start", "key:RETURN"},
        {"back", "Back", "pad:back", "key:BACK"},
        {"ltrigger", "Left trigger", "pad:lt", "key:E"},
        {"rtrigger", "Right trigger", "pad:rt", "key:R"},
        {"dpad_up", "D-pad up", "pad:dpad_up", "key:UP"},
        {"dpad_down", "D-pad down", "pad:dpad_down", "key:DOWN"},
        {"dpad_left", "D-pad left", "pad:dpad_left", "key:LEFT"},
        {"dpad_right", "D-pad right", "pad:dpad_right", "key:RIGHT"},
        {"lstick_up", "Left stick up", "pad:ly+", ""},
        {"lstick_down", "Left stick down", "pad:ly-", ""},
        {"lstick_left", "Left stick left", "pad:lx-", ""},
        {"lstick_right", "Left stick right", "pad:lx+", ""},
        {"lthumb", "Left stick press", "pad:lthumb", ""},
        {"rstick_up", "Right stick up", "pad:ry+", ""},
        {"rstick_down", "Right stick down", "pad:ry-", ""},
        {"rstick_left", "Right stick left", "pad:rx-", ""},
        {"rstick_right", "Right stick right", "pad:rx+", ""},
        {"rthumb", "Right stick press", "pad:rthumb", ""},
    };
    return k;
}

QStringList padSources()
{
    return {QStringLiteral("none"),
            QStringLiteral("pad:a"), QStringLiteral("pad:b"), QStringLiteral("pad:x"), QStringLiteral("pad:y"),
            QStringLiteral("pad:lshoulder"), QStringLiteral("pad:rshoulder"),
            QStringLiteral("pad:start"), QStringLiteral("pad:back"),
            QStringLiteral("pad:lt"), QStringLiteral("pad:rt"),
            QStringLiteral("pad:lthumb"), QStringLiteral("pad:rthumb"),
            QStringLiteral("pad:dpad_up"), QStringLiteral("pad:dpad_down"),
            QStringLiteral("pad:dpad_left"), QStringLiteral("pad:dpad_right"),
            QStringLiteral("pad:lx-"), QStringLiteral("pad:lx+"), QStringLiteral("pad:ly+"), QStringLiteral("pad:ly-"),
            QStringLiteral("pad:rx-"), QStringLiteral("pad:rx+"), QStringLiteral("pad:ry+"), QStringLiteral("pad:ry-")};
}

QString sourceLabel(const QString &s)
{
    static const QList<QPair<QString, QString>> k = {
        {QStringLiteral("mouse:left"), QStringLiteral("Left mouse button")},
        {QStringLiteral("mouse:right"), QStringLiteral("Right mouse button")},
        {QStringLiteral("mouse:middle"), QStringLiteral("Middle mouse button")},
        {QStringLiteral("mouse:x1"), QStringLiteral("Mouse back button")},
        {QStringLiteral("mouse:x2"), QStringLiteral("Mouse forward button")},
        {QStringLiteral("key:RETURN"), QStringLiteral("Enter")},
        {QStringLiteral("key:BACK"), QStringLiteral("Backspace")},
        {QStringLiteral("key:SPACE"), QStringLiteral("Space")},
        {QStringLiteral("key:LSHIFT"), QStringLiteral("Left Shift")},
        {QStringLiteral("key:RSHIFT"), QStringLiteral("Right Shift")},
        {QStringLiteral("key:SHIFT"), QStringLiteral("Shift")},
        {QStringLiteral("key:LCTRL"), QStringLiteral("Left Ctrl")},
        {QStringLiteral("key:RCTRL"), QStringLiteral("Right Ctrl")},
        {QStringLiteral("key:CTRL"), QStringLiteral("Ctrl")},
        {QStringLiteral("key:ALT"), QStringLiteral("Alt")},
        {QStringLiteral("key:TAB"), QStringLiteral("Tab")},
        {QStringLiteral("key:ESCAPE"), QStringLiteral("Esc")},
        {QStringLiteral("key:UP"), QStringLiteral("Up")},
        {QStringLiteral("key:DOWN"), QStringLiteral("Down")},
        {QStringLiteral("key:LEFT"), QStringLiteral("Left")},
        {QStringLiteral("key:RIGHT"), QStringLiteral("Right")},
        {QStringLiteral("pad:a"), QStringLiteral("A")}, {QStringLiteral("pad:b"), QStringLiteral("B")},
        {QStringLiteral("pad:x"), QStringLiteral("X")}, {QStringLiteral("pad:y"), QStringLiteral("Y")},
        {QStringLiteral("pad:lshoulder"), QStringLiteral("LB")}, {QStringLiteral("pad:rshoulder"), QStringLiteral("RB")},
        {QStringLiteral("pad:start"), QStringLiteral("Start")}, {QStringLiteral("pad:back"), QStringLiteral("Back")},
        {QStringLiteral("pad:lt"), QStringLiteral("Left trigger")}, {QStringLiteral("pad:rt"), QStringLiteral("Right trigger")},
        {QStringLiteral("pad:lthumb"), QStringLiteral("Left stick press")}, {QStringLiteral("pad:rthumb"), QStringLiteral("Right stick press")},
        {QStringLiteral("pad:dpad_up"), QStringLiteral("D-pad up")}, {QStringLiteral("pad:dpad_down"), QStringLiteral("D-pad down")},
        {QStringLiteral("pad:dpad_left"), QStringLiteral("D-pad left")}, {QStringLiteral("pad:dpad_right"), QStringLiteral("D-pad right")},
        {QStringLiteral("pad:lx-"), QStringLiteral("Left stick left")}, {QStringLiteral("pad:lx+"), QStringLiteral("Left stick right")},
        {QStringLiteral("pad:ly+"), QStringLiteral("Left stick up")}, {QStringLiteral("pad:ly-"), QStringLiteral("Left stick down")},
        {QStringLiteral("pad:rx-"), QStringLiteral("Right stick left")}, {QStringLiteral("pad:rx+"), QStringLiteral("Right stick right")},
        {QStringLiteral("pad:ry+"), QStringLiteral("Right stick up")}, {QStringLiteral("pad:ry-"), QStringLiteral("Right stick down")},
    };
    if (s.isEmpty() || s == QLatin1String("none"))
        return QStringLiteral("-");
    for (const auto &p : k)
        if (p.first.compare(s, Qt::CaseInsensitive) == 0)
            return p.second;
    if (s.startsWith(QLatin1String("key:")))
        return s.mid(4);
    if (s.startsWith(QLatin1String("pad:")))
        return s.mid(4);
    return s;
}

bool File::load(const QString &path)
{
    path_ = path;
    root_ = {};
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QJsonDocument d = QJsonDocument::fromJson(f.readAll());
    if (!d.isObject())
        return false;
    root_ = d.object();
    return true;
}

bool File::save(QString *error) const
{
    QJsonObject root = root_;
    if (!root.contains(QLatin1String("version")))
        root.insert(QStringLiteral("version"), 1);
    QDir().mkpath(QFileInfo(path_).absolutePath());
    QSaveFile f(path_);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!f.commit()) {
        if (error)
            *error = f.errorString();
        return false;
    }
    return true;
}

// The entry for port 1: the one that says "port": 1, else the first.
QJsonObject File::controller1() const
{
    const QJsonArray a = root_.value(QLatin1String("controllers")).toArray();
    for (int i = 0; i < a.size(); i++) {
        const QJsonObject c = a[i].toObject();
        const int port = c.contains(QLatin1String("port")) ? c.value(QLatin1String("port")).toInt() : i + 1;
        if (port == 1)
            return c;
    }
    return {};
}

void File::setController1(const QJsonObject &c)
{
    QJsonArray a = root_.value(QLatin1String("controllers")).toArray();
    for (int i = 0; i < a.size(); i++) {
        const QJsonObject o = a[i].toObject();
        const int port = o.contains(QLatin1String("port")) ? o.value(QLatin1String("port")).toInt() : i + 1;
        if (port == 1) {
            a[i] = c;
            root_.insert(QStringLiteral("controllers"), a);
            return;
        }
    }
    QJsonObject n = c;
    n.insert(QStringLiteral("port"), 1);
    a.prepend(n);
    root_.insert(QStringLiteral("controllers"), a);
}

QString File::device() const
{
    QString d = controller1().value(QLatin1String("device")).toString();
    if (d.startsWith(QLatin1String("xinput:")))
        d = QLatin1String("gamepad:") + d.mid(7);
    return d.isEmpty() ? QStringLiteral("gamepad:0") : d;
}

void File::setDevice(const QString &d)
{
    QJsonObject c = controller1();
    c.insert(QStringLiteral("port"), 1);
    c.insert(QStringLiteral("device"), d);
    if (!c.contains(QLatin1String("deadzone")))
        c.insert(QStringLiteral("deadzone"), 7849);
    setController1(c);
}

QString File::padApi() const
{
    return root_.value(QLatin1String("pad_api")).toString() == QLatin1String("xinput") ? QStringLiteral("xinput")
                                                                                       : QStringLiteral("sdl");
}

void File::setPadApi(const QString &api)
{
    root_.insert(QStringLiteral("pad_api"), api);
}

QStringList File::sources(const QString &control) const
{
    const QJsonValue v = controller1().value(QLatin1String("bindings")).toObject().value(control);
    QStringList out;
    if (v.isString()) {
        out << v.toString();
    } else if (v.isArray()) {
        for (const QJsonValue &e : v.toArray())
            out << e.toString();
    } else {
        // Not in the file: the built-in mapping.
        for (const Control &c : controls())
            if (control == QLatin1String(c.name)) {
                out << QLatin1String(c.pad);
                if (*c.key && device() != QLatin1String("none"))
                    out << QLatin1String(c.key);
            }
    }
    return out;
}

void File::setSources(const QString &control, const QStringList &s)
{
    QJsonObject c = controller1();
    if (!c.contains(QLatin1String("device")))
        c.insert(QStringLiteral("device"), QStringLiteral("gamepad:0"));
    c.insert(QStringLiteral("port"), 1);
    QJsonObject b = c.value(QLatin1String("bindings")).toObject();
    QJsonArray a;
    for (const QString &e : s)
        if (!e.isEmpty() && e != QLatin1String("none"))
            a.append(e);
    b.insert(control, a);
    c.insert(QStringLiteral("bindings"), b);
    setController1(c);
}

QString File::padSource(const QString &control) const
{
    for (const QString &s : sources(control))
        if (s.startsWith(QLatin1String("pad:")))
            return s;
    return {};
}

QString File::keySource(const QString &control) const
{
    for (const QString &s : sources(control))
        if (s.startsWith(QLatin1String("key:")) || s.startsWith(QLatin1String("mouse:")))
            return s;
    return {};
}

// Replace the first source of a kind, keep the rest in their order.
static QStringList replaceKind(QStringList s, bool pad, const QString &src)
{
    int at = -1;
    for (int i = 0; i < s.size(); i++) {
        const bool isPad = s[i].startsWith(QLatin1String("pad:"));
        const bool isKey = s[i].startsWith(QLatin1String("key:")) || s[i].startsWith(QLatin1String("mouse:"));
        if ((pad && isPad) || (!pad && isKey)) {
            at = i;
            break;
        }
    }
    const bool none = src.isEmpty() || src == QLatin1String("none");
    if (at >= 0) {
        if (none)
            s.removeAt(at);
        else
            s[at] = src;
    } else if (!none) {
        if (pad)
            s.prepend(src);
        else
            s.append(src);
    }
    return s;
}

void File::setPadSource(const QString &control, const QString &src)
{
    setSources(control, replaceKind(sources(control), true, src));
}

void File::setKeySource(const QString &control, const QString &src)
{
    setSources(control, replaceKind(sources(control), false, src));
}

void File::resetController1()
{
    QJsonObject c = controller1();
    c.insert(QStringLiteral("port"), 1);
    c.insert(QStringLiteral("device"), QStringLiteral("gamepad:0"));
    c.insert(QStringLiteral("deadzone"), 7849);
    QJsonObject b;
    for (const Control &k : controls()) {
        QJsonArray a;
        a.append(QLatin1String(k.pad));
        if (*k.key)
            a.append(QLatin1String(k.key));
        b.insert(QLatin1String(k.name), a);
    }
    c.insert(QStringLiteral("bindings"), b);
    setController1(c);
}

void File::applyPcPreset()
{
    // TimeSplitters 2's own control set-up "Standard" (Options > Controls),
    // from the pad onto a PC shooter's keys. The pad column goes back to
    // the defaults as well, so a pad plays the console's set-up beside it.
    //   left stick   walk / strafe          WASD
    //   R trigger    fire                   left button
    //   L trigger    second weapon function middle button, F
    //   R3           aim (zoom with a scope) right button (the mouse holds
    //                                       the aim mode itself elsewhere)
    //   A            activate               E, and Enter for the menus
    //   B            back, in the menus     Backspace
    //   Y            reload                 R
    //   L3           crouch                 Ctrl, C
    //   D-pad <>     change weapon          the wheel, Q and the arrows
    //   D-pad up/dn  the menus              the arrows
    //   Start, Back  pause, objectives      Escape (also frees the pointer), Tab
    //   White        jump (not the game's own: src/recomp_manual.c)   Space
    // The right stick is the mouse's, so no keys are on it.
    static const struct { const char *control; const char *src[3]; } k[] = {
        {"lstick_up", {"key:W"}},     {"lstick_down", {"key:S"}},
        {"lstick_left", {"key:A"}},   {"lstick_right", {"key:D"}},
        {"rtrigger", {"mouse:left"}},
        {"ltrigger", {"mouse:middle", "key:F"}},
        {"rthumb", {"mouse:right"}},
        {"a", {"key:E", "key:RETURN"}},
        {"b", {"key:BACKSPACE"}},
        {"x", {"key:X"}},
        {"y", {"key:R"}},
        {"lthumb", {"key:LCTRL", "key:C"}},
        {"dpad_left", {"mouse:wheelup", "key:Q", "key:LEFT"}},
        {"dpad_right", {"mouse:wheeldown", "key:RIGHT"}},
        {"dpad_up", {"key:UP"}},      {"dpad_down", {"key:DOWN"}},
        {"start", {"key:ESCAPE"}},    {"back", {"key:TAB"}},
        {"white", {"key:SPACE"}},     {"black", {}},
        {"rstick_up", {}}, {"rstick_down", {}}, {"rstick_left", {}}, {"rstick_right", {}},
    };
    resetController1();
    for (const auto &e : k) {
        QStringList s;
        const QString pad = padSource(QLatin1String(e.control));
        if (!pad.isEmpty())
            s << pad;
        for (const char *src : e.src)
            if (src)
                s << QLatin1String(src);
        setSources(QLatin1String(e.control), s);
    }
}

}
