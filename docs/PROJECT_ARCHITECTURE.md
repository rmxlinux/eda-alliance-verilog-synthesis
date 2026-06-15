# Alliance 项目整体架构文档

本文档基于当前工作区 alliance/src 源码、随源码附带的 FAQ、README、CHANGES、manual page 与 synthesis tutorial 梳理。顶层的 Alliance 安装指导/ 包含视频、安装包和实验材料，其中有超过 GitHub 单文件限制的二进制文件，已在仓库中排除，不作为源码仓库主体。

## 1. 项目定位

Alliance 是一套 VLSI CAD 工具链，包含 VHDL 编译、分析与仿真工具，行为级逻辑优化、门级映射、门级局部优化工具，形式验证、FSM 综合、网表/版图处理、布线与图形查看工具，以及可移植 CMOS 标准单元库和示例工程。

当前源码是 Alliance 5.0 风格的 Autotools 项目。顶层 alliance/src/Makefile.am 只有 SUBDIRS = @TOOLSDIRS@，真实子目录列表由 alliance/src/autostuff 扫描各子目录 Makefile.am 与 configure.in 后生成。

## 2. 顶层结构

~~~text
alliance/src/
  autostuff             Autotools 聚合脚本，生成 configure.ac/configure
  build                 一键 configure + make 脚本
  Makefile.am           顶层 automake 入口，展开 @TOOLSDIRS@
  README, FAQ, CHANGES  项目说明、格式说明、版本变更
  */Makefile.am         每个库/工具的独立构建描述
  */configure.in        每个库/工具的版本与配置片段
~~~

autostuff 中显式定义了核心库优先级，例如 mbk、aut、rds、elp、abl、bdd、log、btr、vex、ctl、ctp、abe、abt、abv、fsm、vpn、vbh、beh、bhl、bvl 等，之后再追加其余工具。这说明工程按“底层库先构建，上层命令行工具后构建”的方式组织。

## 3. 分层架构

~~~mermaid
flowchart TD
  A["输入描述: VHDL / VBE / FSM / VST"] --> B["语言前端与内部表示: VBH, VPN, RTL, BEH"]
  B --> C["行为级综合: VASY, SYF, BOOM"]
  C --> D["门级映射: BOOG"]
  D --> E["门级网表优化: LOON"]
  E --> F["网表与物理流程: MBK, cells, OCP/NERO, S2R"]
  B --> G["仿真/验证: ASIMUT, PROOF, FLATBEH"]
  F --> H["图形工具: XSCH, XPAT, GRAAL, DREAL, XVPN 等"]
~~~

### 3.1 基础库层

| 目录 | 角色 |
| --- | --- |
| mut / aut | 通用工具、内存/链表/hash、环境与错误处理。几乎所有工具都会包含 mut.h，不少工具调用 autenv()。 |
| mbk | Alliance 核心物理/逻辑数据库。提供 lofig、phfig 等网表/版图结构，读取和写出 .al、.vst、.spi、.edi、.vlg 等。 |
| abl | Alliance Boolean Language 表达式树。BEH、BOOM、BOOG 都围绕 ABL 表达式做逻辑处理。 |
| bdd | BDD 支撑库，供 BOOM、BOOG、VASY 等做逻辑优化和表达式处理。 |
| vex | VHDL/Verilog Expression 表示与打印，已有 Verilog operator 名称和表达式输出支持。 |
| log, rds, elp, ctl, ctp, btr, abe, abt, abv | 日志、规则/物理数据、控制/时序/抽象支撑库，作为后续工具的基础依赖。 |

### 3.2 语言前端与中间表示层

| 目录 | 角色 |
| --- | --- |
| vbh | VHDL 行为语法层。vbl_bcomp_l.l / vbl_bcomp_y.y 是词法/语法，getvbfiggenmap() 读取 VHDL，VvhVbh2Vpn() 将 VBH 转为 VPN。 |
| vpn | VASY 使用的过程网络/控制流中间表示，适合表达 process、wait、if/case/loop 等。 |
| rtn / rtd | RTL 图结构与 RTL 读写。VASY 分析 VPN 后得到 rtlfig_list，再由不同 driver 输出。 |
| beh | .vbe 行为级图结构，核心类型包括 befig、beout、beaux、bereg、bepor 等。 |
| bhl / bvl | .vbe 读写前端。vhdlloadbefig() / vhdlsavebefig() 是 BOOM 和 BOOG 读写行为描述的关键 API。 |
| fsm, fks, fvh, ftl | FSM 描述、编码、转换和相关支撑。 |

