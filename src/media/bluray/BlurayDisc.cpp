#include "media/bluray/BlurayDisc.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>

#include <libbluray/bluray-version.h>
#include <libbluray/bluray.h>
#include <libbluray/meta_data.h>

#include <algorithm>

// 编译期能力开关。CLAUDE.md 要求 libbluray >= 1.4，1.5 的新字段做「运行时探测」，
// 所以这里既要能对着老头文件编过，也要在新头文件下不误读老运行库的结构体
// ——章节名与 HDR 字段都是**结构体成员**而非函数，运行库版本不够时读它们
// 会越过实际分配的内存，必须用 bd_get_version() 的运行时版本一起把关。
#if defined(BLURAY_VERSION) && BLURAY_VERSION >= BLURAY_VERSION_CODE(1, 5, 0)
#define MD_BD_HEADER_HAS_CHAPTER_NAME 1
#endif
#if defined(BLURAY_VERSION) && BLURAY_VERSION >= BLURAY_VERSION_CODE(1, 3, 0)
#define MD_BD_HEADER_HAS_DYNAMIC_RANGE 1
#endif

namespace md::media::bluray {

namespace {

struct RuntimeCaps {
    int major = 0, minor = 0, micro = 0;
    bool chapterNames = false; // 运行库 >= 1.5.0 才有 BLURAY_TITLE_CHAPTER::chapter_name
    bool dynamicRange = false; // 运行库 >= 1.3.0 才有 BLURAY_STREAM_INFO::dynamic_range_type
};

const RuntimeCaps& caps() {
    static const RuntimeCaps c = [] {
        RuntimeCaps r;
        bd_get_version(&r.major, &r.minor, &r.micro);
        const long code = r.major * 10000L + r.minor * 100L + r.micro;
#ifdef MD_BD_HEADER_HAS_CHAPTER_NAME
        r.chapterNames = code >= 10500L;
#endif
#ifdef MD_BD_HEADER_HAS_DYNAMIC_RANGE
        r.dynamicRange = code >= 10300L;
#endif
        return r;
    }();
    return c;
}

QString codecName(uint8_t codingType) {
    switch (codingType) {
    case BLURAY_STREAM_TYPE_VIDEO_MPEG1:
        return QStringLiteral("mpeg1");
    case BLURAY_STREAM_TYPE_VIDEO_MPEG2:
        return QStringLiteral("mpeg2");
    case BLURAY_STREAM_TYPE_VIDEO_VC1:
        return QStringLiteral("vc1");
    case BLURAY_STREAM_TYPE_VIDEO_H264:
        return QStringLiteral("h264");
    case BLURAY_STREAM_TYPE_VIDEO_HEVC:
        return QStringLiteral("hevc");
    case BLURAY_STREAM_TYPE_AUDIO_MPEG1:
        return QStringLiteral("mp1");
    case BLURAY_STREAM_TYPE_AUDIO_MPEG2:
        return QStringLiteral("mp2");
    case BLURAY_STREAM_TYPE_AUDIO_LPCM:
        return QStringLiteral("LPCM");
    case BLURAY_STREAM_TYPE_AUDIO_AC3:
        return QStringLiteral("AC3");
    case BLURAY_STREAM_TYPE_AUDIO_DTS:
        return QStringLiteral("DTS");
    case BLURAY_STREAM_TYPE_AUDIO_TRUHD:
        return QStringLiteral("TrueHD");
    case BLURAY_STREAM_TYPE_AUDIO_AC3PLUS:
    case BLURAY_STREAM_TYPE_AUDIO_AC3PLUS_SECONDARY:
        return QStringLiteral("E-AC3");
    case BLURAY_STREAM_TYPE_AUDIO_DTSHD:
    case BLURAY_STREAM_TYPE_AUDIO_DTSHD_SECONDARY:
        return QStringLiteral("DTS-HD");
    case BLURAY_STREAM_TYPE_AUDIO_DTSHD_MASTER:
        return QStringLiteral("DTS-HD MA");
    case BLURAY_STREAM_TYPE_SUB_PG:
        return QStringLiteral("PGS");
    case BLURAY_STREAM_TYPE_SUB_IG:
        return QStringLiteral("IGS");
    case BLURAY_STREAM_TYPE_SUB_TEXT:
        return QStringLiteral("TextST");
    default:
        return QString();
    }
}

QString videoFormatName(uint8_t format) {
    switch (format) {
    case BLURAY_VIDEO_FORMAT_480I:
        return QStringLiteral("480i");
    case BLURAY_VIDEO_FORMAT_576I:
        return QStringLiteral("576i");
    case BLURAY_VIDEO_FORMAT_480P:
        return QStringLiteral("480p");
    case BLURAY_VIDEO_FORMAT_1080I:
        return QStringLiteral("1080i");
    case BLURAY_VIDEO_FORMAT_720P:
        return QStringLiteral("720p");
    case BLURAY_VIDEO_FORMAT_1080P:
        return QStringLiteral("1080p");
    case BLURAY_VIDEO_FORMAT_576P:
        return QStringLiteral("576p");
    case BLURAY_VIDEO_FORMAT_2160P:
        return QStringLiteral("2160p");
    default:
        return QString();
    }
}

QString dynamicRangeName(uint8_t type) {
    switch (type) {
    case BLURAY_DYNAMIC_RANGE_SDR:
        return QStringLiteral("SDR");
    case BLURAY_DYNAMIC_RANGE_HDR10:
        return QStringLiteral("HDR10");
    case BLURAY_DYNAMIC_RANGE_DOLBY_VISION:
        return QStringLiteral("Dolby Vision");
    default:
        return QString();
    }
}

QString languageOf(const BLURAY_STREAM_INFO& s) {
    // lang 是定长 4 字节且不保证以 NUL 结尾，按可打印字符截断。
    QString out;
    for (int i = 0; i < 3 && s.lang[i] >= 0x20; ++i)
        out.append(QChar(static_cast<char16_t>(s.lang[i])));
    return out.trimmed();
}

QVector<StreamInfo> collectStreams(const BLURAY_STREAM_INFO* arr, int count) {
    QVector<StreamInfo> out;
    out.reserve(count);
    for (int i = 0; i < count; ++i) {
        StreamInfo si;
        si.codec = codecName(arr[i].coding_type);
        si.language = languageOf(arr[i]);
        out.push_back(si);
    }
    return out;
}

// 主标题启发式（ARCHITECTURE §bluray）：时长最长优先，并列取章节多者。
// 「并列」按 1 秒容差判定——同一内容的不同 playlist 常有毫秒级差异。
int pickMainTitle(const QVector<PlaylistInfo>& list) {
    int best = -1;
    for (int i = 0; i < list.size(); ++i) {
        if (best < 0) {
            best = i;
            continue;
        }
        const double d = list[i].durationSeconds - list[best].durationSeconds;
        if (d > 1.0)
            best = i;
        else if (d >= -1.0 && list[i].chapters.size() > list[best].chapters.size())
            best = i;
    }
    return best;
}

} // namespace

QString runtimeCapabilities() {
    const RuntimeCaps& c = caps();
    return QStringLiteral("libbluray 运行时 %1.%2.%3 / 编译期 %4；章节名=%5 HDR元数据=%6")
        .arg(c.major)
        .arg(c.minor)
        .arg(c.micro)
        .arg(QStringLiteral(BLURAY_VERSION_STRING),
             c.chapterNames ? QStringLiteral("可用") : QStringLiteral("降级为编号"),
             c.dynamicRange ? QStringLiteral("可用") : QStringLiteral("不可用"));
}

bool looksLikeBluray(const QString& path) {
    const QFileInfo info(path);
    if (info.isDir())
        return QFileInfo::exists(QDir(path).filePath(QStringLiteral("BDMV/index.bdmv")));
    if (info.isFile())
        return info.suffix().compare(QStringLiteral("iso"), Qt::CaseInsensitive) == 0;
    return false;
}

DiscInfo open(const QString& path) {
    DiscInfo out;
    out.rootPath = path;

    const QFileInfo info(path);
    if (!info.exists()) {
        out.status = OpenStatus::OpenFailed;
        out.detail = QStringLiteral("路径不存在");
        return out;
    }
    if (info.isDir() && !QFileInfo::exists(QDir(path).filePath(QStringLiteral("BDMV/index.bdmv")))) {
        out.status = OpenStatus::NotBluray;
        out.detail = QStringLiteral("目录内没有 BDMV/index.bdmv");
        return out;
    }

    // 第二参数是 keyfile 路径，永远传 NULL：本项目不做任何密钥逻辑（铁律 #2）。
    BLURAY* bd = bd_open(path.toUtf8().constData(), nullptr);
    if (!bd) {
        out.status = info.isDir() ? OpenStatus::OpenFailed : OpenStatus::NotBluray;
        out.detail = QStringLiteral("bd_open 返回 NULL");
        return out;
    }

    const BLURAY_DISC_INFO* di = bd_get_disc_info(bd);
    if (!di) {
        bd_close(bd);
        out.status = OpenStatus::NotBluray;
        out.detail = QStringLiteral("bd_get_disc_info 返回 NULL");
        return out;
    }

    // CLAUDE.md 深坑 #4：检测到加密且未解开即判加密盘，不做任何绕过尝试。
    // 这一判定**必须排在 bluray_detected 之前**：加密盘一旦有结构没解开，
    // 后续解析可能失败，那时报「不是蓝光」会把用户引向完全错误的方向。
    if ((di->aacs_detected && !di->aacs_handled) || (di->bdplus_detected && !di->bdplus_handled)) {
        out.status = OpenStatus::Encrypted;
        out.detail =
            QStringLiteral("aacs_detected=%1 aacs_handled=%2 aacs_error=%3 bdplus_detected=%4 bdplus_handled=%5")
                .arg(di->aacs_detected)
                .arg(di->aacs_handled)
                .arg(di->aacs_error_code)
                .arg(di->bdplus_detected)
                .arg(di->bdplus_handled);
        bd_close(bd);
        return out;
    }

    if (!di->bluray_detected) {
        bd_close(bd);
        out.status = OpenStatus::NotBluray;
        out.detail = QStringLiteral("bluray_detected=0");
        return out;
    }

    out.bdjDetected = di->bdj_detected != 0;

    // 碟名优先级：META/DL 的 di_name > UDF 卷标 > 文件或目录名。
    if (const struct meta_dl* meta = bd_get_meta(bd); meta && meta->di_name && *meta->di_name)
        out.discName = QString::fromUtf8(meta->di_name).trimmed();
    if (out.discName.isEmpty() && di->udf_volume_id && *di->udf_volume_id)
        out.discName = QString::fromUtf8(di->udf_volume_id).trimmed();
    if (out.discName.isEmpty() && di->disc_name && *di->disc_name)
        out.discName = QString::fromUtf8(di->disc_name).trimmed();
    if (out.discName.isEmpty())
        out.discName = info.completeBaseName();

    // TITLES_RELEVANT 会滤掉重复标题与重复 clip 的混淆用 playlist，
    // 但**不做时长过滤**——演唱会碟常按曲目组分多个短 playlist，全部保留展示
    // （ARCHITECTURE §bluray：始终同时展示完整列表）。
    const uint32_t count = bd_get_titles(bd, TITLES_RELEVANT, 0);
    out.playlists.reserve(static_cast<int>(count));
    for (uint32_t i = 0; i < count; ++i) {
        BLURAY_TITLE_INFO* ti = bd_get_title_info(bd, i, 0);
        if (!ti)
            continue;

        PlaylistInfo p;
        p.playlistId = ti->playlist;
        p.durationSeconds = static_cast<double>(ti->duration) / 90000.0;
        p.angleCount = static_cast<int>(ti->angle_count);

        if (ti->clip_count > 0) {
            const BLURAY_CLIP_INFO& c = ti->clips[0];
            if (c.video_stream_count > 0) {
                const BLURAY_STREAM_INFO& v = c.video_streams[0];
                p.videoCodec = codecName(v.coding_type);
                p.videoFormat = videoFormatName(v.format);
#ifdef MD_BD_HEADER_HAS_DYNAMIC_RANGE
                if (caps().dynamicRange) {
                    p.dynamicRange = dynamicRangeName(v.dynamic_range_type);
                    p.hdrPlus = v.hdr_plus_flag != 0;
                }
#endif
            }
            p.audioStreams = collectStreams(c.audio_streams, c.audio_stream_count);
            p.subtitleStreams = collectStreams(c.pg_streams, c.pg_stream_count);
        }

        p.chapters.reserve(static_cast<int>(ti->chapter_count));
        for (uint32_t k = 0; k < ti->chapter_count; ++k) {
            ChapterInfo ch;
            ch.number = static_cast<int>(k) + 1;
            ch.startSeconds = static_cast<double>(ti->chapters[k].start) / 90000.0;
#ifdef MD_BD_HEADER_HAS_CHAPTER_NAME
            if (caps().chapterNames && ti->chapters[k].chapter_name)
                ch.name = QString::fromUtf8(ti->chapters[k].chapter_name).trimmed();
#endif
            p.chapters.push_back(ch);
        }

        out.playlists.push_back(p);
        bd_free_title_info(ti);
    }

    bd_close(bd);

    if (out.playlists.isEmpty()) {
        out.status = OpenStatus::NoPlaylists;
        out.detail = QStringLiteral("bd_get_titles 返回 %1 条可用 playlist").arg(count);
        return out;
    }

    out.mainTitleIndex = pickMainTitle(out.playlists);
    if (out.mainTitleIndex >= 0)
        out.playlists[out.mainTitleIndex].isMainTitle = true;
    out.status = OpenStatus::Ok;
    return out;
}

} // namespace md::media::bluray
