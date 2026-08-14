/***************************************************
* @版权：哈尔滨诺信工大测控技术有限公司烟台分公司
* @文件名：libStandard_pxie.h
* @作者：Shi Ranfei
* @版本：V1.0
* @时间：2024-6-20
* @功能描述：标准windows系统下的PXIe设备DLL模版
* @修改日志：
***************************************************/

/*预处理块*/
#ifndef __LIBSTANDARD_PXIE_HEADER__
#define __LIBSTANDARD_PXIE_HEADER__
#if defined(__cplusplus) || defined(__cplusplus__)
extern "C" {
#endif

#include "visa.h"

// BAR空间定义，填充到ViUInt16 space参数中
#define BAR0_SPACE             	0
#define BAR1_SPACE             	1
#define BAR2_SPACE             	2
#define BAR3_SPACE             	3
#define BAR4_SPACE             	4
#define BAR5_SPACE             	5

// 寄存器列表操作模式
#define REG_READ_MODE						0x55		// 读寄存器模式
#define REG_WRITE_MODE						0xAA		// 写寄存器模式

// 根据提供的PCIE总线号、设备号和功能号，返回设备名称
/****************************************************************
* @函数名：	find_dev_name
* @描述：		根据PCIe设备的总线号、设备号和功能号，获取设备的路径
* @输入参数1：	pcie_bus_num		-	PCIe设备总线号
* @输入参数2：	pcie_device_num  	-	PCIe设备设备号
* @输入参数3：	pcie_func_num 		-	PCIe设备功能号
* @输出参数1：	device_name			-	设备路径
* @返回值：	函数执行正确，返回值大于等于0；执行时产生错误，返回值小于0
****************************************************************/
int find_dev_name(ViUInt32 pcie_bus_num, ViUInt32 pcie_device_num, ViUInt32 pcie_func_num, ViChar *device_name);

// 根据提供的PCIE总线号、设备号和功能号，返回设备名称
/****************************************************************
* @函数名：	find_dev_name
* @描述：		根据PCIe设备的总线号、设备号和功能号，获取设备的厂商ID和设备ID
* @输入参数1：	pcie_bus_num		-	PCIe设备总线号
* @输入参数2：	pcie_device_num  	-	PCIe设备设备号
* @输入参数3：	pcie_func_num 		-	PCIe设备功能号
* @输出参数1：	vendor_id			-	厂商ID
* @输出参数2：	device_id			-	设备ID
* @返回值：	函数执行正确，返回值大于等于0；执行时产生错误，返回值小于0
****************************************************************/
int find_dev_id(ViUInt32 pcie_bus_num, ViUInt32 pcie_device_num, ViUInt32 pcie_func_num, ViUInt32 *vendor_id, ViUInt32 *device_id, ViChar *device_ip);

/****************************************************************
 * @函数名：	HITATCI_PXIe_init
 * @描述：		初始化PXIe模块，获取仪器句柄
 * @输入参数1：	ResourceName		-	仪器的逻辑地址
 * @输入参数2：	IdQuery  	 		-	TRUE：进行仪器识别
										FALSE：不进行仪器识别
 * @输入参数3：	Reset 				-	TRUE：复位仪器
										FALSE：不复位仪器
 * @输出参数1：	viPCM				-	仪器句柄
 * @返回值：	函数执行正确，返回值大于等于0；执行时产生错误，返回值小于0
****************************************************************/
ViStatus _VI_FUNC HITATCI_PXIe_init (ViRsrc ResourceName, ViBoolean IdQuery, ViBoolean Reset, ViSession *viPCM);

/****************************************************************
* @函数名：	PXIe_viOpenByBdf
* @描述：		通过PCIe BDF（总线号/设备号/功能号）打开PXIe设备，获取设备句柄
* @输入参数1：	pcie_bus_num		-	PCIe总线号
* @输入参数2：	pcie_device_num		-	PCIe设备号
* @输入参数3：	pcie_func_num		-	PCIe功能号
* @输出参数1：	vi					-	成功返回的设备句柄
* @返回值：	函数执行正确，返回0；执行时产生错误，返回负值
****************************************************************/
ViStatus _VI_FUNC  PXIe_viOpenByBdf(ViUInt32 pcie_bus_num, ViUInt32 pcie_device_num, ViUInt32 pcie_func_num, ViPSession vi);

/****************************************************************
 * @函数名：	HITATCI_PXIe_Close
 * @描述：		关闭PXIe模块，释放仪器句柄
 * @输入参数1：	vi			-	仪器句柄
 * @返回值：	函数执行正确，返回值大于等于0；执行时产生错误，返回值小于0
 ****************************************************************/
ViStatus _VI_FUNC HITATCI_PXIe_Close (ViSession vi);

/****************************************************************
*HITATCI_PXIe_viIn32 32位读取寄存器
input:
vi：句柄
space：bar空间，0,1,2,3,4,5
offset：偏移量
val32：读取返回值
ViStatus:
函数操作结果
****************************************************************/
ViStatus _VI_FUNC  HITATCI_PXIe_viIn32(ViSession vi, ViUInt16 space, ViBusAddress offset, ViPUInt32 val32);

/****************************************************************
*HITATCI_PXIe_viOut32 32位写入寄存器
input:
vi：句柄
space：bar空间，0,1,2,3,4,5
offset：偏移量
val32：写入数据
ViStatus:
函数操作结果
****************************************************************/
ViStatus _VI_FUNC  HITATCI_PXIe_viOut32(ViSession vi, ViUInt16 space, ViBusAddress offset, ViUInt32  val32);

/****************************************************************
 * @函数名：	HITATCI_PXIe_self_test
 * @描述：		PXI板卡自检功能
 * @输入参数1：	vi			-	仪器句柄
 * @返回值：	函数执行正确，返回值大于等于0；执行时产生错误，返回值小于0
 ****************************************************************/
ViStatus _VI_FUNC HITATCI_PXIe_self_test (ViSession vi);

/****************************************************************
* @函数名：	HITATCI_PXIe_viIn32_dma
* @描述：		从FPGA读取数据
* @输入参数1：     vi：句柄
* @输入参数2：     space：bar空间，0,1,2,3,4,5
* @输入参数3：     offset：偏移量
* @输入参数4：     val32：存储读取序列的值
* @输入参数5：     val32_len：读取的个数
* @返回值：	函数执行正确，返回值大于等于0；执行时产生错误，返回值小于0
****************************************************************/
ViStatus _VI_FUNC  HITATCI_PXIe_viIn32_dma (ViSession vi, ViUInt16 space, ViBusAddress offset, ViPUInt32 val32, ViUInt32 val32_len);

/****************************************************************
* @函数名：	HITATCI_PXIe_viOut32_dma
* @描述：		向FPGA写入数据
* @输入参数1：     vi：句柄
* @输入参数2：     space：bar空间，0,1,2,3,4,5
* @输入参数3：     offset：偏移量
* @输入参数4：     val32：存储读取序列的值
* @输入参数5：     val32_len：读取的个数
* @返回值：	函数执行正确，返回值大于等于0；执行时产生错误，返回值小于0
****************************************************************/
ViStatus _VI_FUNC  HITATCI_PXIe_viOut32_dma (ViSession vi, ViUInt16 space, ViBusAddress offset, ViPUInt32  val32, ViUInt32 val32_len);

/****************************************************************
* @函数名：	HITATCI_PXIe_REG_LIST_OPT
* @描述：		向FPGA写入数据
* @输入参数1：     vi：句柄
* @输入参数2：     ListNum：操作序列的个数
* @输入参数3：     space：bar空间序列，0,1,2,3,4,5
* @输入参数4：     offset：偏移量序列
* @输入参数5：     val32：读取或者写入序列值
* @输入参数6：     OptMode：读取或者写入的序列值
* @输入参数7：     DelayTime：延时函数序列值
* @返回值：	函数执行正确，返回值大于等于0；执行时产生错误，返回值小于0
****************************************************************/
ViStatus _VI_FUNC HITATCI_PXIe_REG_LIST_OPT(ViSession vi, ViUInt32 ListNum, ViUInt16 *space, ViUInt32 *offset, ViUInt32 *val32, ViUInt32 * OptMode, ViUInt32 *DelayTime);

/****************************************************************
*HITATCI_PXIe_viIn32_direct 32位读取寄存器
input:
vi：句柄
space：bar空间，0,1,2,3,4,5
offset：偏移量
val32：读取返回值
val32_len：读取区域长度
ViStatus:
函数操作结果
****************************************************************/
ViStatus _VI_FUNC  HITATCI_PXIe_viIn32_direct(ViSession vi, ViUInt16 space, ViBusAddress offset, ViPUInt32 val32, ViUInt32 val32_len);

/****************************************************************
*HITATCI_PXIe_viOut32_direct 32位写入寄存器
input:
vi：句柄
space：bar空间，0,1,2,3,4,5
offset：偏移量
val32：写入数据
val32_len：读取区域长度
ViStatus:
函数操作结果
****************************************************************/
ViStatus _VI_FUNC  HITATCI_PXIe_viOut32_direct(ViSession vi, ViUInt16 space, ViBusAddress offset, ViPUInt32 val32, ViUInt32 val32_len);

#if defined(__cplusplus) || defined(__cplusplus__)
}
#endif

#endif