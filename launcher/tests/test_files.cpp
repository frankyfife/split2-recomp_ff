// The launcher's file handling, with no window and no game: the settings
// file keeps what it does not set, and the controls file keeps the other
// ports and extra sources.
#include "bindings.h"
#include "conffile.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <cstdio>

static int g_fail;
static void check(bool ok, const char *what)
{
    if (!ok) {
        std::printf("  FAIL: %s\n", what);
        g_fail++;
    }
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    std::printf("split2_launcher_files\n");

    // ---- settings
    ConfFile c;
    c.setText(QStringLiteral("# Render at 1-8.\nresolution_scale = 2\n\n# hor_plus = auto\nmy_own = keep me\n"
                             "; a player's note\nwidescreen = 0\nwidescreen = 1\n# game_dir =\n"));
    check(c.getInt(QStringLiteral("resolution_scale"), 1, 1, 8) == 2, "reads a value");
    check(c.getBool(QStringLiteral("widescreen"), false), "the last of a key wins");
    check(!c.has(QStringLiteral("hor_plus")), "a commented-out key is absent");
    c.set(QStringLiteral("resolution_scale"), 3);
    c.set(QStringLiteral("hor_plus"), QStringLiteral("0.75"));
    c.set(QStringLiteral("mouse_invert"), 1, QStringLiteral("help line"));
    c.unset(QStringLiteral("widescreen"));
    const QString t = c.text();
    check(t.contains(QLatin1String("resolution_scale = 3")) && !t.contains(QLatin1String("resolution_scale = 2")), "replaces in place");
    check(t.contains(QLatin1String("my_own = keep me")) && t.contains(QLatin1String("; a player's note")), "keeps what it does not set");
    check(t.contains(QLatin1String("# Render at 1-8.\nresolution_scale = 3")), "keeps the explanation above");
    check(t.contains(QLatin1String("hor_plus = 0.75")) && !t.contains(QLatin1String("# hor_plus = auto")),
          "fills a commented-out line in its place");
    check(t.contains(QLatin1String("# help line\nmouse_invert = 1")), "appends a new key with its help");
    check(!c.has(QStringLiteral("widescreen")), "unset comments every line of the key out");

    // ---- controls
    bind::File b;
    b.setJson(QJsonDocument::fromJson(R"({"version":1,"pad_api":"xinput","extra":true,
        "controllers":[{"port":2,"device":"gamepad:1","bindings":{"a":["pad:b"]}},
                       {"port":1,"device":"xinput:0","bindings":{"a":["pad:a","key:Z","key:SPACE"]}}]})").object());
    check(b.device() == QLatin1String("gamepad:0"), "xinput:N reads as gamepad:N");
    check(b.padApi() == QLatin1String("xinput"), "pad api");
    check(b.keySource(QStringLiteral("a")) == QLatin1String("key:Z"), "first key source");
    check(b.padSource(QStringLiteral("rtrigger")) == QLatin1String("pad:rt"), "a control not in the file is the default");
    b.setKeySource(QStringLiteral("a"), QStringLiteral("mouse:left"));
    b.applyPcPreset();
    const QJsonObject o = b.json();
    check(o.value(QLatin1String("extra")).toBool(), "keeps unknown keys");
    const QJsonArray ctl = o.value(QLatin1String("controllers")).toArray();
    QJsonObject p1, p2;
    for (const auto &e : ctl)
        (e.toObject().value(QLatin1String("port")).toInt() == 1 ? p1 : p2) = e.toObject();
    check(p2.value(QLatin1String("bindings")).toObject().value(QLatin1String("a")).toArray().first().toString() ==
              QLatin1String("pad:b"), "keeps the other ports");
    const QJsonArray a = p1.value(QLatin1String("bindings")).toObject().value(QLatin1String("a")).toArray();
    check(a.size() == 3 && a[0].toString() == QLatin1String("pad:a") && a[1].toString() == QLatin1String("key:SPACE") &&
              a[2].toString() == QLatin1String("key:SPACE"),
          "replaces the first key source and keeps the rest");
    const QJsonArray fire = p1.value(QLatin1String("bindings")).toObject().value(QLatin1String("rtrigger")).toArray();
    check(fire.size() == 2 && fire[0].toString() == QLatin1String("pad:rt") && fire[1].toString() == QLatin1String("mouse:left"),
          "the PC layout fires with the left button and keeps the pad's trigger");
    const QJsonArray rs = p1.value(QLatin1String("bindings")).toObject().value(QLatin1String("rstick_left")).toArray();
    check(rs.size() == 1 && rs[0].toString() == QLatin1String("pad:rx-"), "the PC layout leaves the right stick to the pad");

    std::printf(g_fail ? "split2_launcher_files: %d failure(s)\n" : "split2_launcher_files: ok\n", g_fail);
    return g_fail ? 1 : 0;
}
