#include "mainwindow.h"
#include "paths.h"
#include "tr.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHostAddress>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QNetworkInterface>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>

// ---------------------------------------------------------------- helpers

namespace {

// The toolkit's mouse sensitivity range and default (recomp_config.h).
constexpr int kSensMin = 1, kSensMax = 100, kSensDefault = 80;

QComboBox *combo(const QList<QPair<QString, QString>> &items)
{
    auto *c = new QComboBox;
    for (const auto &i : items)
        c->addItem(i.first, i.second);
    return c;
}

void select(QComboBox *c, const QString &v)
{
    int i = c->findData(v);
    if (i < 0)
        i = c->findData(v.toLower());
    c->setCurrentIndex(i >= 0 ? i : 0);
}

QString valueOf(const QComboBox *c)
{
    return c->currentData().toString();
}

// A tab: a form with a short explanation under each row.
QWidget *page(QFormLayout **form)
{
    auto *inner = new QWidget;
    auto *outer = new QVBoxLayout(inner);
    *form = new QFormLayout;
    (*form)->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    (*form)->setRowWrapPolicy(QFormLayout::DontWrapRows);
    outer->addLayout(*form);
    outer->addStretch();
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(inner);
    return scroll;
}

QLabel *help(const QString &text)
{
    auto *l = new QLabel(text);
    l->setWordWrap(true);
    l->setEnabled(false);
    return l;
}

void addRow(QFormLayout *f, const QString &label, QWidget *field, const QString &explanation = {})
{
    f->addRow(label, field);
    if (!explanation.isEmpty())
        f->addRow(QString(), help(explanation));
}

// Qt's key to the toolkit's key: name (src/input/input_bindings.c accepts
// these); empty when the key has no name there.
QString keySource(const QKeyEvent *e)
{
    const int k = e->key();
    const bool pad = e->modifiers() & Qt::KeypadModifier;
    if (pad && k >= Qt::Key_0 && k <= Qt::Key_9)
        return QStringLiteral("key:NUMPAD%1").arg(k - Qt::Key_0);
    if ((k >= Qt::Key_A && k <= Qt::Key_Z) || (k >= Qt::Key_0 && k <= Qt::Key_9))
        return QStringLiteral("key:") + QChar(k);
    if (k >= Qt::Key_F1 && k <= Qt::Key_F12)
        return QStringLiteral("key:F%1").arg(k - Qt::Key_F1 + 1);
    static const QList<QPair<int, const char *>> named = {
        {Qt::Key_Up, "UP"}, {Qt::Key_Down, "DOWN"}, {Qt::Key_Left, "LEFT"}, {Qt::Key_Right, "RIGHT"},
        {Qt::Key_Return, "RETURN"}, {Qt::Key_Enter, "RETURN"}, {Qt::Key_Backspace, "BACK"},
        {Qt::Key_Space, "SPACE"}, {Qt::Key_Tab, "TAB"}, {Qt::Key_Shift, "SHIFT"}, {Qt::Key_Control, "CTRL"},
        {Qt::Key_Alt, "ALT"}, {Qt::Key_CapsLock, "CAPSLOCK"}, {Qt::Key_Insert, "INSERT"},
        {Qt::Key_Home, "HOME"}, {Qt::Key_End, "END"}, {Qt::Key_PageUp, "PAGEUP"},
        {Qt::Key_PageDown, "PAGEDOWN"}, {Qt::Key_Semicolon, "SEMICOLON"}, {Qt::Key_Equal, "EQUALS"},
        {Qt::Key_Comma, "COMMA"}, {Qt::Key_Minus, "MINUS"}, {Qt::Key_Period, "PERIOD"},
        {Qt::Key_Slash, "SLASH"}, {Qt::Key_QuoteLeft, "GRAVE"}, {Qt::Key_BracketLeft, "LBRACKET"},
        {Qt::Key_Backslash, "BACKSLASH"}, {Qt::Key_BracketRight, "RBRACKET"},
        {Qt::Key_Apostrophe, "QUOTE"}, {Qt::Key_Asterisk, "MULTIPLY"}, {Qt::Key_Plus, "ADD"},
    };
    for (const auto &n : named)
        if (n.first == k)
            return QStringLiteral("key:") + QLatin1String(n.second);
    return {};
}

// Shift and Ctrl are told apart by the scan code where the platform has one.
QString refineSides(const QKeyEvent *e, QString src)
{
#if defined(Q_OS_WIN)
    const quint32 sc = e->nativeScanCode();
    if (src == QLatin1String("key:SHIFT"))
        return sc == 0x36 ? QStringLiteral("key:RSHIFT") : QStringLiteral("key:LSHIFT");
    if (src == QLatin1String("key:CTRL"))
        return (sc & 0x100) ? QStringLiteral("key:RCTRL") : QStringLiteral("key:LCTRL");
#else
    Q_UNUSED(e);
#endif
    return src;
}
}

// A button that answers with the next key or mouse button pressed on it:
// the keyboard-and-mouse column of the controls table. Esc cancels, Delete
// clears.
class KeyCapture : public QPushButton {
public:
    std::function<void(const QString &)> onChange;
    QString source;

