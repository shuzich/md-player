// md-player 用户可见文案统一收口处（CLAUDE.md 铁律 #2 / #3）。
// 所有面向用户的字符串都写在这里，便于统一措辞与后续 i18n。
#pragma once

namespace md::strings {

inline constexpr auto kAppName = "md-player";
inline constexpr auto kOrganizationName = "MusicDisc";
inline constexpr auto kOrganizationDomain = "musicdisc.local";

// 主窗口
inline constexpr auto kWindowTitle = "md-player";
inline constexpr auto kPlaceholderHint = "拖入 BDMV / VIDEO_TS 文件夹，或 BD / DVD / SACD ISO";

// 碟片错误路径。加密盘文案全项目唯一一处（铁律 #2），BD 与 DVD 共用。
inline constexpr auto kEncryptedDisc = "不支持加密原盘，请使用已解密资源";
inline constexpr auto kDiscOpenFailed = "无法读取碟片结构，文件可能损坏或没有读取权限";
inline constexpr auto kDiscNoPlaylists = "碟片结构正常，但没有找到可播放的标题";
inline constexpr auto kBlurayNotDetected = "这不是蓝光结构（缺少 BDMV/index.bdmv）";

// 统一资源路由（T5）
inline constexpr auto kPathNotFound = "找不到这个路径，或者没有读取权限";
inline constexpr auto kDiscImageTruncated = "这个镜像不完整（下载未完成或已损坏），请换一份完整的镜像";
// %1 由调用方填成候选碟根的相对路径列表。绝不替用户挑一个打开。
inline constexpr auto kMultipleDiscRoots = "这个文件夹里有多张碟：%1。请直接拖入其中一张";
// T6 阶段 2 起 SACD 可播；kSacdNotSupportedYet 随之退役（D-027 的提示只在 T5 期间存在）。
inline constexpr auto kSacdOpenFailed = "这张 SACD 读不出来，可能镜像不完整或不是标准 Scarlet Book 结构";
inline constexpr auto kSacdMultichannelNotYet = "多声道区暂不支持播放，请选择立体声区的曲目";
inline constexpr auto kSacdUnnamed = "未命名 SACD";
inline constexpr auto kSacdHelperLost = "SACD 解码进程意外退出，播放已中断";
inline constexpr auto kSacdGainOn = "SACD 增益 +6dB：开";
inline constexpr auto kSacdGainOff = "SACD 增益 +6dB：关";
inline constexpr auto kNoDiscInFolder = "这个文件夹里没有碟（向下 3 层都没找到 BDMV 或 VIDEO_TS）";

// 蓝光面板
inline constexpr auto kTitlePanelTitle = "标题与章节";
inline constexpr auto kMainTitleBadge = "主标题";
inline constexpr auto kNoChapterNames = "本碟未提供章节名，按编号显示";

} // namespace md::strings
