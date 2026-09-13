# STM32F103C8T6 三导联采集：C / FreeRTOS

第一次读代码请先看 [从零读懂项目](docs/从零读懂项目.md)：按数据流讲解设计、关键函数、任务优先级、补传机制和动手实验。

本版已改为 **C固件 + FreeRTOS任务 + C语言Windows上位机**。保留你接好的 PB6→PA0、PB7→PA1、PB8→PA4 和 PA2/PA3 串口，不需要ADS1293或RC。应用、协议、接收器、测试和波形生成器都是C；Cortex-M启动/上下文切换包含必要汇编，构建和下载脚本使用PowerShell。没有Python运行依赖。

这是三导联**模拟信号**经PWM跳线进入ADC的教学实验，不接人体。ADC采集的是高低电平，256次数字平均重建占空比对应的波形。无RC重建精度受PWM分辨率、ADC时钟同步和模拟建立时间限制，不能把12位ADC位宽写成12位系统精度。

## 现在怎么用

1. 板子保持ST-Link连接及供电；CH340仍是TX→PA3、RX→PA2、GND共地，逻辑电平3.3V。
2. 双击 **start_viewer.cmd**，默认连接COM7；端口变了就在窗口中选择实际端口后点击连接。
3. 上位机显示三条波形，同时显示任务优先级、任务栈余量、唤醒延迟、处理时间、缺包和错误计数。
4. 点击“丢弃接收5秒”可测试缓存补传。采集继续，电脑端故意丢字节，然后按序号请求缺口。

上位机可以直接运行 `build/ecg_viewer.exe`，不需要Python、Qt或安装额外界面库。每次仅一个程序占用COM7；使用测试程序前关闭上位机。

CSV需要连接后开启，断开或重新连接会关闭本次记录。超过3秒未收到有效数据时，窗口显示数据超时；主动丢弃5秒测试期间暂不显示这个提示。

## 任务与优先级

| 任务 | FreeRTOS优先级 | 唤醒条件 | 处理内容 | 静态栈 |
|---|---:|---|---|---:|
| adc | 4 | DMA半满/全满通知，约4ms一次 | 检查DMA缓冲有效性、三路平均、写历史、更新PWM | 256字 / 1024B |
| comm | 2 | 1ms阻塞到期 | 解析请求、实时发送、缺口补传 | 448字 / 1792B |
| monitor | 1 | 500ms周期到期 | 心跳、统计、读取任务栈高水位 | 160字 / 640B |
| IDLE | 0 | 没有任务就绪 | FreeRTOS空闲任务 | 128字 / 512B |

**FreeRTOS任务数字越大优先级越高；NVIC中断数字越小优先级越高。** 两者不要混淆。

| 中断 | NVIC抢占优先级 | 是否调用FreeRTOS |
|---|---:|---|
| USART2 RX | 4 | 否，仅将字节放进单生产者/单消费者环形缓冲 |
| ADC DMA1 Channel1 | 5 | 是，vTaskNotifyGiveFromISR + portYIELD_FROM_ISR |
| USART2 TX DMA1 Channel7 | 6 | 否，标记DMA完成 |
| SysTick / PendSV | 15 | 内核使用 |

`configMAX_SYSCALL_INTERRUPT_PRIORITY = 5 << 4`，4个抢占位。高紧迫度UART RX不能调用RTOS API，但可在普通任务临界区内响应。三个用户任务通过xTaskCreateStatic创建，所有栈与TCB静态分配，禁用动态分配，不链接heap_4。

## 采集链路

```text
200点合成PQRST波形表（250SPS，约75BPM）
       ↓ 三路PWM占空比，Ⅲ=Ⅱ−Ⅰ（去偏置）
TIM4 CH1/2/3 → PB6/PB7/PB8 → 跳线 → PA0/PA1/PA4
       ↓
TIM3 TRGO → ADC1扫描IN0、IN1、IN4 → DMA1 Channel1
       ↓ 半缓冲中断只通知任务
adc任务：256次平均 → 250组/秒 → 1600组RAM历史
       ↓
comm任务：USART2 TX DMA → Windows C接收程序
```

系统时钟72MHz，要求板载8MHz HSE；ADC12MHz，采样7.5周期、转换12.5周期；三路约5us完成。TIM3每1125个72MHz时钟触发一次（64k扫描/秒）。TIM4周期256时钟（281250Hz）。两周期互质可避免固定触发相位，但ADC时钟同步会量化实际采样相位，必须实测。

DMA两个半缓冲各256×3个16位码，共3072B。ISR记录完成半区与代数，发任务通知；任务醒来先检查通知数量、代数、DMA位置，再平均，结束时再检查是否被覆盖。出现源头漏采或过期半区时停止采集并报ADC故障，不把坏数据当正常连续心电。

## 修复了什么