    KeyCapture()
    {
        setFocusPolicy(Qt::StrongFocus);
        connect(this, &QPushButton::clicked, this, [this] { arm(); });
    }
    void display(const QString &src)
    {
        source = src;
        armed_ = false;
        setText(src.isEmpty() ? T("Not bound") : T(qPrintable(bind::sourceLabel(src))));
    }

protected:
    void keyPressEvent(QKeyEvent *e) override
    {
        if (!armed_) {
            QPushButton::keyPressEvent(e);
            return;
        }
        if (e->key() == Qt::Key_Escape) {
            display(source);
            return;
        }
        if (e->key() == Qt::Key_Delete) {
            take(QString());
            return;
        }
        const QString s = refineSides(e, keySource(e));
        if (s == QLatin1String("key:F9") || s == QLatin1String("key:F10") || s == QLatin1String("key:F11")) {
            setText(T("F9-F11 belong to the game window"));
            return;
        }
        if (!s.isEmpty())
            take(s);
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        if (!armed_) {
            QPushButton::mousePressEvent(e);
            return;
        }
        switch (e->button()) {
        case Qt::LeftButton: take(QStringLiteral("mouse:left")); break;
        case Qt::RightButton: take(QStringLiteral("mouse:right")); break;
        case Qt::MiddleButton: take(QStringLiteral("mouse:middle")); break;
        case Qt::BackButton: take(QStringLiteral("mouse:x1")); break;
        case Qt::ForwardButton: take(QStringLiteral("mouse:x2")); break;
        default: break;
        }
    }
    void focusOutEvent(QFocusEvent *e) override
    {
        if (armed_)
            display(source);
        QPushButton::focusOutEvent(e);
    }

private:
    void arm()
    {
        armed_ = true;
        setText(T("Press a key or click... (Esc cancels, Del clears)"));
        setFocus();
    }
    void take(const QString &s)
    {
        display(s);
        if (onChange)
            onChange(s);
    }
    bool armed_ = false;
};

// ---------------------------------------------------------------- window

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), conf_(paths::settingsFile())
{
    conf_.load();
    binds_.load(paths::bindingsFile());
    const QString lang = conf_.get(QStringLiteral("launcher_language"));
    setUiGerman(lang.isEmpty() ? QLocale::system().language() == QLocale::German : lang == QLatin1String("de"));

    setWindowTitle(QStringLiteral("TimeSplitters 2"));
    auto *central = new QWidget;
    auto *v = new QVBoxLayout(central);

    auto *header = new QHBoxLayout;
    auto *titles = new QVBoxLayout;
    auto *title = new QLabel(QStringLiteral("TimeSplitters 2"));
    QFont tf = title->font();
    tf.setPointSizeF(tf.pointSizeF() * 1.9);
    tf.setBold(true);
    title->setFont(tf);
    titles->addWidget(title);
    titles->addWidget(help(T("The Xbox game, recompiled for this computer")));
    header->addLayout(titles, 1);
    v->addLayout(header);

    tabs_ = new QTabWidget;
    tabs_->setDocumentMode(true);
    tabs_->addTab(buildGameTab(), T("Game"));
    tabs_->addTab(buildVideoTab(), T("Graphics"));
    tabs_->addTab(buildControlsTab(), T("Controls"));
    tabs_->addTab(buildAudioTab(), T("Sound"));
    tabs_->addTab(buildLinkTab(), T("System Link"));
    tabs_->addTab(buildAdvancedTab(), T("Advanced"));
    tabs_->addTab(buildAboutTab(), T("About"));
    v->addWidget(tabs_, 1);

    auto *bottom = new QHBoxLayout;
    footer_ = new QLabel;
    footer_->setEnabled(false);
    bottom->addWidget(footer_, 1);
    auto *saveButton = new QPushButton(T("Save"));
    play_ = new QPushButton(T("Play"));
    play_->setDefault(true);
    play_->setMinimumWidth(140);
    QFont pf = play_->font();
    pf.setBold(true);
    play_->setFont(pf);
    bottom->addWidget(saveButton);
    bottom->addWidget(play_);
    v->addLayout(bottom);
    connect(saveButton, &QPushButton::clicked, this, [this] {
        if (save())
            footer_->setText(T("Saved to %1").arg(QDir::toNativeSeparators(conf_.path())));
    });
    connect(play_, &QPushButton::clicked, this, &MainWindow::play);

    setCentralWidget(central);
    resize(860, 720);

    connect(&builder_, &Builder::progress, this, [this](double overall, const QString &label, const QString &detail) {
        buildBar_->setValue(int(overall * 1000));
        buildLabel_->setText(detail.isEmpty() ? label : label + QLatin1String(" - ") + detail);
    });
    connect(&builder_, &Builder::finished, this, [this](bool ok, const QString &msg, bool unsupported) {
        cancelBuild_->setVisible(false);
        buildBar_->setVisible(ok);
        buildLabel_->setText(ok ? T("The game is ready.") : (unsupported ? T("This disc can't be used: ") : QString()) + msg);
        refreshGame();
    });

    load();
    refreshGame();
}

// ---------------------------------------------------------------- tabs

