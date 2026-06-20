# 扩展 Alliance 支持 Verilog 逻辑综合：设计历程

本文档面向课程答辩，整理本项目从架构调研、方案选择到分阶段实现的完整设计历程。内容基于仓库提交记录、`docs/PROJECT_ARCHITECTURE.md`、`docs/VERILOG_SYNTHESIS_PLAN.md` 以及当前 `alliance/src/vlog2vbe` 实现。

## 1. 题目理解与问题定位

题目要求是“扩展 Alliance 功能，使其支持 Verilog 语法的逻辑综合”。我们首先没有急着写 parser，而是先分析 Alliance 原有工具链：它已经能处理 VHDL/VBE/FSM，并且可以把行为描述经过 `boom -> boog -> loon` 变成门级网表；它也有 Verilog 输出能力，例如 VASY 可以输出 Verilog，MBK 也有 `.vlg` netlist driver。但问题在于：现有代码中缺少“Verilog 作为输入语言”的综合前端。

因此，本项目的核心不是重写逻辑优化、门级映射或标准单元库相关算法，而是补齐前端：

```text
Verilog .v -> Verilog parser/lowering -> Alliance .vbe -> boom -> boog -> loon
```

这个判断非常关键，因为它决定了我们只在语言输入侧扩展 Alliance，最大程度复用原有成熟后端。

## 2. 总体设计选择

调研后我们比较了两种路线。

第一种是直接改 VASY，在 VASY 中加入 Verilog 输入分支，然后生成 VPN/RTL 或直接生成 Alliance 行为描述。这条路线集成度最高，但风险也最大：VASY 内部的 VBH、VPN、RTL 数据结构复杂，旧代码风格和工具链依赖较重，短时间内很难保证语义正确。

第二种是新增独立命令行工具 `vlog2vbe`，将可综合 Verilog 子集转换成 Alliance `.vbe`。`.vbe` 是 BOOM/BOOG 已经支持的行为综合边界，后续后端完全不需要修改。

最终选择第二种路线，理由如下：

1. 风险低：不破坏 VASY、BOOM、BOOG、LOON 原有行为。
2. 闭环快：只要能输出合法 `.vbe`，就能进入 Alliance 原有综合链。
3. 便于测试：`vlog2vbe` 可以单独编译、单独回归，不依赖完整图形工具环境。
4. 可渐进演进：后续如果时间允许，可以把稳定的 parser/lowering 再接入 VASY。

这也是整个项目最核心的架构决策。

## 3. 提交历史总览

仓库提交记录对应了清晰的阶段推进：

| 提交 | 时间 | 主题 | 阶段定位 |
| --- | --- | --- | --- |
| `bb352b2` | 2026-06-16 00:13 | Initial Alliance source and project planning docs | 引入 Alliance 源码，完成架构调研和路线规划 |
| `2fcc931` | 2026-06-16 11:52 | add verilog initial parsing logic | M1：建立 `vlog2vbe` 骨架，支持基本 module/assign/expression |
| `a522ccd` | 2026-06-16 12:12 | add always support | M2：支持组合 `always @*`、`if/else`、`case/default` |
| `2bcc308` | 2026-06-16 12:35 | add always @(posedge clk) support | M3：支持时序 always、寄存器、enable、异步 reset |
| `df7683b` | 2026-06-16 13:22 | Add multi module support | M4：支持多模块、实例化、层次 flatten |

可以看到，项目不是一次性堆出所有功能，而是按“最小可运行前端 -> 组合逻辑 -> 时序逻辑 -> 层次结构”的顺序逐步扩展。每个阶段都补充对应测试，保证前一阶段行为不会被后一阶段破坏。

## 4. 阶段 0：架构调研与方案规划

对应提交：`bb352b2 Initial Alliance source and project planning docs`

这一阶段主要完成两件事。

第一，引入 Alliance 原始源码和项目规划文档。我们阅读了 `alliance/src` 的工具目录、`README`、`FAQ`、manual page 和 synthesis tutorial，梳理出 Alliance 原有综合链：

```text
VHDL/FSM/VBE -> VASY/SYF/BOOM -> BOOG -> LOON -> VST/AL
```

其中：

