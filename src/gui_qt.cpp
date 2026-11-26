/* Qt Widgets frontend for the same C emulator core used by the other frontends. */
extern "C" {
#include "firmware.h"
#include "machine.h"
#include "nvram.h"
}

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHash>
#include <QImage>
#include <QInputDialog>
#include <QKeyEvent>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

namespace {

struct Firmware {
    QByteArray model;
    QByteArray firstRom;
    QByteArray secondRom;
};

/* The array index is the model's guest keypad number. */
const struct {
    const char *label;
    int hostKey;
} phoneKeys[] = {
    {"Soft left", Qt::Key_F1}, {"Up", Qt::Key_Up}, {"Soft right", Qt::Key_F2},
    {"Left", Qt::Key_Left}, {"OK", Qt::Key_Return}, {"Right", Qt::Key_Right},
    {"Call", Qt::Key_F3}, {"Down", Qt::Key_Down}, {"End", Qt::Key_F4},
    {"1", Qt::Key_1}, {"2", Qt::Key_2}, {"3", Qt::Key_3},
    {"4", Qt::Key_4}, {"5", Qt::Key_5}, {"6", Qt::Key_6},
    {"7", Qt::Key_7}, {"8", Qt::Key_8}, {"9", Qt::Key_9},
    {"*", Qt::Key_Asterisk}, {"0", Qt::Key_0}, {"#", Qt::Key_NumberSign},
    {"C / Back", Qt::Key_Backspace}
};
static_assert(sizeof(phoneKeys) / sizeof(phoneKeys[0]) == SGH_MAX_KEYS);

/* An executable under build/linux/ can also be started from a file manager. */
QString modelsDirectory()
{
    const QDir executable(QApplication::applicationDirPath());
    const QStringList locations = {QDir::currentPath(), executable.absolutePath(),
                                   executable.filePath(".."), executable.filePath("../..")};
    for (const QString &location : locations) {
        const QString models = QDir(location).absoluteFilePath("models");
        if (QFileInfo(models).isDir()) return models;
    }
    return {};
}

/* Use the physical key when available: releasing Shift before '*' or '#'
   can change the logical key name on the release event. */
quint32 keyIdentity(const QKeyEvent *event)
{
    const quint32 scanCode = event->nativeScanCode();
    return scanCode ? scanCode : (0x80000000u | quint32(event->key()));
}

class LcdView final : public QWidget {
public:
    LcdView(const Machine &machine, QWidget *parent) : QWidget(parent), machine_(machine)
    {
        setFixedSize(256, 256); /* 2x for the C100's 128x128 display. */
        setAccessibleName("Phone LCD");
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), Qt::black);
        if (!machine_.pixels) return;

        /* The core leaves the top byte of XRGB unused. Qt's RGB32 format
           expects it to be 0xff. Painting and emulation share the Qt thread. */
        const unsigned width = machine_.width, height = machine_.height;
        QImage frame(width, height, QImage::Format_RGB32);
        if (frame.isNull()) return;
        for (unsigned row = 0; row < height; ++row) {
            const uint32_t *pixels = machine_.pixels + row * width;
            auto *line = reinterpret_cast<QRgb *>(frame.scanLine(row));
            for (unsigned column = 0; column < width; ++column)
                line[column] = 0xff000000u | pixels[column];
        }
        QRect target(QPoint(), frame.size().scaled(size(), Qt::KeepAspectRatio));
        target.moveCenter(rect().center());
        painter.drawImage(target, frame);
    }

private:
    const Machine &machine_;
};

