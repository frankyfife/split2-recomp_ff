#include "tr.h"

#include <QHash>

namespace {
bool g_german = false;

const QHash<QString, QString> &german()
{
    static const QHash<QString, QString> k = {
        // window and tabs
        {QStringLiteral("The Xbox game, recompiled for this computer"), QStringLiteral("Das Xbox-Spiel, neu kompiliert für diesen Computer")},
        {QStringLiteral("Game"), QStringLiteral("Spiel")},
        {QStringLiteral("Graphics"), QStringLiteral("Grafik")},
        {QStringLiteral("Controls"), QStringLiteral("Steuerung")},
        {QStringLiteral("Sound"), QStringLiteral("Ton")},
        {QStringLiteral("Advanced"), QStringLiteral("Erweitert")},
        {QStringLiteral("About"), QStringLiteral("Info")},
        {QStringLiteral("Save"), QStringLiteral("Speichern")},
        {QStringLiteral("Play"), QStringLiteral("Spielen")},
        {QStringLiteral("Saved to %1"), QStringLiteral("Gespeichert in %1")},
        {QStringLiteral("Copied."), QStringLiteral("Kopiert.")},
        {QStringLiteral("On"), QStringLiteral("An")},
        {QStringLiteral("Off"), QStringLiteral("Aus")},
        {QStringLiteral("Default"), QStringLiteral("Standard")},
        {QStringLiteral("Cancel"), QStringLiteral("Abbrechen")},
        {QStringLiteral("Cancelled."), QStringLiteral("Abgebrochen.")},

        // game
        {QStringLiteral("Your copy of the game"), QStringLiteral("Deine Kopie des Spiels")},
        {QStringLiteral("Build from disc image..."), QStringLiteral("Aus Disc-Image bauen...")},
        {QStringLiteral("Build from folder..."), QStringLiteral("Aus Ordner bauen...")},
        {QStringLiteral("Open game folder"), QStringLiteral("Spielordner öffnen")},
        {QStringLiteral("Open saves"), QStringLiteral("Spielstände öffnen")},
        {QStringLiteral("Disc image"), QStringLiteral("Disc-Image")},
        {QStringLiteral("Xbox disc image (*.iso *.xiso);;All files (*)"), QStringLiteral("Xbox-Disc-Image (*.iso *.xiso);;Alle Dateien (*)")},
        {QStringLiteral("The game's folder (with default.xbe)"), QStringLiteral("Der Ordner des Spiels (mit default.xbe)")},
        {QStringLiteral("Saves"), QStringLiteral("Spielstände")},
        {QStringLiteral("No saves yet. TimeSplitters 2 keeps them in UDATA inside the game folder."),
         QStringLiteral("Noch keine Spielstände. TimeSplitters 2 legt sie in UDATA im Spielordner ab.")},
        {QStringLiteral("Mods"), QStringLiteral("Mods")},
        {QStringLiteral("Folders laid out like the disc (data/*.pak, or loose files under files/ and paks/<pak>/, "
                        "ts2/characters.ini for new characters). The first in the list wins over the ones below it. "
                        "See docs/modding.md."),
         QStringLiteral("Ordner im Aufbau der Disc (data/*.pak, oder einzelne Dateien unter files/ und paks/<pak>/, "
                        "ts2/characters.ini für neue Figuren). Der oberste in der Liste gewinnt gegen die darunter. "
                        "Siehe docs/modding.md.")},
        {QStringLiteral("Add..."), QStringLiteral("Hinzufügen...")},
        {QStringLiteral("Remove"), QStringLiteral("Entfernen")},
        {QStringLiteral("Move up"), QStringLiteral("Nach oben")},
        {QStringLiteral("Mod folder"), QStringLiteral("Mod-Ordner")},
        {QStringLiteral("The game has not been built yet: choose your disc image (.iso, .xiso) or the folder with the disc's files."),
         QStringLiteral("Das Spiel ist noch nicht gebaut: wähle dein Disc-Image (.iso, .xiso) oder den Ordner mit den Dateien der Disc.")},
        {QStringLiteral("No game files found. Put the disc's files in a folder called game beside this launcher, or set RECOMP_GAME_DIR."),
         QStringLiteral("Keine Spieldateien gefunden. Lege die Dateien der Disc in einen Ordner namens game neben diesem Launcher, oder setze RECOMP_GAME_DIR.")},
        {QStringLiteral("default.xbe is not one of the two supported releases (SHA-1 %1)."),
         QStringLiteral("default.xbe ist keine der beiden unterstützten Fassungen (SHA-1 %1).")},
        {QStringLiteral("TimeSplitters 2, %1 release."), QStringLiteral("TimeSplitters 2, %1-Fassung.")},
        {QStringLiteral("This release is newer than the build: build it again."),
         QStringLiteral("Diese Version ist neuer als der Build: bitte neu bauen.")},
        {QStringLiteral("The game's files have moved: choose the folder again (nothing is rebuilt)."),
         QStringLiteral("Die Spieldateien wurden verschoben: wähle den Ordner neu (es wird nichts neu gebaut).")},
        {QStringLiteral("The game executable is missing beside the launcher: %1"),
         QStringLiteral("Das Spielprogramm fehlt neben dem Launcher: %1")},
        {QStringLiteral("The game is ready."), QStringLiteral("Das Spiel ist fertig.")},
        {QStringLiteral("This disc can't be used: "), QStringLiteral("Diese Disc ist nicht verwendbar: ")},
        {QStringLiteral("Linking the game to its files again. Nothing is rebuilt."),
         QStringLiteral("Das Spiel wird neu mit seinen Dateien verknüpft. Es wird nichts neu gebaut.")},
        {QStringLiteral("Building the game from your copy. This happens once and takes a few minutes."),
         QStringLiteral("Das Spiel wird aus deiner Kopie gebaut. Das passiert einmal und dauert ein paar Minuten.")},
        {QStringLiteral("The build stopped (exit %1). Its log is %2/build.log."),
         QStringLiteral("Der Build wurde beendet (Code %1). Das Protokoll ist %2/build.log.")},
        {QStringLiteral("The builder could not be started (%1)."), QStringLiteral("Der Builder konnte nicht gestartet werden (%1).")},
        {QStringLiteral("This is not a release: there is no builder at %1."),
         QStringLiteral("Das ist kein Release: es gibt keinen Builder unter %1.")},

        // graphics
        {QStringLiteral("Renderer"), QStringLiteral("Renderer")},
        {QStringLiteral("Direct3D 11 (default)"), QStringLiteral("Direct3D 11 (Standard)")},
        {QStringLiteral("Vulkan (needs a build with DXC)"), QStringLiteral("Vulkan (braucht einen Build mit DXC)")},
        {QStringLiteral("Direct3D 11 is the tested renderer on Windows. Vulkan is the one macOS and Linux use."),
         QStringLiteral("Direct3D 11 ist unter Windows der getestete Renderer. Vulkan nutzen macOS und Linux.")},
        {QStringLiteral("Resolution"), QStringLiteral("Auflösung")},
        {QStringLiteral("Console (640x480)"), QStringLiteral("Konsole (640x480)")},
        {QStringLiteral("Render larger than the console, then filter down. 2x is much sharper and costs little; 3x-4x for 1440p and 4K screens."),
         QStringLiteral("Größer als die Konsole rendern und herunterrechnen. 2x ist viel schärfer und kostet wenig; 3x-4x für 1440p- und 4K-Bildschirme.")},
        {QStringLiteral("Widescreen"), QStringLiteral("Breitbild")},
        {QStringLiteral("16:9"), QStringLiteral("16:9")},
        {QStringLiteral("The camera is widened (you see more to the sides) and the HUD and menus are laid out for 16:9."),
         QStringLiteral("Die Kamera wird verbreitert (man sieht mehr zur Seite), HUD und Menüs sind für 16:9 angeordnet.")},
        {QStringLiteral("Menus"), QStringLiteral("Menüs")},
        {QStringLiteral("16:9 like the levels"), QStringLiteral("16:9 wie die Level")},
        {QStringLiteral("4:3 between bars, as the console"), QStringLiteral("4:3 mit Balken, wie die Konsole")},
        {QStringLiteral("The front end in widescreen, or kept at 4:3."), QStringLiteral("Die Menüs in Breitbild, oder bei 4:3 belassen.")},
        {QStringLiteral("Texture sharpness"), QStringLiteral("Texturschärfe")},
        {QStringLiteral("The game's blur filter"), QStringLiteral("Unschärfe-Filter des Spiels")},
        {QStringLiteral("Sun glare"), QStringLiteral("Sonnenblendung")},
        {QStringLiteral("The haze and flare laid over the whole picture while the sun is in view, as in Siberia."),
         QStringLiteral("Der Dunst und Lichtreflex über dem ganzen Bild, solange die Sonne zu sehen ist, wie in Sibirien.")},
        {QStringLiteral("Falling snow and rain"), QStringLiteral("Schneefall und Regen")},
        {QStringLiteral("The snowflakes or raindrops falling around you. Off is a clearer picture, with less atmosphere."),
         QStringLiteral("Die Schneeflocken oder Regentropfen um dich herum. Aus: klareres Bild, weniger Atmosphäre.")},
        {QStringLiteral("TimeSplitters 2 softens its picture with copies of itself laid over it. Off gives a sharp picture; on, the blur is drawn at the output resolution."),
         QStringLiteral("TimeSplitters 2 weicht sein Bild mit darübergelegten Kopien seiner selbst auf. Aus: scharfes Bild; an: die Unschärfe in Ausgabeauflösung.")},
        {QStringLiteral("Field of view"), QStringLiteral("Sichtfeld")},
        {QStringLiteral("A percentage of the game's own: more shows more of the world. A scope still zooms by the game's ratio."),
         QStringLiteral("In Prozent des Spiels: mehr zeigt mehr von der Welt. Ein Zielfernrohr zoomt weiter im Verhältnis des Spiels.")},
        {QStringLiteral("Anisotropic filtering: floors and walls seen at a glancing angle stay sharp."),
         QStringLiteral("Anisotrope Filterung: Böden und Wände im flachen Winkel bleiben scharf.")},
        {QStringLiteral("Frame rate"), QStringLiteral("Bildrate")},
        {QStringLiteral("Adaptive (as the console)"), QStringLiteral("Adaptiv (wie die Konsole)")},
        {QStringLiteral("60 frames a second"), QStringLiteral("60 Bilder pro Sekunde")},
        {QStringLiteral("30 frames a second"), QStringLiteral("30 Bilder pro Sekunde")},
        {QStringLiteral("Uncapped (the game runs too fast)"), QStringLiteral("Unbegrenzt (das Spiel läuft zu schnell)")},
        {QStringLiteral("TimeSplitters 2 runs its logic once a frame, so it must stay at 60. For smoother motion use frame interpolation. F10 changes it while playing."),
         QStringLiteral("TimeSplitters 2 rechnet einmal pro Bild, deshalb muss es bei 60 bleiben. Für flüssigere Bewegung die Zwischenbilder nutzen. F10 ändert es im Spiel.")},
        {QStringLiteral("Frame interpolation"), QStringLiteral("Zwischenbilder")},
        {QStringLiteral("2x (120 frames a second)"), QStringLiteral("2x (120 Bilder pro Sekunde)")},
        {QStringLiteral("3x (180 frames a second)"), QStringLiteral("3x (180 Bilder pro Sekunde)")},
        {QStringLiteral("4x (240 frames a second)"), QStringLiteral("4x (240 Bilder pro Sekunde)")},
        {QStringLiteral("Draws frames between the game's own: smoother motion while the game still runs at 60. Best with variable refresh."),
         QStringLiteral("Zeichnet Bilder zwischen denen des Spiels: flüssigere Bewegung, während das Spiel weiter mit 60 läuft. Am besten mit variabler Bildwiederholrate.")},
        {QStringLiteral("Fullscreen"), QStringLiteral("Vollbild")},
        {QStringLiteral("Borderless fullscreen"), QStringLiteral("Randloses Vollbild")},
        {QStringLiteral("Alt+Enter switches while playing."), QStringLiteral("Alt+Enter schaltet im Spiel um.")},
        {QStringLiteral("Variable refresh rate"), QStringLiteral("Variable Bildwiederholrate")},
        {QStringLiteral("In fullscreen on a G-SYNC or FreeSync display: an even 60 on a high refresh screen."),
         QStringLiteral("Im Vollbild auf einem G-SYNC- oder FreeSync-Bildschirm: gleichmäßige 60 auf einem Bildschirm mit hoher Bildrate.")},
        {QStringLiteral("Frame rate counter"), QStringLiteral("Bildratenanzeige")},
        {QStringLiteral("Show the counter"), QStringLiteral("Anzeige einblenden")},
        {QStringLiteral("F9 toggles it while playing."), QStringLiteral("F9 schaltet sie im Spiel um.")},
        {QStringLiteral("Game language"), QStringLiteral("Sprache des Spiels")},
        {QStringLiteral("Text"), QStringLiteral("Texte")},
        {QStringLiteral("The language the console was set to. The PAL release has English, German, French, Spanish and Italian text, "
                        "with English speech; the USA release is English only."),
         QStringLiteral("Die Sprache, auf die die Konsole eingestellt war. Die PAL-Fassung hat englische, deutsche, französische, "
                        "spanische und italienische Texte, gesprochen wird Englisch; die USA-Fassung ist nur englisch.")},
        {QStringLiteral("PAL release"), QStringLiteral("PAL-Fassung")},
        {QStringLiteral("60 Hz"), QStringLiteral("60 Hz")},
        {QStringLiteral("The PAL console offered 60 Hz; off plays the PAL release at 50 Hz, as a PAL TV without 60 Hz did."),
         QStringLiteral("Die PAL-Konsole bot 60 Hz an; aus spielt die PAL-Fassung mit 50 Hz, wie an einem PAL-Fernseher ohne 60 Hz.")},

        // controls
        {QStringLiteral("Mouse"), QStringLiteral("Maus")},
        {QStringLiteral("Aim with the mouse"), QStringLiteral("Mit der Maus zielen")},
        {QStringLiteral("The game window takes the pointer while it is in front (click in it to give it back after switching away)."),
         QStringLiteral("Das Spielfenster übernimmt den Mauszeiger, solange es im Vordergrund ist (nach dem Wechseln hineinklicken, um ihn zurückzugeben).")},
        {QStringLiteral("Aiming"), QStringLiteral("Zielen")},
        {QStringLiteral("Direct: the mouse turns the view, the crosshair stays centred"),
         QStringLiteral("Direkt: die Maus dreht die Sicht, das Fadenkreuz bleibt in der Mitte")},
        {QStringLiteral("As the right stick (the console's handling)"), QStringLiteral("Als rechter Stick (wie auf der Konsole)")},
        {QStringLiteral("Direct aim adds the mouse's movement to the view itself, without the stick's dead zone, acceleration or the "
                        "slow aim mode: the aim button is then only for the scope. As the right stick keeps the console's feel."),
         QStringLiteral("Direktes Zielen gibt die Mausbewegung direkt auf die Sicht, ohne Totzone, Beschleunigung und den langsamen "
                        "Zielmodus des Sticks: die Zieltaste braucht man dann nur noch fürs Zielfernrohr. Als rechter Stick behält das Gefühl der Konsole.")},
        {QStringLiteral("Direct aim needs where your release keeps the view angles. Until that is known for it "
                        "(docs/mouse-aim.md), the game falls back to the mouse as the right stick by itself."),
         QStringLiteral("Direktes Zielen braucht die Adressen der Blickwinkel deiner Fassung. Bis sie bekannt sind "
                        "(docs/mouse-aim.md), nutzt das Spiel die Maus von selbst als rechten Stick.")},
        {QStringLiteral("View angles from the settings file (ts2_aim)."), QStringLiteral("Blickwinkel aus der Einstellungsdatei (ts2_aim).")},
        {QStringLiteral("Sensitivity"), QStringLiteral("Empfindlichkeit")},
        {QStringLiteral("80 is the default: about 18 cm for a full turn on an 800 dpi mouse with direct aim."),
         QStringLiteral("80 ist der Standard: etwa 18 cm für eine volle Drehung mit einer 800-dpi-Maus beim direkten Zielen.")},
        {QStringLiteral("Invert"), QStringLiteral("Invertieren")},
        {QStringLiteral("Mouse up looks down"), QStringLiteral("Maus hoch schaut nach unten")},
        {QStringLiteral("Aim without the aim button"), QStringLiteral("Ohne Zieltaste zielen")},
        {QStringLiteral("With direct aim the game's aim mode is held on, so the crosshair shows and sits in the middle. Scoped weapons "
                        "keep the button, for the zoom. The pad's right stick gives the console's handling back."),
         QStringLiteral("Bei direktem Zielen bleibt der Zielmodus des Spiels an: Das Fadenkreuz ist zu sehen und sitzt in der Mitte. "
                        "Waffen mit Zielfernrohr behalten die Taste, für den Zoom. Der rechte Stick am Gamepad gibt die "
                        "Konsolensteuerung zurück.")},
        {QStringLiteral("Look up and down"), QStringLiteral("Nach oben und unten")},
        {QStringLiteral("How far the view tilts with the mouse. The console stops at 40 degrees."),
         QStringLiteral("Wie weit die Maus die Sicht neigt. Die Konsole hört bei 40 Grad auf.")},
        {QStringLiteral("Slower when zoomed"), QStringLiteral("Langsamer im Zoom")},
        {QStringLiteral("100: the mouse slows with the zoom, so a movement covers the same part of the picture. 0: the same speed zoomed."),
         QStringLiteral("100: Die Maus wird mit dem Zoom langsamer, eine Bewegung deckt denselben Bildteil ab. 0: Im Zoom gleich schnell.")},
        {QStringLiteral("In the game, Esc gives the pointer back; a click in the window takes it again."),
         QStringLiteral("Im Spiel gibt Esc die Maus frei, ein Klick ins Fenster fängt sie wieder.")},
        {QStringLiteral("Controller 1"), QStringLiteral("Controller 1")},
        {QStringLiteral("Reads from"), QStringLiteral("Liest von")},
        {QStringLiteral("Gamepad 1"), QStringLiteral("Gamepad 1")},
        {QStringLiteral("Gamepad 2"), QStringLiteral("Gamepad 2")},
        {QStringLiteral("Gamepad 3"), QStringLiteral("Gamepad 3")},
        {QStringLiteral("Gamepad 4"), QStringLiteral("Gamepad 4")},
        {QStringLiteral("Keyboard only"), QStringLiteral("Nur Tastatur")},
        {QStringLiteral("Nothing"), QStringLiteral("Nichts")},
        {QStringLiteral("A gamepad still reads the keyboard and mouse bindings below."),
         QStringLiteral("Ein Gamepad liest zusätzlich die Tastatur- und Mausbelegung unten.")},
        {QStringLiteral("Controllers"), QStringLiteral("Controller")},
        {QStringLiteral("All controllers (SDL3)"), QStringLiteral("Alle Controller (SDL3)")},
        {QStringLiteral("Xbox controllers only (XInput)"), QStringLiteral("Nur Xbox-Controller (XInput)")},
        {QStringLiteral("Xbox control"), QStringLiteral("Xbox-Taste")},
        {QStringLiteral("Controller"), QStringLiteral("Controller")},
        {QStringLiteral("Keyboard / mouse"), QStringLiteral("Tastatur / Maus")},
        {QStringLiteral("Bindings are by Xbox control: what each does is the game's control setup (Options > Controls in the game). "
                        "They are shared by every game built with xboxrecomp."),
         QStringLiteral("Die Belegung gilt pro Xbox-Taste: was sie tut, legt das Steuerungsschema im Spiel fest (Optionen > Steuerung). "
                        "Sie gilt für jedes mit xboxrecomp gebaute Spiel.")},
        {QStringLiteral("PC layout (WASD + mouse)"), QStringLiteral("PC-Belegung (WASD + Maus)")},
        {QStringLiteral("Back to the defaults"), QStringLiteral("Auf Standard zurücksetzen")},
        {QStringLiteral("Not bound"), QStringLiteral("Nicht belegt")},
        {QStringLiteral("Press a key or click... (Esc cancels, Del clears)"),
         QStringLiteral("Taste drücken oder klicken... (Esc bricht ab, Entf löscht)")},
        {QStringLiteral("F9-F11 belong to the game window"), QStringLiteral("F9-F11 gehören dem Spielfenster")},
        {QStringLiteral("Black"), QStringLiteral("Schwarz")},
        {QStringLiteral("White"), QStringLiteral("Weiß")},
        {QStringLiteral("Left trigger"), QStringLiteral("Linker Trigger")},
        {QStringLiteral("Right trigger"), QStringLiteral("Rechter Trigger")},
        {QStringLiteral("D-pad up"), QStringLiteral("Steuerkreuz hoch")},
        {QStringLiteral("D-pad down"), QStringLiteral("Steuerkreuz runter")},
        {QStringLiteral("D-pad left"), QStringLiteral("Steuerkreuz links")},
        {QStringLiteral("D-pad right"), QStringLiteral("Steuerkreuz rechts")},
        {QStringLiteral("Left stick up"), QStringLiteral("Linker Stick hoch")},
        {QStringLiteral("Left stick down"), QStringLiteral("Linker Stick runter")},
        {QStringLiteral("Left stick left"), QStringLiteral("Linker Stick links")},
        {QStringLiteral("Left stick right"), QStringLiteral("Linker Stick rechts")},
        {QStringLiteral("Left stick press"), QStringLiteral("Linker Stick drücken")},
        {QStringLiteral("Right stick up"), QStringLiteral("Rechter Stick hoch")},
        {QStringLiteral("Right stick down"), QStringLiteral("Rechter Stick runter")},
        {QStringLiteral("Right stick left"), QStringLiteral("Rechter Stick links")},
        {QStringLiteral("Right stick right"), QStringLiteral("Rechter Stick rechts")},
        {QStringLiteral("Right stick press"), QStringLiteral("Rechter Stick drücken")},
        {QStringLiteral("Left mouse button"), QStringLiteral("Linke Maustaste")},
        {QStringLiteral("Right mouse button"), QStringLiteral("Rechte Maustaste")},
        {QStringLiteral("Middle mouse button"), QStringLiteral("Mittlere Maustaste")},
        {QStringLiteral("Mouse back button"), QStringLiteral("Maustaste zurück")},
        {QStringLiteral("Mouse forward button"), QStringLiteral("Maustaste vor")},
        {QStringLiteral("Space"), QStringLiteral("Leertaste")},
        {QStringLiteral("Left Shift"), QStringLiteral("Umschalt links")},
        {QStringLiteral("Right Shift"), QStringLiteral("Umschalt rechts")},
        {QStringLiteral("Shift"), QStringLiteral("Umschalt")},
        {QStringLiteral("Left Ctrl"), QStringLiteral("Strg links")},
        {QStringLiteral("Right Ctrl"), QStringLiteral("Strg rechts")},
        {QStringLiteral("Ctrl"), QStringLiteral("Strg")},
        {QStringLiteral("Backspace"), QStringLiteral("Rücktaste")},
        {QStringLiteral("Up"), QStringLiteral("Hoch")},
        {QStringLiteral("Down"), QStringLiteral("Runter")},
        {QStringLiteral("Left"), QStringLiteral("Links")},
        {QStringLiteral("Right"), QStringLiteral("Rechts")},

        // sound
        {QStringLiteral("Native (default)"), QStringLiteral("Nativ (Standard)")},
        {QStringLiteral("Emulated Xbox sound chip (APU)"), QStringLiteral("Emulierter Xbox-Soundchip (APU)")},
        {QStringLiteral("Native plays the game's DirectSound on this computer's audio, with the Xbox's 3D sound and room reverb. "
                        "The emulated chip runs the game's own mixer; try it if a sound is missing."),
         QStringLiteral("Nativ spielt den DirectSound des Spiels über die Audioausgabe dieses Computers, mit 3D-Klang und Raumhall der Xbox. "
                        "Der emulierte Chip nutzt den Mischer des Spiels selbst; ausprobieren, wenn ein Geräusch fehlt.")},
        {QStringLiteral("Volume"), QStringLiteral("Lautstärke")},
        {QStringLiteral("Mute"), QStringLiteral("Stumm")},
        {QStringLiteral("No sound"), QStringLiteral("Kein Ton")},

        // system link
        {QStringLiteral("Host a session"), QStringLiteral("Sitzung eröffnen")},
        {QStringLiteral("Join a session"), QStringLiteral("Sitzung beitreten")},
        {QStringLiteral("Play with other copies over a network. Host: this computer is the session friends join. Join: play in a friend's session."),
         QStringLiteral("Mit anderen Kopien über ein Netzwerk spielen. Eröffnen: dieser Computer ist die Sitzung, der Freunde beitreten. Beitreten: in der Sitzung eines Freundes spielen.")},
        {QStringLiteral("Host's address"), QStringLiteral("Adresse des Gastgebers")},
        {QStringLiteral("the host's address, like 81.2.3.4 or a name"), QStringLiteral("die Adresse des Gastgebers, z. B. 81.2.3.4 oder ein Name")},
        {QStringLiteral("What the host gave you, with :port if theirs is not the port below."),
         QStringLiteral("Was der Gastgeber dir gegeben hat, mit :Port, falls es nicht der Port unten ist.")},
        {QStringLiteral("Port"), QStringLiteral("Port")},
        {QStringLiteral("The UDP port. Change it only if something else already uses 27610."),
         QStringLiteral("Der UDP-Port. Nur ändern, wenn schon etwas anderes 27610 benutzt.")},
        {QStringLiteral("Join code"), QStringLiteral("Beitrittscode")},
        {QStringLiteral("none"), QStringLiteral("keiner")},
        {QStringLiteral("Optional. Only copies with the same code get in."), QStringLiteral("Optional. Nur Kopien mit demselben Code kommen hinein.")},
        {QStringLiteral("Router"), QStringLiteral("Router")},
        {QStringLiteral("Ask the router to forward the port (UPnP)"), QStringLiteral("Router um Portweiterleitung bitten (UPnP)")},
        {QStringLiteral("Copy my address"), QStringLiteral("Meine Adresse kopieren")},
        {QStringLiteral("System Link is built on Windows only so far."), QStringLiteral("System Link gibt es bisher nur unter Windows.")},
        {QStringLiteral("Tell your friends: on your network %1:%2. Over the internet, your public address with the port "
                        "forwarded (the game asks the router when it starts, if allowed above). Then host a game in the "
                        "game's own System Link menu."),
         QStringLiteral("Sag deinen Freunden: im eigenen Netzwerk %1:%2. Über das Internet deine öffentliche Adresse mit "
                        "weitergeleitetem Port (das Spiel fragt beim Start den Router, wenn oben erlaubt). Dann im System-Link-Menü "
                        "des Spiels ein Spiel eröffnen.")},
        {QStringLiteral("Ask the host for their address, and the join code if they set one. Then open the game's System Link menu."),
         QStringLiteral("Frag den Gastgeber nach seiner Adresse und ggf. dem Beitrittscode. Dann das System-Link-Menü des Spiels öffnen.")},
        {QStringLiteral("One player hosts the session, up to 15 more join it, from the same network or over the internet. Each needs their own copy of the game."),
         QStringLiteral("Ein Spieler eröffnet die Sitzung, bis zu 15 weitere treten bei, im selben Netzwerk oder über das Internet. Jeder braucht seine eigene Kopie des Spiels.")},

        // advanced
        {QStringLiteral("Language"), QStringLiteral("Sprache")},
        {QStringLiteral("The launcher's language. It changes when the launcher is opened again."),
         QStringLiteral("Die Sprache des Launchers. Sie wechselt beim nächsten Öffnen.")},
        {QStringLiteral("After Play"), QStringLiteral("Nach dem Start")},
        {QStringLiteral("Close the launcher when the game starts"), QStringLiteral("Launcher schließen, wenn das Spiel startet")},
        {QStringLiteral("Extra switches"), QStringLiteral("Zusätzliche Schalter")},
        {QStringLiteral("For testing: NAME=value pairs separated by ';', given to the game as environment variables. "
                        "xboxrecomp's CLAUDE.md and docs list them."),
         QStringLiteral("Zum Testen: NAME=Wert-Paare, getrennt mit ';', die das Spiel als Umgebungsvariablen bekommt. "
                        "Die Liste steht in CLAUDE.md und den Dokumenten von xboxrecomp.")},
        {QStringLiteral("Files"), QStringLiteral("Dateien")},
        {QStringLiteral("Open the settings file"), QStringLiteral("Einstellungsdatei öffnen")},
        {QStringLiteral("Open the game's log"), QStringLiteral("Protokoll des Spiels öffnen")},

        // about
        {QStringLiteral("TimeSplitters 2 for the original Xbox, recompiled into a native program on the xboxrecomp toolkit. "
                        "It is built from your own copy of the game; no game code or data comes with it."),
         QStringLiteral("TimeSplitters 2 für die erste Xbox, mit dem xboxrecomp-Toolkit zu einem nativen Programm neu kompiliert. "
                        "Es wird aus deiner eigenen Kopie des Spiels gebaut; Spielcode oder -daten liegen nicht bei.")},
        {QStringLiteral("Title id"), QStringLiteral("Titel-ID")},
        {QStringLiteral("Settings"), QStringLiteral("Einstellungen")},
        {QStringLiteral("While playing: F9 frame rate, F10 frame cap, F11 screenshot and capture for bug reports, Alt+Enter fullscreen."),
         QStringLiteral("Im Spiel: F9 Bildrate, F10 Bildratenbegrenzung, F11 Screenshot und Aufzeichnung für Fehlerberichte, Alt+Enter Vollbild.")},

        // messages
        {QStringLiteral("The settings could not be written:\n%1\n%2"), QStringLiteral("Die Einstellungen konnten nicht geschrieben werden:\n%1\n%2")},
        {QStringLiteral("The controls could not be written:\n%1\n%2"), QStringLiteral("Die Steuerung konnte nicht geschrieben werden:\n%1\n%2")},
        {QStringLiteral("Cannot start the game"), QStringLiteral("Das Spiel kann nicht starten")},
        {QStringLiteral("The game could not be started:\n%1\n%2"), QStringLiteral("Das Spiel konnte nicht gestartet werden:\n%1\n%2")},
        {QStringLiteral("The game is running."), QStringLiteral("Das Spiel läuft.")},
    };
    return k;
}
}

void setUiGerman(bool g)
{
    g_german = g;
}

bool uiGerman()
{
    return g_german;
}

QString T(const char *english)
{
    const QString e = QString::fromUtf8(english);
    if (!g_german)
        return e;
    const auto it = german().constFind(e);
    return it == german().constEnd() ? e : it.value();
}
