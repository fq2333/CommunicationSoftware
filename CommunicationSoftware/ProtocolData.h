#ifndef PROTOCOL_DATA_H
#define PROTOCOL_DATA_H

#include <cstdint>

#pragma pack(push, 1) // 强制 1 字节对齐，防止跨平台/跨编译器结构体填充导致的错位
struct RfmImageHeader {
    uint32_t magicNum;    // 校验码 (0xABCD1234)
    uint8_t  imgBitDepth; // 图像位深 (8 或 16)
    uint32_t imgWidth;    // 图像宽 
    uint32_t imgHeight;   // 图像高 
    uint32_t imgDataSize; // 图像裸数据总字节数
    uint8_t  addedType;   // 附加数据类型
    uint32_t addedSize;   // 附加数据长度
};
struct RfmDynamicsHeader {
    uint32_t magicNum;  // 魔法数 (0xD0D0D0D0)
    uint32_t frameId;   // 递增的帧序号
    uint32_t dataCount; // 数据个数 (133)
    uint32_t dataBytes; // 字节数 (133 * 8 = 1064)
};
#pragma pack(pop)

#endif // PROTOCOL_DATA_H