QWidget *MainWindow::buildGameTab()
{
    auto *w = new QWidget;
    auto *l = new QVBoxLayout(w);

    auto *box = new QGroupBox(T("Your copy of the game"));
    auto *bl = new QVBoxLayout(box);
    gameStatus_ = new QLabel;
    gameStatus_->setWordWrap(true);
    gamePath_ = new QLabel;
    gamePath_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    gamePath_->setWordWrap(true);
    bl->addWidget(gameStatus_);
    bl->addWidget(gamePath_);
    auto *row = new QHBoxLayout;
    buildImage_ = new QPushButton(T("Build from disc image..."));
    buildFolder_ = new QPushButton(T("Build from folder..."));
    cancelBuild_ = new QPushButton(T("Cancel"));
    cancelBuild_->setVisible(false);
    auto *openGame = new QPushButton(T("Open game folder"));
    auto *openSaves = new QPushButton(T("Open saves"));
    row->addWidget(buildImage_);
    row->addWidget(buildFolder_);
    row->addWidget(cancelBuild_);
    row->addStretch();
    row->addWidget(openGame);
    row->addWidget(openSaves);
    bl->addLayout(row);
    buildBar_ = new QProgressBar;
    buildBar_->setRange(0, 1000);
    buildBar_->setVisible(false);
    buildLabel_ = new QLabel;
    buildLabel_->setWordWrap(true);
    bl->addWidget(buildBar_);
    bl->addWidget(buildLabel_);
    l->addWidget(box);

    connect(buildImage_, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getOpenFileName(this, T("Disc image"), QString(),
                                                       T("Xbox disc image (*.iso *.xiso);;All files (*)"));
        if (!f.isEmpty())
            startBuild(f, false);
    });
    connect(buildFolder_, &QPushButton::clicked, this, [this] {
        const QString d = QFileDialog::getExistingDirectory(this, T("The game's folder (with default.xbe)"));
        if (!d.isEmpty())
            startBuild(d, builder_.state() == Builder::State::Moved);
    });
    connect(cancelBuild_, &QPushButton::clicked, this, [this] { builder_.cancel(); });
    connect(openGame, &QPushButton::clicked, this, [this] {
        if (!gameDir_.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(gameDir_));
    });
    connect(openSaves, &QPushButton::clicked, this, [this] {
        const QString s = paths::savesDir(gameDir_);
        if (!s.isEmpty() && QFileInfo::exists(s))
            QDesktopServices::openUrl(QUrl::fromLocalFile(s));
        else
            QMessageBox::information(this, T("Saves"), T("No saves yet. TimeSplitters 2 keeps them in UDATA inside the game folder."));
    });

    auto *modBox = new QGroupBox(T("Mods"));
    auto *ml = new QVBoxLayout(modBox);
    ml->addWidget(help(T("Folders laid out like the disc (data/*.pak, or loose files under files/ and paks/<pak>/, "
                         "ts2/characters.ini for new characters). The first in the list wins over the ones below it. "
                         "See docs/modding.md.")));
    mods_ = new QListWidget;
    mods_->setMaximumHeight(120);
    ml->addWidget(mods_);
    auto *mrow = new QHBoxLayout;
    auto *add = new QPushButton(T("Add..."));
    auto *remove = new QPushButton(T("Remove"));
    auto *up = new QPushButton(T("Move up"));
    mrow->addWidget(add);
    mrow->addWidget(remove);
    mrow->addWidget(up);
    mrow->addStretch();
    ml->addLayout(mrow);
    l->addWidget(modBox);
    connect(add, &QPushButton::clicked, this, [this] {
        const QString d = QFileDialog::getExistingDirectory(this, T("Mod folder"));
        if (!d.isEmpty())
            mods_->addItem(QDir::toNativeSeparators(d));
    });
    connect(remove, &QPushButton::clicked, this, [this] { delete mods_->currentItem(); });
    connect(up, &QPushButton::clicked, this, [this] {
        const int r = mods_->currentRow();
        if (r > 0) {
            mods_->insertItem(r - 1, mods_->takeItem(r));
            mods_->setCurrentRow(r - 1);
        }
    });
    l->addStretch();
    return w;
}

QWidget *MainWindow::buildVideoTab()
{
    QFormLayout *f;
    QWidget *w = page(&f);

    renderer_ = combo({{T("Direct3D 11 (default)"), QStringLiteral("default")},
                       {T("Vulkan (needs a build with DXC)"), QStringLiteral("vulkan")}});
#if defined(Q_OS_WIN)
    addRow(f, T("Renderer"), renderer_, T("Direct3D 11 is the tested renderer on Windows. Vulkan is the one macOS and Linux use."));
#else
    renderer_->setVisible(false);
#endif

    resolution_ = new QComboBox;
    for (int n = 1; n <= 8; n++)
        resolution_->addItem(n == 1 ? T("Console (640x480)") : QStringLiteral("%1x (%2x%3)").arg(n).arg(640 * n).arg(480 * n),
                             QString::number(n));
    addRow(f, T("Resolution"), resolution_, T("Render larger than the console, then filter down. 2x is much sharper and costs little; 3x-4x for 1440p and 4K screens."));

    widescreen_ = new QCheckBox(T("16:9"));
    addRow(f, T("Widescreen"), widescreen_, T("The camera is widened (you see more to the sides) and the HUD and menus are laid out for 16:9."));
    menus_ = combo({{T("16:9 like the levels"), QStringLiteral("wide")}, {T("4:3 between bars, as the console"), QStringLiteral("43")}});
    addRow(f, T("Menus"), menus_, T("The front end in widescreen, or kept at 4:3."));

    fov_ = new QSpinBox;
    fov_->setRange(70, 150);
    fov_->setSingleStep(5);
    fov_->setSuffix(QStringLiteral(" %"));
    addRow(f, T("Field of view"), fov_, T("A percentage of the game's own: more shows more of the world. A scope still zooms by the game's ratio."));

    aniso_ = combo({{T("Off"), QStringLiteral("1")}, {QStringLiteral("2x"), QStringLiteral("2")}, {QStringLiteral("4x"), QStringLiteral("4")},
                    {QStringLiteral("8x"), QStringLiteral("8")}, {QStringLiteral("16x"), QStringLiteral("16")}});
    addRow(f, T("Texture sharpness"), aniso_, T("Anisotropic filtering: floors and walls seen at a glancing angle stay sharp."));

    frameCap_ = combo({{T("Adaptive (as the console)"), QStringLiteral("adaptive")}, {T("60 frames a second"), QStringLiteral("60")},
                       {T("30 frames a second"), QStringLiteral("30")}, {T("Uncapped (the game runs too fast)"), QStringLiteral("0")}});
    addRow(f, T("Frame rate"), frameCap_, T("TimeSplitters 2 runs its logic once a frame, so it must stay at 60. For smoother motion use frame interpolation. F10 changes it while playing."));

    interp_ = combo({{T("Off"), QStringLiteral("1")}, {T("2x (120 frames a second)"), QStringLiteral("2")},
                     {T("3x (180 frames a second)"), QStringLiteral("3")}, {T("4x (240 frames a second)"), QStringLiteral("4")}});
    addRow(f, T("Frame interpolation"), interp_, T("Draws frames between the game's own: smoother motion while the game still runs at 60. Best with variable refresh."));

    fullscreen_ = new QCheckBox(T("Borderless fullscreen"));
    addRow(f, T("Fullscreen"), fullscreen_, T("Alt+Enter switches while playing."));
    vrr_ = new QCheckBox(T("On"));
    addRow(f, T("Variable refresh rate"), vrr_, T("In fullscreen on a G-SYNC or FreeSync display: an even 60 on a high refresh screen."));
    fps_ = new QCheckBox(T("Show the counter"));
    addRow(f, T("Frame rate counter"), fps_, T("F9 toggles it while playing."));
    pal60_ = new QCheckBox(T("60 Hz"));
    addRow(f, T("PAL release"), pal60_, T("The PAL console offered 60 Hz; off plays the PAL release at 50 Hz, as a PAL TV without 60 Hz did."));

    connect(widescreen_, &QCheckBox::toggled, this, [this] { refreshEnabled(); });
    return w;
}

