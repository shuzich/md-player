#include "media/router/DiscRouter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <cstring>

namespace md::media::router {

namespace {

constexpr qint64 kSectorSize = 2048;
// SACD 的 Master TOC 固定落在 LSN 510（scarletbook 血统代码里的 START_OF_MASTER_TOC）。
constexpr qint64 kSacdMasterTocLsn = 510;

bool hasChild(const QString& dir, const QString& child) {
    return QFileInfo::exists(dir + QLatin1Char('/') + child);
}

// 碟根判定用的是**结构文件**而非目录名：只有目录在、里面却没有 index.bdmv 的
// 残缺资源相当常见，那种情况不该被当成碟根收下再在打开时报错。
bool isBlurayRoot(const QString& dir) {
    return hasChild(dir, QStringLiteral("BDMV/index.bdmv"));
}

bool isDvdRoot(const QString& dir) {
    return hasChild(dir, QStringLiteral("VIDEO_TS/VIDEO_TS.IFO"));
}

bool isDiscImageSuffix(const QFileInfo& fi) {
    const QString s = fi.suffix().toLower();
    return s == QLatin1String("iso") || s == QLatin1String("img");
}

// 碟内目录，递归到这里就停——碟内还有一堆 STREAM/CLIPINF 之类的子目录，
// 继续下钻既没有意义，在 UHD 原盘上还很慢。
bool isDiscInternalDir(const QString& name) {
    const QString n = name.toUpper();
    return n == QLatin1String("BDMV") || n == QLatin1String("VIDEO_TS") || n == QLatin1String("CERTIFICATE") ||
           n == QLatin1String("AUDIO_TS");
}

// 深度优先收集候选碟根：目录形态的碟根，或者碟镜像文件。
// depth 是「相对拖入目录还要再下几层」，0 表示不再下钻。
void collectCandidates(const QDir& dir, int depth, QStringList& out) {
    const QFileInfoList entries =
        dir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot | QDir::Readable, QDir::Name);
    for (const QFileInfo& fi : entries) {
        if (fi.isFile()) {
            if (isDiscImageSuffix(fi))
                out << fi.absoluteFilePath();
            continue;
        }
        if (!fi.isDir())
            continue;
        if (isDiscInternalDir(fi.fileName()))
            continue;
        const QString sub = fi.absoluteFilePath();
        if (isBlurayRoot(sub) || isDvdRoot(sub)) {
            out << sub; // 命中即收，不再往这条枝里钻
            continue;
        }
        if (depth > 0)
            collectCandidates(QDir(sub), depth - 1, out);
    }
}

// 读一个扇区。越界或读不满都返回空。
QByteArray readSector(QFile& f, qint64 lsn) {
    if (!f.seek(lsn * kSectorSize))
        return {};
    const QByteArray b = f.read(kSectorSize);
    return b.size() == kSectorSize ? b : QByteArray();
}

quint16 le16(const QByteArray& b, int off) {
    return static_cast<quint16>(static_cast<quint8>(b[off])) | static_cast<quint16>(static_cast<quint8>(b[off + 1]))
                                                                   << 8;
}

quint32 le32(const QByteArray& b, int off) {
    quint32 v = 0;
    for (int i = 3; i >= 0; --i)
        v = (v << 8) | static_cast<quint8>(b[off + i]);
    return v;
}

// UDF 的 Anchor Volume Descriptor Pointer，tag identifier == 2。
bool isAvdp(const QByteArray& sector) {
    return sector.size() >= 2 && le16(sector, 0) == 2;
}

} // namespace

bool isSacdImage(const QString& imagePath) {
    QFile f(imagePath);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    if (!f.seek(kSacdMasterTocLsn * kSectorSize))
        return false;
    return f.read(8) == QByteArrayLiteral("SACDMTOC");
}

