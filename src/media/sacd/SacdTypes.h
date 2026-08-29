// SACD 碟的展示层数据（T6 阶段 2）。helper 只吐字节与编号，转码与降级都在这里。
#pragma once

#include <QString>
#include <QVector>

namespace md::media::sacd {

struct TrackInfo {
    int index = 0; // 1 起
    double seconds = 0.0;
    QString title;     // 已解码；碟上没有则为空
    QString performer; // 同上
};

struct AreaInfo {
    int area = -1; // helper 侧 handle->area[] 的下标
    int channels = 0;
    bool dst = false;
    bool multichannel = false;
    QVector<TrackInfo> tracks;
};

struct DiscInfo {
    bool ok = false;
    QString error;
    QString isoPath;
    QString album;  // 已按降级链定好的显示名
    QString artist; // 碟上没有则为空
    QVector<AreaInfo> areas;
};

// 碟内 character_set_t 编号 → 文本。不认的编码返回空串，由调用方降级。
QString decodeDiscText(const QByteArray& raw, int charset);

} // namespace md::media::sacd
