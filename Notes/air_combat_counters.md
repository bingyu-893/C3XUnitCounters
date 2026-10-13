# 空对空 Unit Counter

## 规则方向与范围

拦截机是**战斗攻击方**，执行空中任务的飞机是**战斗防守方**。规则的 `self` 始终指 `vs` 左侧，`enemy` 始终指右侧，因此：

| 规则方向 | 改变拦截机攻击 | 改变任务飞机防御 |
| --- | --- | --- |
| `Interceptor vs MissionAircraft` | `self-atk` | `enemy-def` |
| `MissionAircraft vs Interceptor` | `enemy-atk` | `self-def` |

例如 `Fighter vs Bomber self-atk 125` 提高战斗机拦截轰炸机时的攻击倍率；`Bomber vs Fighter self-def 125` 提高轰炸机被战斗机拦截后的防御倍率。左右名称仍须匹配场景单位名或 tag。普通与隐形飞机共用此结算。

`terrain`、`in-city`、`district` 均检查空中任务目标的 `tile_x/tile_y`。两架飞机的 `Body.X/Body.Y` 是基地坐标，不能作为这些条件的依据。`self-exp` / `enemy-exp` 检查相应飞机的当前经验等级，方向与上表一致。

本补丁仅改变已经发生拦截后的逐轮胜负。选择拦截机、普通/隐形拦截概率、城市防空和地面单位防空的判定保持原样。`self-bombard` / `enemy-bombard` 不用于空对空攻防。

## 逆向证据

2026-10-13 核对了本地 Steam 未修改 EXE，以及 `C3CRE3-Jun-06-2026.gar` 内 GOG 和 `/PCGames.de/Civ3Conquests.exe` 的原始指令、函数签名与交叉引用。PCG 工程显示的 program name 为 `FIXED-Civ3Conquests.exe`。地址来自各版本实际指令，未用固定偏移推算。

| 项目（虚拟地址） | GOG | Steam | PCG |
| --- | --- | --- | --- |
| `Unit::try_flying_over_tile` | `0x5C68A0` | `0x5D5700` | `0x5C65B0` |
| 飞机拦截概率的 `rand_int(100)` 调用，未修改 | `0x5C70C4` | `0x5D5F72` | `0x5C6DD4` |
| 从上述函数调用空战 | `0x5C719E` | `0x5D6040` | `0x5C6EAE` |
| 联机消息处理调用空战 | `0x478A35` | `0x47BA25` | `0x478AB5` |
| `Fighter::do_air_combat`，新增 inlead | `0x4A4520` | `0x4AB1C0` | `0x4A45B0` |
| 空战内的赔率调用，新增 repl call | `0x4A4555` | `0x4AB1F5` | `0x4A45E5` |
| 该赔率调用的原目标 | `0x4A0ED0` | `0x4A7A90` | `0x4A0F60` |
| 空战内的 `rand_int(1024)` 调用，未修改 | `0x4A4566` | `0x4AB206` | `0x4A45F6` |

三个版本的空战 ABI 均为 `void __thiscall(Fighter *, Unit *interceptor, Unit *defender, int tile_x, int tile_y)`；项目用 `__fastcall` 加占位 EDX 表达。函数入口均为 `51 53 55 56 8B 74 24 14`，末尾均为 `C2 10 00`（四个栈参数）。

在 `try_flying_over_tile` 的空战调用前，EBP 是执行任务的飞机，ESI 是选中的拦截机。三个版本均按 `push y; push x; push ebp; push esi` 传参。联机处理函数从消息取出相同顺序的两架飞机和目标坐标，再调用同一入口。只包围 `try_flying_over_tile` 会漏掉联机消息稍后执行的空战，因此补丁包围 `do_air_combat`。

