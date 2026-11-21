# SmartBattBox 固件烧录说明

**适用产品**：SmartBattBox 移动储能逆变电源控制板（主控 STM32F407VGT6）
**适用固件**：`SmartBattBox.bin`
**烧录工具**：STMicroelectronics STM32CubeProgrammer（本说明以 **v2.23.0** 为基准，v2.x 各版本界面一致）
**文档版本**：V1.0　　**更新日期**：2026-11-16

---

## 一、准备清单

| 类别 | 项目 | 说明 |
|---|---|---|
| 硬件 | ST-LINK 下载器 | V2 或 V3 均可（需支持 SWD） |
| 硬件 | 4 芯 SWD 排线 | SWDIO / SWCLK / GND / 3V3 |
| 硬件 | 目标板 | 控制板 + 电池（12V 系统） |
| 硬件 | 电脑 | Windows 10 / 11 64 位 |
| 软件 | STM32CubeProgrammer | 官网免费下载，安装时保留默认选项（含 ST-LINK 驱动） |
| 文件 | `SmartBattBox.bin` | 随本说明一同发放的固件文件 |

> 固件文件请勿改名、勿用压缩包内的其他文件（如 `.elf`）。

---

## 二、软件安装

1. 访问 ST 官网下载 STM32CubeProgrammer：
   `https://www.st.com/en/development-tools/stm32cubeprog.html`
2. 安装时保持默认勾选（会自动安装 **ST-LINK 驱动**）。
3. 首次插入 ST-LINK 后，打开「设备管理器」，应能看到：
   - `通用串行总线设备 → STMicroelectronics STLink dongle`
   - 或 `端口 → STMicroelectronics STLink Virtual COM Port`
4. 若出现黄色感叹号，请重新安装驱动或更换 USB 口（建议用主板后置 USB 口）。

---

## 三、接线（SWD 四线）

控制板上标有 **SWD** 的调试排针，按下表对应连接：

| ST-LINK 信号（丝印） | 控制板信号 | MCU 引脚 | 是否必须 |
|---|---|---|---|
| SWDIO | SWDIO | PA13 | **必须** |
| SWCLK | SWCLK | PA14 | **必须** |
| GND | GND | — | **必须** |
| 3V3 / VAPP | 3V3U | — | 建议接（电平参考） |
| NRST / RST | NRST | — | 可选（连接异常时用） |

**接线要点**

- 不同型号 ST-LINK 的排针编号不同，**一律按丝印名称对应**，不要按引脚序号。
- 目标板由电池独立供电时，ST-LINK 的 3V3 只作电平参考，**不要用它给整机供电**（带不动，且可能损坏下载器）。
- 排线尽量短（≤ 20 cm），过长会导致连接不稳定。

---

## 四、烧录步骤（图形界面）

1. **给目标板上电**（接入电池，整机处于通电状态），再把 ST-LINK 插入电脑 USB 口。
2. 打开 **STM32CubeProgrammer**。
3. 在右上角连接栏按以下设置，然后点击 **Connect（连接）**：

   | 项目 | 设置值 |
   |---|---|
   | 接口类型 | **ST-LINK** |
   | Port（端口） | **SWD** |
   | Reset mode（复位方式） | **Software reset**（默认） |
   | Frequency（频率） | 默认（4 MHz） |

4. 连接成功后，右上角会显示芯片型号 **STM32F407VG**，且 `Device Memory` 区域可以读取。
5. 切换到左侧 **「Erasing & Programming」**（擦除与编程）页面。
6. 按下表填写：

   | 界面项 | 填写内容 |
   |---|---|
   | **File path**（文件路径） | 选择 `SmartBattBox.bin` |
   | **Start address**（起始地址） | **0x08000000** |
   | File size | 自动识别（约 0x116C4，71 332 字节） |
   | **Verify programming**（校验） | **勾选** |
   | **Skip flash erase before programming** | **勾选**（跳过擦除，更快） |
   | **Full chip erase**（全片擦除） | **不要勾选** |

7. 点击 **Start Programming（开始编程）**。
8. 等待进度到 **100%**，下方日志出现 `File download complete` 与 `Programmed and verified successfully`。
9. 点击 **Disconnect（断开）**，然后给目标板**断电重启**（拔掉电池再上电）。

