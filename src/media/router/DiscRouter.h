// 统一资源路由（M1-PLAN T5）：把「用户拖进来的一个路径」判定成四类资源之一，
// 并在必要时向下找出真正的碟根。判定表见 docs/ARCHITECTURE.md §src/media/router。
//
// 本层只做**识别**，不做打开：识别完把 target 交给 BlurayController / DvdController，
// 或直通 mpv。这样路由逻辑可以独立测试，也不必重复两个模块已有的解析代码。
#pragma once

#include <QString>
#include <QStringList>

namespace md::media::router {

enum class Kind {
    Bluray,    // 目录，已确认含 BDMV/index.bdmv
    Dvd,       // 目录，已确认含 VIDEO_TS/VIDEO_TS.IFO
    Sacd,      // 镜像，510*2048 处签名为 SACDMTOC（M1 只识别，播放归 T6）
    DiscImage, // 镜像，完整性通过且不是 SACD——交给蓝光 → DVD 依次尝试
    Plain,     // 普通媒体文件，直通 mpv
    Ambiguous, // 目录下有多个候选碟根，让用户指定，绝不替他猜
    Truncated, // 镜像被截断 / 残缺，结构声明覆盖不到文件末尾
    NotFound,  // 路径不存在或没有读取权限
};

struct Route {
    Kind kind = Kind::Plain;
    QString target;         // 实际要打开的路径（下探后的碟根 / 镜像 / 原路径）
    QStringList candidates; // Ambiguous 时列出全部候选碟根（绝对路径）
    QString detail;         // 诊断细节，进日志不进 UI
};

// 向下找碟根的最大层数。实际资源常在外面套一到两层同名目录，3 层足够，
// 再深就会在大目录上扫出可观的开销，也不像是「拖了一张碟」的意图。
inline constexpr int kMaxDescendDepth = 3;

Route resolve(const QString& inputPath);

// 镜像完整性：只用「结构声明的总扇区数」与「文件实际大小」比对，
// 不抽扇区试读——试读失败在真盘上会误伤（M1-PLAN T5 验收要求）。
// 返回空串表示完整；否则是诊断细节。
QString imageTruncationDetail(const QString& imagePath);

bool isSacdImage(const QString& imagePath);

// 用户可见的候选碟根列表文案（相对拖入目录，最多列前几条）。
QString describeCandidates(const QString& base, const QStringList& candidates);

} // namespace md::media::router
