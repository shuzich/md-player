// sacd_reader 接口的最小声明。
//
// 刻意不使用 vendored 的 sacd_reader.h：那个头 #include <sacd_input.h>，会把 PS3
// 光驱 ioctl、SPU sac_accessor 与 protobuf-over-TCP 那一整条链路拖进来（我们一个
// 文件都没导入）。同时它声明的 sacd_decrypt() / sacd_authenticate() 是 PS3 上读
// 物理 SACD 的鉴权面——**本项目绝不实现，也绝不声明**（铁律 #2 / D-003）。
// 我们只读本地已存在的 ISO 文件，用得到的只有下面这几个。
#ifndef MD_SHIM_SACD_READER_H
#define MD_SHIM_SACD_READER_H

#include <inttypes.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sacd_reader_s sacd_reader_t;

sacd_reader_t* sacd_open(const char* path);
void sacd_close(sacd_reader_t* reader);
// 读 block_count 个 2048 字节扇区到 data，返回实际读到的扇区数。
uint32_t sacd_read_block_raw(sacd_reader_t* reader, uint32_t lb_number, uint32_t block_count, uint8_t* data);
uint32_t sacd_get_total_sectors(sacd_reader_t* reader);

#ifdef __cplusplus
}
#endif

#endif