QWidget *MainWindow::buildControlsTab()
{
    auto *w = new QWidget;
    auto *l = new QVBoxLayout(w);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(w);

    auto *aimBox = new QGroupBox(T("Mouse"));
    auto *f = new QFormLayout(aimBox);
    mouseAim_ = new QCheckBox(T("Aim with the mouse"));
    addRow(f, QString(), mouseAim_, T("The game window takes the pointer while it is in front (click in it to give it back after switching away)."));
    aimMode_ = combo({{T("Direct: the mouse turns the view, the crosshair stays centred"), QStringLiteral("native")},
                      {T("As the right stick (the console's handling)"), QStringLiteral("stick")}});
    addRow(f, T("Aiming"), aimMode_,
           T("Direct aim adds the mouse's movement to the view itself, without the stick's dead zone, acceleration or the "
             "slow aim mode: the aim button is then only for the scope. As the right stick keeps the console's feel."));
    aimNote_ = help(QString());
    f->addRow(QString(), aimNote_);
    auto *sensRow = new QHBoxLayout;
    sens_ = new QSlider(Qt::Horizontal);
    sens_->setRange(kSensMin, kSensMax);
    sensBox_ = new QSpinBox;
    sensBox_->setRange(kSensMin, kSensMax);
    auto *sensReset = new QPushButton(T("Default"));
    sensRow->addWidget(sens_, 1);
    sensRow->addWidget(sensBox_);
    sensRow->addWidget(sensReset);
    auto *sensW = new QWidget;
    sensW->setLayout(sensRow);
    sensRow->setContentsMargins(0, 0, 0, 0);
    addRow(f, T("Sensitivity"), sensW, T("80 is the default: about 18 cm for a full turn on an 800 dpi mouse with direct aim."));
    invert_ = new QCheckBox(T("Mouse up looks down"));
    addRow(f, T("Invert"), invert_);
    l->addWidget(aimBox);
    connect(sens_, &QSlider::valueChanged, sensBox_, &QSpinBox::setValue);
    connect(sensBox_, qOverload<int>(&QSpinBox::valueChanged), sens_, &QSlider::setValue);
    connect(sensReset, &QPushButton::clicked, this, [this] { sens_->setValue(kSensDefault); });
    connect(mouseAim_, &QCheckBox::toggled, this, [this] { refreshEnabled(); });
    connect(aimMode_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { refreshEnabled(); });

    auto *bindBox = new QGroupBox(T("Controller 1"));
    auto *bl = new QVBoxLayout(bindBox);
    auto *top = new QFormLayout;
    device_ = combo({{T("Gamepad 1"), QStringLiteral("gamepad:0")}, {T("Gamepad 2"), QStringLiteral("gamepad:1")},
                     {T("Gamepad 3"), QStringLiteral("gamepad:2")}, {T("Gamepad 4"), QStringLiteral("gamepad:3")},
                     {T("Keyboard only"), QStringLiteral("keyboard")}, {T("Nothing"), QStringLiteral("none")}});
    top->addRow(T("Reads from"), device_);
    top->addRow(QString(), help(T("A gamepad still reads the keyboard and mouse bindings below.")));
    padApi_ = combo({{T("All controllers (SDL3)"), QStringLiteral("sdl")}, {T("Xbox controllers only (XInput)"), QStringLiteral("xinput")}});
    top->addRow(T("Controllers"), padApi_);
    bl->addLayout(top);

    const auto &ctl = bind::controls();
    bindTable_ = new QTableWidget(ctl.size(), 3);
    bindTable_->setHorizontalHeaderLabels({T("Xbox control"), T("Controller"), T("Keyboard / mouse")});
    bindTable_->verticalHeader()->setVisible(false);
    bindTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    bindTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    bindTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    bindTable_->setSelectionMode(QAbstractItemView::NoSelection);
    bindTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    bindTable_->setMinimumHeight(24 * bindTable_->verticalHeader()->defaultSectionSize() / 2);
    const QStringList pads = bind::padSources();
    for (int r = 0; r < ctl.size(); r++) {
        const QString name = QLatin1String(ctl[r].name);
        bindTable_->setItem(r, 0, new QTableWidgetItem(T(ctl[r].label)));
        auto *pc = new QComboBox;
        for (const QString &p : pads)
            pc->addItem(p == QLatin1String("none") ? T("Not bound") : T(qPrintable(bind::sourceLabel(p))), p);
        bindTable_->setCellWidget(r, 1, pc);
        padCombos_ << pc;
        connect(pc, qOverload<int>(&QComboBox::activated), this,
                [this, name, pc] { binds_.setPadSource(name, valueOf(pc)); });
        auto *kc = new KeyCapture;
        kc->onChange = [this, name](const QString &s) { binds_.setKeySource(name, s); };
        bindTable_->setCellWidget(r, 2, kc);
        keyButtons_ << kc;
    }
    bl->addWidget(bindTable_, 1);
    bl->addWidget(help(T("Bindings are by Xbox control: what each does is the game's control setup (Options > Controls in the game). "
                         "They are shared by every game built with xboxrecomp.")));
    auto *brow = new QHBoxLayout;
    auto *pc = new QPushButton(T("PC layout (WASD + mouse)"));
    auto *reset = new QPushButton(T("Back to the defaults"));
    brow->addWidget(pc);
    brow->addWidget(reset);
    brow->addStretch();
    bl->addLayout(brow);
    l->addWidget(bindBox, 1);
    connect(pc, &QPushButton::clicked, this, [this] {
        binds_.applyPcPreset();
        mouseAim_->setChecked(true);
        refreshBindings();
    });
    connect(reset, &QPushButton::clicked, this, [this] {
        binds_.resetController1();
        refreshBindings();
    });
    connect(device_, qOverload<int>(&QComboBox::activated), this, [this] { binds_.setDevice(valueOf(device_)); });
    connect(padApi_, qOverload<int>(&QComboBox::activated), this, [this] { binds_.setPadApi(valueOf(padApi_)); });
    return scroll;
}

