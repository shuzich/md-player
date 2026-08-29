# sacd-helper：独立的 C 进程，vendored GPL 代码只存在于本目录（D-006 / D-033）。
# 刻意不链接 Qt、不链接播放器的任何东西——它只认管道。
add_executable(sacd-helper
    src/main.c
    src/proto.c
    src/dsf_view.c
    src/track_reader.c
    src/shim/shim.c
    vendor/scarletbook/scarletbook.c
    vendor/scarletbook/scarletbook_read.c
    vendor/dstdec/ccp_calc.c
    vendor/dstdec/dst_ac.c
    vendor/dstdec/dst_data.c
    vendor/dstdec/dst_fram.c
    vendor/dstdec/dst_init.c
    vendor/dstdec/unpack_dst.c
)

target_include_directories(sacd-helper PRIVATE
    src
    src/shim              # 必须排在 vendor 前面：shim/sacd_reader.h 要盖掉 vendored 那个
    vendor/scarletbook
    vendor/dstdec
)

# 顶层为 md-player 打开了 AUTOMOC，helper 是纯 C，明确关掉。
set_target_properties(sacd-helper PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

# vendored 代码是 2004–2015 年的老 C，警告成吨且不归我们修；只关掉噪音，不关错误。
target_compile_options(sacd-helper PRIVATE
    -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function
    -Wno-sign-compare -Wno-pointer-sign -Wno-format
)
