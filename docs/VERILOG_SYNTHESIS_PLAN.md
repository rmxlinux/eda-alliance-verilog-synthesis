# Verilog 逻辑综合支持修改方案

大作业题目要求：扩展 Alliance 功能，使其支持 Verilog 语法的逻辑综合。

当前项目已经支持 VHDL/VBE/FSM 进入综合链，也支持输出 Verilog 网表/RTL，但缺少 Verilog 作为输入语言进入综合链的前端。因此实现重点是“Verilog 源码解析与 lowering”，目标是把 Verilog 转换成 Alliance 既有 .vbe 行为描述或内部 RTL，再复用现有 BOOM -> BOOG -> LOON。

## 1. 现状判断

### 已有能力

- VASY 能把 RTL VHDL 转成 Alliance .vbe/.vst、标准 VHDL、Verilog 或 RTL。
- BOOM 能读取 .vbe，做 Boolean minimization，再写回 .vbe。
- BOOG 能读取 .vbe，结合 MBK_TARGET_LIB 的 cell library，映射为 .vst/.al。
- LOON 能读取 .vst/.al，结合 cell library 做门级局部优化。
- MBK 可以输出 Verilog netlist：FAQ 中 .vlg 标为 netlist output，mbk/src/drive_vlog.c 是 driver。
- VEX 已有 Verilog expression 输出能力。

### 缺失能力

- 没有 Verilog lexer/parser 作为输入前端。
- MBK_IN_LO 不支持 .vlg 输入，FAQ 也说明 .vlg 仅 output。
- VASY 的 -v 是 Verilog output，不是 input；-I 当前用于指定 VHDL 输入后缀。
- 现有综合后端以 .vbe/BEH 作为行为综合边界，并不直接理解 Verilog AST。

## 2. 推荐总体路线

采用“两阶段实现”：

1. 第一阶段新增独立工具 vlog2vbe：解析 synthesizable Verilog 子集，输出 Alliance .vbe。
2. 第二阶段把成熟的 Verilog 前端接入 vasy -I v 或新增兼容选项，使 VASY 也能直接处理 Verilog 输入。

第一阶段可最快形成完整闭环：

~~~mermaid
flowchart LR
  V["Verilog .v"] --> VLOG2VBE["vlog2vbe"]
  VLOG2VBE --> VBE["Alliance .vbe"]
  VBE --> BOOM["boom"]
  BOOM --> VBE2["optimized .vbe"]
  VBE2 --> BOOG["boog"]
  BOOG --> VST[".vst/.al"]
  VST --> LOON["loon"]
  LOON --> VST2["optimized .vst"]
~~~

选择 .vbe 作为第一阶段输出，是因为 BOOM 与 BOOG 的主流程都从 vhdlloadbefig() 读取 .vbe，后端可不改。

## 3. Verilog 支持范围

第一版建议支持 Verilog-2001 中适合课程项目的可综合子集：

| 类别 | 支持内容 |
| --- | --- |
| module | 单文件/多 module，module ... endmodule，ANSI 与非 ANSI port 声明优先支持 ANSI。 |
| 端口与声明 | input、output、inout、wire、reg、向量范围 [msb:lsb]。 |
| 连续赋值 | assign y = expr; |
| 组合 always | always @*、always @(*)，支持阻塞赋值、if/else、case。 |
| 时序 always | always @(posedge clk)、always @(negedge clk)，支持非阻塞赋值、同步/异步 reset 的常见模板。 |
| 表达式 | 常量、标识符、位选/片选、拼接、条件运算符、括号、单目/双目逻辑与位运算、比较、加减。 |
| 实例化 | 简单 module 实例化和命名端口连接，第一阶段可限制为已能生成 .vbe 或可内联的子模块。 |

第一版明确不支持或延后：

- initial、always #delay、assign #delay、specify、UDP、PLI/system task；
- real/integer/time、signed 复杂规则、四态精细仿真语义；
- generate/parameter 的完整 elaboration；
- tri-state 总线的复杂冲突解析；
- 行为级 for/while/repeat 的动态循环，只允许静态展开的简单 for。

## 4. 关键文件与计划修改

