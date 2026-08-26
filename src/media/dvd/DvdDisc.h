// DVD 结构解析（M1-PLAN T4）。只用 libdvdread 读 IFO，不做任何解密尝试（铁律 #2）。
// 加密盘由本模块**主动**自检 VOB 扇区的 PES 扰码位拦截，不依赖底层库是否恰好解不开（D-012）。
#pragma once

#include <QString>
#include <QVector>

namespace md::media::dvd {

struct StreamInfo {
    QString codec;
    QString language;
    int channels = 0;
};

struct ChapterInfo {
    int number = 0; // 1 起
    double startSeconds = 0;
};

struct TitleInfo {
    int titleNumber = 0; // libdvdread 口径，1 起；mpv 的 dvd://N 要减 1（D-021）
    double durationSeconds = 0;
    int angleCount = 1;
    QString videoFormat; // 「NTSC 720x480」
    QVector<StreamInfo> audioStreams;
    QVector<StreamInfo> subtitleStreams;
    QVector<ChapterInfo> chapters;
    bool isMainTitle = false;
};

enum class OpenStatus { Ok, Encrypted, NotDvd, OpenFailed, NoTitles };

struct DiscInfo {
    OpenStatus status = OpenStatus::NotDvd;
    QString rootPath;
    QString discName;
    QVector<TitleInfo> titles;
    int mainTitleIndex = -1;
    QString detail; // 日志用的技术细节，不直接给用户看
};

// 轻量预判：目录里有 VIDEO_TS，或是 .iso/.img 文件。真伪由 open() 定夺。
bool looksLikeDvd(const QString& path);

DiscInfo open(const QString& path);

} // namespace md::media::dvd
