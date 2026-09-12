# OptiScaler Aurora 维护者笔记

> 本文件记录 OptiScaler Aurora 的核心工程约束、已验证兼容性、维护原则与发布规则。
>
> 它面向项目维护者，用于避免后续更新时重复踩坑，不替代面向普通用户的 `README.md`。

---

## 1. 当前核心运行库

Aurora 当前主要运行库组合：

- DLSS：`310.9`
- Streamline：`2.14`
- DLSS Neural Rendering：`310.8`

涉及运行库升级时，必须优先确认：

- 是否破坏现有已验证游戏
- 是否影响 Runtime Sync
- 是否影响 Streamline 1.x 保护
- 是否影响 RTX 40 Multi Frame Generation
- 是否影响 DLSS Neural Rendering

---

## 2. Runtime Sync 核心原则

Aurora 的 Runtime Sync 不只是“把旧 DLL 换成新 DLL”。

它必须优先保证：

1. 游戏原生兼容性
2. 文件可恢复性
3. 已知旧版运行库保护
4. 未知状态下不擅自修改游戏文件

核心原则：

```text
能确认兼容 → 才处理

无法确认版本 → 保留原文件

发现未知修改 → 不自动覆盖

恢复备份 → 必须先验证
```

---

## 3. Streamline 1.x 必须保护

Aurora **绝不能把游戏原生 Streamline 1.x 无脑替换成 bundled Streamline 2.x**。

这是目前最重要的 Runtime Sync 不变量之一。

《巫师3：狂猎》DX12 已作为实际回归案例验证。

其原生 Streamline 版本为：

```text
sl.common.dll      1.5.6.0
sl.dlss.dll        1.5.6.0
sl.dlss_g.dll      1.5.6.0
sl.interposer.dll  1.5.6.0
sl.reflex.dll      1.5.6.0
```

如果错误替换为 Streamline 2.x，可能出现入口点缺失，例如：

```text
slGetFeatureSettings
```

因此 Runtime Sync 必须遵守：

```text
Streamline major = 1
→ 保留游戏原生文件

Streamline major = 2
→ 可进入正常 Aurora Runtime Sync 流程

Streamline major = unknown
→ 保留游戏原生文件
```

**Unknown 不代表旧版本。Unknown 的含义是：不要动。**

---

## 4. Streamline 版本判断规则

不要只依赖：

```text
FileVersion
ProductVersion
```

因为在部分本地化 Windows 环境中，版本字符串可能显示为：

```text
2,14,0,0
```

而不是：

```text
2.14.0.0
```

正确优先级：

```text
FileMajorPart
ProductMajorPart
```

如果数字字段不可用，再 fallback 到：

```text
FileVersion
ProductVersion
```

字符串解析必须同时兼容：

```text
1.5.6.0
2,14,0,0
```

无法可靠判断 major 时，应返回 unknown，并保留游戏原文件。

---

## 5. Runtime Sync 恢复安全规则

Aurora 可以恢复自己过去备份的游戏原文件，但只能在备份经过验证后进行。

对于旧版 Aurora 曾错误替换的 Streamline 1.x：

必须确认：

```text
BackupPath 存在

Backup 检测为 Streamline 1.x

Backup SHA256 与记录的 OriginalHash 一致
```

然后根据当前目标文件状态处理。

如果游戏或 Steam 已经恢复原生 SL1：

```text
只清理旧 Aurora 管理记录
不再次覆盖
```

如果当前文件仍然等于 Aurora 过去部署的 `DeployedHash`：

```text
允许恢复已验证 Backup
恢复后再次校验 SHA256
```

如果当前文件已经发生未知变化：

```text
不要自动覆盖
```

核心原则：

```text
Never restore over an unknown changed state.
```

---

## 6. Runtime Sync 已完成的回归测试

Streamline 1.x 自动恢复逻辑已经经过模拟测试。

测试流程：

```text
纯净《巫师3》SL1 文件
→ 记录版本与 SHA256
→ 模拟旧 Aurora 错误替换为 SL2
→ 写入旧管理状态
→ 运行新版 Runtime Sync Check
→ 自动恢复已验证 SL1 Backup
→ 再次比较版本与 SHA256
```

最终使用：

```powershell
Compare-Object $before $after -Property Name,Version,SHA256
```

结果无差异。

说明恢复后的文件与纯净 baseline 完全一致。

再次运行 Check 时，Runtime Sync 会识别并保护这些 Streamline 1.x 文件，而不是重新替换。

---

## 7. DLSS Neural Rendering：DualFeature 策略

配置项：

```ini
DualFeature=false
```

对应 UI：

```text
在超分器内部运行
Run inside the upscaler
```

该功能目前属于实验性功能。

当前 Aurora 的正式策略：

```text
默认关闭
```

原因：

- 《鬼武者：剑之道》已实测会出现严重花屏
- 目前没有足够广泛的实测证据证明它适合默认启用
- 多款测试游戏中未观察到足以抵消兼容性风险的普遍收益