QWidget *MainWindow::buildAudioTab()
{
    QFormLayout *f;
    QWidget *w = page(&f);
    audioPath_ = combo({{T("Native (default)"), QStringLiteral("native")}, {T("Emulated Xbox sound chip (APU)"), QStringLiteral("apu")}});
    addRow(f, T("Sound"), audioPath_, T("Native plays the game's DirectSound on this computer's audio, with the Xbox's 3D sound and room reverb. "
                                        "The emulated chip runs the game's own mixer; try it if a sound is missing."));
    volume_ = new QSlider(Qt::Horizontal);
    volume_->setRange(5, 100);
    addRow(f, T("Volume"), volume_);
    mute_ = new QCheckBox(T("No sound"));
    addRow(f, T("Mute"), mute_);
    return w;
}

QWidget *MainWindow::buildLinkTab()
{
    QFormLayout *f;
    QWidget *w = page(&f);
    linkMode_ = combo({{T("Off"), QStringLiteral("off")}, {T("Host a session"), QStringLiteral("host")}, {T("Join a session"), QStringLiteral("join")}});
    addRow(f, T("System Link"), linkMode_, T("Play with other copies over a network. Host: this computer is the session friends join. Join: play in a friend's session."));
    linkHost_ = new QLineEdit;
    linkHost_->setPlaceholderText(T("the host's address, like 81.2.3.4 or a name"));
    addRow(f, T("Host's address"), linkHost_, T("What the host gave you, with :port if theirs is not the port below."));
    linkPort_ = new QSpinBox;
    linkPort_->setRange(1, 65535);
    addRow(f, T("Port"), linkPort_, T("The UDP port. Change it only if something else already uses 27610."));
    linkCode_ = new QLineEdit;
    linkCode_->setPlaceholderText(T("none"));
    addRow(f, T("Join code"), linkCode_, T("Optional. Only copies with the same code get in."));
    linkUpnp_ = new QCheckBox(T("Ask the router to forward the port (UPnP)"));
    addRow(f, T("Router"), linkUpnp_);
    linkInfo_ = new QLabel;
    linkInfo_->setWordWrap(true);
    linkInfo_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    f->addRow(QString(), linkInfo_);
    auto *copy = new QPushButton(T("Copy my address"));
    f->addRow(QString(), copy);
    connect(copy, &QPushButton::clicked, this, [this] {
        for (const QHostAddress &a : QNetworkInterface::allAddresses())
            if (a.protocol() == QAbstractSocket::IPv4Protocol && !a.isLoopback()) {
                QApplication::clipboard()->setText(QStringLiteral("%1:%2").arg(a.toString()).arg(linkPort_->value()));
                footer_->setText(T("Copied."));
                return;
            }
    });
#if !defined(Q_OS_WIN)
    f->addRow(QString(), help(T("System Link is built on Windows only so far.")));
#endif
    connect(linkMode_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { refreshLink(); });
    return w;
}

QWidget *MainWindow::buildAdvancedTab()
{
    QFormLayout *f;
    QWidget *w = page(&f);
    language_ = combo({{QStringLiteral("Deutsch"), QStringLiteral("de")}, {QStringLiteral("English"), QStringLiteral("en")}});
    addRow(f, T("Language"), language_, T("The launcher's language. It changes when the launcher is opened again."));
    closeOnPlay_ = new QCheckBox(T("Close the launcher when the game starts"));
    addRow(f, T("After Play"), closeOnPlay_);
    extraEnv_ = new QLineEdit;
    extraEnv_->setPlaceholderText(QStringLiteral("RECOMP_RES_SCALE=3; RECOMP_TS2_AIM_PROBE=1"));
    addRow(f, T("Extra switches"), extraEnv_,
           T("For testing: NAME=value pairs separated by ';', given to the game as environment variables. "
             "xboxrecomp's CLAUDE.md and docs list them."));
    auto *row = new QHBoxLayout;
    auto *openSettings = new QPushButton(T("Open the settings file"));
    auto *openLog = new QPushButton(T("Open the game's log"));
    row->addWidget(openSettings);
    row->addWidget(openLog);
    row->addStretch();
    auto *rw = new QWidget;
    rw->setLayout(row);
    row->setContentsMargins(0, 0, 0, 0);
    f->addRow(T("Files"), rw);
    connect(openSettings, &QPushButton::clicked, this, [this] {
        save();
        QDesktopServices::openUrl(QUrl::fromLocalFile(conf_.path()));
    });
    connect(openLog, &QPushButton::clicked, this, [] {
        const QString log = paths::gameExe() + QLatin1String(".log");
        if (QFileInfo::exists(log))
            QDesktopServices::openUrl(QUrl::fromLocalFile(log));
        else
            QDesktopServices::openUrl(QUrl::fromLocalFile(paths::gameWorkDir()));
    });
    return w;
}