- `boom` 读取 `.vbe`，做 Boolean minimization，再输出优化后的 `.vbe`。
- `boog` 读取 `.vbe` 和标准单元库，将行为逻辑映射为 `.vst/.al`。
- `loon` 读取 `.vst/.al`，做门级局部优化。
- Verilog 相关能力主要是输出，不是输入。

第二，形成修改方案文档 `VERILOG_SYNTHESIS_PLAN.md`。计划中明确采用两阶段路线：第一阶段先做独立 `vlog2vbe`，第二阶段再考虑接入 VASY。这个规划让后续实现有了明确边界：我们先追求“Verilog 输入能进入 Alliance 综合链”，而不是一开始追求完整 Verilog 标准。

## 5. 阶段 M1：建立最小 Verilog 前端

对应提交：`2fcc931 add verilog initial parsing logic`

这一阶段新增了完整的 `alliance/src/vlog2vbe` 目录，包括：

- `configure.in`、`Makefile.am`、`src/Makefile.am`：接入 Alliance 原有 autotools 体系。
- `vlog_ast.[ch]`：定义 Verilog AST。
- `vlog_parse.[ch]`：实现基础 Verilog parser。
- `vlog_emit.[ch]`：将 AST 输出成 Alliance `.vbe`。
- `vlog_main.c`：命令行入口。
- `README.md`、`man1/vlog2vbe.1`、`tests/`：使用说明和测试。

### 5.1 为什么先做独立 AST

Verilog 语法和 Alliance `.vbe` 表示之间并不是一一对应关系。例如 Verilog 有 bit-select、part-select、拼接、条件表达式、不同宽度常量，而 VBE 更偏向布尔/向量表达式。直接边 parse 边输出会让逻辑混乱，后续 `always` 和层次结构也很难扩展。

因此我们先设计了轻量 AST：

- `VlogModule` 表示模块。
- `VlogSignal` 表示端口和内部信号。
- `VlogAssign` 表示连续赋值。
- `VlogExpr` 表示表达式树。
- `VlogRef` 表示信号引用及位选/片选。

这个 AST 只覆盖可综合子集，保持足够小，便于后续阶段扩展。

### 5.2 为什么使用手写 parser

计划文档中曾考虑 lex/yacc 风格，但第一版实际采用手写递归下降 parser。原因是：

1. 第一版 Verilog 子集有限，手写 parser 更快落地。
2. 避免引入额外生成文件和 yacc/flex 版本差异。
3. 方便在 parser 中加入清晰错误信息。
4. 与独立命令行工具配合，调试成本低。

这不是否定 lex/yacc，而是为了课程项目第一版闭环做出的工程取舍。

### 5.3 M1 支持内容

M1 支持：

- 单模块文件。
- ANSI/non-ANSI 端口声明。
- `input/output/inout/wire/reg`。
- 标量和向量范围 `[msb:lsb]`。
- `assign` 连续赋值。
- 标识符、常量、位选、片选、拼接。
- `~ ! & | ^ && || == != ?:` 等基础逻辑表达式。

输出侧则实现了基础 VBE emitter：

- Verilog `input/output` 映射到 VBE `PORT`。
- 内部 `wire/reg` 映射到 VBE `SIGNAL`。
- `assign` 映射为 VBE 并发赋值。
- 常量归一化为 `'0'/'1'` 或 `B"..."`。
- 向量表达式按需要进行逐 bit 展开。

M1 的测试包括 `simple_and.v`、`mux2.v`、`vector_ops.v`，并加入 `expected/simple_and.vbe` 作为最小 golden output。

## 6. 阶段 M2：组合 always 支持

对应提交：`a522ccd add always support`

M1 只能处理 dataflow 风格的 `assign`。但实际 Verilog 中大量组合逻辑会写成：

```verilog
always @* begin
  if (sel) y = b;
  else y = a;
end
```

所以 M2 的目标是把组合过程块 lowering 成普通组合表达式。

### 6.1 过程语句中间表示

M2 在 parser 内部引入了过程语句结构：

- blocking assignment。
- `begin/end` block。
- `if/else`。
- `case/default`。

这些语句不会直接输出 VBE，而是先构造临时过程 AST，再降低为连续赋值。

### 6.2 if/case 的 lowering 思路

组合 `always @*` 本质上要变成每个目标信号的组合表达式。

