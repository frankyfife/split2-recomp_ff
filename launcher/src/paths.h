// Where things are: the per-user folder, the settings and bindings files,
// the game, and the self-building release's folders. The same rules as the
// toolkit's (src/config/recomp_config.c, src/launcher/launcher.c), so this
// launcher and the toolkit's agree on every file.
#pragma once

#include <QString>

namespace paths {

// TimeSplitters 2's title id, the same in every release.
constexpr unsigned kTitleId = 0x4553000Au;
QString titleIdHex();                   // "4553000A"

// %APPDATA%\xboxrecomp, ~/Library/Application Support/xboxrecomp or
// $XDG_CONFIG_HOME|~/.config/xboxrecomp; RECOMP_USER_DIR replaces it.
QString userDir();
// RECOMP_DISPLAY_CONFIG, else <userDir>/titles/4553000A.conf.
QString settingsFile();
// RECOMP_INPUT_CONFIG, else <userDir>/input_bindings.json.
QString bindingsFile();

// The game executable's name on this OS (split2_recomp.exe / split2_recomp).
QString gameExeName();

// A self-building release: release.json beside the launcher (macOS: in the
// bundle's Resources). Empty when this is a build from source.
QString releaseJson();
// The player's build folder: XBOXRECOMP_BUILD_DIR/<ID>, else
// %LOCALAPPDATA%\xboxrecomp\builds\<ID>, ~/Library/Application Support/
// xboxrecomp/builds/<ID>, $XDG_DATA_HOME|~/.local/share/xboxrecomp/builds/<ID>.
QString releaseUserDir();
// <release>/bin/xboxrecomp-build(.exe): the release's builder.
QString builderExe();

// The game folder the executable will use: release mode <build>/game; else
// RECOMP_GAME_DIR, then game/ beside the launcher, ../../game, ../game.
// Empty when none holds default.xbe.
QString gameDir();
// The executable to start and the folder to start it in.
QString gameExe();
QString gameWorkDir();

// Which release default.xbe is, by its SHA-1 (the only two the project
// supports): "PAL", "USA", or empty when it is neither or missing.
QString xbeRelease(const QString &gameFolder, QString *sha1 = nullptr);

// Where the game keeps its saves: <game>/UDATA/4553000a.
QString savesDir(const QString &gameFolder);

}