---

## 五、烧录成功的确认方法

**方法一：看屏幕**
上电后约 1~2 秒，屏幕电量区域会先空白，随后显示真实电量数字（如 `90`）与电量条，说明固件已正常运行。

**方法二：看串口日志（推荐）**
用 USB 转 TTL 连接调试串口（UART1，**115200 8N1**），上电应打印：

```
=== SmartBattBox boot ===
clk: 168000000 Hz
tim4: ok
boardio: all drivers init
bsp init done, starting RTOS...
=== System ready, tasks running ===
```

若能看到以上内容，说明固件烧录正确、系统已启动。

---

## 六、重要注意事项

1. **起始地址必须是 `0x08000000`**。填错（例如漏掉 `0x08`）会导致烧录"成功"但板子不启动。
2. **文件类型必须选 `.bin`**，不要选 `.elf`、`.hex` 或压缩包。
3. **不要勾选「Full chip erase」**：
   - `0x080E0000` 起 128 KB 为**参数区**（电池学习参数、标定值等），全片擦除会清除。
   - 本机型在外部 Flash 中另有参数备份，全片擦除后会自动恢复，但仍建议避免。
4. 烧录过程中**不要断电、不要拔 USB**，否则可能损坏固件或导致芯片进入异常状态。
5. 若之前烧过其他程序并开启了**读保护（RDP）**，需要先在 `Option bytes` 页解除保护（会触发全片擦除）。

---

## 七、产线批量烧录（命令行，可选）

安装 CubeProgrammer 后自带命令行工具，默认路径：

```
C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe
```

单次烧录命令：

```bat
STM32_Programmer_CLI.exe -c port=SWD -w "SmartBattBox.bin" 0x08000000 -v -rst
```

参数说明：

| 参数 | 含义 |
|---|---|
| `-c port=SWD` | 用 SWD 接口连接 |
| `-w <文件> 0x08000000` | 把文件写到 0x08000000 |
| `-v` | 烧录后校验 |
| `-rst` | 烧录完成后复位运行 |

批量烧录时，可在批处理中用 `-q`（静默模式）并检查返回码（`0` 为成功）。

---

## 八、常见问题排查

| 现象 | 可能原因 | 处理方法 |
|---|---|---|
| 点 Connect 提示 **No STM32 target found** | 接线错误 / 目标板未上电 / SWD 被占用 | 检查 SWDIO、SWCLK、GND；确认板子已通电；先断开再重新上电 |
| 连接栏里找不到 ST-LINK | 驱动未装 / USB 线只能充电 | 重装 CubeProgrammer；换数据线、换 USB 口 |
| 连接成功但读不出芯片型号 | SWD 频率过高 / 排线过长 | 把 Frequency 降到 1 MHz 重试；缩短排线 |
| 烧录完成但板子不启动、无串口输出 | 起始地址填错 / 文件不是本产品固件 | 确认地址 `0x08000000`、文件为 `SmartBattBox.bin` |
| 校验失败（Verification failed） | 供电不稳 / 目标板在烧录中被复位 | 加大供电、缩短排线、降低频率后重烧 |
| 提示 **Device is protected** | 芯片开启了读保护 | `Option bytes` 页把 RDP 设为 `Level 0`，执行后全片擦除，再重新烧录 |
| 烧录成功但屏幕无显示 | 屏幕排线/供电问题，非固件问题 | 检查屏幕 UART4 接线与 12V_LCD 供电 |

---

## 九、版本记录

| 固件文件 | 大小 | 构建时间 | 说明 |
|---|---|---|---|
| `SmartBattBox.bin` | 71 332 字节（69.7 KB） | 2026-11-15 23:10 | 当前发布版本 |

**固件占用的 Flash 范围**

| 区域 | 地址范围 | 大小 | 说明 |
|---|---|---|---|
| 应用程序 | 0x08000000 – 0x080DFFFF | 896 KB | 固件写入区域 |
| 参数区 | 0x080E0000 – 0x080FFFFF | 128 KB | 掉电保存参数，烧录时不要擦除 |

---

*如有疑问，请联系技术支持并提供：烧录日志截图、串口上电日志、板卡版本。*
