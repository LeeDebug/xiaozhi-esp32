# 产线 RS485 / Modbus 通信设计与扩展指南

> 适用板卡：`waveshare/esp32-p4-nano`
> 相关源文件（均位于 `main/boards/waveshare/esp32-p4-nano/`）：
> - `config.h` — 引脚与端口定义
> - `production_modbus.h` / `production_modbus.cc` — 485 / Modbus-RTU 主站（底层通信）
> - `production_mcp_tools.h` / `production_mcp_tools.cc` — MCP 工具（业务指令封装）
> - `esp32-p4-nano.cc` — 板卡装配入口（启动顺序）

---

## 1. 结论：485 通信配置在哪些文件？

直接回答：**`production_mcp_tools.cc` 只是「业务/工具封装层」，并不是真正的 485 通信配置地点。**

RS485（Modbus-RTU）通信的完整配置分布在三层：

| 层 | 文件 | 职责 |
|----|------|------|
| **物理引脚层** | `config.h` | 定义用哪一路 UART、TX/RX 接到 RS485 收发器的哪个 GPIO |
| **总线/协议层** | `production_modbus.cc` + `production_modbus.h` | 真正的 485 Modbus-RTU 主站：创建串口、配置 9600-8N1、读/写保持寄存器、本地缓存、互斥锁 |
| **业务指令层** | `production_mcp_tools.cc` + `.h` | 把寄存器操作封装成 MCP 工具（如「开主水阀」「读料塔重量」），暴露给 MCP 客户端 |

所以「要新增一条查询/控制指令」时，你主要在 **`production_mcp_tools.cc`** 里加工具；只有当需要改总线参数（波特率、引脚、从机地址）时才动 `production_modbus.cc` / `config.h`。

---

## 2. 硬件连接与总线参数

来自 `config.h` 与 `production_modbus.cc`：

- 端口：**UART1**（`PRODUCTION_MODBUS_UART_PORT = UART_NUM_1`）
- TX（接 RS485 **DI**）：`GPIO_NUM_20`（`PRODUCTION_MODBUS_UART_TX_PIN`）
- RX（接 RS485 **RO**）：`GPIO_NUM_21`（`PRODUCTION_MODBUS_UART_RX_PIN`）
- 串口参数：**9600 波特率、8 数据位、1 停止位、无校验（8N1）、RTU 模式**
- 响应超时：1000 ms；总线锁超时：1500 ms
- 方向控制：仅接 TX/RX，**依赖 RS485 收发器自动收发切换**，未使用 DE/RE（RTS）引脚（`uart_set_mode` 为普通 UART 模式）。
- 从机地址：板卡启动时在 `esp32-p4-nano.cc` 构造函数中**硬编码为 `0x01`**（`SetDeviceAddress(0x01)`）。地址 0 表示「未配置」，此时所有读写被禁用。

> ⚠️ 若从机地址不是 0x01，需修改 `esp32-p4-nano.cc` 第 437 行的 `SetDeviceAddress(0x01)`，或将来把地址暴露成 MCP 工具 / 启动参数。

---

## 3. `production_modbus.cc`：底层通信做了什么？

`ProductionModbus` 是一个**线程安全的单例**，对外提供按需读写保持寄存器（holding register 0..166）的能力。

### 3.1 启动（`Start()`）
1. 用 `compare_exchange` 保证只启动一次。
2. 创建两个互斥量：`bus_mutex_`（总线锁，读写共用，保证 RTU 请求不重叠）、`cache_mutex_`（缓存锁）。
3. 通过 `mbc_master_create_serial()` 创建 ESP-IDF Modbus 主站，参数写死 9600-8N1-RTU。
4. `uart_set_pin()` 绑定 GPIO20/21；`uart_set_mode()` 设为普通 UART（交给 RS485 芯片自动换向）。
5. `mbc_master_start()` 启动主站。
6. 当前 `kAutomaticPollingEnabled = false`，**为「请求驱动」模式**，不会主动轮询从机（轮询任务 `PollingTask` 保留但未启用）。

