#include "ui/twitch_account_config.h"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <diagnostics/log.h>

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QSizePolicy>
#include <QSpinBox>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

extern "C" {
#include "integrations/twitch/account_manager.h"
#include "integrations/twitch/speech_announcer.h"
#include "integrations/speech/model_manager.h"
#include "io/state.h"
}

namespace {

bool collect_audio_source_name(void *param, obs_source_t *source) {
    auto *names = static_cast<QStringList *>(param);

    if (obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO) {
        const char *name = obs_source_get_name(source);
        if (name && name[0] != '\0') {
            names->append(QString::fromUtf8(name));
        }
    }

    return true;
}

class TwitchAccountDialog final : public QDialog {
    public:
    explicit TwitchAccountDialog(QWidget *parent = nullptr)
        : QDialog(parent),
          m_statusValue(new QLabel(this)),
          m_versionValue(new QLabel(this)),
          m_helpText(new QLabel(this)),
          m_accountButton(new QPushButton(this)),
          m_enabledCheck(new QCheckBox(this)),
          m_onlyWhenLiveCheck(new QCheckBox(this)),
          m_templateEdit(new QLineEdit(this)),
          m_announceGameChangesCheck(new QCheckBox(this)),
          m_gameTemplateEdit(new QLineEdit(this)),
          m_announceMasteryCheck(new QCheckBox(this)),
          m_masteryTemplateEdit(new QLineEdit(this)),
          m_announceProgressCheck(new QCheckBox(this)),
          m_progressTemplateEdit(new QLineEdit(this)),
          m_speechEnabledCheck(new QCheckBox(this)),
          m_speechOnlyWhenLiveCheck(new QCheckBox(this)),
          m_speechSourceCombo(new QComboBox(this)),
          m_speechSilenceSpin(new QSpinBox(this)),
          m_speechTemplateEdit(new QLineEdit(this)),
          m_speechModelStatus(new QLabel(this)),
          m_speechModelButton(new QPushButton("Download speech model", this)),
          m_refreshTimer(new QTimer(this)) {
        setWindowTitle("Twitch Account");
        setModal(false);
        setMinimumWidth(620);

        auto *rootLayout = new QVBoxLayout(this);
        auto *formLayout = new QFormLayout();

        formLayout->setLabelAlignment(Qt::AlignLeft);
        formLayout->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
        formLayout->setVerticalSpacing(10);
        formLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

        m_statusValue->setTextInteractionFlags(Qt::TextSelectableByMouse);
        m_statusValue->setWordWrap(true);
        m_statusValue->setMinimumHeight(m_statusValue->fontMetrics().height() * 2);
        m_statusValue->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        m_versionValue->setTextInteractionFlags(Qt::TextSelectableByMouse);
        m_helpText->setWordWrap(true);
        m_helpText->setAlignment(Qt::AlignLeft);
        m_helpText->setText(
            "Connect a Twitch account to automatically post a chat message whenever an achievement unlocks.");

        auto *accountLayout = new QHBoxLayout();
        accountLayout->addWidget(m_accountButton);
        accountLayout->addStretch(1);
        accountLayout->setContentsMargins(0, 0, 0, 0);

        formLayout->addRow("Status", m_statusValue);
        formLayout->addRow("Plugin version", m_versionValue);
        formLayout->addRow("Account", accountLayout);

        rootLayout->addWidget(m_helpText);
        rootLayout->addSpacing(8);
        rootLayout->addLayout(formLayout);

        // ---- Announcement settings -------------------------------------------
        auto *settingsLabel = new QLabel("<b>Achievement Announcements</b>", this);

        auto *settingsForm = new QFormLayout();
        settingsForm->setLabelAlignment(Qt::AlignLeft);
        settingsForm->setVerticalSpacing(6);
        settingsForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

        m_enabledCheck->setText("Post achievement unlocks to Twitch chat");
        m_onlyWhenLiveCheck->setText("Only when the channel is live");

        m_templateEdit->setToolTip(
            "Supports {name}, {value}, {gamertag}, {unlocked}, and {total} placeholders.");

        auto *templateHelp = new QLabel(this);
        templateHelp->setWordWrap(true);
        templateHelp->setText("Placeholders: {name}, {value}, {gamertag}, {unlocked}, {total}");

        settingsForm->addRow(m_enabledCheck);
        settingsForm->addRow(m_onlyWhenLiveCheck);
        settingsForm->addRow("Message template", m_templateEdit);
        settingsForm->addRow(templateHelp);

        rootLayout->addSpacing(8);
        rootLayout->addWidget(settingsLabel);
        rootLayout->addSpacing(4);
        rootLayout->addLayout(settingsForm);

        // ---- Game-change announcements -----------------------------------------
        auto *gameLabel = new QLabel("<b>Game Announcements</b>", this);

        auto *gameForm = new QFormLayout();
        gameForm->setLabelAlignment(Qt::AlignLeft);
        gameForm->setVerticalSpacing(6);
        gameForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

        m_announceGameChangesCheck->setText("Announce game changes to Twitch chat");
        m_gameTemplateEdit->setToolTip("Supports {game}, {console} and {gamertag} placeholders.");

        auto *gameTemplateHelp = new QLabel(this);
        gameTemplateHelp->setWordWrap(true);
        gameTemplateHelp->setText("Placeholders: {game}, {console}, {gamertag}");

        gameForm->addRow(m_announceGameChangesCheck);
        gameForm->addRow("Message template", m_gameTemplateEdit);
        gameForm->addRow(gameTemplateHelp);

        rootLayout->addSpacing(8);
        rootLayout->addWidget(gameLabel);
        rootLayout->addSpacing(4);
        rootLayout->addLayout(gameForm);

        // ---- Mastery announcements ----------------------------------------------
        auto *masteryLabel = new QLabel("<b>Mastery Announcements</b>", this);

        auto *masteryForm = new QFormLayout();
        masteryForm->setLabelAlignment(Qt::AlignLeft);
        masteryForm->setVerticalSpacing(6);
        masteryForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

        m_announceMasteryCheck->setText("Announce when the current game is mastered (100% unlocked)");
        m_masteryTemplateEdit->setToolTip("Supports {game}, {console} and {gamertag} placeholders.");

        auto *masteryTemplateHelp = new QLabel(this);
        masteryTemplateHelp->setWordWrap(true);
        masteryTemplateHelp->setText("Placeholders: {game}, {console}, {gamertag}");

        masteryForm->addRow(m_announceMasteryCheck);
        masteryForm->addRow("Message template", m_masteryTemplateEdit);
        masteryForm->addRow(masteryTemplateHelp);

        rootLayout->addSpacing(8);
        rootLayout->addWidget(masteryLabel);
        rootLayout->addSpacing(4);
        rootLayout->addLayout(masteryForm);

        // ---- Progress announcements ---------------------------------------------
        auto *progressLabel = new QLabel("<b>Progress Announcements</b>", this);

        auto *progressForm = new QFormLayout();
        progressForm->setLabelAlignment(Qt::AlignLeft);
        progressForm->setVerticalSpacing(6);
        progressForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

        m_announceProgressCheck->setText("Announce achievement progress to Twitch chat");
        m_progressTemplateEdit->setToolTip(
            "Supports {name}, {progress}, {gamertag}, {unlocked}, and {total} placeholders.");

        auto *progressTemplateHelp = new QLabel(this);
        progressTemplateHelp->setWordWrap(true);
        progressTemplateHelp->setText("Placeholders: {name}, {progress}, {gamertag}, {unlocked}, {total}");

        progressForm->addRow(m_announceProgressCheck);
        progressForm->addRow("Message template", m_progressTemplateEdit);
        progressForm->addRow(progressTemplateHelp);

        rootLayout->addSpacing(8);
        rootLayout->addWidget(progressLabel);
        rootLayout->addSpacing(4);
        rootLayout->addLayout(progressForm);

        // ---- Voice to chat -------------------------------------------------------
        auto *speechLabel = new QLabel("<b>Voice to Chat</b>", this);

        auto *speechForm = new QFormLayout();
        speechForm->setLabelAlignment(Qt::AlignLeft);
        speechForm->setVerticalSpacing(6);
        speechForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

        m_speechEnabledCheck->setText("Transcribe speech and post it to Twitch chat");
        m_speechOnlyWhenLiveCheck->setText("Only when the channel is live");
        m_speechSourceCombo->setToolTip("The OBS audio source to listen to (e.g. your microphone).");
        m_speechSilenceSpin->setRange(200, 5000);
        m_speechSilenceSpin->setSingleStep(100);
        m_speechSilenceSpin->setSuffix(" ms");
        m_speechSilenceSpin->setToolTip("How long you need to pause before the sentence so far is posted.");
        m_speechTemplateEdit->setToolTip("Supports the {text} placeholder (the transcribed sentence).");

        auto *speechTemplateHelp = new QLabel(this);
        speechTemplateHelp->setWordWrap(true);
        speechTemplateHelp->setText("Placeholders: {text}");

        auto *speechModelLayout = new QHBoxLayout();
        speechModelLayout->addWidget(m_speechModelButton);
        speechModelLayout->addWidget(m_speechModelStatus, 1);
        speechModelLayout->setContentsMargins(0, 0, 0, 0);

        speechForm->addRow(m_speechEnabledCheck);
        speechForm->addRow(m_speechOnlyWhenLiveCheck);
        speechForm->addRow("Audio source", m_speechSourceCombo);
        speechForm->addRow("Pause before posting", m_speechSilenceSpin);
        speechForm->addRow("Message template", m_speechTemplateEdit);
        speechForm->addRow(speechTemplateHelp);
        speechForm->addRow("Speech model", speechModelLayout);

        rootLayout->addSpacing(8);
        rootLayout->addWidget(speechLabel);
        rootLayout->addSpacing(4);
        rootLayout->addLayout(speechForm);

        // ---- Buttons -----------------------------------------------------------
        auto *buttonBox = new QDialogButtonBox(this);
        m_saveButton    = buttonBox->addButton("Save", QDialogButtonBox::AcceptRole);
        buttonBox->addButton(QDialogButtonBox::Close);

        rootLayout->addSpacing(8);
        rootLayout->addWidget(buttonBox);

        m_versionValue->setText(PLUGIN_VERSION);

        connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::close);
        connect(m_saveButton, &QPushButton::clicked, this, &TwitchAccountDialog::onSave);
        connect(m_accountButton, &QPushButton::clicked, this, &TwitchAccountDialog::onAccountButtonClicked);
        connect(m_speechModelButton, &QPushButton::clicked, this, &TwitchAccountDialog::onDownloadModelClicked);
        connect(m_refreshTimer, &QTimer::timeout, this, &TwitchAccountDialog::refreshUi);

