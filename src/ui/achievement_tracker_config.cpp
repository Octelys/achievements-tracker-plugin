#include "ui/achievement_tracker_config.h"

#include <obs-frontend-api.h>
#include <obs-hotkey.h>
#include <obs-module.h>
#include <diagnostics/log.h>

#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidget>

extern "C" {
#include "sources/common/achievement_cycle.h"
#include "io/state.h"
}

// ----------------------------------------------------------------------------
// Hotkey IDs
// ----------------------------------------------------------------------------

static obs_hotkey_id g_hotkey_next_id           = OBS_INVALID_HOTKEY_ID;
static obs_hotkey_id g_hotkey_previous_id       = OBS_INVALID_HOTKEY_ID;
static obs_hotkey_id g_hotkey_toggle_cycle_id   = OBS_INVALID_HOTKEY_ID;
static obs_hotkey_id g_hotkey_first_locked_id   = OBS_INVALID_HOTKEY_ID;
static obs_hotkey_id g_hotkey_first_unlocked_id = OBS_INVALID_HOTKEY_ID;

// ----------------------------------------------------------------------------
// Hotkey callbacks
// ----------------------------------------------------------------------------

static void on_next_achievement(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed) {
    if (!pressed) {
        return;
    }

    obs_log(LOG_DEBUG, "Achievement Tracker: hotkey 'Next Achievement' pressed");
    achievement_cycle_navigate_next();
}

static void on_previous_achievement(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed) {
    if (!pressed) {
        return;
    }

    obs_log(LOG_DEBUG, "Achievement Tracker: hotkey 'Previous Achievement' pressed");
    achievement_cycle_navigate_previous();
}

static void on_first_locked_achievement(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed) {
    if (!pressed) {
        return;
    }

    obs_log(LOG_DEBUG, "Achievement Tracker: hotkey 'First Locked Achievement' pressed");
    achievement_cycle_navigate_first_locked();
}

static void on_first_unlocked_achievement(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed) {
    if (!pressed) {
        return;
    }

    obs_log(LOG_DEBUG, "Achievement Tracker: hotkey 'First Unlocked Achievement' pressed");
    achievement_cycle_navigate_first_unlocked();
}

static void on_toggle_auto_cycle(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed) {
    if (!pressed) {
        return;
    }

    const bool enabled = !achievement_cycle_is_auto_cycle_enabled();
    achievement_cycle_set_auto_cycle(enabled);
    state_set_auto_cycle_enabled(enabled);
    obs_log(LOG_INFO, "Achievement Tracker: auto-cycle toggled %s", enabled ? "on" : "off");
}

// ----------------------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------------------

/**
 * @brief Load a single-binding default (modifier + key) for a hotkey if the
 *        hotkey currently has no saved bindings.
 */
static void load_default_binding(obs_hotkey_id id, const char *key_name, bool shift, bool ctrl, bool alt) {

    obs_data_array_t *current     = obs_hotkey_save(id);
    const bool        has_binding = obs_data_array_count(current) > 0;
    obs_data_array_release(current);

    if (has_binding) {
        return; /* User already configured this hotkey — respect their choice. */
    }

    obs_data_array_t *defaults = obs_data_array_create();
    obs_data_t       *binding  = obs_data_create();

    obs_data_set_string(binding, "key", key_name);
    obs_data_set_bool(binding, "shift", shift);
    obs_data_set_bool(binding, "control", ctrl);
    obs_data_set_bool(binding, "alt", alt);
    obs_data_set_bool(binding, "command", false);

    obs_data_array_push_back(defaults, binding);
    obs_data_release(binding);

    obs_hotkey_load(id, defaults);
    obs_data_array_release(defaults);
}

/**
 * @brief Return a human-readable representation of the first binding registered
 *        for a hotkey (e.g. "Shift + Left").  Returns "None" when unbound.
 */