对于 `if/else`：

```verilog
if (sel) y = b;
else y = a;
```

lowering 为：

```text
y = sel ? b : a
```

再由 VBE emitter 展开为：

```vhdl
y <= ((sel and b) or ((not sel) and a));
```

对于 `case`，每个 case item 被转换为“selector == label”的条件表达式，再串成条件链。

### 6.3 为什么主动拒绝 latch

组合 always 如果某些路径没有赋值，Verilog 综合会推断 latch。但 Alliance `.vbe` 后端对隐式 latch 支持不如寄存器模板明确，课程项目也更关注标准组合/时序综合。因此 M2 对组合 always 做路径覆盖检查：

- 如果 `if` 缺少某一路赋值，报错。
- 如果 `case` 没有 default 或某目标缺少赋值，报错。

这样避免“看起来能输出，但综合语义不清”的情况。

M2 新增测试：

- `always_mux.v`
- `always_default.v`
- `case_decoder.v`

并固定 `always_mux.vbe`、`always_default.vbe` expected 输出。

## 7. 阶段 M3：时序 always 与寄存器支持

对应提交：`2bcc308 add always @(posedge clk) support`

M3 让前端从组合逻辑扩展到基本时序逻辑，支持：

- `always @(posedge clk)`
- `always @(negedge clk)`
- blocking/nonblocking assignment
- enable register
- 常见异步 reset 模板

### 7.1 AST 增加寄存器驱动

M3 在 AST 中增加 `VlogRegDriver`，用于描述寄存器赋值：

- 目标寄存器。
- clock 名称。
- posedge/negedge。
- guard 条件。
- next-state 表达式。

这样 parser 不必直接拼 VBE 文本，emitter 可以统一处理寄存器输出。

### 7.2 VBE 中如何表示寄存器

VBE 使用 `REGISTER` 类型和 guarded block 表达边沿触发行为。比如：

```vhdl
SIGNAL q_reg : REG_BIT REGISTER;
label0 : BLOCK ((clk and not (clk'STABLE)) = '1')
BEGIN
  q_reg <= GUARDED d;
END BLOCK label0;
q <= q_reg;
```

如果 Verilog 输出端口本身是寄存器，例如 `output reg q`，实现中会使用内部存储名 `q_reg`，再把 `q <= q_reg` 接回输出端口。这样既符合 VBE register 表示，也保持顶层端口名称稳定。

### 7.3 enable register 的处理

对于：

```verilog
if (en) q <= d;
```

缺失的 else 在时序逻辑里不是 latch，而是“保持原值”。因此 M3 和 M2 的策略不同：组合 always 缺赋值报错；时序 always 缺赋值则自动转换为 self reference，也就是 `q` 保持。

### 7.4 异步 reset 模板

M3 支持常见模板：

```verilog
always @(posedge clk or negedge rst_n) begin
  if (!rst_n) q <= 1'b0;
  else q <= d;
end
```

lowering 时拆成两类 driver：

1. reset 条件下的 level-sensitive guarded assignment。
2. clock 边沿下、且 reset 不活跃时的 next-state assignment。

这样既保持 reset 优先级，也能被后端按 VBE register 结构处理。

M3 新增测试：

- `seq/dff.v`
- `seq/dff_en.v`
- `seq/dff_async_reset.v`

并增加对应 expected VBE 文件。

## 8. 阶段 M4：多模块、实例化与层次展开

对应提交：`df7683b Add multi module support`

M4 解决真实工程中常见的层次结构问题。之前 `vlog2vbe` 只能处理一个 module；M4 后支持一个 `.v` 文件里多个 module，并支持 module instance。

### 8.1 为什么引入 elaboration 层

实例化支持不是简单语法问题。解析到：

```verilog
and2 u0(.a(a), .b(b), .y(n1));
```

还需要知道：

- `and2` 模块在哪里。
- `a/b/y` 各是什么方向。
- 子模块端口连接到父模块哪个信号。
- 子模块内部 wire 如何避免命名冲突。
- 子模块里是否还有实例。

因此 M4 没有把所有逻辑塞进 parser，而是新增 `vlog_elab.[ch]`，把流程拆成：

```text
parse design -> select top -> elaborate/flatten -> emit VBE
```