        populateAudioSources();
        loadConfiguration();

        m_refreshTimer->start(1000);
        refreshUi();
    }

    void loadConfiguration() {
        twitch_configuration_t *config = state_get_twitch_configuration();
        if (config) {
            m_enabledCheck->setChecked(config->enabled);
            m_onlyWhenLiveCheck->setChecked(config->only_when_live);
            m_templateEdit->setText(QString::fromUtf8(config->message_template));
            m_announceGameChangesCheck->setChecked(config->announce_game_changes);
            m_gameTemplateEdit->setText(QString::fromUtf8(config->game_announcement_template));
            m_announceMasteryCheck->setChecked(config->announce_mastery);
            m_masteryTemplateEdit->setText(QString::fromUtf8(config->mastery_announcement_template));
            m_announceProgressCheck->setChecked(config->announce_progress);
            m_progressTemplateEdit->setText(QString::fromUtf8(config->progress_announcement_template));

            state_free_twitch_configuration(&config);
        }

        speech_to_text_configuration_t *speechConfig = state_get_speech_to_text_configuration();
        if (speechConfig) {
            m_speechEnabledCheck->setChecked(speechConfig->enabled);
            m_speechOnlyWhenLiveCheck->setChecked(speechConfig->only_when_live);
            m_speechSilenceSpin->setValue(speechConfig->silence_duration_ms);
            m_speechTemplateEdit->setText(QString::fromUtf8(speechConfig->message_template));

            int sourceIndex = m_speechSourceCombo->findText(QString::fromUtf8(speechConfig->audio_source_name));
            m_speechSourceCombo->setCurrentIndex(sourceIndex >= 0 ? sourceIndex : -1);

            state_free_speech_to_text_configuration(&speechConfig);
        }

        refreshSpeechModelStatus();
    }

