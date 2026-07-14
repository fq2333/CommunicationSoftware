#ifndef HARDWAREREGISTERS_H
#define HARDWAREREGISTERS_H

#include <cstdint> // 使用标准的定长整数类型

// 使用命名空间包裹，防止与其他模块的同名宏/变量冲突
namespace HardwareReg {

    // ==========================================
    // 1. 内存映射地址 (Memory Addresses)
    // ==========================================

    // 板卡 DDR 读写的基地址
    constexpr uint32_t DDR_BASE_ADDR = 0x20000000;

    // ==========================================
    // 2. 常规控制与状态寄存器 (Control & Status)
    // ==========================================

    // 模块通用参数寄存器 / 自检测试寄存器[cite: 1]
    constexpr uint32_t REG_SELF_TEST = 0x00;

    // ==========================================
    // 3. 监控与多路测试寄存器 (Monitor & Multiplex)
    // 根据 main.c 中的注释部分提取[cite: 1]
    // ==========================================

    constexpr uint32_t REG_MONITOR_CTRL_1 = 0x0240;
    constexpr uint32_t REG_MONITOR_CTRL_2 = 0x0242;
    constexpr uint32_t REG_MONITOR_STATUS = 0x0244;

    constexpr uint32_t REG_PERF_MONITOR = 0x1050;

    // ==========================================
    // 4. 常用默认值与基准值 (Default Values)
    // ==========================================

    // REG_SELF_TEST 寄存器测试写入的基准值[cite: 1]
    constexpr uint32_t VAL_SELF_TEST_BASE = 0x146F;

} // namespace HardwareReg

#endif // HARDWAREREGISTERS_H