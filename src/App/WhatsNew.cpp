#include "WhatsNew.h"

#include "config.h"

#include <QCoreApplication>
#include <QSettings>

namespace
{
	constexpr const char* kLastShownKey = "whatsNew/lastShownVersion";
}

namespace WhatsNew
{
	bool dueOnStartup(const QSettings& settings)
	{
		return settings.value(kLastShownKey).toString() != QStringLiteral(APP_VERSION_STRING);
	}

	void markShown()
	{
		QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
		settings.setValue(kLastShownKey, QStringLiteral(APP_VERSION_STRING));
	}

	QUrl releaseNotesUrl()
	{
		return QUrl(QStringLiteral("https://github.com/sharjith/ModelViewer-Qt/releases/tag/Release-%1").arg(QStringLiteral(APP_VERSION_STRING)));
	}

}
