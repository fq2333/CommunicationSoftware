#ifndef PROTOCOL_DATA_H
#define PROTOCOL_DATA_H

#include <cstdint>

#pragma pack(push, 1) // 强制 1 字节对齐
struct RfmImageHeader {
    uint32_t magicNum;      // 0xABCD1234
    uint32_t camId;         // 【新增】：专属的相机 ID 字段
    uint8_t  imgBitDepth;
    uint32_t imgWidth;
    uint32_t imgHeight;
    uint32_t imgDataSize;
    uint8_t  addedType;
    uint32_t addedSize;
};
struct RfmDynamicsHeader {
    uint32_t magicNum;  // 魔法数 (0xD0D0D0D0)
    uint32_t frameId;   // 递增的帧序号
    uint32_t dataCount; // 数据个数 (133)
    uint32_t dataBytes; // 字节数 (133 * 8 = 1064)
};
#pragma pack(pop)

#endif // PROTOCOL_DATA_H
