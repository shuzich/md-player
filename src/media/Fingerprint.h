// 碟片指纹 v1（docs/ARCHITECTURE.md §碟片指纹 v1）。
// M1 只做两件事：算出来、写进结构化日志。**不做任何网络上行**——
// 上行 MusicDisc 匹配属 P2（CLAUDE.md「与产品侧的关系」）。
//
// 规范一旦发布不可变更：字节拼接方式的逐条定义见 docs/DECISIONS.md D-024，
// 改动等于换指纹，只能新增 v2 并双写。
#pragma once

#include <QString>

namespace md::media {

struct Fingerprint {
    QString type;   // bluray / dvd / sacd
    QString sha256; // 小写十六进制；算不出时为空
    QString label;  // 卷标或专辑名，可能为空
    QString detail; // 算不出来的原因，进日志不进 UI

    bool valid() const { return !sha256.isEmpty(); }
};

// root 可以是目录，也可以是镜像文件——两者都由底层库的虚拟文件系统读取。
Fingerprint computeBluray(const QString& root, const QString& label);
Fingerprint computeDvd(const QString& root, const QString& label);
// SACD 只认镜像。M1 不播放，但指纹可以照算（识别归 T5，播放归 T6）。
Fingerprint computeSacd(const QString& imagePath);

// 打一行 `fingerprint={"type":...,"sha256":...,"label":...}`。
void logFingerprint(const Fingerprint& fp);

} // namespace md::media
