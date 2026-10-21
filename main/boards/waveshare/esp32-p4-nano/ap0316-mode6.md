# AP-0316 模式 6：独立 I2S 输入，板载 ES8311 输出

本接法针对用户提供的 **ESP32-P4-WIFI6-DEV-KIT 扩展口**，继续使用项目中
`waveshare/esp32-p4-nano` 的板级实现。原有 ES8311 引脚保持不变。
接线依据 AP-0316 手册（2025/07/02，Rev 1.0）第 2、3、5、10、11、24 页，
并核对了[微雪官方开发板原理图](https://files.waveshare.com/wiki/ESP32-P4-WIFI6-DEV-KIT/ESP32-P4-WIFI6-DEV-KIT.pdf)。
不同板卡修订请按 GPIO 标识和信号网络核对焊点，不按排针外观推断引脚编号。

## 音频与时钟

- ES8311 输出：P4 的 **I2S0 TX 主机**，24 kHz、16-bit 单声道 PCM，保持现有播放参数。
- AP-0316 输入：AP 为主机，P4 的 **I2S1 RX 从机**，48 kHz、16-bit Philips I2S。
- 手册给出的 BCLK 为 3.072 MHz，因此每帧为 64 bit，左右时隙各 32 bit。
  RX 配置为 **16-bit 有效数据、32-bit 时隙、WS 宽度 32**，不能按 16-bit 时隙配置。
- 输入单声道 PCM 交给现有 AudioService 重采样至 16 kHz，再用于唤醒、VAD 和上传。
- 两条总线的 BCLK/WS 不相连。AP 输入不需要 P4 输出 MCLK。

```mermaid
flowchart LR
    MIC[驻极体麦克风] --> AP[AP-0316：ENC / AEC]
    AP -->|I2S 主机：48 kHz / BCLK 3.072 MHz| RX[P4 I2S1 从机 RX]
    RX -->|单声道 PCM| AS[AudioService]
    AS -->|24 kHz PCM| TX[P4 I2S0 主机 TX]
    TX --> ES[板载 ES8311 DAC]
    ES --> PA[板载功放 / 喇叭]
    ES -->|功放前模拟音频：隔直 / 幅度匹配| REF[AP LINE_IN]
    REF --> AP
```

## 推荐接线

扩展口方位按用户附件的正面方向描述，以排针的 **GPIO 标签**为准。
AP 脚号为手册的邮票半孔脚号，J1/J3 则为 AP 自身的插线端子。

| AP-0316 端口 | P4 开发板连接 | 方向 / 作用 |
| --- | --- | --- |
| 4：5V，或 J1-1 | 扩展口 5V | P4 电源到 AP；使用一路供电即可 |
| 1：GND，或 J1-4/5 | 扩展口 GND | 共地；I2S 信号线旁伴随地线 |
| 5：LRCLK | **GPIO5**，附件右列 GPIO5 | AP 输出 → P4 输入，48 kHz |
| 6：BCLK | **GPIO4**，附件右列 GPIO4 | AP 输出 → P4 输入，3.072 MHz |
| 7：D_OUT | **GPIO2**，附件左列 GPIO2 | AP 处理后 PCM → P4 DIN |
| 20：LINE_IN | ES8311 到功放之间的模拟参考信号，见下节 | 播放回声消除参考，不能用 GPIO 代替 |
| 15/16：MIC-/MIC+，或 J3-1/J3-2 | 驻极体麦克风负/正端 | 麦克风接 AP，板载麦克风不参与采集 |
| 18/19：T1/T2 | 初次调试均悬空 | 内部上拉，默认中距离参数 |

GPIO2/4/5 避开当前 ES8311（GPIO9-13、PA GPIO53）、I2C（GPIO7/8）、
Modbus（GPIO20/21）、Wi-Fi SDIO（GPIO14-19、复位 GPIO54）及调试 UART。
GPIO3 在官方原理图中另有外设连接，本方案不使用它。

AP 使用 5V 供电，手册规定范围为 4.0-5.25V。其 **12 脚 3V3 是输出**，
本方案悬空，不能把它当作模块供电输入。P4 的 I2S GPIO 使用 3.3V 电平；
手册未单列 I2S 电平指标，上线前应确认所购 AP 固件/硬件的 BCLK、WS、D_OUT
为 3.3V 逻辑，若为 5V 则先做电平转换。

I2S 三根线尽量短（建议 10 cm 内），BCLK 配邻近地线。AP 与 P4 必须共地。
AP 的 USB D+/D-、D_IN（8 脚）、PDM CLK/DAT、AOUT1/AOUT2 和 SPK 输出均不接。
模式 6 保留模块原有的 **板载 R1**；拆除板载 R1 是模式 7 的数字参考输入改法。
T1/T2 只选择增益/拾音距离，并不选择模式 6。

## 模拟 AEC 参考信号

建议采用手册第 11 页下图的“功放前取参考”方式。微雪官方原理图中的
`DAC_AOUTLP` 网络位于 ES8311 与板载功放之间，可在 **C126 的 DAC_AOUTLP 侧**
取信号，先核对实际 PCB 修订和网络连通性。

推荐按手册第 10 页预留以下电路：

```text
DAC_AOUTLP ── Cext 1 µF（隔直） ── Rext 2 kΩ ──┬── AP-0316 20 脚 LINE_IN
                                              │
                                        Rdiv（按需分压）
                                              │
                                             GND
P4 GND ───────────────────────────────────────── AP GND
```

上述 Rext 是外接串联电阻，与 AP 板上的 R1 不同。手册给出 LINE_IN 输入阻抗
30 kΩ、最大输入 1 Vrms。先测量最大计划播放音量下 LINE_IN 的幅度；若超限，
增加 Rdiv/调整串联电阻，使参考信号不削顶。电容推荐无极性型号。
不能把 ES8311 的 OUTN 或板载喇叭负端接地，也不要把功放差分输出直接接 LINE_IN：
本方案取的是功放前的音频支路，功放后取样需要单独设计衰减与滤波。

只有 I2S 三线而没有 LINE_IN 参考时，AP 可以传回麦克风音频，但不能据此认定
播放回声消除已正常工作。AP 自己的 SPK/J2 不接喇叭；喇叭仍接 P4 原有输出。

## 固件选择

直接修改此开发板的 `config.h`：

```c
#define AUDIO_DEVICE AUDIO_DEVICE_AP0316_ES8311
```

另外两种取值为 `AUDIO_DEVICE_USB_UAC`（默认）和 `AUDIO_DEVICE_ES8311`。
`AUDIO_USE_USB_UAC`/`AUDIO_USE_AP0316` 由 `AUDIO_DEVICE` 派生，不直接修改。
音频设备和时隙配置均在 `config.h` 中，不需要 menuconfig。

正式构建使用独立变体，按实际芯片修订选其一：

```sh
python3 scripts/build.py waveshare/esp32-p4-nano --name esp32-p4-nano-10.1-a-ap0316
python3 scripts/build.py waveshare/esp32-p4-nano --name esp32-p4x-nano-10.1-a-ap0316
```

两者继承原变体的芯片修订限制、屏幕选项及项目默认 flash/partition 配置，
变体名用于区分 OTA 硬件接法。先在 `config.h` 选择 AP-0316，再使用对应的
`-ap0316` 发布名；发布名不覆盖头文件配置，也不会自动修改音频选择。

手册未说明模式 6 的处理后音频究竟出现在左时隙还是右时隙。固件默认只采集
**左时隙（WS 低）**，不会盲目混合左右数据。若实测模块固件把目标音频放在右时隙，
将 `config.h` 中的 `AP0316_INPUT_RIGHT_SLOT` 从 `0` 改为 `1`。
用双时隙录波确认后再决定。

输入设置 `input_reference=false`：上行只有 AP 处理后的麦克风通道，参考已在 AP
内部经 LINE_IN 采样。现有 AFE 可继续做唤醒/VAD，不会为此输入创建软件 AEC 参考通道。
AP 的 I2S 输出取决于模块自身固件；ESP32 端不通过 I2C 或 USB 切换 AP 工作模式。

## 上板验证

1. 先检查电源、电平与共地，确认 AP 的 WS=48 kHz、BCLK=3.072 MHz，且 P4 两脚为输入。
2. 确认启动日志显示 `AP-0316 I2S1 slave RX 48 kHz`，并核对左右时隙选择。
   出现 `Incomplete AP-0316 input` 时检查供电和三根 I2S 线；缺时钟只返回采集失败，
   不会把半帧/旧数据送入语音引擎。
3. 确认 AP 麦克风采集、音量柱、唤醒和上传正常；断开板载麦克风不应影响 AP 采集。
4. 检查 ES8311 启动音、对话播放、音量调节和中断；输入为 48 kHz、输出为 24 kHz，
   播放速度和音调应正常。输入与输出使用独立锁和有界 I/O。
5. 确认输出空闲关闭、再次播放、AP 断电再上电后均可恢复，且关闭输出不会停止 AP 输入。
6. 接好 LINE_IN，在计划使用的播放音量下验证双讲、回声抑制与说话打断，检查参考不削顶。
   AP 录音有声、唤醒成功或固件编译成功均不能代替这一项声学验证。

## 语音打断

本板 `config.h` 中 `AP0316_HAS_HARDWARE_AEC=1` 与
`AUDIO_VOICE_INTERRUPTION_ENABLED=1` 默认启用 AP 硬件 AEC 和本地 VAD 打断。
连续人声确认时间默认 200 ms，播放起始保护默认 300 ms，均在该文件调整。
TTS stop 与最后音频包之后保留默认 180 ms 的有限静默宽限，且播放队列排空后再回到监听，
避免 MQTT/UDP 尾包稍晚到达时过早结束播放；对应 `AUDIO_VOICE_INTERRUPTION_PLAYBACK_DRAIN_MS`。
LINE_IN 播放参考必须接好，否则小智自身声音可能被误判为用户打断。
无需启用 `CONFIG_USE_DEVICE_AEC`，AP 输入仍为无独立参考通道的单声道。

说话打断时保持 AP I2S1 输入运行，只静音 ES8311 的 PA、取消旧播放代，
新回答开始前在输出任务清空 I2S0 TX DMA。服务端需要支持 realtime 监听与 abort。
请实测只播放不说话、同时说话、连续多次打断、首音节完整性及下一回答恢复。
切回普通板载输入输出使用 `AUDIO_DEVICE_ES8311`；若没有可用硬件 AEC，
应关闭对应硬件 AEC 声明。更多 USB 模式1配置及参数见 [README](README.md)。
