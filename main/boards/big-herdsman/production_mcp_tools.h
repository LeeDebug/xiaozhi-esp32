#pragma once
/**
 * @file production_mcp_tools.h
 * @brief 向 MCP 客户端暴露生产监控终端的全部语音化工具，覆盖保持寄存器 0x00-0xA6：
 * 设备信息与从站地址、料塔称重/加卸料统计、水表、四路灯光与批量总控/定时、
 * 电压电能、系统状态、报警与报警暂停、加药、水线冲洗及计划/参数、料线行车、
 * 料线模式与状态、脱轨报警、鸡蛋计数与产蛋率、模块启用状态。
 * @note 幂等: 重复调用 `InitializeProductionMcpTools()` 只注册一次.
 * 底层经 `ProductionModbus` 按需读写保持寄存器, 地址未配置(0)时工具抛异常.
 */

// Registers the production-control tools exposed to the device's MCP client.
// This function is idempotent.
void InitializeProductionMcpTools();