### 3.3 综合工具层

| 工具/目录 | 输入 | 输出 | 作用 |
| --- | --- | --- | --- |
| vasy | RTL VHDL: .vhd/.vhdl/.vbe/.vst | 标准 VHDL、Alliance .vbe/.vst、Verilog、RTL | VHDL Analyzer for Synthesis。将 VHDL 解析为 VBH/VPN/RTL，再输出目标格式。 |
| syf | .fsm | .vbe | FSM synthesizer，把 FSM 描述综合为 VHDL data-flow。 |
| boom | .vbe | 优化后的 .vbe | Boolean minimization，综合流程第一步，基于 BDD 优化行为表达式。 |
| boog | .vbe + 标准单元库 | .vst 或 .al | Binding and Optimizing On Gates，把行为逻辑映射为标准单元网表。 |
| loon | .vst + 标准单元库 + 可选 .lax | 优化后的 .vst | Local Optimization On Nets，门级本地优化、缓冲插入、RC/延迟优化。 |
| flatbeh, flatlo, flatph | 分层行为/网表/版图 | 扁平化结果 | 后续验证和物理流程辅助。 |

### 3.4 仿真、验证、转换与物理设计层

| 目录 | 角色 |
| --- | --- |
| asimut | VHDL/VBE/PAT 仿真。 |
| proof | 行为等价验证，常与 flatbeh 配合验证综合前后 .vbe。 |
| b2f, fmi | 行为/FSM 相关转换或抽取。 |
| x2y, k2f, m2e, pat2spi | 格式转换工具。x2y 已列出 vlg 为 Verilog netlist 格式。 |
| cells | 标准单元库，包含 .vbe 行为模型、.ap 物理抽象、.lib 等。 |
| genlib, genpat, mips_asm | 生成器和样例辅助工具。 |
| ocp, nero, ring, s2r, scapin, druc, dreal, graal, xsch, xpat, xvpn, xfsm, xgra | 自动布局布线、DRC、图形查看、scan-path 插入和物理/网表可视化。 |
| documentation, distrib, debian | 文档、安装脚本与打包文件。 |

## 4. 既有逻辑综合流程

源码附带 synthesis tutorial 对标准流程描述很明确：逻辑综合从 .vbe Boolean network 开始，得到 .vst 或 .al 网表；典型命令包括 boom、boog、loon、flatbeh、proof。

~~~mermaid
flowchart LR
  VHDL["RTL VHDL"] --> VASY["vasy -a"]
  FSM["FSM .fsm"] --> SYF["syf"]
  VASY --> VBE["Alliance VBE"]
  SYF --> VBE
  VBE --> BOOM["boom: Boolean minimization"]
  BOOM --> VBE_OPT["optimized VBE"]
  VBE_OPT --> BOOG["boog: map to cells"]
  BOOG --> VST["VST / AL netlist"]
  VST --> LOON["loon: local net optimization"]
  LOON --> VST_OPT["optimized VST"]
~~~

关键边界：

- VASY 主流程位于 alliance/src/vasy/src/vasy_main.c。VasyMainTreatModel() 先调用 VasyParseVbhFig() 读 VHDL，再调用 VasyParseVbh2VpnFig()、VasyAnalysisVpnFig() 得到 RTL，最后根据 VasyFlagDrive 输出 Verilog、Alliance 或标准 VHDL。
- VASY 的 Alliance 输出入口是 VasyDriveAllianceRtlFig()，位于 alliance/src/vasy/src/vasy_drvalc.c。
- BOOM 通过 vhdlloadbefig() 读取 .vbe，再通过 vhdlsavebefig() 写回 .vbe。
- BOOG 在 bog_main.c 中调用 vhdlloadbefig() 读取 .vbe，再读 MBK_TARGET_LIB 指向的 cell library，最终 map_befig() 生成 lofig 并 savelofig()。
- LOON 在 lon_main.c 中调用 getlofig() 读取 .vst/.al 逻辑网表，再读 cell library 做门级优化并 savelofig()。

## 5. 文件格式与环境变量

FAQ 中列出的关键格式：

| 视图 | 格式 | 方向 |
| --- | --- | --- |
| netlist | .vst VHDL structural | input/output |
| netlist | .vlg Verilog netlist | output |
| behavior | .vbe VHDL data-flow | input/output |
| behavior | .fsm VHDL FSM | input |
| layout | .ap symbolic layout | input/output |

重要环境变量：