上一裸机版的实板5秒测试最终存在缺口，虽然纯软件测试通过。重构没有沿用“已完整补传”的结论。

本版针对可能挡住串口接收的长临界区改成逐组短临界区，UART RX中断不受普通RTOS临界区屏蔽；接收器改为尽快读取串口积压、收到补传后立即请求下一段。不能仅根据错误计数证明旧版唯一根因，但新链路经过实板验证：5秒丢弃后记录补齐、RX错误为0。具体数据见 `docs/validation.md`。

## 缓存与协议

三路×250SPS×2B=1500B/s。历史1600组，占9600B，可保留6.4秒。每25组一个实时包，100ms周期。通信任务优先实时包，剩余带宽发送补传。接收器一次请求最多25组，100ms未收到则重试。重复包按会话号和采样序号去重，过期区间返回GAP。

第一条STATUS的next作为本次连接的记录起点，不把连接前历史缺失计入本次丢包。重连建立新随机化会话号；它用于区分记录，不是安全认证令牌。CSV按接收顺序追加，补传可能乱序，请按session、seq排序。保存的是ADC平均码，未用生成波形表替代。

协议字节序、帧字段和DIAG统计详见 `docs/design.md`。STATUS每秒发送，DIAG约500ms发送。无外部Flash，因此断电清空历史；超过6.4秒或长时间吞吐不足不能保证完整恢复。

## 代码怎么读

| 文件 | 责任 |
|---|---|
| firmware/FreeRTOSConfig.h | 抢占、静态分配、RTOS和中断优先级 |
| firmware/app.c | 任务创建、三项任务主体、补传调度 |
| firmware/board.c | 时钟、引脚、PWM、ADC/DMA、UART中断 |
| firmware/stm32.h | 本工程用到的寄存器定义 |
| firmware/startup.S / link.ld | 向量表与Flash/RAM布局 |
| common/history.c | ADC平均与历史环形缓冲，不依赖RTOS |
| common/protocol.c | MCU和PC共享的帧格式、CRC与解析器 |
| host/receiver.c | 有界记录窗口、去重、缺包请求 |
| host/serial.c / session.c | Win32串口与一次连接的状态 |
| host/viewer.c | Win32/GDI界面，三路波形和RTOS统计 |
| host/board_test.c | C语言实板5秒断传测试 |
| tests/test_core.c | 本机C协议/缓存/恢复测试 |
| tools/make_wave.c | 可选波形表生成器 |

寄存器驱动保留紧凑写法，业务逻辑用具名函数和分层接口；注释解释中断约束、缓冲所有权和时序原因。

## 构建、烧录与测试

源码仓库不包含build目录中的成品，请先构建。需要Windows PowerShell、GNU Arm Embedded GCC、Windows原生MinGW GCC和OpenOCD。设置环境变量ARM_GCC_BIN、NATIVE_GCC_BIN为对应编译器的bin目录，OPENOCD_ROOT为OpenOCD安装根目录；也可通过脚本参数ArmBin、NativeBin、OpenOcdRoot指定。无需下载FreeRTOS，所需内核源码随包附带。

```powershell
# 同时构建固件、上位机、测试，并运行本机测试
.\tools\build.ps1
# ST-Link下载、校验、复位（会覆盖板上的程序）
.\tools\flash.ps1
# 关闭上位机后，运行16秒实板测试
.\build\board_test.exe COM7
```

也可双击build.cmd、flash_stlink.cmd。换电脑后修改build.ps1的ArmBin/NativeBin参数和flash.ps1的OpenOcdRoot。固件HEX：build/ecg_freertos.hex；BIN烧录地址0x08000000；ELF用于调试。使用ST-Link不需要改PA2/PA3，也不需要改BOOT0到1。

Windows脚本入口使用系统powershell。路径含空格时参数已按数组传递。固件启动与FreeRTOS调度涉及汇编属于处理器要求；不把这些汇编或构建脚本伪称C代码。

## 资源和依赖

链接脚本严格限制64KB Flash / 20KB RAM，预留1KB主中断栈。任务栈单独计入静态内存。本版接近SRAM容量上限，增加任务、队列或大数组前必须重新核算。

FreeRTOS Kernel **V10.3.1**，复制自本机已有STM32Cube工程中间件，所选源文件和许可证原样保留在third_party/FreeRTOS；本版不声称使用最新内核。应用代码与FreeRTOS均有MIT许可，见LICENSE与第三方LICENSE。

参考：[FreeRTOS Cortex-M中断优先级说明](https://www.freertos.org/FreeRTOS_Support_Forum_Archive/March_2018/freertos_Interrupt_Priority_For_Cortex_M_series_configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY_fe512cffj.html)、[ST RM0008](https://www.st.com/resource/en/reference_manual/cd00171190.pdf)。
