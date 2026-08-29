#include "media/sacd/SacdProbe.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStringDecoder>

namespace md::media::sacd {
namespace {

// 碟内文本一律由 helper 原样吐字节 + 字符集编号，转码在这里做（D-036）。
// 编号取自 Scarlet Book 的 character_set_t。
const char* codecFor(int charset) {
    switch (charset) {
    case 1: // ISO 646（IRV），等同 ASCII
        return "US-ASCII";
    case 2: // ISO 8859-1
    case 7: // ISO 8859-1 + 转义序列；按 8859-1 读，转义序列极罕见
        return "ISO 8859-1";
    case 3: // Music Shift-JIS（RIS-506）。Qt 认 Shift_JIS，音乐扩展字符会落到替换符
        return "Shift_JIS";
    case 4: // 韩文 KSC 5601
        return "EUC-KR";
    case 5: // 简体中文 GB 2312-80
        return "GB18030";
    case 6: // 繁体中文 Big5
        return "Big5";
    default:
        return nullptr; // 0 = 未使用，8..255 保留
    }
}

} // namespace

QString decodeDiscText(const QByteArray& raw, int charset) {
    if (raw.isEmpty())
        return {};
    const char* codec = codecFor(charset);
    if (!codec)
        return {};
    QStringDecoder dec(codec);
    if (!dec.isValid())
        return {};
    QString out = dec.decode(raw);
    if (dec.hasError())
        return {};
    return out.trimmed();
}

QString helperPath() {
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("sacd-helper"));
}

DiscInfo probe(const QString& isoPath) {
    DiscInfo info;
    info.isoPath = isoPath;

    const QString exe = helperPath();
    if (!QFileInfo::exists(exe)) {
        info.error = QStringLiteral("找不到 sacd-helper（%1）").arg(exe);
        return info;
    }

    QProcess proc;
    proc.setProcessChannelMode(QProcess::ForwardedErrorChannel); // 诊断留在 stderr
    proc.start(exe, {});
    if (!proc.waitForStarted(3000)) {
        info.error = QStringLiteral("sacd-helper 启动失败");
        return info;
    }
    QJsonObject req{
        {QStringLiteral("id"), 1}, {QStringLiteral("cmd"), QStringLiteral("open")}, {QStringLiteral("path"), isoPath}};
    proc.write(QJsonDocument(req).toJson(QJsonDocument::Compact) + '\n');
    proc.write("{\"id\":2,\"cmd\":\"quit\"}\n");

    QByteArray line;
    // 大碟的曲目表能到几十 KB，等够久一点；helper 只读 TOC，不会真的慢。
    while (line.isEmpty() && proc.waitForReadyRead(10000))
        line = proc.readLine();
    proc.waitForFinished(3000);
    if (proc.state() != QProcess::NotRunning)
        proc.kill();

    const QJsonObject res = QJsonDocument::fromJson(line.trimmed()).object();
    if (res.isEmpty() || !res.value(QStringLiteral("ok")).toBool()) {
        info.error = res.value(QStringLiteral("error")).toString(QStringLiteral("sacd-helper 没有给出可用的应答"));
        return info;
    }

    const QJsonObject album = res.value(QStringLiteral("album")).toObject();
    const int albumCharset = album.value(QStringLiteral("charset")).toInt();
    const auto b64 = [&](const QJsonObject& o, const char* key) {
        return QByteArray::fromBase64(o.value(QLatin1String(key)).toString().toLatin1());
    };
    info.album = decodeDiscText(b64(album, "title_b64"), albumCharset);
    if (info.album.isEmpty())
        info.album = decodeDiscText(b64(album, "disc_title_b64"), albumCharset);
    info.artist = decodeDiscText(b64(album, "artist_b64"), albumCharset);

    for (const QJsonValue& av : res.value(QStringLiteral("areas")).toArray()) {
        const QJsonObject ao = av.toObject();
        AreaInfo area;
        area.area = ao.value(QStringLiteral("area")).toInt();
        area.channels = ao.value(QStringLiteral("channels")).toInt();
        area.dst = ao.value(QStringLiteral("dst")).toBool();
        area.multichannel = ao.value(QStringLiteral("kind")).toString() == QLatin1String("multi");
        const int charset = ao.value(QStringLiteral("charset")).toInt();
        for (const QJsonValue& tv : ao.value(QStringLiteral("tracks")).toArray()) {
            const QJsonObject to = tv.toObject();
            TrackInfo t;
            t.index = to.value(QStringLiteral("index")).toInt();
            t.seconds = to.value(QStringLiteral("seconds")).toDouble();
            t.title = decodeDiscText(b64(to, "title_b64"), charset);
            t.performer = decodeDiscText(b64(to, "performer_b64"), charset);
            area.tracks.append(t);
        }
        info.areas.append(area);
    }
    info.ok = !info.areas.isEmpty();
    if (!info.ok)
        info.error = QStringLiteral("这张碟里没有可播放的音频区");
    return info;
}

} // namespace md::media::sacd
