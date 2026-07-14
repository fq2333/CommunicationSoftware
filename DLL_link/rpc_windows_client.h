/****************************************************************
 *This file is rpc_windows_client.h
 ****************************************************************/

#ifndef __RPC_WINDOWS_CLIENT_H_
#define __RPC_WINDOWS_CLIENT_H_

#include "visa.h"

#if defined(__cplusplus) || defined(__cplusplus__)
extern "C" {
#endif

// 函数返回值定义
#define OPT_OK				0
#define ERR_WSASTARTUP		-1				// 加载windows的socket库失败
#define ERR_SERVER			-2				// 远端服务器连接失败
#define ERR_MEMMORY			-3				// 内存空间申请失败
#define ERR_COMMUNICATE		-4				// 数据通信失败

// 默认远端IP地址
#define DEFAULT_SERVER_IP	"10.109.3.200"

// FPGA第二个FIFO的深度
#define 	REG_READ_MODE					0x55
#define 	REG_WRITE_MODE					0xAA

// 函数声明
ViStatus _VI_FUNC HITMC_MODULE_init (ViRsrc ResourceName, ViBoolean IdQuery,  ViBoolean Reset, ViSession *vigswc);
ViStatus _VI_FUNC HITMC_MODULE_close(ViSession vi);
ViStatus _VI_FUNC HITMC_SET_MODULE_para (ViSession vi, ViUInt32 RegAddr, ViUInt32 Data);
ViStatus _VI_FUNC HITMC_GET_MODULE_para (ViSession vi, ViUInt32 RegAddr, ViUInt32 *Data);
ViStatus _VI_FUNC HITMC_REG_LIST_OPT (ViSession vi, ViUInt32 ListNum, ViUInt32 *RegAddr, ViUInt32 *Data,  ViUInt32 * OptMode, ViUInt32 *DelayTime);
ViStatus _VI_FUNC HITMC_DMA_SEND(ViSession vi, ViUInt32 RegAddr,  ViUInt32* data_value, ViInt32 data_len);
ViStatus _VI_FUNC HITMC_DMA_BUFFER_LEN(ViSession vi, ViUInt32 RegAddr, ViInt32 *data_len);
ViStatus _VI_FUNC HITMC_DMA_REV(ViSession vi, ViUInt32 RegAddr,  ViInt32* data_value, ViInt32 *data_len);
ViStatus _VI_FUNC HITMC_RAM_SEND(ViSession vi, ViUInt32 RegAddr,  ViUInt32* data_value, ViInt32 data_len);
ViStatus _VI_FUNC HITMC_RAM_REV(ViSession vi, ViUInt32 RegAddr,  ViInt32* data_value, ViInt32 *data_len);

//**********  备注   **********//
/*
*	(1)HITMC_MODULE_init函数，ResourceName变量有三种表示：
*		"10.109.3.200::PXI29::12::INSTR"	这种模式针对于一个DLL对于多个PXI机箱，此时加上IP地址和设备描述符，可以定位到具体的板卡
*		"10.109.3.200";						这种模式，一个PXI机箱只有一个板卡
*		"PXI29::12::INSTR";					只有情况，只有一个PXI机箱
*		
*
*
*/

#if defined(__cplusplus) || defined(__cplusplus__)
}
#endif


#endif