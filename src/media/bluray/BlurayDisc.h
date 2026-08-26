// libbluray 封装：枚举 playlist / 章节 / 流信息，并做加密检测。
// 只依赖 libbluray 与 Qt 基础类型，不碰 mpv，也不碰 QML——UI 侧见 BlurayController。
// 规格见 docs/ARCHITECTURE.md §src/media/bluray。
#pragma once

#include <QString>
#include <QVector>

#include <cstdint>

namespace md::media::bluray {

// 一条音轨 / 字幕轨的摘要。语言码为 ISO-639-2（碟内原样），可能为空。
struct StreamInfo {
    QString codec;
    QString language;
};

struct ChapterInfo {
    int number = 0;          // 从 1 开始，面向用户
    double startSeconds = 0; // 相对 playlist 起点
    QString name;            // libbluray >= 1.5.0 且碟内有 META TOC 时才有，否则为空
};

struct PlaylistInfo {
    quint32 playlistId = 0; // mpls 编号，即 bd://mpls/<N> 里的 N
    double durationSeconds = 0;
    int angleCount = 1;
    QString videoCodec;   // h264 / hevc / vc1 ...
    QString videoFormat;  // 1080p / 2160p ...
    QString dynamicRange; // SDR / HDR10 / DolbyVision，探测不到时为空
    bool hdrPlus = false; // HDR10+ 标志
    QVector<StreamInfo> audioStreams;
    QVector<StreamInfo> subtitleStreams;
    QVector<ChapterInfo> chapters;
    bool isMainTitle = false;
};

enum class OpenStatus {
    Ok,
    Encrypted,   // AACS / BD+ 检测到但未解开 —— 走「不支持加密原盘」统一文案
    NotBluray,   // 不是蓝光结构
    OpenFailed,  // 是蓝光但打不开（损坏 / 无权限）
    NoPlaylists, // 打开了但一条 playlist 都没枚举到
};

struct DiscInfo {
    OpenStatus status = OpenStatus::OpenFailed;
    QString rootPath; // 传给 mpv --bluray-device 的值（目录或 ISO 路径）
    QString discName; // META di_name > UDF 卷标 > 文件/目录名
    bool bdjDetected = false;
    QVector<PlaylistInfo> playlists;
    int mainTitleIndex = -1; // playlists 的下标；无则 -1
    QString detail;          // 诊断细节，进日志不进 UI
};

// 目录 → 存在 BDMV/index.bdmv；ISO → 后缀为 .iso 时才尝试。
// 这是廉价的预判，真正的判定以 open() 的返回为准。
bool looksLikeBluray(const QString& path);

DiscInfo open(const QString& path);

// 运行时能力探测结果，启动时打一条日志。
QString runtimeCapabilities();

} // namespace md::media::bluray
