# 三导联 ADC / FreeRTOS 重构

保留原接线、72MHz时钟、64k扫描/秒、256次平均后250SPS、9600字节6.4秒历史。新工程与裸机版分目录，应用代码和测试全部C；Windows端Win32/GDI原生窗口，不依赖Python。构建用PowerShell，启动文件少量ARM汇编，FreeRTOS端口包含必要汇编，不能声称CPU上下文切换也用纯C实现。

任务：采集优先级4，通信2，监测1，Idle0。静态分配任务栈/TCB，不启用RTOS堆。ADC DMA中断(硬件优先级5)只交接半缓冲并FromISR通知采集任务；任务检查代数、DMA位置、时限后平均/写历史/更新PWM。UART RX硬件优先级4，高于MAX_SYSCALL5，不调用RTOS，短ISR写SPSC字节环，避免被RTOS临界区挡住。TX DMA中断优先级6。SysTick/PendSV15。通信任务唯一持有发送缓冲，1ms阻塞轮询，实时数据优先、补传用剩余带宽。监测500ms周期阻塞，采集任务平时阻塞在通知上。统计任务运行次数、通知延迟、处理时间和各任务栈高水位。

公共协议：A55A version1 type8 session32 seq32 count16 size16 payload CRC16，小端。DATA1 size=count*6,最多25组；STATUS2 size20: rate16,channels16,oldest32,next32,adc_faults32,rx_errors32。GAP3 size8 start/end。DIAG4 size48: uptime_ms,acq_runs,comm_runs,monitor_runs,max_wake_us,max_work_us,adc_stack_words,comm_stack_words,monitor_stack_words,adc_priority,comm_priority,monitor_priority（均32位）。READ80无payload，count1..25；HELLO81和BIND82无payload count0。新BIND设置会话，不清除历史；PC以首个STATUS的next作为本次记录起点。

接收端最多缓存2000组窗口，保存CSV去重后按到达次序追加。一次缺口最多25组，重试100ms，成功后尽快安排下段；每次循环先读取完整接收积压再补传。丢弃按钮只丢接收，采集继续。内核源码来自本机STM32Cube项目内FreeRTOS V10.3.1，保留MIT许可，不声称最新版本。

必须完成：本机C协议/缓存/接收器测试，真实ARM构建64KB/20KB，Windows EXE构建；通过ST-Link烧录verify，实际三路数据、任务统计及5秒断传恢复验收。此前裸机实板5秒测试存在950组缺口/8次RX错误，不能沿用软件测试结果冒称实测成功。