### 3.2 读写
- **读** `ReadHoldingRegister` / `ReadHoldingRegisters`：
  - Modbus 功能码 **0x03**（读保持寄存器）。
  - 单次最多读 `kMaximumReadRegisters = 78` 个；起始+数量不能超过 167。
  - 抢占 `bus_mutex_` → `mbc_master_send_request()` → 释放锁。
  - 成功后把值写入本地缓存 `cache_`（含 `valid` 标记、时间戳、`generation` 代系计数）。
- **写** `WriteHoldingRegister`：
  - Modbus 功能码 **0x06**（写单个保持寄存器）。
  - 同样占用 `bus_mutex_`（与读共享，故读写不会重叠）。
  - 写成功后立即更新本地缓存，使后续 `GetCachedRegister()` 可直接命中。

### 3.3 本地缓存
- `StateSnapshot` 持有 167 个寄存器值 + 有效性位 + 时间戳 + 代系号。
- 提供 `GetCachedRegister()`（单值）和 `GetSnapshot()`（全量），供 UI/显示层零总线开销地读取最近一次结果。
- `SetDeviceAddress()` 改变地址时会 `InvalidateCache()` 清空缓存，避免串号。

### 3.4 线程安全要点
- 总线互斥：保证同一时刻只有一条 RTU 请求在飞。
- 缓存互斥：读写缓存时加锁。
- `device_address_`、`started_`、`running_` 用 `std::atomic` 保护。

---

## 4. `production_mcp_tools.cc`：业务指令（你每天打交道的地方）

该文件把上面的 Modbus 能力包装成一组 MCP 工具。入口 `InitializeProductionMcpTools()` 用 `static bool initialized` 保证**幂等**（重复调用只注册一次），并向 `McpServer` 单例逐个 `AddTool(...)`。

### 4.1 工具清单（已注册，共 43 个）

按 `docs/big-herdsman-docs/` 下的指令集与通讯控制逻辑表补齐，覆盖保持寄存器 0x00-0xA6。
完整的工具名/寄存器对照表见 `README.md` 的 *Production-monitoring Modbus* 一节。

| 功能组 | 代表工具 | 寄存器范围 |
|--------|----------|------------|
| 设备信息 | `device.get_info` / `device.address.set` / `modules.get` / `log.get` | 0x00-0x01, 0x6A, 0xA6 |
| 料塔 | `silos.get_info` / `silos.get_statistics` | 0x02-0x31 |
| 水表 | `water_meters.get` | 0x32-0x3A |
| 灯光 | `light.get` / `lights.get` / `light_1..4.open` `.close` / `lights.set_all` / `light_schedule.get` `.set` | 0x3B-0x3E, 0x8D-0x94 |
| 电压电能 | `power.get` | 0x3F-0x46 |
| 系统状态 | `system_status.get` / `daily_mortality.set` | 0x47-0x4D |
| 报警 | `alarms.get` / `alarm_pause.set` | 0x4E-0x66, 0x95 |
| 主水阀/水温 | `main_water_valve.get` `.set` / `water_temperature.get` | 0x6B-0x6C |
| 加药 | `dosing.get` / `dosing.set` | 0x6D |
| 水线冲洗 | `flush.get` / `flush.start` / `flush.stop` / `flush_schedule.set` / `flush_config.set` | 0x6E-0x7A |
| 料线行车 | `feed_cart.get` / `feed_cart.start` / `feed_cart.start_time.set` | 0x7B-0x82 |
| 料线状态 | `feed_lines.get` / `feed_lines.set` / `feed_line_mode.set` / `derailment.get` | 0x83-0x8C |
| 鸡蛋计数 | `eggs.get` | 0x96-0xA5（另读 0x47 计算产蛋率） |

> 新增工具沿用「`AddTool` + `EnsureDeviceAddressIsConfigured` + `*OrThrow` + `cJSON` 返回」模板；
> 连续寄存器用一次 `ReadHoldingRegisters()` 批量读，位域寄存器先读后改再写。
> 协议歧义（0x87/0x88 位序、灯光定时编码、0x4D 时间格式）与 0x10 批量写的限制见 README。

### 4.2 一个工具的标准执行流程

以 `main_water_valve.set` 为例，内部逻辑是「模板方法」：

