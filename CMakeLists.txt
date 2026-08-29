cmake_minimum_required(VERSION 3.26)

project(md-player
    VERSION 0.1.0
    DESCRIPTION "MusicDisc Player — 本地实体碟资源统一播放器"
    # C 是给 helper/sacd-helper 用的：vendored 的 scarletbook 与 DST 解码器都是 C（T6）。
    LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------------------
# 自编译依赖优先（CLAUDE.md 深坑 #3 / M1-PLAN T0）
# third_party/prefix 存在时，其 pkg-config 与 CMake 包路径排在系统之前，
# 保证链接到编入 bd:// / dvd:// 的自编译 libmpv 而非系统版本。
# ---------------------------------------------------------------------------
set(MD_THIRD_PARTY_PREFIX "${CMAKE_SOURCE_DIR}/third_party/prefix"
    CACHE PATH "自编译依赖安装前缀（libmpv 等）")

if(EXISTS "${MD_THIRD_PARTY_PREFIX}")
    list(PREPEND CMAKE_PREFIX_PATH "${MD_THIRD_PARTY_PREFIX}")
    if(DEFINED ENV{PKG_CONFIG_PATH})
        set(ENV{PKG_CONFIG_PATH} "${MD_THIRD_PARTY_PREFIX}/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
    else()
        set(ENV{PKG_CONFIG_PATH} "${MD_THIRD_PARTY_PREFIX}/lib/pkgconfig")
    endif()
    message(STATUS "md-player: 使用自编译依赖前缀 ${MD_THIRD_PARTY_PREFIX}")
else()
    message(STATUS "md-player: 未发现 ${MD_THIRD_PARTY_PREFIX}，将使用系统依赖")
endif()

# ---------------------------------------------------------------------------
# Qt 6
# ---------------------------------------------------------------------------
find_package(Qt6 6.7 REQUIRED COMPONENTS Core Gui Quick QuickControls2 OpenGL)

# libmpv 经 pkg-config 定位。third_party/prefix 已在上方前置到 PKG_CONFIG_PATH，
# 故此处优先命中自编译版本（含 bd:// / dvd://，且不带 libdvdcss）。
find_package(PkgConfig REQUIRED)
pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv>=2.0)
message(STATUS "md-player: libmpv ${MPV_VERSION} <- ${MPV_LINK_LIBRARIES}")

# libbluray：CLAUDE.md 要求 >= 1.4；1.5 才有的章节名与 HDR 字段在 BlurayDisc.cpp 里
# 用 bd_get_version() 做运行时探测，故这里只卡最低版本，不硬性要求 1.5。
pkg_check_modules(BLURAY REQUIRED IMPORTED_TARGET libbluray>=1.4)
pkg_check_modules(DVDREAD REQUIRED IMPORTED_TARGET dvdread)
message(STATUS "md-player: libbluray ${BLURAY_VERSION} <- ${BLURAY_LINK_LIBRARIES}")

qt_standard_project_setup(REQUIRES 6.7)

# QML 源码放在 src/ui/ 子目录，需显式采用新策略，否则 Qt 要求每个子目录自带 qmldir。
qt_policy(SET QTP0004 NEW)

qt_add_executable(md-player
    src/app/main.cpp
    src/app/strings.h
    src/core/PlayerController.cpp
    src/core/PlayerController.h
    src/core/MpvObject.cpp
    src/core/MpvObject.h
    src/core/ResumeStore.cpp
    src/core/ResumeStore.h
    src/media/DiscModel.cpp
    src/media/DiscModel.h
    src/media/Fingerprint.cpp
    src/media/Fingerprint.h
    src/media/router/DiscRouter.cpp
    src/media/router/DiscRouter.h
    src/media/sacd/SacdProbe.cpp
    src/media/sacd/SacdProbe.h
    src/media/sacd/SacdStream.cpp
    src/media/sacd/SacdStream.h
    src/media/sacd/SacdController.cpp
    src/media/sacd/SacdController.h
    src/media/sacd/SacdTypes.h
    src/media/router/RouterController.cpp
    src/media/router/RouterController.h
    src/media/bluray/BlurayDisc.cpp
    src/media/bluray/BlurayDisc.h
    src/media/bluray/BlurayController.cpp
    src/media/dvd/DvdDisc.cpp
    src/media/dvd/DvdController.cpp
    src/media/bluray/BlurayController.h
)

qt_add_qml_module(md-player
    URI MdPlayer
    VERSION 1.0
    QML_FILES
        src/ui/Main.qml
        src/ui/SeekBar.qml
        src/ui/ControlBar.qml
        src/ui/TitlePanel.qml
)

# src 供 "core/PlayerController.h" 这类带目录前缀的引用；
# src/core 供 qmltyperegistrar 生成的 <MpvObject.h> 裸文件名引用。
target_include_directories(md-player PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/src/core"
)

target_compile_definitions(md-player PRIVATE
    MD_PLAYER_VERSION="${PROJECT_VERSION}"
    # 开发期从源码树读取 configs/mpv-baseline.conf；安装形态由 M2 打包时覆盖。
    MD_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

target_link_libraries(md-player PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::OpenGL
    PkgConfig::MPV
    PkgConfig::BLURAY
    PkgConfig::DVDREAD
)

# 验收要求 `./build/md-player` 可直接运行：不打成 .app bundle，产物落在 build 根。
set_target_properties(md-player PROPERTIES
    MACOSX_BUNDLE FALSE
    WIN32_EXECUTABLE FALSE
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

# SACD helper（T6）：独立进程，vendored GPL 代码物理隔离在 helper/ 下（D-006）。
# 与 md-player 无任何链接关系，只通过管道说话。产物同样落在 build 根，便于驱动脚本调用。
add_subdirectory(helper/sacd-helper)
set_target_properties(sacd-helper PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
