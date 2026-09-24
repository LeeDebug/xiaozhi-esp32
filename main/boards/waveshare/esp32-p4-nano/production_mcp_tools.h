#pragma once
/**
 * @file production_mcp_tools.h
 * @brief 向 MCP 客户端暴露产线控制工具(主水阀/水温/灯光/料塔重量).
 * @note 幂等: 重复调用 `InitializeProductionMcpTools()` 只注册一次.
 * 底层经 `ProductionModbus` 按需读写保持寄存器, 地址未配置(0)时工具抛异常.
 */

// Registers the production-control tools exposed to the device's MCP client.
// This function is idempotent.
void InitializeProductionMcpTools();
