# 扩展版 Alliance 安装与测试指南

本文档基于原始资料 `Alliance 安装指导/安装指导书/alliance 安装指导书.docx` 整理，并补充本项目新增 Verilog 前端 `vlog2vbe` 的安装、回归测试和综合闭环验证步骤。

## 1. 原安装指导书要点

原安装指导书面向课程服务器，核心流程是：

1. 登录服务器，可以使用 MobaXterm 的 SSH/VNC，或先 SSH 后启动 `vncserver`。
2. 在用户目录下创建源码目录：

   ```sh
   mkdir -p ${HOME}/coriolis-2.x/src
   cd ${HOME}/coriolis-2.x/src
   ```

3. 下载或上传 Alliance 源码。原指导书给了两种方式：
   - 服务器联网后执行 `git clone https://gitlab.lip6.fr/jpc/alliance.git`
   - 上传附件里的压缩包，放到 `${HOME}/coriolis-2.x/src` 后解压。

4. 使用 `autostuff -> configure -> make install` 编译安装，安装目录默认放在：

   ```sh
   ${HOME}/coriolis-2.x/Linux.SL7_64/Release.Shared/install
   ```

本项目要测试“扩展版 Alliance”，不要直接使用未修改的上游 Alliance 源码；必须使用包含 `alliance/src/vlog2vbe` 的本项目源码。

## 2. 推荐目录布局

为避免旧版 autotools、shell 脚本和编译器对中文路径处理不稳定，建议在服务器或 WSL 中使用纯 ASCII 路径。

推荐把本项目放到：

```sh
${HOME}/coriolis-2.x/src/eda-alliance-verilog
```

对应的 Alliance 源码目录应为：

```sh
${HOME}/coriolis-2.x/src/eda-alliance-verilog/alliance/src
```

检查扩展源码是否存在：

```sh
srcDir=${HOME}/coriolis-2.x/src/eda-alliance-verilog/alliance/src
test -d "${srcDir}/vlog2vbe" && echo "vlog2vbe source found"
```

如果输出不是 `vlog2vbe source found`，说明源码目录不对，或者使用了未扩展的原版 Alliance。

## 3. 获取扩展源码

有两种推荐方式。

方式 A：从课程项目仓库获取。本地或服务器上 clone 你们提交用的项目仓库，要求 clone 后能看到：

```text
alliance/src/vlog2vbe/
docs/
```

方式 B：从本机打包上传服务器。在本机项目根目录执行：

```sh
tar czf eda-alliance-verilog.tar.gz alliance docs .gitattributes .gitignore
```

把 `eda-alliance-verilog.tar.gz` 上传到服务器的：

```sh
${HOME}/coriolis-2.x/src
```

然后在服务器上解压并改成推荐目录名：

```sh
cd ${HOME}/coriolis-2.x/src
tar xzf eda-alliance-verilog.tar.gz
mkdir -p eda-alliance-verilog
mv alliance docs .gitattributes .gitignore eda-alliance-verilog/
```

如果你们上传的是整个项目文件夹压缩包，解压后只需要保证最终路径满足：

```sh
${HOME}/coriolis-2.x/src/eda-alliance-verilog/alliance/src/vlog2vbe
```

后续命令中的 `srcDir` 都按这个路径设置。

## 4. 服务器完整安装

适用场景：要验证 `vlog2vbe -> .vbe -> boom -> boog -> loon` 的完整综合链。

先设置路径：

```sh
srcDir=${HOME}/coriolis-2.x/src/eda-alliance-verilog/alliance/src
commonRoot=${HOME}/coriolis-2.x/Linux.SL7_64/Release.Shared
buildDir=${commonRoot}/build
installDir=${commonRoot}/install

export ALLIANCE_TOP=${installDir}
export LD_LIBRARY_PATH=${installDir}/lib:${LD_LIBRARY_PATH}
```

生成 configure 脚本并配置：

```sh
cd "${srcDir}"
./autostuff clean
./autostuff

mkdir -p "${buildDir}"
cd "${buildDir}"
"${srcDir}/configure" --prefix="${ALLIANCE_TOP}" --enable-alc-shared
```

编译安装：

```sh
make -j1 install
```

安装完成后设置环境：

```sh
export PATH=${ALLIANCE_TOP}/bin:${PATH}
export LD_LIBRARY_PATH=${ALLIANCE_TOP}/lib:${LD_LIBRARY_PATH}

if [ -f "${ALLIANCE_TOP}/etc/alc_env.sh" ]; then
  . "${ALLIANCE_TOP}/etc/alc_env.sh"
fi
```

检查工具是否安装成功：

```sh
which vlog2vbe
which boom
which boog
which loon
vlog2vbe -V
```