class PhoneWindow final : public QMainWindow {
public:
    PhoneWindow();
    ~PhoneWindow() override;

protected:
    bool event(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    enum class State { Empty, Running, Paused, Stopped, Fault };

    void openFirmwareFolder();
    void openModel();
    void loadFirmware(const Firmware &firmware, State next, bool initializeDefaults = false);
    bool saveStorage();
    void setState(State state, const QString &detail = {});
    void runSlice();
    bool emulate(unsigned steps, State afterReset);
    void updateKey(unsigned key);
    void releaseKeys();

    Machine machine_{};
    NvramStore storage_{};
    Firmware firmware_;
    QByteArray simStorage_;
    QTimer runTimer_;
    QElapsedTimer lastSave_;
    LcdView *lcd_;
    QAction *run_, *pause_, *stop_, *step_;
    QPushButton *buttons_[SGH_MAX_KEYS]{};
    QHash<quint32, unsigned> keyboardKeys_;
    uint32_t mouseKeys_ = 0;
};

PhoneWindow::PhoneWindow()
{
    setWindowTitle("SGHemu");
    setFocusPolicy(Qt::StrongFocus);

    auto *file = menuBar()->addMenu("&File");
    file->addAction("Open &firmware folder...", this, &PhoneWindow::openFirmwareFolder, QKeySequence::Open);
    file->addAction("Open &model config...", this, &PhoneWindow::openModel);
    file->addSeparator();
    file->addAction("&Quit", this, &QWidget::close, QKeySequence::Quit);

    auto *emulation = menuBar()->addMenu("&Emulation");
    run_ = emulation->addAction("&Run", this, [this] { setState(State::Running); }, Qt::Key_F5);
    pause_ = emulation->addAction("&Pause", this, [this] {
        setState(State::Paused);
        saveStorage();
    }, Qt::Key_F6);
    stop_ = emulation->addAction("&Stop / Reset", this, [this] {
        loadFirmware(firmware_, State::Stopped, storage_.defaults_boot != 0);
    }, QKeySequence("Ctrl+R"));
    step_ = emulation->addAction("Step &instruction", this, [this] {
        if (emulate(1, State::Paused)) setState(State::Paused);
    }, Qt::Key_F10);
    connect(file, &QMenu::aboutToShow, this, &PhoneWindow::releaseKeys);
    connect(emulation, &QMenu::aboutToShow, this, &PhoneWindow::releaseKeys);

    auto *controls = addToolBar("Emulation controls");
    controls->setMovable(false);
    controls->setFloatable(false);
    controls->setToolButtonStyle(Qt::ToolButtonTextOnly);
    controls->addActions({run_, pause_, stop_});

    auto *panel = new QWidget(this);
    auto *layout = new QVBoxLayout(panel);
    lcd_ = new LcdView(machine_, panel);
    layout->addWidget(lcd_, 0, Qt::AlignHCenter);

    auto *keypad = new QGridLayout;
    layout->addLayout(keypad);
    for (int column = 0; column < 3; ++column) keypad->setColumnStretch(column, 1);
    for (unsigned key = 0; key < SGH_MAX_KEYS; ++key) {
        auto *button = new QPushButton(phoneKeys[key].label, panel);
        buttons_[key] = button;
        button->setFocusPolicy(Qt::NoFocus); /* Route host keys to the whole phone. */
        connect(button, &QPushButton::pressed, this, [this, key] {
            mouseKeys_ |= 1u << key;
            updateKey(key);
        });
        connect(button, &QPushButton::released, this, [this, key] {
            mouseKeys_ &= ~(1u << key);
            updateKey(key);
        });
        /* Clear/Back fills row 3; move the digit rows down to leave room. */
        const int row = key / 3;
        if (key == SGH_MAX_KEYS - 1)
            keypad->addWidget(button, 3, 0, 1, 3);
        else
            keypad->addWidget(button, row < 3 ? row : row + 1, key % 3);
    }
    layout->addStretch();
    setCentralWidget(panel);

    runTimer_.setInterval(8);
    runTimer_.setTimerType(Qt::PreciseTimer);
    connect(&runTimer_, &QTimer::timeout, this, &PhoneWindow::runSlice);
    setState(State::Empty);
}

PhoneWindow::~PhoneWindow()
{
    nvram_store_release(&storage_);
    machine_destroy(&machine_);
}

void PhoneWindow::openFirmwareFolder()
{
    const QString models = modelsDirectory();
    if (models.isEmpty()) {
        QMessageBox::warning(this, "Firmware folder", "Cannot find models/. Run SGHemu from the repository root.");
        return;
    }
    if (runTimer_.isActive()) setState(State::Paused);
    const QString folder = QFileDialog::getExistingDirectory(this, "Open firmware folder");
    if (folder.isEmpty()) return;

    const QByteArray romFolder = QFile::encodeName(folder);
    const QByteArray modelsRoot = QFile::encodeName(models);
    FirmwareMatch matches[FIRMWARE_MAX_MATCHES];
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const unsigned count = firmware_scan(romFolder.constData(), modelsRoot.constData(),
                                         matches, FIRMWARE_MAX_MATCHES);
    QApplication::restoreOverrideCursor();
    if (!count) {
        QMessageBox::information(this, "Firmware folder",
            "No supported ROM pair found. Both files must match a known firmware version.");
        return;
    }

    int choice = 0;
    if (count > 1) {
        QStringList versions;
        for (unsigned i = 0; i < count; ++i)
            versions << QString("%1. %2").arg(i + 1).arg(QString::fromUtf8(matches[i].name));
        bool accepted = false;
        const QString version = QInputDialog::getItem(this, "Select firmware", "Firmware version:",
                                                      versions, 0, false, &accepted);
        if (!accepted) return;
        choice = versions.indexOf(version);
    }
    const FirmwareMatch &match = matches[choice];
    loadFirmware({match.model, match.rom[0], match.rom[1]}, State::Running);
}

void PhoneWindow::openModel()
{
    if (runTimer_.isActive()) setState(State::Paused);
    const QString path = QFileDialog::getOpenFileName(this, "Open model config", modelsDirectory(),
                                                      "SGH model configs (*.cfg)");
    if (!path.isEmpty()) loadFirmware({QFile::encodeName(path), {}, {}}, State::Stopped);
}

void PhoneWindow::loadFirmware(const Firmware &firmware, State next, bool initializeDefaults)
{
    if (runTimer_.isActive()) setState(State::Paused);
    if (!saveStorage()) return;

    /* Save before releasing the lock. Loading the next model tears down the
       old machine, so the store must be closed before loading or resetting. */
    nvram_store_release(&storage_);
    simStorage_.clear();
    QString error;
    if (!machine_load_pair(&machine_, firmware.model.constData(),
                           firmware.firstRom.isEmpty() ? nullptr : firmware.firstRom.constData(),
                           firmware.secondRom.isEmpty() ? nullptr : firmware.secondRom.constData(), nullptr) ||
        !nvram_store_open(&storage_, &machine_, nullptr)) {
        error = QString::fromUtf8(machine_.error);
    } else if (storage_.active && machine_.sim.attached) {
        simStorage_ = QByteArray(storage_.path) + ".sim";
        if (!sim_card_load_storage(&machine_.sim.card, simStorage_.constData()))
            error = QString::fromUtf8(machine_.sim.card.error);
    }

    if (!error.isEmpty()) {
        nvram_store_release(&storage_);
        machine_destroy(&machine_);
        simStorage_.clear();
        setWindowTitle("SGHemu");
        setState(State::Empty);
        QMessageBox::warning(this, "Cannot load firmware", error);
        return;
    }

    firmware_ = firmware;
    storage_.defaults_boot = initializeDefaults;
    battery_set(&machine_.battery, 100, 0);
    lastSave_.start();
    setWindowTitle(QString("SGHemu - %1").arg(QString::fromUtf8(machine_.name)));
    setState(next);
}

bool PhoneWindow::saveStorage()
{
    if (!storage_.active) return true;
    QString error;
    /* During the first boot, the core saves the defaults only when the guest
       finishes initializing them. A pause or close must not save half of them. */
    if (!storage_.defaults_boot && !nvram_store_flush(&storage_, &machine_))
        error = QString::fromUtf8(machine_.error);
    else if (!simStorage_.isEmpty() && !sim_card_save_storage(&machine_.sim.card, simStorage_.constData()))
        error = QString::fromUtf8(machine_.sim.card.error);
    else {
        lastSave_.restart();
        return true;
    }
    if (runTimer_.isActive()) setState(State::Paused);
    QMessageBox::warning(this, "Cannot save phone state", error);
    return false;
}

void PhoneWindow::setState(State state, const QString &detail)
{
    const bool running = state == State::Running;
    const bool canRun = machine_.loaded && !running && state != State::Fault;
    if (running) runTimer_.start();
    else {
        runTimer_.stop();
        releaseKeys();
    }
    run_->setEnabled(canRun);
    pause_->setEnabled(running);
    stop_->setEnabled(machine_.loaded);
    step_->setEnabled(canRun);
    for (auto *button : buttons_) button->setEnabled(running);

    QString message;
    switch (state) {
    case State::Empty: message = "Open firmware folder (Ctrl+O)"; break;
    case State::Running:
        if (storage_.provision || storage_.defaults_boot) message = "Preparing phone storage...";
        else message = machine_.bringup ? "Running (experimental)" : "Running";
        break;
    case State::Paused: message = "Paused"; break;
    case State::Stopped: message = "Stopped - press F5 to run"; break;
    case State::Fault: {
        const QString pc = QString("%1").arg(machine_.cpu.r[15], 8, 16, QLatin1Char('0'));
        message = QString("Stopped at 0x%1: %2").arg(pc, detail);
        break;
    }
    }
    statusBar()->showMessage(message);
    statusBar()->setToolTip(message);
    lcd_->update();
}

bool PhoneWindow::emulate(unsigned steps, State afterReset)
{
    /* Provisioning and legacy battery callbacks watch for exact guest PCs.
       In those cases check every instruction; ordinary execution can batch. */
    const bool singleStep = storage_.provision || storage_.defaults_boot ||
                            (machine_.battery.enabled && !machine_.battery.adc);
    const unsigned batch = singleStep ? 1 : steps;
    for (unsigned done = 0; done < steps; done += batch) {
        const int provision = nvram_store_step(&storage_, &machine_);
        if (!provision) {
            setState(State::Fault, QString::fromUtf8(machine_.error));
            return false;
        }
        if (provision == 2) {
            /* After formatting, boot to create the UI defaults. The next
               reset starts the phone with the finished, saved snapshot. */
            loadFirmware(firmware_, afterReset, storage_.provision && storage_.stage == 3);
            return false;
        }
        if (!machine_.cellular.stage && !battery_step(&machine_.battery, &machine_.cpu)) {
            setState(State::Fault, "Guest battery event failed");
            return false;
        }
        if (machine_run(&machine_, batch, machine_.bringup) != batch) {
            const char *reason = machine_.cpu.error ? machine_.cpu.error : "Unmapped hardware access";
            setState(State::Fault, QString::fromUtf8(reason));
            return false;
        }
    }
    return true;
}

void PhoneWindow::runSlice()
{
    /* Yield to Qt after a short slice so painting, input and pause happen
       between batches without sharing the machine with a worker thread. */
    QElapsedTimer slice;
    slice.start();
    do {
        if (!emulate(64, State::Running)) return;
    } while (slice.elapsed() < 5);
    lcd_->update();
    if (storage_.active && lastSave_.elapsed() >= 5000) saveStorage();
}

void PhoneWindow::updateKey(unsigned key)
{
    /* Releasing a key on the PC must not lift the same on-screen button. */
    const bool held = (mouseKeys_ & (1u << key)) || keyboardKeys_.values().contains(key);
    machine_key(&machine_, key, held);
    buttons_[key]->setDown(held);
}

void PhoneWindow::releaseKeys()
{
    const uint32_t held = machine_.keys;
    mouseKeys_ = 0;
    keyboardKeys_.clear();
    for (unsigned key = 0; key < SGH_MAX_KEYS; ++key)
        if (held & (1u << key)) updateKey(key);
}

bool PhoneWindow::event(QEvent *event)
{
    if (event->type() == QEvent::WindowDeactivate) releaseKeys();
    return QMainWindow::event(event);
}

void PhoneWindow::keyPressEvent(QKeyEvent *event)
{
    if (runTimer_.isActive() && !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
        int hostKey = event->key();
        if (hostKey == Qt::Key_Enter || hostKey == Qt::Key_Space) hostKey = Qt::Key_Return;
        for (unsigned key = 0; key < SGH_MAX_KEYS; ++key) {
            if (phoneKeys[key].hostKey != hostKey) continue;
            if (!event->isAutoRepeat()) {
                keyboardKeys_.insert(keyIdentity(event), key);
                updateKey(key);
            }
            event->accept();
            return;
        }
    }
    QMainWindow::keyPressEvent(event);
}

void PhoneWindow::keyReleaseEvent(QKeyEvent *event)
{
    auto held = keyboardKeys_.find(keyIdentity(event));
    if (held == keyboardKeys_.end()) {
        QMainWindow::keyReleaseEvent(event);
        return;
    }
    if (!event->isAutoRepeat()) {
        const unsigned key = held.value();
        keyboardKeys_.erase(held);
        updateKey(key);
    }
    event->accept();
}

void PhoneWindow::closeEvent(QCloseEvent *event)
{
    if (runTimer_.isActive()) setState(State::Paused);
    if (saveStorage()) event->accept();
    else event->ignore();
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    PhoneWindow window;
    window.show();
    return app.exec();
}
