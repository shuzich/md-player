#include "media/DiscModel.h"

#include <QSettings>
#include <QVariantMap>

#include <utility>

namespace md::media {

namespace {
constexpr auto kSettingHideShort = "ui/hideShortTitles";
// 上手提示只在**每次会话**的首张碟上出现一次，所以状态留在进程内，不落盘（D-020）。
bool titleHintShown = false;
} // namespace

QString formatDuration(double seconds) {
    const int total = static_cast<int>(seconds + 0.5);
    return QStringLiteral("%1:%2:%3")
        .arg(total / 3600, 2, 10, QLatin1Char('0'))
        .arg((total / 60) % 60, 2, 10, QLatin1Char('0'))
        .arg(total % 60, 2, 10, QLatin1Char('0'));
}

QVariantList filterVisible(const QVariantList& all, bool hideShort, int currentIndex) {
    QVariantList out;
    for (const QVariant& v : all) {
        const QVariantMap m = v.toMap();
        const bool keep = !hideShort || m.value(QStringLiteral("duration")).toDouble() >= kShortTitleSeconds ||
                          m.value(QStringLiteral("isMainTitle")).toBool() ||
                          m.value(QStringLiteral("index")).toInt() == currentIndex;
        if (keep)
            out.append(v);
    }
    return out;
}

bool hideShortTitlesSetting() {
    return QSettings().value(QString::fromLatin1(kSettingHideShort), true).toBool();
}

void setHideShortTitlesSetting(bool hide) {
    QSettings().setValue(QString::fromLatin1(kSettingHideShort), hide);
}

bool takeTitleHintSetting() {
    if (titleHintShown)
        return false;
    titleHintShown = true;
    return true;
}

} // namespace md::media