QWidget *MainWindow::buildAboutTab()
{
    auto *w = new QWidget;
    auto *l = new QVBoxLayout(w);
    auto *t = new QLabel;
    t->setWordWrap(true);
    t->setTextInteractionFlags(Qt::TextSelectableByMouse);
    t->setText(T("TimeSplitters 2 for the original Xbox, recompiled into a native program on the xboxrecomp toolkit. "
                 "It is built from your own copy of the game; no game code or data comes with it.") +
               QLatin1String("\n\n") + T("Title id") + QLatin1String("  ") + paths::titleIdHex() + QLatin1Char('\n') +
               T("Settings") + QLatin1String("  ") + QDir::toNativeSeparators(paths::settingsFile()) + QLatin1Char('\n') +
               T("Controls") + QLatin1String("  ") + QDir::toNativeSeparators(paths::bindingsFile()) + QLatin1Char('\n') +
               T("Game") + QLatin1String("  ") + QDir::toNativeSeparators(paths::gameExe()) + QLatin1String("\n\n") +
               T("While playing: F9 frame rate, F10 frame cap, F11 screenshot and capture for bug reports, Alt+Enter fullscreen.") +
               QLatin1String("\n\nLauncher ") + QLatin1String(SPLIT2_LAUNCHER_VERSION) + QLatin1String(", Qt ") +
               QLatin1String(qVersion()));
    l->addWidget(t);
    l->addStretch();
    return w;
}

// ---------------------------------------------------------------- state

void MainWindow::load()
{
    select(renderer_, conf_.get(QStringLiteral("launcher_renderer"), QStringLiteral("default")));
    select(resolution_, QString::number(conf_.getInt(QStringLiteral("resolution_scale"), 1, 1, 8)));
    widescreen_->setChecked(conf_.getBool(QStringLiteral("widescreen"), false));
    select(menus_, conf_.get(QStringLiteral("ts2_menus"), QStringLiteral("wide")) == QLatin1String("43") ||
                           conf_.get(QStringLiteral("ts2_menus")) == QLatin1String("4:3")
                       ? QStringLiteral("43")
                       : QStringLiteral("wide"));
    fov_->setValue(conf_.getInt(QStringLiteral("ts2_fov"), 100, 70, 150));
    {
        const int a = conf_.getInt(QStringLiteral("anisotropy"), 1, 1, 16);
        select(aniso_, QString::number(a >= 16 ? 16 : a >= 8 ? 8 : a >= 4 ? 4 : a >= 2 ? 2 : 1));
    }
    select(frameCap_, conf_.get(QStringLiteral("frame_cap"), QStringLiteral("adaptive")));
    select(interp_, QString::number(conf_.getInt(QStringLiteral("frame_interp"), 1, 1, 4)));
    fullscreen_->setChecked(conf_.getBool(QStringLiteral("fullscreen"), false));
    vrr_->setChecked(conf_.getBool(QStringLiteral("vrr"), true));
    fps_->setChecked(conf_.getBool(QStringLiteral("fps_overlay"), false));
    pal60_->setChecked(conf_.getBool(QStringLiteral("pal60"), true));

    mouseAim_->setChecked(conf_.getBool(QStringLiteral("mouse_look"), false));
    select(aimMode_, conf_.get(QStringLiteral("ts2_mouse_aim"), QStringLiteral("native")));
    sens_->setValue(conf_.getInt(QStringLiteral("mouse_sensitivity"), kSensDefault, kSensMin, kSensMax));
    invert_->setChecked(conf_.getBool(QStringLiteral("mouse_invert"), false));

    select(audioPath_, conf_.get(QStringLiteral("audio"), QStringLiteral("native")));
    volume_->setValue(conf_.getInt(QStringLiteral("launcher_volume"), 100, 5, 100));
    mute_->setChecked(conf_.getBool(QStringLiteral("launcher_mute"), false));

    const QString mode = conf_.get(QStringLiteral("syslink"), QStringLiteral("off"));
    select(linkMode_, mode == QLatin1String("udp") ? QStringLiteral("off") : mode);
    linkHost_->setText(conf_.get(QStringLiteral("syslink_host")));
    linkPort_->setValue(conf_.getInt(QStringLiteral("syslink_port"), 27610, 1, 65535));
    linkCode_->setText(conf_.get(QStringLiteral("syslink_code")));
    linkUpnp_->setChecked(conf_.getBool(QStringLiteral("syslink_upnp"), true));

    select(language_, uiGerman() ? QStringLiteral("de") : QStringLiteral("en"));
    closeOnPlay_->setChecked(conf_.getBool(QStringLiteral("launcher_close_on_play"), true));
    extraEnv_->setText(conf_.get(QStringLiteral("launcher_env")));
    mods_->clear();
    for (const QString &m : conf_.get(QStringLiteral("launcher_mods")).split(QLatin1Char('|'), Qt::SkipEmptyParts))
        mods_->addItem(m);

    refreshBindings();
    refreshLink();
    refreshEnabled();
}