因此：

```text
全局默认
→ false

明确验证兼容的游戏
→ 可手动开启测试

没有验证
→ 保持关闭
```

除非有新的、经过回归测试的明确理由，否则不要把默认值改回 `true`。

---

## 8. RTX 40 Multi Frame Generation 已验证兼容性

### 异环 / Neverness to Everness

已验证：

```text
RTX 40 Multi Frame Generation：6X
测试 GPU：RTX 4080 Laptop
DLSS Neural Rendering：可用
```

Aurora 已修复：

```text
Unlock MFG on RTX 40
```

启用时瞬间闪退的问题。

推荐 Proxy：

```text
winmm.dll
```

`dxgi.dll` 可能触发非法模块检测。

DualFeature 当前不推荐开启。

---

### 鬼武者：剑之道 / Onimusha: Way of the Sword

已验证：

```text
RTX 40 Multi Frame Generation：6X
DLSS Neural Rendering：可用
```

已知问题：

```text
DualFeature
→ 严重花屏
```

因此保持关闭。

---

### 巫师3：狂猎 / The Witcher 3: Wild Hunt

已验证：

```text
RTX 40 Multi Frame Generation：6X
测试 GPU：RTX 4080 Laptop
```

必须保留游戏原生：

```text
Streamline 1.5.6
```

已验证成功的 MFG 路线：

```text
FG Input
→ OptiFG (Upscaler)

FG Output
→ DLSSG

FG Nvngx Replacement
→ None (Real DLSSG)
```

推荐流程：

```text
1. 游戏内开启 DLSS 超分
2. 关闭游戏自己的 Frame Generation
3. 设置 Aurora FG Input / Output
4. Save Settings
5. 完全退出游戏
6. 重启游戏
7. 进入实际游戏场景
8. 开启 Aurora DLSSG Frame Generation
9. 选择 2X～6X
```

修改 FG Input / FG Output 后，完整退出并重启游戏非常重要。

否则 DLSSG 的完整控制项可能不会正确出现。

---

### 黎明行者之血 / The Blood of Dawnwalker

已实机验证：

```text
DLSS 5：正常

RTX 40 Multi Frame Generation：6X

DLSS 5 + 6X MFG：
可以同时正常运行
```

因此该游戏在 Aurora 下**不存在固有的 2X 上限**。

如果用户反馈只能到 2X，应优先检查：

```text
Aurora 配置

设置是否保存

是否完整重启游戏

FG 路径

实际加载的 nvngx_dlssg.dll

是否存在多份 DLSSG Runtime
```

不要直接认定为游戏兼容性上限。

---

## 9. 游戏原生只有 2X，不代表 Aurora 只有 2X

排查 MFG 时必须区分：

```text
A. 游戏自己的设置菜单最高只有 2X
```

和：

```text
B. Aurora 自己的 DLSSG 倍率也只有 2X
```

情况 A：

```text
不能证明 Aurora Unlock 失败
```

情况 B：

```text
才需要进一步调查 MFG Unlock / Runtime / Patch 状态
```

《巫师3》和《黎明行者之血》的实际测试都已经证明：

```text
游戏原生菜单能力
≠
Aurora 最终 MFG 能力
```

---

## 10. DLSSG Runtime 排查规则

不要只检查游戏目录里最显眼的那一份：

```text
nvngx_dlssg.dll
```

一个游戏进程可能存在并加载多份 DLSSG Runtime。

尤其 Unreal Engine 游戏常见于：

```text
游戏 EXE 目录

Engine\Plugins\Runtime\Nvidia\StreamlineCore\Binaries\ThirdParty\Win64\

Engine\Plugins\Runtime\Nvidia\Streamline\Binaries\ThirdParty\Win64\

Engine\Plugins\Marketplace\DLSS\Binaries\ThirdParty\Win64\

OptiScaler\
```

当 Aurora 只能显示 2X 时，需要确认：

```text
到底加载了哪一份 DLL

是否加载了多份 DLL

版本是否一致

MFG patch 是否成功
```

不要仅根据磁盘中某个文件的版本下结论。

---

## 11. 中文 UI 翻译规则

Aurora 使用简体中文 UI。

翻译目标：

```text
让普通用户更容易理解
而不是机械逐字翻译
```

简单常用操作词优先直接中文：

```text
Save Settings
→ 保存设置

Close
→ 关闭

Active
→ 启用

None
→ 无
```

技术名词保留：

```text
DLSS
DLSSG
MFG
DMFG
NVNGX
Streamline
OptiFG
FSR
XeSS
XeFG
Reflex
```

---

## 12. 避免机翻式技术表达

不推荐：

```text
经 Streamline

经 NVNGX
```

推荐：

```text
Streamline 路径

NVNGX 路径
```

翻译时优先表达“这个设置是做什么的”，而不是严格照搬英文语序。

---

## 13. 动态多帧生成命名规则

Aurora 中文 UI 统一使用：