- MBK_WORK_LIB：当前读写工作目录。
- MBK_CATA_LIB：只读库搜索路径。
- MBK_IN_LO：逻辑网表输入格式，mbk_util.c 支持如 vst。
- MBK_OUT_LO：逻辑网表输出格式，mbk_util.c 支持 vst 与 vlg 等。
- MBK_TARGET_LIB：BOOG/LOON 读取标准单元库的目录。
- VH_BEHSFX：BVL 读取/写出行为文件时使用的行为描述后缀，默认 .vbe。

## 6. 与 Verilog 相关的现状

当前代码中已有 Verilog 输出能力，但没有完整 Verilog 输入综合前端。

- vasy -v 是 Verilog 输出选项；vasy_main.c 的 usage 明确写出 -v Verilog output。
- vasy_drvvlog.c 负责从 rtlfig_list 输出 Verilog。
- vex 中已有 Verilog operator 名称和表达式输出逻辑。
- mbk 中已有 drive_vlog.c，并且 MBK_OUT_LO=vlg 可以输出 Verilog netlist。
- FAQ 明确把 Verilog .vlg 标为 netlist output，而不是 input。

结论：题目“扩展 Alliance 功能，使其支持 Verilog 语法的逻辑综合”的核心缺口是 Verilog 输入前端与内部表示转换，而不是后端门级映射算法。

## 7. 关键源码索引

| 文件 | 关键位置 | 说明 |
| --- | --- | --- |
| alliance/src/autostuff | order 变量 | 构建顺序与顶层 configure.ac 生成逻辑。 |
| alliance/src/vasy/src/vasy_main.c | VasyMainTreatModel()、输出格式选项 | VASY 主流程与输出格式选择。 |
| alliance/src/vasy/src/vasy_parse.c | VasyParseVbhFig()、VasyParseVbh2VpnFig() | VHDL 前端接入点与 VBH->VPN 转换。 |
| alliance/src/vbh/src/vbl_parse.c | getvbfiggenmap() | VBH/VHDL 解析入口。 |
| alliance/src/vbh/src/vbl_bcomp_l.l / vbl_bcomp_y.y | lexer/parser | VHDL 词法和语法。 |
| alliance/src/vbh/src/vvh_vbh2vpn.c | VvhVbh2Vpn() | VBH 到 VPN 的转换。 |
| alliance/src/vasy/src/vasy_analys.c | VasyAnalysisVpnFig() | VPN 到 RTL 的分析综合。 |
| alliance/src/vasy/src/vasy_drvalc.c | VasyDriveAllianceRtlFig() | RTL 输出 Alliance .vbe/.vst 的现有 driver。 |
| alliance/src/vasy/src/vasy_drvvlog.c | VasyDriveVerilogRtlFig() | RTL 输出 Verilog 的现有 driver。 |
| alliance/src/beh/src/beh.h | befig, beout, beaux, bereg, bepor | .vbe 行为图核心数据结构。 |
| alliance/src/bvl/src/bvl_parse.c / bvl_drive.c | VH_BEHSFX | .vbe 行为格式读写。 |
| alliance/src/boom/src/boom_parse.c | vhdlloadbefig() | BOOM 读取 .vbe 的入口。 |
| alliance/src/boog/src/bog_main.c | vhdlloadbefig()、library_reader()、map_befig() | BOOG 从 .vbe 到门级网表的主流程。 |
| alliance/src/loon/src/lon_main.c | getlofig()、library_reader()、savelofig() | LOON 门级网表优化主流程。 |
| alliance/src/mbk/src/mbk_util.c / mbk_lo_util.c | MBK_IN_LO、MBK_OUT_LO、vlg | 逻辑格式选择与 .vlg 输出。 |
| alliance/src/documentation/tutorials/synthesis/tex/synthesis.tex | synthesis commands | 官方教程中的综合链命令和 .vbe -> .vst 规则。 |

## 8. 后续改造切入点

为支持 Verilog 逻辑综合，有两种可行接入层：

1. 新增 Verilog -> .vbe 前端工具。
   - 优点：改动隔离、风险小，可直接复用 BOOM/BOOG/LOON。
   - 缺点：与 VASY 的统一 RTL pipeline 集成较弱。

2. 在 VASY 中新增 Verilog 输入前端，直接生成 rtlfig_list 或先生成 VPN。
   - 优点：与现有 VHDL RTL 分析、driver 体系更统一。
   - 缺点：需要理解并正确生成 VPN/RTL 结构，工程风险更高。

推荐先实现方案 1 作为可交付最小闭环，再把稳定的解析和 lowering 层逐步接进 VASY。