这是 M4 最重要的结构性变化。

### 8.2 AST 扩展

M4 新增：

- `VlogDesign`：表示一个文件中的多个 module。
- `VlogInstance`：表示一个 module instance。
- `VlogConn`：表示端口连接。

parser 从 `vlog_parse_file(..., VlogModule*)` 扩展出：

```c
vlog_parse_design_file(..., VlogDesign*)
```

旧接口仍保留，作为单模块兼容包装。

### 8.3 端口连接支持

M4 支持两种基础连接方式：

```verilog
and2 u0(.a(a), .b(b), .y(y));
and2 u1(a, b, y);
```

设计上要求同一个实例内不能混用命名端口和位置端口。这样可以让错误更早暴露，也避免端口绑定规则复杂化。

对于输出端口，要求连接到可赋值 reference，例如 `y` 或简单位选。输出端口连接到复杂表达式会报错，因为 `.vbe` 目标必须是可驱动信号。

### 8.4 为什么选择 flatten

M4 有两个可选策略：

1. 每个 Verilog module 输出一个 `.vbe`。
2. 把层次结构 flatten 成一个 top `.vbe`。

实现选择 flatten，原因是：

- BOOM/BOOG 的输入主路径是单个 `.vbe` 行为描述。
- flatten 后不需要处理跨 `.vbe` module 绑定。
- 对课程项目的小规模可综合设计更直观。
- 可以复用原来的 emitter，不需要引入多文件输出管理。

flatten 的核心规则：

- 顶层端口和顶层内部信号保持原名。
- 子模块内部信号加实例名前缀，如 `u_stage_nb`。
- 子模块 input reference 替换为父模块连接表达式。
- 子模块 output assignment 改写为父模块连接目标。
- 子模块内部实例递归展开。
- 检测递归实例，避免无限展开。

后续与 BOOM 联调时发现，旧版 BVL/VBE parser 对标识符规则比较严格，只接受类似 `{letter}(_?{letter_or_digit})*` 的形式，连续下划线会被拒绝。因此 flatten 命名从最初的双下划线分隔改为单下划线分隔，避免生成 `u_stage__nb` 这类后端无法解析的名字。

### 8.5 top 选择

M4 支持三种 top 选择方式：

1. 命令行 `-top top`。
2. 位置参数 module 名。
3. 如果多模块文件中只有一个 module 未被其他 module 实例化，则自动推断它是 top。

如果无法唯一推断，就要求用户显式传入 `-top`。

M4 新增测试：

- `hier/named_two_level.v`：命名端口，两个组合子模块。
- `hier/positional_chain.v`：位置端口。
- `hier/nested_modules.v`：递归层次 flatten。
- `hier/seq_instance.v`：实例化时序子模块。

这些测试证明 M4 不只是识别 instance 语法，而是真正完成了端口绑定和层次展开。

## 9. 测试策略演进

本项目测试方式也按阶段演进。

M1 时只验证最小 assign 输出：

```sh
vlog2vbe simple_and.v simple_and
diff expected/simple_and.vbe generated/simple_and.vbe
```

M2 后加入组合 always golden output，重点验证：

- if/case lowering。
- ternary 表达式 VBE 展开。
- latch prevention。

M3 后加入时序 golden output，重点验证：

- register signal 声明。
- clock edge guarded block。
- enable self-hold。
- async reset 拆分。

M4 后加入层次用例，重点验证：

- 多 module parse。
- named/positional port binding。
- top selection。
- recursive flatten。
- 子模块寄存器驱动提升到顶层。

当前统一测试命令为：

```sh
gcc -std=c99 -Wall -Wextra \
  -o alliance/src/vlog2vbe/src/vlog2vbe \
  alliance/src/vlog2vbe/src/vlog_ast.c \
  alliance/src/vlog2vbe/src/vlog_elab.c \
  alliance/src/vlog2vbe/src/vlog_emit.c \
  alliance/src/vlog2vbe/src/vlog_main.c \
  alliance/src/vlog2vbe/src/vlog_parse.c

cd alliance/src/vlog2vbe
make -C tests VLOG2VBE=../src/vlog2vbe clean check
```

在完整 Alliance 环境中，还可以继续验证后端闭环：

