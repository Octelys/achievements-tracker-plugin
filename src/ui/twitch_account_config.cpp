#include "ui/twitch_account_config.h"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <diagnostics/log.h>

#include <QCheckBox>
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
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

extern "C" {
#include "integrations/twitch/account_manager.h"
#include "io/state.h"
}

namespace {

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
          m_refreshTimer(new QTimer(this)) {
        setWindowTitle("Twitch Account");
        setModal(false);
        setMinimumWidth(460);

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

        m_templateEdit->setToolTip("Supports {name}, {value}, and {gamertag} placeholders.");

        auto *templateHelp = new QLabel(this);
        templateHelp->setWordWrap(true);
        templateHelp->setText("Placeholders: {name}, {value}, {gamertag}");

        settingsForm->addRow(m_enabledCheck);
        settingsForm->addRow(m_onlyWhenLiveCheck);
        settingsForm->addRow("Message template", m_templateEdit);
        settingsForm->addRow(QString(), templateHelp);

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
        m_gameTemplateEdit->setToolTip("Supports {game} and {gamertag} placeholders.");

        auto *gameTemplateHelp = new QLabel(this);
        gameTemplateHelp->setWordWrap(true);
        gameTemplateHelp->setText("Placeholders: {game}, {gamertag}");

        gameForm->addRow(m_announceGameChangesCheck);
        gameForm->addRow("Message template", m_gameTemplateEdit);
        gameForm->addRow(QString(), gameTemplateHelp);

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
        m_masteryTemplateEdit->setToolTip("Supports {game} and {gamertag} placeholders.");

        auto *masteryTemplateHelp = new QLabel(this);
        masteryTemplateHelp->setWordWrap(true);
        masteryTemplateHelp->setText("Placeholders: {game}, {gamertag}");

        masteryForm->addRow(m_announceMasteryCheck);
        masteryForm->addRow("Message template", m_masteryTemplateEdit);
        masteryForm->addRow(QString(), masteryTemplateHelp);

        rootLayout->addSpacing(8);
        rootLayout->addWidget(masteryLabel);
        rootLayout->addSpacing(4);
        rootLayout->addLayout(masteryForm);

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
        connect(m_refreshTimer, &QTimer::timeout, this, &TwitchAccountDialog::refreshUi);

        loadConfiguration();

        m_refreshTimer->start(1000);
        refreshUi();
    }

    void loadConfiguration() {
        twitch_configuration_t *config = state_get_twitch_configuration();
        if (!config) {
            return;
        }

        m_enabledCheck->setChecked(config->enabled);
        m_onlyWhenLiveCheck->setChecked(config->only_when_live);
        m_templateEdit->setText(QString::fromUtf8(config->message_template));
        m_announceGameChangesCheck->setChecked(config->announce_game_changes);
        m_gameTemplateEdit->setText(QString::fromUtf8(config->game_announcement_template));
        m_announceMasteryCheck->setChecked(config->announce_mastery);
        m_masteryTemplateEdit->setText(QString::fromUtf8(config->mastery_announcement_template));

        state_free_twitch_configuration(&config);
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
        config.enabled              = m_enabledCheck->isChecked();
        config.announce_game_changes = m_announceGameChangesCheck->isChecked();
        config.announce_mastery     = m_announceMasteryCheck->isChecked();
        config.only_when_live       = m_onlyWhenLiveCheck->isChecked();

        QByteArray templateUtf8        = m_templateEdit->text().toUtf8();
        QByteArray gameTemplateUtf8    = m_gameTemplateEdit->text().toUtf8();
        QByteArray masteryTemplateUtf8 = m_masteryTemplateEdit->text().toUtf8();
        config.message_template            = templateUtf8.data();
        config.game_announcement_template  = gameTemplateUtf8.data();
        config.mastery_announcement_template = masteryTemplateUtf8.data();

        state_set_twitch_configuration(&config);

        obs_log(LOG_INFO,
                "Twitch Account: configuration saved (enabled=%s, announce_game_changes=%s, announce_mastery=%s, "
                "only_when_live=%s)",
                config.enabled ? "true" : "false",
                config.announce_game_changes ? "true" : "false",
                config.announce_mastery ? "true" : "false",
                config.only_when_live ? "true" : "false");
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
