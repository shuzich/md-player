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

// 蓝光面板
inline constexpr auto kTitlePanelTitle = "标题与章节";
inline constexpr auto kMainTitleBadge = "主标题";
inline constexpr auto kNoChapterNames = "本碟未提供章节名，按编号显示";

} // namespace md::strings
