// 协议 v1 的线格式（ARCHITECTURE §SACD）。
//   控制面：stdin 一行一条 JSON 请求，stdout 一行一条 JSON 响应
//   数据面：read 的响应是二进制帧 —— 4 字节请求 id + 4 字节负载长度，**小端**
// read 失败时帧头 id 的最高位置 1（错误标志），负载是一行 JSON 错误对象；
// 请求 id 因此必须 < 2^31。
#ifndef MD_SACD_PROTO_H
#define MD_SACD_PROTO_H

#include <stddef.h>
#include <stdint.h>

#define PROTO_ERROR_FLAG 0x80000000u

// 保险闸：把 fd 1 复制到一个私有 fd 供协议独占，再把 stdout 整个指向 stderr。
// 此后任何库函数（包括 vendored 代码）调 printf 都只会打到 stderr，撕不到帧流。
int proto_guard_stdout(void);

void proto_write_json(const char* line, size_t len);
void proto_write_frame(uint32_t id, const uint8_t* payload, uint32_t len);

// base64（标准字母表，带 = 填充）。返回 malloc 的 NUL 结尾字符串。
char* proto_base64(const uint8_t* data, size_t len);
// JSON 字符串转义，只用于 ASCII 安全的字段（路径、错误文案）。
char* proto_json_escape(const char* s);

#endif
