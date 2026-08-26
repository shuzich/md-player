#include "media/dvd/DvdDisc.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <dvdread/dvd_reader.h>
#include <dvdread/ifo_read.h>
#include <dvdread/ifo_types.h>

#include <array>
#include <cstring>

namespace md::media::dvd {

namespace {

// BCD 编码的 dvd_time_t → 秒。frame_u 的高 2 位是帧率标志：1=25fps，3=29.97fps。
double toSeconds(const dvd_time_t& t) {
    const auto bcd = [](uint8_t v) { return ((v & 0xf0) >> 4) * 10 + (v & 0x0f); };
    const double fps = ((t.frame_u & 0xc0) >> 6) == 1 ? 25.0 : 30000.0 / 1001.0;
    return bcd(t.hour) * 3600 + bcd(t.minute) * 60 + bcd(t.second) + bcd(t.frame_u & 0x3f) / fps;
}

QString audioCodec(uint32_t format) {
    switch (format) {
    case 0:
        return QStringLiteral("ac3");
    case 2:
        return QStringLiteral("mpeg1");
    case 3:
        return QStringLiteral("mpeg2");
    case 4:
        return QStringLiteral("lpcm");
    case 6:
        return QStringLiteral("dts");
    default:
        return QStringLiteral("audio%1").arg(format);
    }
}

QString langOf(uint16_t code, uint32_t type) {
    if (type == 0 || code == 0)
        return {};
    const char raw[3] = {char(code >> 8), char(code & 0xff), 0};
    return QString::fromLatin1(raw).trimmed();
}

QString videoFormatOf(const video_attr_t& v) {
    const bool pal = v.video_format != 0;
    int w = 720;
    switch (v.picture_size) {
    case 1:
        w = 704;
        break;
    case 2:
    case 3:
        w = 352;
        break;
    default:
        break;
    }
    const int h = v.picture_size == 3 ? (pal ? 288 : 240) : (pal ? 576 : 480);
    return QStringLiteral("%1 %2x%3").arg(pal ? QStringLiteral("PAL") : QStringLiteral("NTSC")).arg(w).arg(h);
}

struct ScramblingScan {
    bool anyVobOpened = false;
    bool scrambled = false;
    int sampledSectors = 0;
    int pesSectors = 0;
    int scrambledSectors = 0;
    int titleSetsProbed = 0;
};

// CSS 自检：DVD 的 pack 头恒为 14 字节，紧随其后的 PES 头在扇区偏移 0x14 处有 flags 字节，
// 其中 bit5..4 即 PES_scrambling_control，非 0 就是加扰内容。实测确认（M1-PLAN T4「以实测为准」）：
// 三张已解密真盘 8 个 title set 全为 0，自造加扰骨架 40/40 命中。
// 本函数**不解密任何东西**，只读原始扇区看这两个比特。
ScramblingScan scanScrambling(dvd_reader_t* dvd, int titleSets) {
    ScramblingScan scan;
    constexpr int kSamplesPerSet = 32;
    for (int ts = 1; ts <= titleSets && ts <= 4; ++ts) {
        dvd_file_t* f = DVDOpenFile(dvd, ts, DVD_READ_TITLE_VOBS);
        if (!f)
            continue;
        scan.anyVobOpened = true;
        ++scan.titleSetsProbed;
        const ssize_t blocks = DVDFileSize(f);
        std::array<unsigned char, DVD_VIDEO_LB_LEN> buf{};
        for (int k = 0; k < kSamplesPerSet; ++k) {
            const int block = blocks > kSamplesPerSet ? int((blocks / kSamplesPerSet) * k) : 0;
            if (DVDReadBlocks(f, block, 1, buf.data()) != 1)
                break;
            ++scan.sampledSectors;
            // 必须是 pack 头开头的扇区，否则 0x14 的含义无从谈起。
            if (!(buf[0] == 0 && buf[1] == 0 && buf[2] == 1 && buf[3] == 0xBA))
                continue;
            if (!(buf[0x0E] == 0 && buf[0x0F] == 0 && buf[0x10] == 1))
                continue;
            const unsigned char streamId = buf[0x11];
            // 只有 private_stream_1 与音视频 PES 才带 flags 字节；导航包（0xBB/0xBF）跳过。
            if (!(streamId == 0xBD || (streamId >= 0xC0 && streamId <= 0xEF)))
                continue;
            ++scan.pesSectors;
            if ((buf[0x14] & 0x30) != 0)
                ++scan.scrambledSectors;
        }
        DVDCloseFile(f);
    }
    scan.scrambled = scan.scrambledSectors > 0;
    return scan;
}

QString volumeName(dvd_reader_t* dvd) {
    char volume[33] = {0};
    unsigned char volumeSetId[129] = {0};
    if (DVDUDFVolumeInfo(dvd, volume, sizeof volume - 1, volumeSetId, sizeof volumeSetId - 1) == 0) {
        const QString name = QString::fromUtf8(volume).trimmed();
        if (!name.isEmpty())
            return name;
    }
    char vol[33] = {0};
    unsigned char setId[129] = {0};
    if (DVDISOVolumeInfo(dvd, vol, sizeof vol - 1, setId, sizeof setId - 1) == 0)
        return QString::fromLatin1(vol).trimmed();
    return {};
}

// 主标题启发式：与蓝光同口径——时长最长优先，1 秒容差内并列取章节多者。
// DVD 没有 bd_get_main_title() 那样的官方答案，只能靠这个。
int pickMainTitle(const QVector<TitleInfo>& list) {
    int best = -1;
    for (int i = 0; i < list.size(); ++i) {
        if (best < 0) {
            best = i;
            continue;
        }
        const double delta = list[i].durationSeconds - list[best].durationSeconds;
        if (delta > 1.0)
            best = i;
        else if (delta >= -1.0 && list[i].chapters.size() > list[best].chapters.size())
            best = i;
    }
    return best;
}

void fillChapters(TitleInfo& out, const pgc_t* pgc) {
    if (!pgc || !pgc->program_map || !pgc->cell_playback)
        return;
    QVector<double> cellStart(pgc->nr_of_cells, 0.0);
    double running = 0.0;
    for (int c = 0; c < pgc->nr_of_cells; ++c) {
        cellStart[c] = running;
        running += toSeconds(pgc->cell_playback[c].playback_time);
    }
    for (int p = 0; p < pgc->nr_of_programs; ++p) {
        const int cell = pgc->program_map[p] - 1; // program_map 是 1 起的 cell 号
        if (cell < 0 || cell >= pgc->nr_of_cells)
            continue;
        ChapterInfo ch;
        ch.number = p + 1;
        ch.startSeconds = cellStart[cell];
        out.chapters.append(ch);
    }
}

} // namespace

bool looksLikeDvd(const QString& path) {
    const QFileInfo info(path);
    if (info.isDir()) {
        const QDir dir(path);
        for (const QString& name : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
            if (name.compare(QStringLiteral("VIDEO_TS"), Qt::CaseInsensitive) == 0)
                return true;
        // 直接拖 VIDEO_TS 目录本身也认。
        return info.fileName().compare(QStringLiteral("VIDEO_TS"), Qt::CaseInsensitive) == 0;
    }
    const QString suffix = info.suffix().toLower();
    return suffix == QStringLiteral("iso") || suffix == QStringLiteral("img");
}

DiscInfo open(const QString& path) {
    DiscInfo out;
    out.rootPath = path;

    QString target = path;
    const QFileInfo info(path);
    // 拖的是 VIDEO_TS 目录本身时，libdvdread 要的是它的父目录。
    if (info.isDir() && info.fileName().compare(QStringLiteral("VIDEO_TS"), Qt::CaseInsensitive) == 0) {
        target = info.absolutePath();
        out.rootPath = target;
    }
    const bool isImage = !info.isDir();

    const QByteArray raw = QFile::encodeName(target);
    dvd_reader_t* dvd = DVDOpen(raw.constData());
    if (!dvd) {
        // 镜像文件打不开更可能是「压根不是 DVD」（SACD / 数据盘 ISO 都会走到这），
        // 交还给上层继续分派；明确摆着 VIDEO_TS 的目录打不开才是真故障。
        out.status = isImage ? OpenStatus::NotDvd : OpenStatus::OpenFailed;
        out.detail = QStringLiteral("DVDOpen 失败");
        return out;
    }

    ifo_handle_t* vmg = ifoOpen(dvd, 0);
    const int titleSets = (vmg && vmg->vmgi_mat) ? vmg->vmgi_mat->vmg_nr_of_title_sets : 0;

    // 加密判定排在结构解析结论之前（与蓝光侧 D-016 同口径）：加密盘的 IFO 往往读得动，
    // 万一读不动，先报「不是 DVD / 打不开」会把用户引向完全错误的方向。
    const ScramblingScan scan = scanScrambling(dvd, titleSets > 0 ? titleSets : 9);
    if (scan.scrambled) {
        out.status = OpenStatus::Encrypted;
        out.detail = QStringLiteral("PES 扰码位命中: %1/%2 个 PES 扇区加扰（探测 %3 个 title set）")
                         .arg(scan.scrambledSectors)
                         .arg(scan.pesSectors)
                         .arg(scan.titleSetsProbed);
        if (vmg)
            ifoClose(vmg);
        DVDClose(dvd);
        return out;
    }

    if (!vmg || !vmg->tt_srpt) {
        out.status = (isImage && !scan.anyVobOpened) ? OpenStatus::NotDvd : OpenStatus::OpenFailed;
        out.detail = QStringLiteral("VMG 不可用（VOB 可开=%1）").arg(scan.anyVobOpened);
        if (vmg)
            ifoClose(vmg);
        DVDClose(dvd);
        return out;
    }

    // IFO 读得动、VOB 一个也开不了 = 内容缺失（截断的镜像、只拷了 IFO 的目录）。
    // 这种盘如果放行，用户会先看到「已载入 DVD」再看到播放失败，比一开始就报错更糟。
    // 判据用「读到过扇区」而不是「文件打开成功」：截断的镜像里目录项还在，
    // DVDOpenFile 照样成功，真读才会失败。
    if (scan.sampledSectors == 0) {
        out.status = OpenStatus::OpenFailed;
        out.detail = QStringLiteral("VMG 声明 %1 个 title set，但一个 VOB 扇区都读不出来").arg(titleSets);
        ifoClose(vmg);
        DVDClose(dvd);
        return out;
    }

    out.discName = volumeName(dvd);
    if (out.discName.isEmpty())
        out.discName = QFileInfo(out.rootPath).completeBaseName();

    for (int i = 0; i < vmg->tt_srpt->nr_of_srpts; ++i) {
        const title_info_t& ti = vmg->tt_srpt->title[i];
        ifo_handle_t* vts = ifoOpen(dvd, ti.title_set_nr);
        if (!vts || !vts->vts_ptt_srpt || !vts->vts_pgcit || !vts->vtsi_mat) {
            if (vts)
                ifoClose(vts);
            continue;
        }
        if (ti.vts_ttn < 1 || ti.vts_ttn > vts->vts_ptt_srpt->nr_of_srpts) {
            ifoClose(vts);
            continue;
        }
        const int pgcn = vts->vts_ptt_srpt->title[ti.vts_ttn - 1].ptt[0].pgcn;
        if (pgcn < 1 || pgcn > vts->vts_pgcit->nr_of_pgci_srp) {
            ifoClose(vts);
            continue;
        }
        const pgc_t* pgc = vts->vts_pgcit->pgci_srp[pgcn - 1].pgc;
        if (!pgc) {
            ifoClose(vts);
            continue;
        }

        TitleInfo t;
        t.titleNumber = i + 1;
        t.durationSeconds = toSeconds(pgc->playback_time);
        t.angleCount = ti.nr_of_angles;
        t.videoFormat = videoFormatOf(vts->vtsi_mat->vts_video_attr);
        // vtsi_mat 里声明的流数是**该 title set 的上限**，不是这条 title 真正有的流。
        // 张学友那张 DVD 声明 8 音轨 / 32 字幕，mpv 实际只看到 4 音轨 / 0 字幕——
        // 多出来的条目是没启用的占位，attr 里全是垃圾值。真正的可用性写在 PGC 的
        // audio_control / subp_control 最高位上，必须按它过滤（实测对齐 mpv）。
        for (int a = 0; a < vts->vtsi_mat->nr_of_vts_audio_streams && a < 8; ++a) {
            if (!(pgc->audio_control[a] & 0x8000))
                continue;
            const audio_attr_t& aa = vts->vtsi_mat->vts_audio_attr[a];
            StreamInfo s;
            s.codec = audioCodec(aa.audio_format);
            s.language = langOf(aa.lang_code, aa.lang_type);
            s.channels = aa.channels + 1;
            t.audioStreams.append(s);
        }
        for (int sp = 0; sp < vts->vtsi_mat->nr_of_vts_subp_streams && sp < 32; ++sp) {
            if (!(pgc->subp_control[sp] & 0x80000000))
                continue;
            const subp_attr_t& sa = vts->vtsi_mat->vts_subp_attr[sp];
            StreamInfo s;
            s.codec = QStringLiteral("dvd_subtitle");
            s.language = langOf(sa.lang_code, sa.type);
            t.subtitleStreams.append(s);
        }
        fillChapters(t, pgc);
        out.titles.append(t);
        ifoClose(vts);
    }

    const int declaredTitles = vmg->tt_srpt->nr_of_srpts;
    ifoClose(vmg);
    DVDClose(dvd);

    if (out.titles.isEmpty()) {
        out.status = OpenStatus::NoTitles;
        out.detail = QStringLiteral("tt_srpt 声明 %1 条 title，但没有一条能解出 PGC").arg(declaredTitles);
        return out;
    }

    out.mainTitleIndex = pickMainTitle(out.titles);
    if (out.mainTitleIndex >= 0)
        out.titles[out.mainTitleIndex].isMainTitle = true;
    out.status = OpenStatus::Ok;
    return out;
}

} // namespace md::media::dvd
