# 本地验证记录

日期：2026-07-23

环境：macOS、PlatformIO Core 6.1.19、ST STM32 platform 19.5.0、Apple Clang

## 已通过

### 主机侧测试

命令：

```sh
sh scripts/run_host_tests.sh
```

结果：`All native firmware tests passed.`

编译参数包含 `-Wall -Wextra -Werror -Wpedantic -Wshadow -Wconversion`，并启用
AddressSanitizer 与 UndefinedBehaviorSanitizer。覆盖内容：

- 去年寻卡和读块 4 命令的逐字节黄金向量；
- XOR 校验、坏校验拒绝、解析器恢复及 `0x7F` 转义；
- UID 响应、块响应、跨块拼接和跳过物理块 7；
- GB2312 合法性检查与非法双字节拒绝；
- 同卡驻留只触发一次、三次无卡后重新允许触发；
- 不同 UID 即使文字相同仍分别触发；
- UID/块响应必须分别为 6/22 字节，拒绝带多余字段的已知响应；
- TTS 队列满时 RFID 事件保留并重试，成功前不提交 UID 去重状态；
- TTS 启动语速命令、包含 `0x00` 的显式长度发送和 ASCII/GB2312 字符计时。

### PlatformIO 目标编译

| 环境 | 结果 | Flash | RAM |
| --- | --- | ---: | ---: |
| `stm32f103c6` | 通过 | 7160 / 32768 B，21.9% | 1124 / 10240 B，11.0% |
| `stm32f103c8` | 通过 | 7172 / 65536 B，10.9% | 1124 / 20480 B，5.5% |
| `stm32f103c6_motor_check` | 通过 | 8368 / 32768 B，25.5% | 1204 / 10240 B，11.8% |

三个环境均完成编译、链接、容量检查和 `firmware.bin` 生成。C6 使用 PlatformIO
官方板定义的 32 KB Flash / 10 KB RAM，没有复用去年错误的 64 KB / 20 KB 链接区。

## 本地无法证明

以下项目必须等当前电路定稿并连接实物后执行：

1. RFID 模块上电波特率确为 9600，响应帧字段与去年模块完全相同。
2. 不同长度 GB2312 卡片在 17-30 mm 高度和实际车速下均能完整读出。
3. TTS 接收 `<S>3` 和 1-64 字节文本后能完整播报，且没有 BUSY 引脚需求。
4. 电机启动、电源跌落和 2 A 限流条件下串口不丢帧、MCU 不复位。
5. `led灯1` 确实接在 `PA8`，低电平极性正确。
6. 电机驱动器对 PA0/PA1 的停止、正转真值表与去年一致。

在完成第 6 项前保持 `APP_ENABLE_MOTOR=0`。首次电机测试必须架空车轮并准备断电。