期望至少能看到 `vlog2vbe`、`boom`、`boog`、`loon` 的路径。

## 5. 本地 WSL 轻量测试

适用场景：只测试新增 Verilog 前端，不跑 Alliance 后端。

在 Ubuntu/WSL 中安装基础工具：

```sh
sudo apt update
sudo apt install -y build-essential make gcc diffutils git
```

进入项目目录并手动编译 `vlog2vbe`：

```sh
cd /mnt/d/开发区/EDA大作业

gcc -std=c99 -Wall -Wextra \
  -o alliance/src/vlog2vbe/src/vlog2vbe \
  alliance/src/vlog2vbe/src/vlog_ast.c \
  alliance/src/vlog2vbe/src/vlog_elab.c \
  alliance/src/vlog2vbe/src/vlog_emit.c \
  alliance/src/vlog2vbe/src/vlog_main.c \
  alliance/src/vlog2vbe/src/vlog_parse.c
```

运行前端回归：

```sh
cd alliance/src/vlog2vbe
make -C tests VLOG2VBE=../src/vlog2vbe clean check
```

如果命令结束时没有 `diff` 报错，则说明新增 Verilog 前端通过了当前回归测试。

## 6. 扩展版前端回归测试

如果已经通过完整安装得到 `${ALLIANCE_TOP}/bin/vlog2vbe`，推荐用安装后的程序跑回归：

```sh
cd "${srcDir}/vlog2vbe"
make -C tests VLOG2VBE="${ALLIANCE_TOP}/bin/vlog2vbe" clean check
```

当前回归覆盖：

- `comb/`：连续赋值、向量表达式、组合 always、if/case。
- `seq/`：DFF、enable register、异步 reset DFF。
- `hier/`：多模块文件、命名端口实例、位置端口实例、嵌套模块、时序子模块实例。

也可以单独测试层次化 Verilog：

```sh
cd "${srcDir}/vlog2vbe"
vlog2vbe -top top -o /tmp/nested_modules.vbe tests/hier/nested_modules.v
head -20 /tmp/nested_modules.vbe
```

期望输出以 `ENTITY top IS` 开头，并包含 Alliance 后端需要的电源端口：

```sh
grep -E "vdd|vss" /tmp/nested_modules.vbe
```

层次 flatten 产生的内部信号名也应兼容 BOOM 的旧 VBE parser，不应包含连续下划线：

```sh
grep "__" /tmp/nested_modules.vbe && echo "unexpected double underscore" || echo "flat names ok"
```

## 7. 完整综合闭环 smoke test

这一步验证 Verilog 输入已经真正进入 Alliance 原有综合后端。

创建临时测试目录：

```sh
work=${HOME}/vlog2vbe-smoke
rm -rf "${work}"
mkdir -p "${work}"
cd "${work}"
```

复制一个层次化组合 Verilog 用例：

```sh
cp "${srcDir}/vlog2vbe/tests/hier/nested_modules.v" top.v
```

生成 Alliance VBE：

```sh
vlog2vbe -top top -o top.vbe top.v
ls -l top.vbe
```

建议先检查生成的 VBE 是否带有电源端口，且层次展开信号名没有连续下划线：

```sh
grep -E "vdd|vss" top.vbe
grep "__" top.vbe && echo "unexpected double underscore" || echo "flat names ok"
```

设置 Alliance 后端环境变量。标准单元库一般在安装目录下的 `sxlib`，可先自动查找：

```sh
export MBK_WORK_LIB=.
export MBK_CATAL_NAME=CATAL
export MBK_IN_LO=vst
export MBK_OUT_LO=vst

export MBK_TARGET_LIB=$(find "${ALLIANCE_TOP}" -type d -name sxlib | head -n 1)
export MBK_CATA_LIB=${MBK_TARGET_LIB}

echo "MBK_TARGET_LIB=${MBK_TARGET_LIB}"
```

如果 `MBK_TARGET_LIB=` 后面为空，说明标准单元库没有安装成功，或安装目录不对。

运行综合链：

```sh
boom -V top top_o
boog top_o top_g
loon top_g top_l
```

检查结果：

```sh
ls -l top.vbe top_o.vbe top_g.vst top_l.vst
```

看到这些文件即表示：

```text
Verilog .v -> vlog2vbe -> Alliance .vbe -> boom -> boog -> loon
```

这条链路已跑通。

## 8. 可选：时序层次用例

当前 `vlog2vbe` 已支持常见边沿触发 always 和简单时序子模块 flatten。可以再测试：

```sh
cd "${work}"
cp "${srcDir}/vlog2vbe/tests/hier/seq_instance.v" seq_top.v
vlog2vbe -top top -o seq_top.vbe seq_top.v
head -40 seq_top.vbe
```

