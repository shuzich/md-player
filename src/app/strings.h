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
inline constexpr auto kSacdNotSupportedYet = "识别到 SACD 碟，播放支持将在后续版本提供";
inline constexpr auto kNoDiscInFolder = "这个文件夹里没有碟（向下 3 层都没找到 BDMV 或 VIDEO_TS）";

// 蓝光面板
inline constexpr auto kTitlePanelTitle = "标题与章节";
inline constexpr auto kMainTitleBadge = "主标题";
inline constexpr auto kNoChapterNames = "本碟未提供章节名，按编号显示";

} // namespace md::strings