static QString format_binding(obs_hotkey_id id) {

    obs_data_array_t *bindings = obs_hotkey_save(id);
    const size_t      count    = obs_data_array_count(bindings);

    if (count == 0) {
        obs_data_array_release(bindings);
        return "None";
    }

    obs_data_t *item = obs_data_array_item(bindings, 0);

    QStringList parts;
    if (obs_data_get_bool(item, "shift"))
        parts << "Shift";
    if (obs_data_get_bool(item, "control"))
        parts << "Ctrl";
    if (obs_data_get_bool(item, "alt"))
        parts << "Alt";
    if (obs_data_get_bool(item, "command"))
        parts << "Cmd";

    // Convert "OBS_KEY_LEFT" → a friendly label
    QString key = QString::fromUtf8(obs_data_get_string(item, "key"));
    if (key.startsWith("OBS_KEY_")) {
        key = key.mid(8); // strip prefix
    }

    // Map common keys to nicer symbols / names
    static const struct {
        const char *from;
        const char *to;
    } key_map[] = {
        {"LEFT", "←"},
        {"RIGHT", "→"},
        {"UP", "↑"},
        {"DOWN", "↓"},
        {"SPACE", "Space"},
        {nullptr, nullptr},
    };
    for (int i = 0; key_map[i].from; ++i) {
        if (key == key_map[i].from) {
            key = key_map[i].to;
            break;
        }
    }

    parts << key;

    obs_data_release(item);
    obs_data_array_release(bindings);

    return parts.join(" + ");
}

// ----------------------------------------------------------------------------
// Dialog
// ----------------------------------------------------------------------------

