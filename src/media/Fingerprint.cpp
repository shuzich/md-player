#include "media/Fingerprint.h"

#include <QCryptographicHash>
#include <QDebug>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>

#include <dvdread/dvd_reader.h>
#include <dvdread/ifo_read.h>
#include <dvdread/ifo_types.h>
#include <libbluray/bluray.h>
#include <libbluray/filesystem.h>

#include <algorithm>
#include <cstdlib>

namespace md::media {

namespace {

constexpr qint64 kSectorSize = 2048;
// scarletbook 血统代码里的 START_OF_MASTER_TOC / MASTER_TOC_LEN。
constexpr qint64 kSacdMasterTocLsn = 510;
constexpr qint64 kSacdMasterTocSectors = 10;

QString hex(const QByteArray& raw) {
    return QString::fromLatin1(QCryptographicHash::hash(raw, QCryptographicHash::Sha256).toHex());
}

QByteArray rawSha256(const QByteArray& data) {
    return QCryptographicHash::hash(data, QCryptographicHash::Sha256);
}

// 每条 mpls 的贡献：文件名（UTF-8，碟内原样）∥ 大小（十进制 ASCII）∥ sha256 全文（32 字节裸值）。
// 三段之间不插分隔符——名字与大小的长度自身可变，但顺序固定且大小紧跟名字之后，
// 拼法只要钉死就不会歧义（D-024）。
QByteArray mplsChunk(const QByteArray& name, qint64 size, const QByteArray& content) {
    QByteArray out = name;
    out += QByteArray::number(size);
    out += rawSha256(content);
    return out;
}

struct BdHandle {
    BLURAY* bd = nullptr;
    explicit BdHandle(const QString& root) { bd = bd_open(root.toUtf8().constData(), nullptr); }
    ~BdHandle() {
        if (bd)
            bd_close(bd);
    }
    BdHandle(const BdHandle&) = delete;
    BdHandle& operator=(const BdHandle&) = delete;
};

bool readBdFile(BLURAY* bd, const char* path, QByteArray& out) {
    void* data = nullptr;
    int64_t size = 0;
    if (!bd_read_file(bd, path, &data, &size) || !data)
        return false;
    out = QByteArray(static_cast<const char*>(data), static_cast<int>(size));
    free(data);
    return true;
}

QStringList listMpls(BLURAY* bd) {
    QStringList names;
    BD_DIR_H* dir = bd_open_dir(bd, "BDMV/PLAYLIST");
    if (!dir)
        return names;
    BD_DIRENT entry{};
    while (dir->read(dir, &entry) == 0) {
        const QString name = QString::fromUtf8(entry.d_name);
        if (name.endsWith(QLatin1String(".mpls"), Qt::CaseInsensitive))
            names << name;
    }
    dir->close(dir);
    names.sort(); // 「按文件名排序」——碟内目录顺序不保证稳定，必须自己排
    return names;
}

QByteArray readDvdIfo(dvd_reader_t* dvd, int titleSet) {
    dvd_file_t* f = DVDOpenFile(dvd, titleSet, DVD_READ_INFO_FILE);
    if (!f)
        return {};
    const ssize_t blocks = DVDFileSize(f);
    QByteArray out;
    if (blocks > 0) {
        out.resize(static_cast<int>(blocks * kSectorSize));
        const ssize_t got = DVDReadBytes(f, out.data(), static_cast<size_t>(out.size()));
        out.truncate(got > 0 ? static_cast<int>(got) : 0);
    }
    DVDCloseFile(f);
    return out;
}

} // namespace

Fingerprint computeBluray(const QString& root, const QString& label) {
    Fingerprint fp;
    fp.type = QStringLiteral("bluray");
    fp.label = label;

    BdHandle h(root);
    if (!h.bd) {
        fp.detail = QStringLiteral("bd_open 失败");
        return fp;
    }

    QByteArray blob;
    QByteArray index;
    if (!readBdFile(h.bd, "BDMV/index.bdmv", index)) {
        fp.detail = QStringLiteral("读不到 index.bdmv");
        return fp;
    }
    blob += index;

    QByteArray movieObject;
    // MovieObject.bdmv 理论上必有，但纯 BD-J 碟上见过缺的，缺了就按空串参与拼接
    // ——空与不存在在这里等价，不影响同一张碟的可重复性。
    if (readBdFile(h.bd, "BDMV/MovieObject.bdmv", movieObject))
        blob += movieObject;

    int counted = 0;
    for (const QString& name : listMpls(h.bd)) {
        QByteArray content;
        const QByteArray path = QStringLiteral("BDMV/PLAYLIST/%1").arg(name).toUtf8();
        if (!readBdFile(h.bd, path.constData(), content))
            continue;
        blob += mplsChunk(name.toUtf8(), content.size(), content);
        ++counted;
    }
    if (counted == 0) {
        fp.detail = QStringLiteral("PLAYLIST 目录下没读到 mpls");
        return fp;
    }

    fp.sha256 = hex(blob);
    fp.detail =
        QStringLiteral("index=%1B movobj=%2B mpls=%3 条").arg(index.size()).arg(movieObject.size()).arg(counted);
    return fp;
}

Fingerprint computeDvd(const QString& root, const QString& label) {
    Fingerprint fp;
    fp.type = QStringLiteral("dvd");
    fp.label = label;

    dvd_reader_t* dvd = DVDOpen(root.toUtf8().constData());
    if (!dvd) {
        fp.detail = QStringLiteral("DVDOpen 失败");
        return fp;
    }

    QByteArray blob;
    const QByteArray vmg = readDvdIfo(dvd, 0);
    if (vmg.isEmpty()) {
        DVDClose(dvd);
        fp.detail = QStringLiteral("读不到 VIDEO_TS.IFO");
        return fp;
    }
    blob += vmg;

    // title set 数只能从解析过的 VMG 拿，别去猜 1..99——碟上不存在的编号
    // DVDOpenFile 未必干净地失败。
    int titleSets = 0;
    if (ifo_handle_t* ifo = ifoOpen(dvd, 0)) {
        if (ifo->vmgi_mat)
            titleSets = ifo->vmgi_mat->vmg_nr_of_title_sets;
        ifoClose(ifo);
    }

    int counted = 0;
    for (int ts = 1; ts <= titleSets; ++ts) {
        const QByteArray ifo = readDvdIfo(dvd, ts);
        if (ifo.isEmpty())
            continue;
        blob += rawSha256(ifo); // 「每个 VTS_xx_0.IFO 的 sha256」——顺序即 title set 编号顺序
        ++counted;
    }
    DVDClose(dvd);

    fp.sha256 = hex(blob);
    fp.detail = QStringLiteral("vmg=%1B vts=%2/%3 条").arg(vmg.size()).arg(counted).arg(titleSets);
    return fp;
}

Fingerprint computeSacd(const QString& imagePath) {
    Fingerprint fp;
    fp.type = QStringLiteral("sacd");

    QFile f(imagePath);
    if (!f.open(QIODevice::ReadOnly)) {
        fp.detail = QStringLiteral("打不开镜像");
        return fp;
    }
    if (!f.seek(kSacdMasterTocLsn * kSectorSize)) {
        fp.detail = QStringLiteral("镜像放不下 Master TOC");
        return fp;
    }
    const QByteArray toc = f.read(kSacdMasterTocSectors * kSectorSize);
    if (toc.size() != kSacdMasterTocSectors * kSectorSize || !toc.startsWith(QByteArrayLiteral("SACDMTOC"))) {
        fp.detail = QStringLiteral("Master TOC 不完整");
        return fp;
    }
    // Master TOC 区共 10 个扇区，SACDText 区块就在其中，按碟上顺序整块参与
    // ——这样不必把 scarletbook 的结构体拉进 M1（那属于 T6）。
    fp.sha256 = hex(toc);

    // 卷标用 ISO9660 主卷描述符的 volume identifier；专辑名要解 SACDText，归 T6。
    if (f.seek(16 * kSectorSize)) {
        const QByteArray pvd = f.read(kSectorSize);
        if (pvd.size() == kSectorSize && pvd.mid(1, 5) == QByteArrayLiteral("CD001"))
            fp.label = QString::fromLatin1(pvd.mid(40, 32)).trimmed();
    }
    fp.detail = QStringLiteral("master toc %1 扇区").arg(kSacdMasterTocSectors);
    return fp;
}

void logFingerprint(const Fingerprint& fp) {
    QJsonObject o;
    o[QStringLiteral("type")] = fp.type;
    o[QStringLiteral("sha256")] = fp.sha256;
    o[QStringLiteral("label")] = fp.label;
    const QByteArray json = QJsonDocument(o).toJson(QJsonDocument::Compact);
    if (fp.valid())
        qInfo("fingerprint=%s | %s", json.constData(), qUtf8Printable(fp.detail));
    else
        qWarning("fingerprint=%s | 算不出指纹: %s", json.constData(), qUtf8Printable(fp.detail));
}

} // namespace md::media