每轮赔率调用前均为 `push 1; push 0; push edi; push esi`，即 `get_combat_odds(interceptor, mission_aircraft, false, true)`。返回值是防守方获胜的 1/1024 阈值：随机数小于阈值时扣拦截机 HP，否则扣任务飞机 HP。随后读取剩余 HP，继续循环或处理飞机销毁/联机消息。隐形能力 `0x15` 仅在此前选择拦截概率时检查，空战循环无隐形专用分支。

原始 EXE SHA-256（GOG/PCG 来自 Ghidra 的导入元数据）：

```text
GOG   838df6f8b3518d5f5f7ff50c7c7add715628ffabbd37221d0bcc7f080afc2746
Steam 55a6efca2b51cad94a91fc1f67192742fcd35ad669204df54142781d2700aa4c
PCG   fbb9ae4c6fa9ece58e065b6396b25d693630afefe0cc45b31fc66914d1b44990
```

三个空战函数从入口起 `0x179` 字节的 SHA-256（测试读取并实际执行的内容）：

```text
GOG   6b714cd06b152a3d21d1a85106d45b98f65c3ed786dc90727c2e946ecbe0dad8
Steam db8bcefa779c1bbd94828719dc408f086dd8b454a8ad9d67d887179a10a9cd1a
PCG   f6d0ac4f22c040bf0b11d848c2b875586bfb72db57bfe892b2eb4ce68370ea7a
```

本次三版本均已确认，无需跳过。以后若有未确认版本，应使用项目已有的 `repl call` 地址 `0x0FF`（OFF）跳过该版本调用点，不能填写猜测地址。`0x0FF` 只适用于 `repl call`，不能用于 inlead；若入口无法确认，需同时调整入口接入方式，不能保留一个猜测的 inlead。

## 实现与状态边界

`patch_Fighter_do_air_combat` 将原有 `counter_air_combat_ctx` 保存在栈上，写入当前 Fighter、两架飞机和任务目标，调用引擎后恢复原值。即使开关关闭或参数无效，也覆盖当前作用域，避免内层继承外层目标。原函数的零攻防提前返回、联机权限提前返回、胜负结算返回均经过包装函数的同一恢复位置。恢复时不解引用飞机指针，允许引擎在结算中销毁飞机。

现有防守方选择 context、army 选择 context 和 Fighter 的普通战斗字段不被此补丁改写。空战赔率补丁还核对 Fighter 和两架飞机的身份，且仅替换空战循环内的一个调用点；HUD、轰炸与普通战斗赔率入口不消费这个 context，也不受防御轰炸死亡标记影响。这里覆盖引擎现有正常/提前返回，不引入 SEH 异常恢复机制。

每轮保留一次原始 `Fighter_get_combat_odds` 调用，原样传递 `false, true`，然后使用现有位图规则求值、惰性地块条件缓存、固定精度倍率和 `counter_adjust_odds`。既不改 UnitType，也不提前把乘数舍入成整数攻防。规则仍按索引升序叠加。空战原本忽略普通防御加成；`ignore-defensive-bonuses` 沿用统一求值器现有的“替代本规则 enemy-def”语义。

新代码不调用 RNG，不重掷、不预演战斗。离线和联机均保留原循环的一轮一次 `rand_int(1024)`，拦截前的 RNG 也没有改动。改变赔率可能自然改变战斗轮数，因此整场消耗的随机数数量可能随胜负过程改变；不额外补齐或丢弃随机数，否则反而改变引擎行为。联机各端需使用相同补丁和规则配置。

## 空战测试弹窗

在本次测试所加载的配置中加入（默认关闭）：

```ini
enable_unit_counters = true
debug_air_combat_counters = true
counter_rules = ["Fighter" vs "Bomber" self-atk 1000]
```