namespace {

class AchievementTrackerDialog final : public QDialog {
    public:
    explicit AchievementTrackerDialog(QWidget *parent = nullptr) : QDialog(parent) {

        setWindowTitle("Achievement Tracker");
        setModal(false);
        setMinimumWidth(460);

        auto *rootLayout = new QVBoxLayout(this);

        // ---- Hotkeys section ------------------------------------------------
        auto *hotkeysLabel = new QLabel("<b>Navigation Hotkeys</b>", this);

        auto *hotkeysHelp = new QLabel(this);
        hotkeysHelp->setWordWrap(true);
        hotkeysHelp->setText(
            "Keyboard shortcuts for cycling through achievements. "
            "To reconfigure, open OBS Settings \u2192 Hotkeys and search for \"Achievement Tracker\".\n\n"
            "On macOS, global hotkeys require Input Monitoring permission "
            "(System Settings \u2192 Privacy & Security \u2192 Input Monitoring). "
            "Use the buttons below as a fallback when that permission is not granted.");

        auto *hotkeysForm = new QFormLayout();
        hotkeysForm->setLabelAlignment(Qt::AlignLeft);
        hotkeysForm->setVerticalSpacing(6);

        m_prevBinding          = new QLabel(this);
        m_nextBinding          = new QLabel(this);
        m_firstUnlockedBinding = new QLabel(this);
        m_firstLockedBinding   = new QLabel(this);
        m_toggleCycleBinding   = new QLabel(this);
        m_autoCycleState       = new QLabel(this);
        m_prevBinding->setTextInteractionFlags(Qt::TextSelectableByMouse);
        m_nextBinding->setTextInteractionFlags(Qt::TextSelectableByMouse);
        m_firstUnlockedBinding->setTextInteractionFlags(Qt::TextSelectableByMouse);
        m_firstLockedBinding->setTextInteractionFlags(Qt::TextSelectableByMouse);
        m_toggleCycleBinding->setTextInteractionFlags(Qt::TextSelectableByMouse);

        hotkeysForm->addRow("Previous Achievement", m_prevBinding);
        hotkeysForm->addRow("Next Achievement", m_nextBinding);
        hotkeysForm->addRow("First Unlocked Achievement", m_firstUnlockedBinding);
        hotkeysForm->addRow("First Locked Achievement", m_firstLockedBinding);
        hotkeysForm->addRow("Toggle Auto Cycle", m_toggleCycleBinding);
        hotkeysForm->addRow("Auto Cycle Status", m_autoCycleState);

        // Manual navigation buttons — fallback when global hotkeys are unavailable.
        auto *navLayout       = new QHBoxLayout();
        m_prevButton          = new QPushButton("← Previous", this);
        m_nextButton          = new QPushButton("Next →", this);
        m_firstUnlockedButton = new QPushButton("⊤ First Unlocked", this);
        m_firstLockedButton   = new QPushButton("⊥ First Locked", this);
        m_toggleCycleButton   = new QPushButton("Toggle Cycle", this);
        m_prevButton->setToolTip("Show the previous achievement");
        m_nextButton->setToolTip("Show the next achievement");
        m_firstUnlockedButton->setToolTip("Jump to the first (most recent) unlocked achievement");
        m_firstLockedButton->setToolTip("Jump to the first locked achievement");
        m_toggleCycleButton->setToolTip("Toggle the automatic achievement rotation on/off");
        navLayout->addWidget(m_prevButton);
        navLayout->addWidget(m_nextButton);
        navLayout->addWidget(m_firstUnlockedButton);
        navLayout->addWidget(m_firstLockedButton);
        navLayout->addWidget(m_toggleCycleButton);

        connect(m_prevButton, &QPushButton::clicked, this, []() { achievement_cycle_navigate_previous(); });
        connect(m_nextButton, &QPushButton::clicked, this, []() { achievement_cycle_navigate_next(); });
        connect(m_firstUnlockedButton, &QPushButton::clicked, this, []() {
            achievement_cycle_navigate_first_unlocked();
        });
        connect(m_firstLockedButton, &QPushButton::clicked, this, []() { achievement_cycle_navigate_first_locked(); });
        connect(m_toggleCycleButton, &QPushButton::clicked, this, [this]() {
            const bool enabled = !achievement_cycle_is_auto_cycle_enabled();
            achievement_cycle_set_auto_cycle(enabled);
            state_set_auto_cycle_enabled(enabled);
            obs_log(LOG_INFO, "Achievement Tracker: auto-cycle toggled %s via dialog", enabled ? "on" : "off");
            refreshBindings();
        });

        rootLayout->addWidget(hotkeysLabel);
        rootLayout->addSpacing(4);
        rootLayout->addWidget(hotkeysHelp);
        rootLayout->addSpacing(6);
        rootLayout->addLayout(hotkeysForm);
        rootLayout->addSpacing(6);
        rootLayout->addLayout(navLayout);

        // ---- Separator -------------------------------------------------------
        auto *separator = new QFrame(this);
        separator->setFrameShape(QFrame::HLine);
        separator->setFrameShadow(QFrame::Sunken);
        rootLayout->addSpacing(8);
        rootLayout->addWidget(separator);
        rootLayout->addSpacing(8);

        // ---- Timing section --------------------------------------------------
        auto *timingLabel = new QLabel("<b>Display Timing</b>", this);

        auto *timingHelp = new QLabel(this);
        timingHelp->setWordWrap(true);
        timingHelp->setText("How long each phase of the automatic achievement cycle is displayed.");

        auto *timingForm = new QFormLayout();
        timingForm->setLabelAlignment(Qt::AlignLeft);
        timingForm->setVerticalSpacing(6);
        timingForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

        m_lastUnlockedSpin = new QSpinBox(this);
        m_lastUnlockedSpin->setRange(ACHIEVEMENT_CYCLE_MIN_DURATION, 3600);
        m_lastUnlockedSpin->setSuffix(" s");
        m_lastUnlockedSpin->setToolTip("Seconds to display the most recently unlocked achievement.");

        m_lockedEachSpin = new QSpinBox(this);
        m_lockedEachSpin->setRange(ACHIEVEMENT_CYCLE_MIN_DURATION, 3600);
        m_lockedEachSpin->setSuffix(" s");
        m_lockedEachSpin->setToolTip("Seconds to display each random locked achievement during the rotation phase.");

        m_lockedTotalSpin = new QSpinBox(this);
        m_lockedTotalSpin->setRange(ACHIEVEMENT_CYCLE_MIN_DURATION, 3600);
        m_lockedTotalSpin->setSuffix(" s");
        m_lockedTotalSpin->setToolTip(
            "Total seconds to spend in the locked rotation. Must be at least the per-locked duration "
            "so that at least one locked achievement is shown per cycle.");

        timingForm->addRow("Last unlocked", m_lastUnlockedSpin);
        timingForm->addRow("Each locked", m_lockedEachSpin);
        timingForm->addRow("Locked rotation total", m_lockedTotalSpin);

        /* Keep the locked-total minimum in sync: it must always be >= each-locked so
         * that the rotation phase can show at least one locked achievement. */
        connect(m_lockedEachSpin, &QSpinBox::valueChanged, this, [this](int value) {
            m_lockedTotalSpin->setMinimum(value);
        });

        rootLayout->addWidget(timingLabel);
        rootLayout->addSpacing(4);
        rootLayout->addWidget(timingHelp);
        rootLayout->addSpacing(6);
        rootLayout->addLayout(timingForm);

        // ---- Buttons ---------------------------------------------------------
        auto *buttonBox = new QDialogButtonBox(this);
        m_saveButton    = buttonBox->addButton("Save", QDialogButtonBox::AcceptRole);
        buttonBox->addButton(QDialogButtonBox::Close);

        rootLayout->addSpacing(8);
        rootLayout->addWidget(buttonBox);

        connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::close);
        connect(m_saveButton, &QPushButton::clicked, this, &AchievementTrackerDialog::onSave);