    void populateAudioSources() {
        QStringList names;
        obs_enum_sources(&collect_audio_source_name, &names);

        m_speechSourceCombo->clear();
        m_speechSourceCombo->addItem("(none)");
        m_speechSourceCombo->addItems(names);
    }

    void refreshSpeechModelStatus() {
        m_speechModelStatus->setText(speech_model_is_downloaded() ? "Model downloaded" : "Model not downloaded");
    }

    void refreshUi() {
        char status[1024];

        twitch_account_get_status_text(status, sizeof(status));
        m_statusValue->setText(QString::fromUtf8(status));

        const bool signedIn = twitch_account_is_signed_in();
        m_accountButton->setText(signedIn ? "Sign out from Twitch" : "Sign in with Twitch");

        char pendingCode[512];
        twitch_account_get_pending_code_text(pendingCode, sizeof(pendingCode));
        if (pendingCode[0] != '\0') {
            QMessageBox::information(this, "Twitch Account", QString::fromUtf8(pendingCode));
        }
    }

    private:
    void onAccountButtonClicked() {
        if (twitch_account_is_signed_in()) {
            twitch_account_sign_out();
            refreshUi();
        } else {
            if (!twitch_account_sign_in()) {
                QMessageBox::warning(this, "Twitch Account", "Unable to start the Twitch sign-in flow.");
                refreshUi();
                return;
            }
            refreshUi();
        }
    }