重新运行更新后的补丁，在离线游戏中让战斗机拦截轰炸机。每场实际空战结束后弹一次 `C3X - Air combat counter test`，显示第一轮实际使用的数据：双方单位类型及 ID、经验 ID、各自基地坐标、任务目标坐标、counter 开关状态、加载规则数、拦截机攻击倍率、任务飞机防御倍率、调整前后的拦截机每轮获胜概率及原始防守方胜率阈值。`Interceptor ATK: 1000% of base` 表示攻击乘以 10；`100%` 表示该项没有净加成。

`Net ATK/DEF effect: UNCHANGED` 表示攻防倍率最终均为 100%，可能没有规则匹配，也可能存在相互抵消或只影响其他属性的规则，不等同于“没有命中规则”。开关关闭或规则为空时也会报告，便于确认配置是否加载。引擎提前返回且没有计算赔率时会提示未捕获到逐轮赔率。未发生飞机间拦截空战时不会弹窗。

报告只记录第一轮真实赔率调用，不额外调用引擎赔率函数或 RNG。报告字符串在飞机可能被销毁之前生成；弹窗在原 counter context 恢复后显示，避免弹窗消息处理继承已结束的空战状态。嵌套空战使用各自栈上的报告缓冲。联机时不显示测试弹窗。测试结束后将 `debug_air_combat_counters` 改回 `false`。

弹窗回归已验证：10 倍攻击对应的实际倍率与赔率文本、无净变化/关闭/空规则提示、提前返回提示、飞机失效后使用已捕获文本、嵌套上下文恢复、每场只弹一次，以及关闭弹窗和联机时不弹窗。三版本原始空战循环在测试开关开/关两种状态下均保持每轮一次 RNG；新增弹窗后的注入编译验证通过。

## 验证

无需游戏数据的回归测试：

```powershell
python tests/test_counter_air_combat.py
```

此测试使用项目 TCC，提取实际生产函数并包含真实数据结构，不复制规则算法。引擎数据访问由可控替身提供。覆盖四种效果、正反方向、普通/隐形类型、16 种双方经验组合、反向经验限制、非法经验、任务坐标与基地不同、地形/城市/区域条件、tag、同名类型复制体、跨 32 条位图边界的规则顺序、多条倍率叠加、0% 边界、所有 1..1023 基础赔率的无匹配不变、关闭/空规则/非法类型快速路径、嵌套空战、HUD 探测及既有 context/单位类型/位置不变。

可选的原始机器码循环测试（需要 Python `capstone` 及自己的游戏数据）：

```powershell
python tests/test_counter_air_combat.py --steam-exe ../../Civ3Conquests-Unmodded.exe --gog-text ref/air_re/gog.text.bin --pcg-text ref/air_re/pcg.text.bin
```

GOG/PCG 的 `.text` 由 `Ghidra scripts/ExportAirCombat.java` 从各自工程导出，起址必须为 `0x401000`；脚本参数为输出目录和 `gog`/`steam`/`pcg` 标签。导出物放在忽略的 `ref` 目录，不提交游戏二进制。对现有工程使用 `-readOnly -noanalysis`。

原始循环测试先核对 CSV 入口、赔率调用及全部 12 个外部调用目标，再重定位空战函数，仅将外部依赖接到受控替身。实际执行其循环、扣血、分支和返回指令，检查普通/隐形飞机、四个经验等级、目标地形/城市条件、两个不同胜负方向、离线销毁分支、联机消息分支、动画参数，以及每轮一次 RNG。另测零攻防和联机权限提前返回。

2026-10-13 结果：生产函数回归通过；GOG、Steam、PCG 原始循环测试全部通过。`TEST_INJECTED_CODE_COMPILE.bat` 在本机 Steam 未修改 EXE 上输出 `Injected code compiled successfully.`。首次沙箱运行未找到父目录游戏程序，允许该验证访问父目录并创建暂停进程后重跑通过；未启动实际游戏回合，也未替换磁盘上的游戏 EXE。

验证范围不包括完整游戏画面操作、真实多人会话同步或 GOG/PCG 完整进程注入；这三项仍需游戏内验收。