        refreshBindings();
        loadTimings();
    }

    void refreshBindings() {
        m_prevBinding->setText(format_binding(g_hotkey_previous_id));
        m_nextBinding->setText(format_binding(g_hotkey_next_id));
        m_firstUnlockedBinding->setText(format_binding(g_hotkey_first_unlocked_id));
        m_firstLockedBinding->setText(format_binding(g_hotkey_first_locked_id));
        m_toggleCycleBinding->setText(format_binding(g_hotkey_toggle_cycle_id));
        m_autoCycleState->setText(achievement_cycle_is_auto_cycle_enabled() ? "On" : "Off");
    }

    void loadTimings() {
        achievement_cycle_timings_t *timings = state_get_achievement_cycle_timings();
        if (!timings) {
            return;
        }

        m_lastUnlockedSpin->setValue(timings->last_unlocked_duration);
        m_lockedEachSpin->setValue(timings->locked_achievement_duration);
        m_lockedTotalSpin->setValue(timings->locked_cycle_total_duration);

        bfree(timings);
    }

    private:
    void onSave() {
        achievement_cycle_timings_t timings;
        timings.last_unlocked_duration      = m_lastUnlockedSpin->value();
        timings.locked_achievement_duration = m_lockedEachSpin->value();
        timings.locked_cycle_total_duration = m_lockedTotalSpin->value();

        state_set_achievement_cycle_timings(&timings);

        achievement_cycle_set_timings((float)timings.last_unlocked_duration,
                                      (float)timings.locked_achievement_duration,
                                      (float)timings.locked_cycle_total_duration);

        obs_log(LOG_INFO,
                "Achievement Tracker: timings saved — last_unlocked=%ds, locked_each=%ds, locked_total=%ds",
                timings.last_unlocked_duration,
                timings.locked_achievement_duration,
                timings.locked_cycle_total_duration);
    }

    QLabel      *m_prevBinding;
    QLabel      *m_nextBinding;
    QLabel      *m_firstUnlockedBinding;
    QLabel      *m_firstLockedBinding;
    QLabel      *m_toggleCycleBinding;
    QLabel      *m_autoCycleState;
    QPushButton *m_prevButton;
    QPushButton *m_nextButton;
    QPushButton *m_firstUnlockedButton;
    QPushButton *m_firstLockedButton;
    QPushButton *m_toggleCycleButton;
    QSpinBox    *m_lastUnlockedSpin;
    QSpinBox    *m_lockedEachSpin;
    QSpinBox    *m_lockedTotalSpin;
    QPushButton *m_saveButton;
};