    void onSave() {
        twitch_configuration_t config;
        config.enabled               = m_enabledCheck->isChecked();
        config.announce_game_changes = m_announceGameChangesCheck->isChecked();
        config.announce_mastery      = m_announceMasteryCheck->isChecked();
        config.announce_progress     = m_announceProgressCheck->isChecked();
        config.only_when_live        = m_onlyWhenLiveCheck->isChecked();

        QByteArray templateUtf8               = m_templateEdit->text().toUtf8();
        QByteArray gameTemplateUtf8           = m_gameTemplateEdit->text().toUtf8();
        QByteArray masteryTemplateUtf8        = m_masteryTemplateEdit->text().toUtf8();
        QByteArray progressTemplateUtf8       = m_progressTemplateEdit->text().toUtf8();
        config.message_template               = templateUtf8.data();
        config.game_announcement_template     = gameTemplateUtf8.data();
        config.mastery_announcement_template  = masteryTemplateUtf8.data();
        config.progress_announcement_template = progressTemplateUtf8.data();

        state_set_twitch_configuration(&config);

        obs_log(LOG_INFO,
                "Twitch Account: configuration saved (enabled=%s, announce_game_changes=%s, announce_mastery=%s, "
                "announce_progress=%s, only_when_live=%s)",
                config.enabled ? "true" : "false",
                config.announce_game_changes ? "true" : "false",
                config.announce_mastery ? "true" : "false",
                config.announce_progress ? "true" : "false",
                config.only_when_live ? "true" : "false");

        speech_to_text_configuration_t speechConfig;
        speechConfig.enabled        = m_speechEnabledCheck->isChecked();
        speechConfig.only_when_live = m_speechOnlyWhenLiveCheck->isChecked();
        speechConfig.silence_duration_ms = m_speechSilenceSpin->value();

        QByteArray sourceNameUtf8         = m_speechSourceCombo->currentIndex() > 0
                                                 ? m_speechSourceCombo->currentText().toUtf8()
                                                 : QByteArray();
        QByteArray speechTemplateUtf8     = m_speechTemplateEdit->text().toUtf8();
        speechConfig.audio_source_name    = sourceNameUtf8.data();
        speechConfig.message_template     = speechTemplateUtf8.data();

        state_set_speech_to_text_configuration(&speechConfig);
        twitch_speech_announcer_start();

        obs_log(LOG_INFO,
                "Twitch Account: speech-to-chat configuration saved (enabled=%s, source='%s')",
                speechConfig.enabled ? "true" : "false",
                speechConfig.audio_source_name);
    }

    void onDownloadModelClicked() {
        m_speechModelButton->setEnabled(false);
        m_speechModelStatus->setText("Downloading...");
        QCoreApplication::processEvents();

        bool ok = speech_model_ensure_downloaded();

        if (!ok) {
            QMessageBox::warning(this, "Voice to Chat", "Failed to download the speech model. Check your network "
                                                          "connection and try again.");
        }

        refreshSpeechModelStatus();
        m_speechModelButton->setEnabled(true);
    }

    QLabel      *m_statusValue;
    QLabel      *m_versionValue;
    QLabel      *m_helpText;
    QPushButton *m_accountButton;
    QCheckBox   *m_enabledCheck;
    QCheckBox   *m_onlyWhenLiveCheck;
    QLineEdit   *m_templateEdit;
    QCheckBox   *m_announceGameChangesCheck;
    QLineEdit   *m_gameTemplateEdit;
    QCheckBox   *m_announceMasteryCheck;
    QLineEdit   *m_masteryTemplateEdit;
    QCheckBox   *m_announceProgressCheck;
    QLineEdit   *m_progressTemplateEdit;
    QCheckBox   *m_speechEnabledCheck;
    QCheckBox   *m_speechOnlyWhenLiveCheck;
    QComboBox   *m_speechSourceCombo;
    QSpinBox    *m_speechSilenceSpin;
    QLineEdit   *m_speechTemplateEdit;
    QLabel      *m_speechModelStatus;
    QPushButton *m_speechModelButton;
    QPushButton *m_saveButton;
    QTimer      *m_refreshTimer;
};

QPointer<TwitchAccountDialog> g_dialog;

void show_twitch_account_dialog(void *) {
    auto *parent = static_cast<QWidget *>(obs_frontend_get_main_window());

    if (!g_dialog) {
        g_dialog = new TwitchAccountDialog(parent);
        g_dialog->setAttribute(Qt::WA_DeleteOnClose, true);
    }

    g_dialog->populateAudioSources();
    g_dialog->loadConfiguration();
    g_dialog->show();
    g_dialog->raise();
    g_dialog->activateWindow();
}

} // namespace

extern "C" void twitch_account_config_register(void) {
    obs_frontend_add_tools_menu_item("Twitch Account", &show_twitch_account_dialog, nullptr);
}

extern "C" void twitch_account_config_unregister(void) {
    if (g_dialog) {
        g_dialog->close();
        g_dialog->deleteLater();
        g_dialog.clear();
    }
}
