#include "ui/xbox_account_config.h"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <diagnostics/log.h>

#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QSizePolicy>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

extern "C" {
#include "integrations/xbox/account_manager.h"
}

namespace {

class XboxAccountDialog final : public QDialog {
    public:
    explicit XboxAccountDialog(QWidget *parent = nullptr)
        : QDialog(parent),
          m_statusValue(new QLabel(this)),
          m_versionValue(new QLabel(this)),
          m_helpText(new QLabel(this)),
          m_accountButton(new QPushButton(this)),
          m_refreshTimer(new QTimer(this)) {
        setWindowTitle("Xbox Account");
        setModal(false);
        setMinimumWidth(460);

        auto *rootLayout = new QVBoxLayout(this);
        auto *formLayout = new QFormLayout();
        auto *buttonBox  = new QDialogButtonBox(QDialogButtonBox::Close, this);

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
            "Manage your Xbox sign-in once here. All Xbox sources in the plugin will use the same account.");

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
        rootLayout->addWidget(buttonBox);

        m_versionValue->setText(PLUGIN_VERSION);

        connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::close);
        connect(m_accountButton, &QPushButton::clicked, this, &XboxAccountDialog::onAccountButtonClicked);
        connect(m_refreshTimer, &QTimer::timeout, this, &XboxAccountDialog::refreshUi);

        m_refreshTimer->start(1000);
        refreshUi();
    }

    private:
    void onAccountButtonClicked() {
        if (xbox_account_is_signed_in()) {
            xbox_account_sign_out();
            refreshUi();
        } else {
            if (!xbox_account_sign_in()) {
                QMessageBox::warning(this, "Xbox Account", "Unable to start the Xbox sign-in flow.");
                refreshUi();
                return;
            }
            QMessageBox::information(this,
                                     "Xbox Account",
                                     "Your browser was opened to continue Xbox sign-in. Finish the authentication "
                                     "there, then return to OBS.");
            refreshUi();
        }
    }

    void refreshUi() {
        char status[1024];

        xbox_account_get_status_text(status, sizeof(status));
        m_statusValue->setText(QString::fromUtf8(status));

        const bool signedIn = xbox_account_is_signed_in();
        m_accountButton->setText(signedIn ? "Sign out from Xbox" : "Sign in with Xbox");
    }

    private:
    QLabel      *m_statusValue;
    QLabel      *m_versionValue;
    QLabel      *m_helpText;
    QPushButton *m_accountButton;
    QTimer      *m_refreshTimer;
};

QPointer<XboxAccountDialog> g_dialog;

void show_xbox_account_dialog(void *) {
    auto *parent = static_cast<QWidget *>(obs_frontend_get_main_window());

    if (!g_dialog) {
        g_dialog = new XboxAccountDialog(parent);
        g_dialog->setAttribute(Qt::WA_DeleteOnClose, true);
    }

    g_dialog->show();
    g_dialog->raise();
    g_dialog->activateWindow();
}

} // namespace

extern "C" void xbox_account_config_register(void) {
    obs_frontend_add_tools_menu_item("Xbox Account", &show_xbox_account_dialog, nullptr);
}

extern "C" void xbox_account_config_unregister(void) {
    if (g_dialog) {
        g_dialog->close();
        g_dialog->deleteLater();
        g_dialog.clear();
    }
}