| 文件/目录 | 现有角色 | 计划修改 |
| --- | --- | --- |
| alliance/src/autostuff | 扫描子目录并生成总 configure.ac；显式指定核心库优先级。 | 新增工具目录后确认构建顺序。如果做成库，应把新库排在依赖它的工具前；如果做独立二进制，通常自动追加即可。 |
| alliance/src/Makefile.am | SUBDIRS = @TOOLSDIRS@。 | 通常无需直接改，运行 autostuff 后由 @TOOLSDIRS@ 管理。 |
| alliance/src/vlog2vbe/ 或 alliance/src/vlog/ | 新目录。 | 新增 Verilog 前端工具。包含 configure.in、Makefile.am、src/、man1/、tests/。 |
| alliance/src/vlog2vbe/src/vlog_lex.l | 新文件。 | Verilog 词法，识别关键字、标识符、数字常量、运算符、注释和属性。 |
| alliance/src/vlog2vbe/src/vlog_parse.y | 新文件。 | Verilog 语法，产出轻量 AST；保持 yacc/flex 风格与现有 vbl/bvl 一致。 |
| alliance/src/vlog2vbe/src/vlog_ast.[ch] | 新文件。 | AST 节点、链表、范围、表达式、statement、module、instance。 |
| alliance/src/vlog2vbe/src/vlog_elab.[ch] | 新文件。 | 解析参数/范围、端口方向、实例关系，做宽度推导和静态展开。 |
| alliance/src/vlog2vbe/src/vlog_to_beh.[ch] | 新文件。 | 将 Verilog AST lowering 到 befig_list、bepor_list、beout_list、beaux_list、bereg_list 与 ABL 表达式。 |
| alliance/src/vlog2vbe/src/vlog_main.c | 新文件。 | 命令行入口，例如 vlog2vbe [-o out] [-top top] [-V] file.v，调用 parser/elab/BEH driver。 |
| alliance/src/beh/src/beh.h | BEH 数据结构定义。 | 原则上不改；通过 beh_add*() API 构造 .vbe。只有遇到 Verilog 特有语义无法表达时才扩展。 |
| alliance/src/abl/src/abl.h | ABL 表达式构造 API。 | 原则上不改；Verilog 表达式 lowering 到 createablatom()、createablbinexpr()、createablnotexpr() 等。 |
| alliance/src/bvl/src/bvl_drive.c | .vbe driver，默认后缀受 VH_BEHSFX 控制。 | 复用 vhdlsavebefig() 写 .vbe，不直接手写 VBE 文本。 |
| alliance/src/boom/src/boom_parse.c | BOOM 读取 .vbe。 | 不改；新增前端输出 .vbe 后直接复用。 |
| alliance/src/boog/src/bog_main.c | BOOG 读取 .vbe、读取 cell library、调用 map_befig()。 | 不改；只需保证生成 .vbe 合法且符合 BOOG 对 BEH 的约束。 |
| alliance/src/loon/src/lon_main.c | LOON 读取 .vst/.al，再优化。 | 不改。 |
| alliance/src/vasy/src/vasy_main.c | VASY 主流程和选项解析。 | 第二阶段可新增 Verilog input 识别，例如 -I v / -I verilog；注意 -v 已被 Verilog output 占用，不能复用。 |
| alliance/src/vasy/src/vasy_parse.[ch] | VHDL 解析与 VBH->VPN 转换封装。 | 第二阶段可新增 VasyParseVerilogRtlFig() 或 VasyParseVerilogVbhFig()，按输入格式路由。 |
| alliance/src/vasy/src/Makefile.am | VASY 依赖和源文件列表。 | 第二阶段接入 Verilog parser 时增加 include path、源文件和库依赖。 |
| alliance/src/documentation/ | 官方文档和教程。 | 新增 vlog2vbe 使用说明、Verilog 子集说明和综合流程示例。 |

## 5. 数据转换设计

### 5.1 表达式 lowering

Verilog 表达式优先转换为 ABL：

| Verilog | ABL/BEH 映射 |
| --- | --- |
| a & b | ABL_AND |
| a \| b | ABL_OR |
| a ^ b | ABL_XOR |
| ~a | ABL_NOT |
| a ? b : c | 展开为 (a and b) or ((not a) and c)，必要时逐 bit 展开。 |
| ==, != | 逐 bit XNOR/XOR 后规约。 |
| +, - | 第一版可通过 ripple adder 展开为 bit-level ABL，或限制为 VASY Alliance driver 已能表达的形式。 |
| {a,b} | 拼接到目标 bit 范围。 |
| x[i], x[msb:lsb] | 规范化为 Alliance bit/vector 名称与范围。 |

注意：ABL 更像 bit-level Boolean expression，Verilog vector expression 需要做宽度推导和逐 bit lowering。

### 5.2 声明和端口映射

- input/output/inout 映射为 bepor_list。
- wire 且被连续赋值/组合逻辑驱动的内部信号映射为 beaux_list。
- output wire 的表达式映射为 beout_list。
- reg 如果只在组合 always @* 中赋值，仍可 lowering 为组合 beaux/beout。
- reg 如果在边沿触发 always 中赋值，映射为 bereg_list。

### 5.3 always 块映射

组合 always：

- 建立默认赋值和路径覆盖检查，避免隐式 latch。
- if/else 和 case 转换为条件表达式树。
- 对每个被赋值对象生成一个完整的 ABL 表达式。

时序 always：

- 识别 posedge/negedge clock。
- 识别常见 reset 模板，例如 always @(posedge clk or negedge rst_n)，以及 if (!rst_n) q <= 0; else q <= d;。
- 映射到 bereg 的 next-state 表达式和 control 条件。
- 第一版不支持多 clock 同块、多源驱动和复杂 gated clock。

## 6. 里程碑

### M1: 解析器与 AST

- 新增 vlog2vbe 目录和构建文件。
- 支持 module、端口、wire/reg、assign、基础表达式。
- 添加 parser 单元样例：简单与门、非门、mux、向量赋值。