```sh
vlog2vbe -top top -o top.vbe tests/hier/nested_modules.v
boom top top_o
boog top_o top_g
loon top_g top_l
```

联调阶段还补充了一个后端兼容性修正：`vlog2vbe` 输出的 VBE 会自动追加 `vdd`、`vss` 输入端口，并在 architecture 开头生成 Alliance 单元库常见的 power-supply assertion。这样生成的 `.vbe` 可以直接进入 BOOG/LOON 使用的电源端口约定，不需要同学们手工修改中间文件。

## 10. 关键设计取舍总结

### 10.1 不改后端，先补前端

这是项目成功的关键。Alliance 的后端工具已经围绕 `.vbe` 运转多年，直接复用它们比重写综合算法更可靠，也更符合课程项目范围。

### 10.2 输出 `.vbe` 而不是直接输出门级网表

如果直接从 Verilog 生成 `.vst`，就必须自己做逻辑优化和标准单元映射，等于绕开 BOOM/BOOG。选择 `.vbe` 可以把项目控制在“语言前端”范围内，并证明 Verilog 输入能进入 Alliance 原有综合链。

### 10.3 先文本生成 VBE，保留未来 BEH API 接入空间

计划中提到可以通过 BEH API 构造 `befig_list` 后调用 driver 输出。当前实现为了快速闭环，采用手写 VBE emitter。这个做法的优点是独立、可调试、依赖少；缺点是需要自己保证 VBE 文本格式合法。后续如果时间允许，可以把 emitter 替换为 BEH 数据结构构造，以获得更强的内部一致性。

### 10.4 组合 always 禁止隐式 latch

这是为了保证第一版输出的 `.vbe` 更可控。Verilog 中隐式 latch 虽然可综合，但容易导致后端语义和预期不一致。第一版选择报错而不是猜测用户意图。

### 10.5 时序 always 支持 self-hold

与组合逻辑不同，时序逻辑中的缺失分支通常表示寄存器保持。因此 M3 对 enable register 做 self-reference，符合硬件语义。

### 10.6 层次结构采用 flatten

flatten 避免了多 `.vbe` 文件之间的 module 绑定问题，也让 BOOM/BOOG 继续处理单个 top 行为描述。对课程项目的小型设计来说，这是最稳妥的层次化实现方式。

## 11. 当前能力边界

当前版本已经支持课程展示所需的基础可综合 Verilog 子集：

- 多 module 文件。
- ANSI/non-ANSI port declaration。
- `input/output/inout/wire/reg`。
- scalar/vector signal。
- `assign`。
- 组合 `always @*`、`if/else`、`case/default`。
- `posedge/negedge` 时序 always。
- enable register。
- 常见异步 reset。
- 命名端口和位置端口实例化。
- 层次 flatten。
- 常见布尔表达式、比较、拼接、位选/片选、条件表达式。

暂不支持：

- parameter/localparam 和 parameter override。
- generate。
- arithmetic operator。
- signed 复杂规则。
- 四态 `x/z` 精确语义。
- tri-state 多驱动总线。
- procedural loop。
- 更复杂的 always sensitivity list。

这些限制是主动收敛范围的结果，不是架构上无法扩展。后续可以继续按阶段补齐。

## 12. 答辩时可强调的主线

答辩时建议按以下逻辑讲述：

1. Alliance 已经有成熟的 `.vbe -> boom -> boog -> loon` 综合后端。
2. 项目缺口是 Verilog 输入前端，而不是后端综合算法。
3. 我们选择新增 `vlog2vbe`，让 Verilog 转成 `.vbe`，复用原后端。
4. 实现按 M1-M4 逐步推进：
   - M1：assign/dataflow 最小闭环。
   - M2：组合 always lowering。
   - M3：时序 always/register lowering。
   - M4：多模块、实例化、flatten。
5. 每个阶段都有对应测试和 expected output。
6. 最终形成：

   ```text
   Verilog .v -> vlog2vbe -> Alliance .vbe -> boom -> boog -> loon
   ```

7. 当前实现覆盖课程项目常见基础功能，复杂 Verilog 特性作为后续工作。

这条叙述能体现：我们不是简单写了一个格式转换脚本，而是在理解 Alliance 原有架构后，选择了低风险、高复用、可验证的扩展方案。