期望能看到类似：

```vhdl
SIGNAL q_reg : REG_BIT REGISTER;
label0 : BLOCK ((clk and not (clk'STABLE)) = '1')
```

如果要继续跑 `boom/boog/loon`，使用同样的环境变量：

```sh
boom -V seq_top seq_top_o
boog seq_top_o seq_top_g
loon seq_top_g seq_top_l
```

## 9. 常见问题

### `vlog2vbe: command not found`

没有安装扩展版工具，或 `PATH` 没有包含 `${ALLIANCE_TOP}/bin`。

处理：

```sh
export PATH=${ALLIANCE_TOP}/bin:${PATH}
which vlog2vbe
```

如果仍然找不到，确认 `${srcDir}/vlog2vbe` 是否存在，并重新 `./autostuff && configure && make install`。

### `boom/boog/loon: command not found`

Alliance 没有完整安装，或环境没有加载。

处理：

```sh
export PATH=${ALLIANCE_TOP}/bin:${PATH}
. ${ALLIANCE_TOP}/etc/alc_env.sh
which boom boog loon
```

### `MBK_TARGET_LIB` 为空或 `boog` 报 cell library 错误

`boog` 和 `loon` 需要标准单元库，原实验 Makefile 使用的是 `sxlib`。

处理：

```sh
find "${ALLIANCE_TOP}" -type d -name sxlib
export MBK_TARGET_LIB=/实际找到的/sxlib
export MBK_CATA_LIB=${MBK_TARGET_LIB}
```

### `configure` 报 X11 或 Motif 缺失

完整 Alliance 构建需要 X11/Motif 相关库。课程服务器通常已经准备好；如果在自己的 Ubuntu/WSL 中编译完整 Alliance，可尝试：

```sh
sudo apt install -y xorg-dev libx11-dev libxt-dev libxpm-dev libmotif-dev
```

### 在中文路径下编译失败或行为异常

旧版 autotools 和部分 C 工具对非 ASCII 路径支持不好。建议把源码放在：

```sh
${HOME}/coriolis-2.x/src/eda-alliance-verilog
```

不要在服务器上使用中文目录名作为源码路径。

### `make -C tests ... check` 的 `diff` 失败

可能原因：

- 使用的不是最新扩展源码。
- 手动改过 expected 文件。
- 编译的 `vlog2vbe` 不是当前源码生成的。

处理：

```sh
cd "${srcDir}/vlog2vbe"
make -C tests clean
which vlog2vbe
vlog2vbe -V
make -C tests VLOG2VBE="${ALLIANCE_TOP}/bin/vlog2vbe" check
```

## 10. 当前扩展支持范围

已支持：

- 单文件多个 `module`。
- ANSI 与非 ANSI port declaration。
- `input/output/inout/wire/reg` 和 `[msb:lsb]` 向量。
- `assign`。
- 组合 `always @*` / `always @(*)`，支持 blocking assignment、`if/else`、`case/default`。
- 边沿触发 `always @(posedge clk)` / `always @(negedge clk)`。
- 常见异步 reset 模板。
- 简单 module instance。
- 整数 `parameter/localparam` 声明，以及实例上的命名/位置参数覆盖。
- 命名端口连接和位置端口连接。
- 层次 flatten 到 top module。
- 静态 `generate for` / `genvar` 展开，循环体支持连续赋值和简单实例。
- 生成 BOOM 旧 VBE parser 可接受的 flatten 信号名。
- VBE 自动附加 `vdd`/`vss` 电源端口和 power-supply assertion，方便直接进入 BOOG/LOON。
- `-top module` 指定 top，以及唯一未被实例化模块的自动 top 推断。
- 常见布尔表达式、位选、片选、拼接、条件表达式，以及 `+/-/*` 普通 RTL 算术。

暂不支持：

- generate-if / generate-case。
- delay / specify / UDP / system task。
- 四态 `x/z` 精确语义。
- 普通 RTL 表达式中的除法和取模。
- output port 连接到复杂表达式。
- 多驱动 tri-state 总线。

## 11. 最小验收清单

提交或演示前建议至少完成：

```sh
which vlog2vbe
vlog2vbe -V

cd "${srcDir}/vlog2vbe"
make -C tests VLOG2VBE="${ALLIANCE_TOP}/bin/vlog2vbe" clean check

cd "${HOME}/vlog2vbe-smoke"
ls -l top.vbe top_o.vbe top_g.vst top_l.vst
```

若没有完整 Alliance 后端环境，至少完成本地 WSL 的前端回归测试，并在报告中说明 `boom/boog/loon` 需要在课程服务器或完整 Alliance 安装环境中验证。