1. `auto& modbus = ProductionModbus::GetInstance();` 取单例。
2. `EnsureDeviceAddressIsConfigured(modbus);` —— 若从机地址为 0（未配置），直接抛 `"Modbus device address is not configured"` 异常，工具失败。
3. 读取参数（`properties["open"].value<bool>()`），换算成寄存器值。
4. （可选）`ReadRegisterOrThrow(...)` 先读旧值，用于回传 `previous_state`。
5. `WriteRegisterOrThrow(modbus, 寄存器, 值, "操作描述");` —— 底层失败即抛异常。
6. 用 `cJSON_CreateObject()` 组装返回结果（含 `success`、状态名、寄存器号、原始值等）。

### 4.3 文件内的辅助函数（重复利用）

- `EnsureDeviceAddressIsConfigured()`：地址守卫，所有工具的「第一道闸」。
- `ReadRegisterOrThrow()` / `WriteRegisterOrThrow()`：带异常封装的读/写，失败抛出 `"操作: 错误名"`。
- `WaterValveStateName()`：寄存器值 → `"open"/"closed"/"none"/"unknown"` 字符串。
- `CreateLightStatus()`：生成统一的灯光状态 JSON（`channel` / `on` / `brightness_percent`）。
- `SetLightBrightness()`：按 channel 选择 0x3B/0x3C 写亮度。
- `ReadSiloWeight()`：读 2 个寄存器并**小端拼成 int32**（高字在高 16 位、低字在低 16 位）。

文件顶部还以 `constexpr` 集中定义了所有寄存器地址、取值边界、缩放系数，方便统一维护。

---

## 5. 整体调用链（数据流）

```
MCP 客户端（如 AI 助手）
   │  tool: self.production.xxx
   ▼
McpServer::GetInstance().AddTool 回调
   │  InitializeProductionMcpTools() 中注册
   ▼
ProductionModbus 单例（production_modbus.cc）
   │  ReadHoldingRegister / WriteHoldingRegister
   │  抢占 bus_mutex_ → mbc_master_send_request（功能码 0x03 / 0x06）
   ▼
esp-idf Modbus master → UART1 (9600-8N1) → RS485 收发器 → 从机设备
```

装配入口在 `esp32-p4-nano.cc` 构造函数（上电顺序：I2C → LCD → 触摸 → 摄像头 → 按键 → **Modbus Start + SetDeviceAddress(0x01) + InitializeProductionMcpTools()**）。

---

## 6. 如何扩展新的查询/控制指令

核心原则：**沿用现有模式，不要改底层核心模块**（符合 `AGENTS.md` —— 板级行为只放 board 目录）。

### 6.1 新增「查询（读）指令」

例：新增「读进线电压」寄存器 0x50。

1. **（可选）加寄存器常量**：在 `production_mcp_tools.cc` 顶部
   ```cpp
   constexpr uint16_t kLineVoltageRegister = 0x50;
   constexpr double kLineVoltageScale = 0.1;  // 协议单位换算
   ```
2. **注册工具**：在 `InitializeProductionMcpTools()` 内追加
   ```cpp
   server.AddTool(
       "self.production.line_voltage.get",
       "读取保持寄存器 0x50 的进线电压，协议值按 0.1 V 换算。",
       PropertyList(),
       [](const PropertyList&) -> ReturnValue {
           auto& modbus = ProductionModbus::GetInstance();
           EnsureDeviceAddressIsConfigured(modbus);
           uint16_t raw = ReadRegisterOrThrow(modbus, kLineVoltageRegister,
                                              "Failed to read line voltage");
           cJSON* result = cJSON_CreateObject();
           cJSON_AddBoolToObject(result, "success", true);
           cJSON_AddNumberToObject(result, "voltage_v", raw * kLineVoltageScale);
           cJSON_AddNumberToObject(result, "register", kLineVoltageRegister);
           cJSON_AddNumberToObject(result, "raw_value", raw);
           return result;
       });
   ```
3. 若需组合/换算（如多字、位域），仿照 `ReadSiloWeight()`、`CreateLightStatus()` 写辅助函数。
4. 若一次读多个连续寄存器，直接用 `ReadHoldingRegisters(start, count, buf)`。

### 6.2 新增「控制（写）指令」

例：新增「开关补光灯」寄存器 0x40（1=开、0=关）。

1. **加常量**：
   ```cpp
   constexpr uint16_t kFillLightRegister = 0x40;
   constexpr uint16_t kFillLightOnValue = 1;
   constexpr uint16_t kFillLightOffValue = 0;
   ```