验收：vlog2vbe simple_and.v simple_and 能生成语法合法的 .vbe。

### M2: 组合逻辑综合闭环

- 支持 always @*、if/else、case。
- 完成 vector width 推导和逐 bit ABL lowering。
- 用 vhdlsavebefig() 输出 .vbe。

验收：生成的 .vbe 可被 boom 读取优化，可被 boog 映射为 .vst。

### M3: 时序逻辑支持

- 支持边沿触发 always 和常见 reset。
- reg lowering 到 bereg。
- 增加 DFF、计数器、移位寄存器样例。

验收：asimut 可仿真 .vbe，boom/boog 可继续处理。

### M4: 层次与实例化

- 支持简单 module instance 和命名端口连接。
- 对可综合小层次设计做 flatten 或输出多个 .vbe。
- 处理 top module 选择。

验收：多 module Verilog 输入可产生 top 和子模块对应 .vbe，并走通 boog。

### M5: VASY 集成与文档

- 评估把 parser 接入 vasy -I v。
- 复用 VasyDriveAllianceRtlFig() 或封装 vlog2vbe 的 BEH 输出。
- 写 man page、README、示例 Makefile。

验收：用户可以选择 vlog2vbe foo.v foo 或 vasy -I v -a foo foo 进入 Alliance 综合流程。

## 7. 测试计划

| 测试类型 | 样例 |
| --- | --- |
| 词法/语法 | 注释、数字常量、向量范围、ANSI port、case items。 |
| 组合逻辑 | gates、mux、decoder、priority encoder、adder、comparator。 |
| 时序逻辑 | DFF、reset DFF、enable register、counter。 |
| 层次结构 | top 实例化两个组合子模块。 |
| 后端兼容 | boom generated generated_o、boog generated_o generated_g、loon generated_g generated_l。 |
| 行为一致性 | 对同一设计准备 Verilog test vector 与 .pat，或生成 truth table 对比。 |
| 错误诊断 | 不支持语法给出明确行号和原因。 |

测试文件建议放在：

~~~text
alliance/src/vlog2vbe/tests/
  comb/
  seq/
  hierarchy/
  expected/
~~~

如果课程环境无法完整编译 X11/Motif 图形工具，可先只构建核心库和命令行工具；后端验证优先覆盖 vlog2vbe、boom、boog。

## 8. 风险与规避

| 风险 | 说明 | 规避 |
| --- | --- | --- |
| Verilog 宽度/有符号规则复杂 | Verilog 表达式宽度和 signed 规则容易出错。 | 第一版限制 unsigned，所有 expression 在 elaboration 后记录 width，测试覆盖扩展/截断。 |
| 四态语义不完全匹配 | Alliance ABL/VBE 更偏 Boolean，Verilog x/z 语义不同。 | 第一版只支持可综合 0/1，x 映射为 don't-care 需谨慎，z 只支持明确 tri-state 或报错。 |
| latch 隐式生成 | 组合 always 未覆盖所有路径会生成 latch，后端未必友好。 | 第一版对未完整赋值直接报错，后续再显式支持 latch。 |
| 多驱动信号 | Verilog wire 可多驱动，VBE/BEH 对综合表达更偏单驱动。 | lowering 阶段做 driver count 检查，多驱动默认报错。 |
| 命名大小写 | Alliance 旧代码常用 namealloc()，部分路径可能归一化名称。 | 统一定义 Verilog name mangling 规则，保留映射表，避免大小写冲突。 |
| 旧式 C/Autotools | 代码是 K&R/旧 Autotools 风格。 | 新代码尽量保持 ANSI C89 和现有 yacc/lex 生成方式，不引入外部依赖。 |
| 后端 cell library 约束 | BOOG/LOON 依赖 MBK_TARGET_LIB 和 .lax。 | 文档中明确环境变量，测试用已有 cells/src/sxlib 或 msxlib。 |

## 9. 推荐第一批代码任务

1. 建立 alliance/src/vlog2vbe 骨架和命令行。
2. 复用 beh/abl API，先生成最小合法 .vbe。
3. 支持 assign + bitwise expression，跑通 boom。
4. 扩展 always @*，跑通 mux/decoder。
5. 扩展 posedge DFF，跑通 boog。
6. 补文档与课程展示用 demo Makefile。

## 10. 最小可交付示例

输入 Verilog：

~~~verilog
module simple_and(input a, input b, output y);
  assign y = a & b;
endmodule
~~~

目标输出 .vbe 形态：

~~~vhdl
ENTITY simple_and IS
PORT (
  a : in BIT;
  b : in BIT;
  y : out BIT
);
END simple_and;

ARCHITECTURE behaviour_data_flow OF simple_and IS
BEGIN
  y <= a and b;
END;
~~~

后续命令：

~~~sh
vlog2vbe simple_and.v simple_and
boom simple_and simple_and_o
boog simple_and_o simple_and_g
loon simple_and_g simple_and_l
~~~

这个闭环能证明“Verilog 语法输入已经进入 Alliance 逻辑综合链”，适合作为阶段性验收。
