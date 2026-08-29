// 跑一次 helper 的 open，把碟内结构读回来（T6 阶段 2）。
// 只在 GUI 线程用，短命进程，拿完就退——播放期的长连接是 SacdStream 那条路。
#pragma once

#include "media/sacd/SacdTypes.h"

#include <QString>

namespace md::media::sacd {

// helper 可执行文件的位置：与 md-player 同目录。
QString helperPath();

DiscInfo probe(const QString& isoPath);

} // namespace md::media::sacd