QString imageTruncationDetail(const QString& imagePath) {
    QFile f(imagePath);
    if (!f.open(QIODevice::ReadOnly))
        return QStringLiteral("打不开镜像文件");
    const qint64 size = f.size();
    if (size < 17 * kSectorSize)
        return QStringLiteral("镜像仅 %1 字节，连卷描述符区都放不下").arg(size);

    // 路线一：ISO9660 主卷描述符。DVD 镜像与 SACD 镜像都有，声明的
    // volume space size 就是整卷的逻辑块数，和文件大小一比即知有没有被截断。
    const QByteArray pvd = readSector(f, 16);
    if (pvd.size() == kSectorSize && static_cast<quint8>(pvd[0]) == 1 && pvd.mid(1, 5) == QByteArrayLiteral("CD001")) {
        const qint64 blocks = le32(pvd, 80);
        const qint64 blockSize = le16(pvd, 128);
        if (blocks > 0 && blockSize > 0) {
            const qint64 declared = blocks * blockSize;
            if (size < declared)
                return QStringLiteral("ISO9660 声明 %1 字节（%2 块 × %3），文件只有 %4 字节")
                    .arg(declared)
                    .arg(blocks)
                    .arg(blockSize)
                    .arg(size);
            return {};
        }
    }

    // 路线二：纯 UDF 镜像（蓝光原盘常见，没有 ISO9660 桥）。UDF 规定
    // 锚点描述符必须同时出现在 LSN 256 与卷末（末扇区或末扇区 - 256）。
    // 头部锚点在、尾部锚点不在，就是被截断了。
    if (isAvdp(readSector(f, 256))) {
        const qint64 last = size / kSectorSize - 1;
        if (isAvdp(readSector(f, last)) || (last >= 256 && isAvdp(readSector(f, last - 256))))
            return {};
        return QStringLiteral("UDF 头部锚点在 LSN 256，卷末（LSN %1 / %2）却没有锚点").arg(last).arg(last - 256);
    }

    // 既没有 ISO9660 主卷描述符，也没有 UDF 头部锚点——不是碟镜像，
    // 交给后面的模块去判，别在这里冒充「截断」结论。
    return {};
}

QString describeCandidates(const QString& base, const QStringList& candidates) {
    Q_UNUSED(base);
    // 只列碟名（末级名字），不列相对路径。套两层同名目录的资源相对路径能有一百多个字符，
    // 四张碟拼出来的提示条比整个窗口还宽，用户反而认不出哪张是哪张。
    QStringList shown;
    for (int i = 0; i < candidates.size() && i < 3; ++i)
        shown << QFileInfo(candidates.at(i)).fileName();
    QString text = shown.join(QStringLiteral("、"));
    if (candidates.size() > shown.size())
        text += QStringLiteral(" 等共 %1 张").arg(candidates.size());
    return text;
}

Route resolve(const QString& inputPath) {
    Route r;
    const QFileInfo fi(inputPath);
    if (!fi.exists() || !fi.isReadable()) {
        r.kind = Kind::NotFound;
        r.target = inputPath;
        r.detail = fi.exists() ? QStringLiteral("存在但不可读") : QStringLiteral("路径不存在");
        return r;
    }

    if (fi.isFile()) {
        r.target = fi.absoluteFilePath();
        if (!isDiscImageSuffix(fi)) {
            r.kind = Kind::Plain;
            return r;
        }
        if (const QString bad = imageTruncationDetail(r.target); !bad.isEmpty()) {
            r.kind = Kind::Truncated;
            r.detail = bad;
            return r;
        }
        r.kind = isSacdImage(r.target) ? Kind::Sacd : Kind::DiscImage;
        return r;
    }

    // 目录。先看它自己是不是碟根，再看是不是被直接拖了 BDMV / VIDEO_TS 本身。
    QString dir = QDir(fi.absoluteFilePath()).absolutePath();
    if (isDiscInternalDir(fi.fileName())) {
        const QString parent = QFileInfo(dir).absolutePath();
        if (isBlurayRoot(parent) || isDvdRoot(parent))
            dir = parent;
    }
    if (isBlurayRoot(dir)) {
        r.kind = Kind::Bluray;
        r.target = dir;
        return r;
    }
    if (isDvdRoot(dir)) {
        r.kind = Kind::Dvd;
        r.target = dir;
        return r;
    }

    QStringList candidates;
    collectCandidates(QDir(dir), kMaxDescendDepth - 1, candidates);
    candidates.removeDuplicates();

    if (candidates.isEmpty()) {
        r.kind = Kind::Plain; // 普通文件夹，走既有文案
        r.target = dir;
        r.detail = QStringLiteral("向下 %1 层未找到碟根").arg(kMaxDescendDepth);
        return r;
    }
    if (candidates.size() > 1) {
        r.kind = Kind::Ambiguous;
        r.target = dir;
        r.candidates = candidates;
        r.detail = QStringLiteral("找到 %1 个候选碟根").arg(candidates.size());
        return r;
    }

    // 恰好一个候选，按它的形态继续判定。
    const QString only = candidates.first();
    r.detail = QStringLiteral("自动下探到 %1").arg(QDir(dir).relativeFilePath(only));
    const QFileInfo onlyInfo(only);
    if (onlyInfo.isDir()) {
        r.kind = isBlurayRoot(only) ? Kind::Bluray : Kind::Dvd;
        r.target = only;
        return r;
    }
    if (const QString bad = imageTruncationDetail(only); !bad.isEmpty()) {
        r.kind = Kind::Truncated;
        r.target = only;
        r.detail = bad;
        return r;
    }
    r.kind = isSacdImage(only) ? Kind::Sacd : Kind::DiscImage;
    r.target = only;
    return r;
}

} // namespace md::media::router
