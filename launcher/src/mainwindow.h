// The launcher's window: the game and its build, the settings on tabs, and
// Play. Everything is read when the window opens and written on Play or
// Save; closing the window writes nothing.
#pragma once

#include "bindings.h"
#include "builder.h"
#include "conffile.h"

#include <QMainWindow>
#include <QVector>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;
class QSlider;
class QSpinBox;
class QTableWidget;
class QTabWidget;
class KeyCapture;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    // A release that has not built the game yet: ask for the disc first.
    void setupIfNeeded();
    // Each tab as <dir>/<n>.png, for the docs and for checking the layout
    // with no desktop (QT_QPA_PLATFORM=offscreen): `split2_launcher --shots <dir>`.
    void saveTabShots(const QString &dir);

private:
    QWidget *buildGameTab();
    QWidget *buildVideoTab();
    QWidget *buildControlsTab();
    QWidget *buildAudioTab();
    QWidget *buildLinkTab();
    QWidget *buildAdvancedTab();
    QWidget *buildAboutTab();

    void load();
    bool save();
    void play();
    void refreshGame();
    void refreshBindings();
    void refreshLink();
    void refreshEnabled();
    void startBuild(const QString &source, bool relink);

    ConfFile conf_;
    bind::File binds_;
    Builder builder_;
    QString gameDir_;

    QTabWidget *tabs_ = nullptr;
    QPushButton *play_ = nullptr;
    QLabel *footer_ = nullptr;

    // game
    QLabel *gameStatus_ = nullptr, *gamePath_ = nullptr;
    QPushButton *buildImage_ = nullptr, *buildFolder_ = nullptr, *cancelBuild_ = nullptr;
    QProgressBar *buildBar_ = nullptr;
    QLabel *buildLabel_ = nullptr;
    QListWidget *mods_ = nullptr;

    // video
    QComboBox *renderer_ = nullptr, *resolution_ = nullptr, *menus_ = nullptr, *aniso_ = nullptr;
    QComboBox *frameCap_ = nullptr, *interp_ = nullptr;
    QSpinBox *fov_ = nullptr;
    QCheckBox *widescreen_ = nullptr, *fullscreen_ = nullptr, *vrr_ = nullptr, *fps_ = nullptr, *pal60_ = nullptr;

    // controls
    QCheckBox *screenFilters_ = nullptr, *sunGlare_ = nullptr, *weather_ = nullptr;
    QCheckBox *mouseAim_ = nullptr, *invert_ = nullptr, *autoAim_ = nullptr;
    QSpinBox *pitchLimit_ = nullptr, *zoomMatch_ = nullptr;
    QComboBox *aimMode_ = nullptr, *device_ = nullptr, *padApi_ = nullptr;
    QSlider *sens_ = nullptr;
    QSpinBox *sensBox_ = nullptr;
    QLabel *aimNote_ = nullptr;
    QTableWidget *bindTable_ = nullptr;
    QVector<QComboBox *> padCombos_;
    QVector<KeyCapture *> keyButtons_;

    // audio
    QComboBox *audioPath_ = nullptr;
    QCheckBox *mute_ = nullptr;
    QSlider *volume_ = nullptr;

    // system link
    QComboBox *linkMode_ = nullptr;
    QLineEdit *linkHost_ = nullptr, *linkCode_ = nullptr;
    QSpinBox *linkPort_ = nullptr;
    QCheckBox *linkUpnp_ = nullptr;
    QLabel *linkInfo_ = nullptr;

    // game
    QComboBox *gameLang_ = nullptr;

    // advanced
    QComboBox *language_ = nullptr;
    QCheckBox *closeOnPlay_ = nullptr;
    QLineEdit *extraEnv_ = nullptr;
};