bool MainWindow::save()
{
    // The toolkit's keys, with the explanation the toolkit gives them when
    // the file does not have them yet.
    conf_.set(QStringLiteral("resolution_scale"), valueOf(resolution_),
              QStringLiteral("Render at 1-8 times 640x480.  [RECOMP_RES_SCALE]"));
    conf_.setBool(QStringLiteral("widescreen"), widescreen_->isChecked(),
                  QStringLiteral("16:9, with the wider camera and menus.  [RECOMP_WIDESCREEN]"));
    conf_.set(QStringLiteral("ts2_menus"), valueOf(menus_),
              QStringLiteral("TimeSplitters 2: the front end at 16:9 (wide) or 4:3 (43).  [RECOMP_TS2_MENUS]"));
    if (fov_->value() == 100)
        conf_.unset(QStringLiteral("ts2_fov"));
    else
        conf_.set(QStringLiteral("ts2_fov"), fov_->value(),
                  QStringLiteral("TimeSplitters 2: field of view, percent of the game's (70-150).  [RECOMP_TS2_FOV]"));
    conf_.set(QStringLiteral("anisotropy"), valueOf(aniso_), QStringLiteral("Anisotropic filtering, 1-16.  [RECOMP_ANISO]"));
    conf_.set(QStringLiteral("frame_cap"), valueOf(frameCap_),
              QStringLiteral("adaptive, 60, 30 or 0 (uncapped).  [RECOMP_FPS_CAP]"));
    conf_.set(QStringLiteral("frame_interp"), valueOf(interp_),
              QStringLiteral("Frames shown for each the game draws, 1 (off) to 4.  [RECOMP_FRAME_INTERP]"));
    conf_.setBool(QStringLiteral("fullscreen"), fullscreen_->isChecked(), QStringLiteral("Borderless fullscreen.  [RECOMP_FULLSCREEN]"));
    conf_.setBool(QStringLiteral("vrr"), vrr_->isChecked(), QStringLiteral("Variable refresh in fullscreen.  [RECOMP_VRR]"));
    conf_.setBool(QStringLiteral("fps_overlay"), fps_->isChecked(), QStringLiteral("Start with the frame rate on screen.  [RECOMP_FPS_OVERLAY]"));
    conf_.setBool(QStringLiteral("pal60"), pal60_->isChecked(),
                  QStringLiteral("A PAL release at 60 Hz (1) or 50 Hz (0).  [RECOMP_PAL60]"));

    conf_.setBool(QStringLiteral("mouse_look"), mouseAim_->isChecked(),
                  QStringLiteral("The game window takes the pointer and the mouse aims.  [RECOMP_MOUSE_LOOK]"));
    conf_.set(QStringLiteral("ts2_mouse_aim"), valueOf(aimMode_),
              QStringLiteral("TimeSplitters 2: native (the mouse turns the view) or stick.  [RECOMP_TS2_MOUSE_AIM]"));
    conf_.set(QStringLiteral("mouse_sensitivity"), sens_->value(), QStringLiteral("1-100, 80 the default.  [RECOMP_MOUSE_SENSITIVITY]"));
    conf_.setBool(QStringLiteral("mouse_invert"), invert_->isChecked(),
                  QStringLiteral("1 turns the view down when the mouse moves up.  [RECOMP_MOUSE_INVERT]"));

    if (valueOf(audioPath_) == QLatin1String("native"))
        conf_.unset(QStringLiteral("audio"));
    else
        conf_.set(QStringLiteral("audio"), valueOf(audioPath_), QStringLiteral("native or apu.  [RECOMP_AUDIO]"));

    if (!(conf_.get(QStringLiteral("syslink")) == QLatin1String("udp") && valueOf(linkMode_) == QLatin1String("off")))
        conf_.set(QStringLiteral("syslink"), valueOf(linkMode_), QStringLiteral("off, host or join.  [RECOMP_SYSLINK]"));
    if (linkHost_->text().trimmed().isEmpty())
        conf_.unset(QStringLiteral("syslink_host"));
    else
        conf_.set(QStringLiteral("syslink_host"), linkHost_->text().trimmed(), QStringLiteral("[RECOMP_SYSLINK_HOST]"));
    conf_.set(QStringLiteral("syslink_port"), linkPort_->value(), QStringLiteral("[RECOMP_SYSLINK_PORT]"));
    if (linkCode_->text().trimmed().isEmpty())
        conf_.unset(QStringLiteral("syslink_code"));
    else
        conf_.set(QStringLiteral("syslink_code"), linkCode_->text().trimmed(), QStringLiteral("[RECOMP_SYSLINK_CODE]"));
    conf_.setBool(QStringLiteral("syslink_upnp"), linkUpnp_->isChecked(), QStringLiteral("[RECOMP_SYSLINK_UPNP]"));

    // This launcher's own: the game does not read them; they become its
    // environment on Play.
    const QString own = QStringLiteral("split2 launcher (the game ignores this)");
    conf_.set(QStringLiteral("launcher_renderer"), valueOf(renderer_), own);
    conf_.set(QStringLiteral("launcher_volume"), volume_->value(), own);
    conf_.setBool(QStringLiteral("launcher_mute"), mute_->isChecked(), own);
    conf_.set(QStringLiteral("launcher_language"), valueOf(language_), own);
    conf_.setBool(QStringLiteral("launcher_close_on_play"), closeOnPlay_->isChecked(), own);
    if (extraEnv_->text().trimmed().isEmpty())
        conf_.unset(QStringLiteral("launcher_env"));
    else
        conf_.set(QStringLiteral("launcher_env"), extraEnv_->text().trimmed(), own);
    QStringList mods;
    for (int i = 0; i < mods_->count(); i++)
        mods << mods_->item(i)->text();
    if (mods.isEmpty())
        conf_.unset(QStringLiteral("launcher_mods"));
    else
        conf_.set(QStringLiteral("launcher_mods"), mods.join(QLatin1Char('|')), own);

    QString err;
    if (conf_.path().isEmpty() || !conf_.save(&err)) {
        QMessageBox::warning(this, T("Settings"), T("The settings could not be written:\n%1\n%2").arg(conf_.path(), err));
        return false;
    }
    if (!binds_.path().isEmpty() && !binds_.save(&err)) {
        QMessageBox::warning(this, T("Controls"), T("The controls could not be written:\n%1\n%2").arg(binds_.path(), err));
        return false;
    }
    return true;
}

void MainWindow::refreshBindings()
{
    select(device_, binds_.device());
    select(padApi_, binds_.padApi());
    const auto &ctl = bind::controls();
    for (int r = 0; r < ctl.size(); r++) {
        const QString name = QLatin1String(ctl[r].name);
        const QString p = binds_.padSource(name);
        select(padCombos_[r], p.isEmpty() ? QStringLiteral("none") : p);
        keyButtons_[r]->display(binds_.keySource(name));
    }
}

void MainWindow::refreshLink()
{
    const QString m = valueOf(linkMode_);
    linkHost_->setEnabled(m == QLatin1String("join"));
    linkPort_->setEnabled(m != QLatin1String("off"));
    linkCode_->setEnabled(m != QLatin1String("off"));
    linkUpnp_->setEnabled(m == QLatin1String("host"));
    QString lan;
    for (const QHostAddress &a : QNetworkInterface::allAddresses())
        if (a.protocol() == QAbstractSocket::IPv4Protocol && !a.isLoopback()) {
            lan = a.toString();
            break;
        }
    if (m == QLatin1String("host"))
        linkInfo_->setText(T("Tell your friends: on your network %1:%2. Over the internet, your public address with the port "
                             "forwarded (the game asks the router when it starts, if allowed above). Then host a game in the "
                             "game's own System Link menu.")
                               .arg(lan.isEmpty() ? QStringLiteral("?") : lan)
                               .arg(linkPort_->value()));
    else if (m == QLatin1String("join"))
        linkInfo_->setText(T("Ask the host for their address, and the join code if they set one. Then open the game's System Link menu."));
    else
        linkInfo_->setText(T("One player hosts the session, up to 15 more join it, from the same network or over the internet. Each needs their own copy of the game."));
}

