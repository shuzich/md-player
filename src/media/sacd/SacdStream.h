// 把 helper 接成 mpv 的 sacd:// 协议（ARCHITECTURE §SACD）。
//
// 这里的代码跑在 **mpv 的解复用线程**上，不是 GUI 线程，所以刻意不碰任何 QObject：
// 每个流自己 fork/exec 一个 helper，用裸管道说话，生命周期与流一致。
// 崩溃隔离因此是天然的——helper 死掉只会让这条流报错，播放器本身不受影响。
#pragma once

#include <QString>

struct mpv_handle;

namespace md::media::sacd {

// 注册 sacd:// 协议。整个进程只需调一次。
void registerProtocol(mpv_handle* mpv);

// 登记一张碟，返回可直接交给 mpv 的 URI：sacd://<token>/<area>/<track>。
// token 与 ISO 路径的映射留在进程内，URI 里不出现路径，省得被各层转义规则啃坏。
QString makeUri(const QString& isoPath, int area, int track);

// helper 供流失败（进程死了 / 管道断了）时被置位，取走即清零。
// 解复用线程不能碰 QObject，所以只能留个原子旗标让 GUI 线程来收——
// 否则 helper 一死播放就静默停住，用户看不到任何解释。
bool takeStreamFailure();

} // namespace md::media::sacd
