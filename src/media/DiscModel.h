// 碟类资源在 QML 侧共用的展示层逻辑：时长格式化、短条目过滤、面板相关的持久化设置。
// 蓝光与 DVD 两个 Controller 的 QML 接口是鸭子类型对齐的（同名属性 / 同名方法），
// TitlePanel 因此可以不加区分地驱动任意一种碟——但设置键只能有一份，故集中在这里。
#pragma once

#include <QString>
#include <QVariantList>

namespace md::media {

// 低于此时长的条目视为占位项（UHD 原盘的 5 秒空壳、DVD 的几秒转场 title）。
inline constexpr double kShortTitleSeconds = 10.0;

QString formatDuration(double seconds);

// 只隐藏不删除：入参是完整列表，返回要显示的子集。主标题与当前播放项永不隐藏。
QVariantList filterVisible(const QVariantList& all, bool hideShort, int currentIndex);

bool hideShortTitlesSetting();
void setHideShortTitlesSetting(bool hide);
// 首次返回 true 并落盘，之后恒为 false。
bool takeTitleHintSetting();

} // namespace md::media
