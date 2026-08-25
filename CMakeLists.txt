cmake_minimum_required(VERSION 3.26)

project(md-player
    VERSION 0.1.0
    DESCRIPTION "MusicDisc Player — 本地实体碟资源统一播放器"
    LANGUAGES CXX)

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
find_package(Qt6 6.7 REQUIRED COMPONENTS Core Gui Quick QuickControls2)

qt_standard_project_setup(REQUIRES 6.7)

qt_add_executable(md-player
    src/app/main.cpp
    src/app/strings.h
)

qt_add_qml_module(md-player
    URI MdPlayer
    VERSION 1.0
    QML_FILES
        src/ui/Main.qml
)

target_include_directories(md-player PRIVATE "${CMAKE_SOURCE_DIR}/src")

target_compile_definitions(md-player PRIVATE
    MD_PLAYER_VERSION="${PROJECT_VERSION}"
)

target_link_libraries(md-player PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::QuickControls2
)

# 验收要求 `./build/md-player` 可直接运行：不打成 .app bundle，产物落在 build 根。
set_target_properties(md-player PROPERTIES
    MACOSX_BUNDLE FALSE
    WIN32_EXECUTABLE FALSE
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