2. **注册工具（带参数校验）**：
   ```cpp
   server.AddTool(
       "self.production.fill_light.set",
       "打开或关闭补光灯。写保持寄存器 0x40，1=开、0=关。",
       PropertyList({Property("on", kPropertyTypeBoolean)}),
       [](const PropertyList& properties) -> ReturnValue {
           auto& modbus = ProductionModbus::GetInstance();
           EnsureDeviceAddressIsConfigured(modbus);
           uint16_t value = properties["on"].value<bool>() ? kFillLightOnValue
                                                            : kFillLightOffValue;
           WriteRegisterOrThrow(modbus, kFillLightRegister, value,
                                "Failed to set fill light");
           cJSON* result = cJSON_CreateObject();
           cJSON_AddBoolToObject(result, "success", true);
           cJSON_AddBoolToObject(result, "on", properties["on"].value<bool>());
           cJSON_AddNumberToObject(result, "register", kFillLightRegister);
           cJSON_AddNumberToObject(result, "raw_value", value);
           return result;
       });
   ```
3. 参数范围校验交给 `Property(kPropertyTypeInteger, min, max)`（如亮度 0-100 已用 `kMinimumBrightness`/`kMaximumBrightness` 约束）。

### 6.3 检查清单与注意事项

- ✅ 工具名统一前缀 `self.production.<模块>.<动作>`，保持已有命名风格。
- ✅ 每个工具开头都调用 `EnsureDeviceAddressIsConfigured()`；底层失败一律走 `*OrThrow` 抛异常（MCP 会转为工具错误返回给客户端）。
- ✅ 寄存器地址范围 0..166（`kRegisterCount = 167`），写/读越界会被 `production_modbus.cc` 拒绝。
- ✅ 当前为**请求驱动**模式，每次调用都是实时的从机请求，没有后台轮询；返回的是「当下」的真实值。
- ⚠️ 若新寄存器属于连续区段（如通用 0..77、告警 78..102、系统 106..166），注意与 `production_modbus.h` 中预留的分区一致，避免与将来轮询逻辑冲突。
- ⚠️ 不要为不同硬件去改已有板子的引脚或寄存器语义（AGENTS.md 要求：新增变体应加独立 board 或 release 变体，而非改同一板子）。
- ⚠️ 改完记得用 `clang-format` 仅格式化本次改动的文件。

---

## 7. 常见风险与未决项

- **从机地址启动时写死 0x01**：`esp32-p4-nano.cc` 仍以 `SetDeviceAddress(0x01)` 初始化；`self.production.device.address.set` 可在运行时改写 0x01 并同步本地主站地址（回读失败会回滚），但仍不支持一条总线上多个从机并发访问。
- **无 DE/RE 控制**：硬件需自带 RS485 自动收发切换；若换用普通 MAX485 这类需 RTS 的芯片，需要改 `production_modbus.cc` 增加 GPIO 方向控制。
- **轮询未启用**：`kAutomaticPollingEnabled = false`。若后续要显示屏常驻刷新状态，可开启 `PollingTask`（已留好框架），届时需注意与按需读写的缓存一致性。
- **功能码 0x10 未实现**：`ProductionModbus` 只提供 0x03/0x06，因此协议文档中的「4 路灯单帧批量总控」由 `lights.set_all` 用 4 次 0x06 顺序写 + 回读模拟，不具原子性。若要求严格单帧总控，需在底层增加 `WriteHoldingRegisters`。
- **写后立即回读**：新增的写工具均先读旧值，多数还会回读确认；返回值中同时给出 `raw_value` 与 `previous_raw_value`，便于发现从机拒绝或钳位的情况。

---

## 8. 一句话总结

> 真正的 485/Modbus 配置在 **`production_modbus.cc`（总线+协议）** 和 **`config.h`（引脚）**；**`production_mcp_tools.cc`** 是把寄存器封装成「可给 AI 调用的业务工具」的地方。要加新指令，就在 `production_mcp_tools.cc` 的 `InitializeProductionMcpTools()` 里按「`AddTool` + `*OrThrow` + `cJSON` 返回」的模板再加一个即可，底层无需改动。
