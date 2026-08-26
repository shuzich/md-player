#include "core/ResumeStore.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace md::core {

namespace {
constexpr int kMaxEntries = 500; // 超出后按时间淘汰，避免文件无限增长
}

ResumeStore::ResumeStore(QObject* parent) : QObject(parent) {
    load();
}

QString ResumeStore::storePath() {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/resume.json");
}

// 用路径的哈希做键：JSON 键不必承载可读路径，也避开各种转义问题。
QString ResumeStore::keyFor(const QString& uri) {
    return QString::fromLatin1(QCryptographicHash::hash(uri.toUtf8(), QCryptographicHash::Sha256).toHex().left(32));
}

void ResumeStore::load() {
    QFile f(storePath());
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    for (auto it = root.begin(); it != root.end(); ++it) {
        const QJsonObject o = it.value().toObject();
        ResumeEntry e;
        e.position = o.value(QStringLiteral("position")).toDouble();
        e.duration = o.value(QStringLiteral("duration")).toDouble();
        e.size = static_cast<qint64>(o.value(QStringLiteral("size")).toDouble());
        e.savedAt = static_cast<qint64>(o.value(QStringLiteral("savedAt")).toDouble());
        entries_.insert(it.key(), e);
    }
}

void ResumeStore::save() const {
    QJsonObject root;
    for (auto it = entries_.cbegin(); it != entries_.cend(); ++it) {
        QJsonObject o;
        o[QStringLiteral("position")] = it.value().position;
        o[QStringLiteral("duration")] = it.value().duration;
        o[QStringLiteral("size")] = static_cast<double>(it.value().size);
        o[QStringLiteral("savedAt")] = static_cast<double>(it.value().savedAt);
        root.insert(it.key(), o);
    }
    QFile f(storePath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning("断点续播记录写入失败: %s", qUtf8Printable(storePath()));
        return;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

ResumeEntry ResumeStore::lookup(const QString& uri) const {
    const ResumeEntry e = entries_.value(keyFor(uri));
    if (!e.isValid())
        return {};
    // 大小对不上说明不是同一份文件，宁可不续播也不要跳错。
    const QFileInfo info(uri);
    if (info.exists() && e.size > 0 && info.size() != e.size)
        return {};
    return e;
}

void ResumeStore::remember(const QString& uri, double position, double duration) {
    if (uri.isEmpty() || position <= kHeadGuardSeconds)
        return;
    if (duration > 0.0 && position >= duration - kTailGuardSeconds) {
        forget(uri); // 已看到尾部，清掉旧记录
        return;
    }
    ResumeEntry e;
    e.position = position;
    e.duration = duration;
    e.size = QFileInfo(uri).size();
    e.savedAt = QDateTime::currentSecsSinceEpoch();
    entries_.insert(keyFor(uri), e);

    if (entries_.size() > kMaxEntries) {
        QString oldestKey;
        qint64 oldest = std::numeric_limits<qint64>::max();
        for (auto it = entries_.cbegin(); it != entries_.cend(); ++it)
            if (it.value().savedAt < oldest) {
                oldest = it.value().savedAt;
                oldestKey = it.key();
            }
        entries_.remove(oldestKey);
    }
}

void ResumeStore::forget(const QString& uri) {
    entries_.remove(keyFor(uri));
}

} // namespace md::core