```text
Force Dynamic MFG
→ 动态多帧生成
```

不再使用：

```text
强制动态 MFG
```

原因：

复选框本身已经表达启用 / 强制行为，用户更需要知道这是“动态多帧生成”功能。

目标帧数使用：

```text
DMFG Target FPS
→ 动态多帧生成目标帧数
```

如果未来实机发现横向空间不足，可以缩短为：

```text
动态 MFG 目标帧数
```

底层配置项名称保持不变。

---

## 14. 中文字体实现

Aurora 中文 UI 已加入：

```text
Windows 中文字体 fallback

Wide String → UTF-8 转换

Tooltip 自动换行
```

维护时不要轻易删除这些逻辑。

这些功能是为了解决：

```text
中文方块

乱码

编译器 execution code page 问题

Tooltip 过宽
```

中文字体 fallback 应继续保持“英文 / 技术字体为主，中文字体补字形”的思路。

---

## 15. 中文 UI 当前已知限制

部分横向空间较窄的控件可能出现文字截断。

这属于 UI 布局限制，不是功能故障。

优先处理顺序：

```text
缩短标签

→ 把详细解释放 Tooltip

→ 必要时局部调整控件宽度
```

不要为了一个标签，全局大幅增加所有 UI 控件宽度。

---

## 16. 构建规则

主要 workflow：

```text
.github/workflows/just_build_no_signature.yml
```

标准流程：

```text
修改

→ Commit

→ Push 到 aurora

→ GitHub Actions 自动构建

→ 下载生成的 .7z

→ 实机快速验证

→ 替换 Release 附件
```

尽量不要：

```text
拿旧 Release 包
→ 手动替换 DLL
→ 再重新压缩
```

这样容易遗漏新文件或保留旧配置。

---

## 17. README 打包规则

最终 Aurora Artifact 必须包含：

```text
仓库根目录 README.md
```

不要恢复为：

```text
dist/README.md
```

`dist/README.md` 属于早期 fork 状态，可能包含过时内容。

---

## 18. Release 原则

一次小改动没有必要反复替换公开 Release。

推荐：

```text
完成一轮修改

→ 一次 Commit / Push

→ 一次 Actions Build

→ 一次实机确认

→ 一次替换 Release
```

这样可以减少：

```text
Release 内容与源码不同步

旧 Artifact 混入

README 与配置不一致
```

---

## 19. 新游戏兼容性验证模板

每测试一款新游戏，建议记录：

```text
游戏版本

Graphics API

GPU

Proxy DLL

游戏原生 DLSS

游戏原生 Frame Generation

原生 FG 最高倍率

Streamline 版本

nvngx_dlss.dll 版本

nvngx_dlssg.dll 版本

Aurora 面板能否打开

DLSS 是否正常

DLSS Neural Rendering 是否正常

2X 是否正常

3X 是否正常

4X 是否正常

5X 是否正常

6X 是否正常

DualFeature 表现

是否花屏

是否 Crash

OptiScaler.log 关键结论
```

最终给出：

```text
推荐 Proxy

FG Input

FG Output

FG Nvngx Replacement

游戏原生 FG 应开还是关

是否需要 Save + Restart

是否需要启用 Active

是否需要特殊 INI

是否需要 game-specific patch
```

---

## 20. 兼容性修改原则

不要为了修复一个游戏，直接放宽整个 Aurora 的安全逻辑。

正确流程：

```text
复现

→ 日志

→ 确认实际加载 Runtime

→ 找根因

→ 做最小修改

→ 构建

→ 实机验证

→ 更新文档
```

避免：

```text
Unknown Runtime 强行替换

取消 SHA256 校验

未验证 Backup 就恢复

为了一个游戏全局开启实验功能

为了出现 6X 而破坏已有游戏

未经实测就写进 README
```

---

## 21. 当前未完成验证的问题

### 明末：渊虚之羽 / WUCHANG: Fallen Feathers

曾有用户反馈 UE5 D3D12：

```text
PresentInternal(syncInterval) failed
```

错误：

```text
80004004
```

目前尚未完成：

```text
稳定复现

明确根因

实机修复验证
```

因此：

**不要把《明末：渊虚之羽》标记为 Aurora 已修复兼容。**

---

## 22. 总维护原则

Aurora 当前已经不仅仅是一个实验性 OptiScaler build。

项目已经包含：

```text
RTX 40 MFG 兼容修改

Runtime Sync

Streamline 1.x 保护

DLSS Neural Rendering

中文 UI

自动化构建

公开 Release

多款游戏实机兼容验证
```

因此后续修改优先级应始终是：

```text
保护已验证兼容性

→ 保留游戏原文件安全

→ 用日志和 Runtime 事实定位问题

→ 最小化修改范围

→ 实机验证后再公开
```

遇到不确定状态时：

```text
宁可 fail safe

不要猜测覆盖

不要因为“理论上应该没问题”而破坏已验证逻辑
```