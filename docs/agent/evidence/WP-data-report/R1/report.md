# WP-data-report R1 证据

## 接口(实测)
- GET http://120.53.1.196:8002/xiaozhi/data-report-config/current -> 200
  {"code":0,"msg":"success","data":{"enabled":1,"reportInterval":1,"reportDeviceInfo":1,
   "reportLocation":1,"reportContent":[28 keys],...}}
  注: 线上 key(如 mb-mcp_silo1Weight/mb-mcp_envTemperature)不在 docx 152 项目录内,
  按用户选择以 docx 地址映射为准, 未知 key 运行时跳过并告警.
- POST /xiaozhi/data-analysis/upload 以探测体发送 -> 200 {"code":0,"msg":"success"}
- GET /xiaozhi/data-analysis/metrics -> 401 (需登录, 未使用)

## 字段目录校验(脚本)
docx 表格 152 行 / 152 唯一 key; 本地 inc 152 项; missing=[] extra=[] dups=[];
所有地址与 docx「地址」列逐一相等; 32 位字段 22 个; 位字段 10 个(0xA6 bit0..9)。

## 未执行
- idf.py build / clang-format / 硬件联调 (用户要求只改代码)。

## 建议的手工验证
1) `idf.py build` (ESP-IDF v6.0.3)，关注 production_data_report.cc 编译。
2) 上电后串口应出现: "Data report task started"、"Network connected, refresh report config"、
   "SNTP time synchronized"、"Config: enabled=1 interval=... keys=N skipped=M"、
   "Reported K fields, next report in ... s"。
3) 检查服务端「数据预览」是否出现 deviceId(timestamp) 记录，字段 key 与 reportContent 对应。
4) 拔网线 -> "Network disconnected, data report paused"；插回后重新拉配置并补报一次。