void MainWindow::refreshEnabled()
{
    menus_->setEnabled(widescreen_->isChecked());
    const bool aim = mouseAim_->isChecked();
    aimMode_->setEnabled(aim);
    sens_->setEnabled(aim);
    sensBox_->setEnabled(aim);
    invert_->setEnabled(aim);
    const bool native = aim && valueOf(aimMode_) == QLatin1String("native");
    const bool known = !conf_.get(QStringLiteral("ts2_aim")).isEmpty();
    aimNote_->setVisible(native);
    aimNote_->setText(known ? T("View angles from the settings file (ts2_aim).")
                            : T("Direct aim needs where your release keeps the view angles. Until that is known for it "
                                "(docs/mouse-aim.md), the game falls back to the mouse as the right stick by itself."));
}

void MainWindow::refreshGame()
{
    gameDir_ = paths::gameDir();
    const Builder::State st = builder_.state();
    const bool release = st != Builder::State::NotRelease;
    buildImage_->setVisible(release);
    buildFolder_->setVisible(release);

    QString sha;
    const QString which = paths::xbeRelease(gameDir_, &sha);
    QString status;
    if (gameDir_.isEmpty())
        status = release ? T("The game has not been built yet: choose your disc image (.iso, .xiso) or the folder with the disc's files.")
                         : T("No game files found. Put the disc's files in a folder called game beside this launcher, or set RECOMP_GAME_DIR.");
    else if (which.isEmpty())
        status = T("default.xbe is not one of the two supported releases (SHA-1 %1).").arg(sha);
    else
        status = T("TimeSplitters 2, %1 release.").arg(which);
    if (st == Builder::State::Stale)
        status += QLatin1Char(' ') + T("This release is newer than the build: build it again.");
    else if (st == Builder::State::Moved)
        status += QLatin1Char(' ') + T("The game's files have moved: choose the folder again (nothing is rebuilt).");
    if (!release && !QFileInfo::exists(paths::gameExe()))
        status += QLatin1Char(' ') + T("The game executable is missing beside the launcher: %1").arg(paths::gameExeName());
    gameStatus_->setText(status);
    gamePath_->setText(gameDir_.isEmpty() ? QString() : QDir::toNativeSeparators(gameDir_));
    const bool canPlay = !builder_.running() && QFileInfo::exists(paths::gameExe()) && !gameDir_.isEmpty() &&
                         (st == Builder::State::NotRelease || st == Builder::State::Ready);
    play_->setEnabled(canPlay);
    buildImage_->setEnabled(!builder_.running());
    buildFolder_->setEnabled(!builder_.running());
}

void MainWindow::startBuild(const QString &source, bool relink)
{
    QString err;
    if (!builder_.start(source, relink, &err)) {
        buildLabel_->setText(err);
        return;
    }
    tabs_->setCurrentIndex(0);
    buildBar_->setValue(0);
    buildBar_->setVisible(true);
    cancelBuild_->setVisible(true);
    buildLabel_->setText(relink ? T("Linking the game to its files again. Nothing is rebuilt.")
                                : T("Building the game from your copy. This happens once and takes a few minutes."));
    refreshGame();
}

void MainWindow::setupIfNeeded()
{
    const Builder::State st = builder_.state();
    if (st == Builder::State::Unbuilt || st == Builder::State::Stale) {
        tabs_->setCurrentIndex(0);
        const QString src = builder_.source();
        if (st == Builder::State::Stale && !src.isEmpty())
            startBuild(src, false);
    }
}

void MainWindow::play()
{
    if (!save())
        return;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (valueOf(renderer_) == QLatin1String("vulkan"))
        env.insert(QStringLiteral("RECOMP_D3D8_BACKEND"), QStringLiteral("vulkan"));
    if (mute_->isChecked())
        env.insert(QStringLiteral("RECOMP_MUTE"), QStringLiteral("1"));
    else if (volume_->value() < 100)
        env.insert(QStringLiteral("RECOMP_AUDIO_GAIN"), QString::number(volume_->value() / 100.0, 'f', 2));
    QStringList mods;
    for (int i = 0; i < mods_->count(); i++)
        mods << QDir::fromNativeSeparators(mods_->item(i)->text());
    if (!mods.isEmpty())
        env.insert(QStringLiteral("RECOMP_MODS"), mods.join(QDir::listSeparator()));
    for (const QString &pair : extraEnv_->text().split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
        const int eq = pair.indexOf(QLatin1Char('='));
        if (eq > 0)
            env.insert(pair.left(eq).trimmed(), pair.mid(eq + 1).trimmed());
    }

    QProcess p;
    p.setProgram(paths::gameExe());
    p.setWorkingDirectory(paths::gameWorkDir());
    p.setProcessEnvironment(env);
    qint64 pid = 0;
    if (!p.startDetached(&pid)) {
        QMessageBox::warning(this, T("Cannot start the game"),
                             T("The game could not be started:\n%1\n%2").arg(QDir::toNativeSeparators(paths::gameExe()), p.errorString()));
        return;
    }
    if (closeOnPlay_->isChecked())
        close();
    else
        footer_->setText(T("The game is running."));
}

void MainWindow::saveTabShots(const QString &dir)
{
    QDir().mkpath(dir);
    for (int i = 0; i < tabs_->count(); i++) {
        tabs_->setCurrentIndex(i);
        QApplication::processEvents();
        grab().save(QStringLiteral("%1/%2.png").arg(dir).arg(i + 1));
    }
}