QPointer<AchievementTrackerDialog> g_dialog;

void show_achievement_tracker_dialog(void *) {
    auto *parent = static_cast<QWidget *>(obs_frontend_get_main_window());

    if (!g_dialog) {
        g_dialog = new AchievementTrackerDialog(parent);
        g_dialog->setAttribute(Qt::WA_DeleteOnClose, true);
    }

    g_dialog->refreshBindings();
    g_dialog->loadTimings();
    g_dialog->show();
    g_dialog->raise();
    g_dialog->activateWindow();
}

} // namespace

// ----------------------------------------------------------------------------
// Public API
// ----------------------------------------------------------------------------

extern "C" void achievement_tracker_config_register(void) {

    g_hotkey_previous_id = obs_hotkey_register_frontend("achievement_tracker.previous_achievement",
                                                        "Achievement Tracker: Previous Achievement",
                                                        on_previous_achievement,
                                                        nullptr);

    g_hotkey_next_id = obs_hotkey_register_frontend("achievement_tracker.next_achievement",
                                                    "Achievement Tracker: Next Achievement",
                                                    on_next_achievement,
                                                    nullptr);

    g_hotkey_first_unlocked_id = obs_hotkey_register_frontend("achievement_tracker.first_unlocked_achievement",
                                                              "Achievement Tracker: First Unlocked Achievement",
                                                              on_first_unlocked_achievement,
                                                              nullptr);

    g_hotkey_first_locked_id = obs_hotkey_register_frontend("achievement_tracker.first_locked_achievement",
                                                            "Achievement Tracker: First Locked Achievement",
                                                            on_first_locked_achievement,
                                                            nullptr);

    g_hotkey_toggle_cycle_id = obs_hotkey_register_frontend("achievement_tracker.toggle_auto_cycle",
                                                            "Achievement Tracker: Toggle Auto Cycle",
                                                            on_toggle_auto_cycle,
                                                            nullptr);

    // Apply default bindings only when the user has not yet configured the hotkey.
    load_default_binding(g_hotkey_previous_id, "OBS_KEY_LEFT", /*shift=*/true, /*ctrl=*/false, /*alt=*/false);
    load_default_binding(g_hotkey_next_id, "OBS_KEY_RIGHT", /*shift=*/true, /*ctrl=*/false, /*alt=*/false);
    load_default_binding(g_hotkey_first_unlocked_id, "OBS_KEY_UP", /*shift=*/true, /*ctrl=*/false, /*alt=*/false);
    load_default_binding(g_hotkey_first_locked_id, "OBS_KEY_DOWN", /*shift=*/true, /*ctrl=*/false, /*alt=*/false);
    load_default_binding(g_hotkey_toggle_cycle_id, "OBS_KEY_SPACE", /*shift=*/true, /*ctrl=*/false, /*alt=*/false);

    obs_frontend_add_tools_menu_item("Achievement Tracker", show_achievement_tracker_dialog, nullptr);

    obs_log(LOG_INFO,
            "Achievement Tracker: hotkeys registered "
            "(previous=%llu, next=%llu, first_unlocked=%llu, first_locked=%llu, toggle_cycle=%llu)",
            (unsigned long long)g_hotkey_previous_id,
            (unsigned long long)g_hotkey_next_id,
            (unsigned long long)g_hotkey_first_unlocked_id,
            (unsigned long long)g_hotkey_first_locked_id,
            (unsigned long long)g_hotkey_toggle_cycle_id);
}

extern "C" void achievement_tracker_config_unregister(void) {

    if (g_dialog) {
        g_dialog->close();
        g_dialog->deleteLater();
        g_dialog.clear();
    }
}
