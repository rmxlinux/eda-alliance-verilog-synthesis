# 扩展版 Alliance 安装与 Verilog 综合实验文档

本文档面向课程验收和复现实验，基于旧资料 `Alliance 安装指导/安装指导书/alliance 安装指导书.docx` 的安装流程整理，并补充本项目新增 Verilog 前端 `vlog2vbe` 的具体测试内容。

本次实验验证目标：

1. 从源码完整构建扩展版 Alliance。
2. 确认新增工具 `vlog2vbe` 能安装并运行。
3. 运行前端回归测试，覆盖组合、时序和层次化 Verilog。
4. 验证 Verilog 输入可以进入原 Alliance 后端链路：

```text
Verilog .v -> vlog2vbe -> Alliance .vbe -> boom -> boog -> loon -> .vst
```

## 1. 资料来源

旧安装资料位于：

```text
Alliance 安装指导/安装指导书/alliance 安装指导书.docx
Alliance 安装指导/安装指导书/Alliance实验指导书.docx
Alliance 安装指导/附件/alliance实验文件/
```

旧安装指导的核心流程是：登录课程服务器，准备源码目录，运行 `autostuff` 生成 `configure`，再执行 `configure` 和 `make install` 安装 Alliance。本文档沿用这一流程，但源码必须使用本项目仓库中的扩展版源码，而不是未修改的原版 Alliance。

扩展源码应包含：

```text
alliance/src/vlog2vbe/
docs/
```

## 2. 推荐实验环境

推荐在课程服务器或 WSL/Linux 中测试。旧版 Autotools 和部分 Alliance 脚本对中文路径兼容性较差，因此建议使用纯 ASCII 路径。

推荐目录布局：

```sh
projectRoot=${HOME}/coriolis-2.x/src/eda-alliance-verilog
srcDir=${projectRoot}/alliance/src
commonRoot=${HOME}/coriolis-2.x/Linux.SL7_64/Release.Shared
buildDir=${commonRoot}/build
installDir=${commonRoot}/install
```

检查源码是否正确：

```sh
test -d "${srcDir}/vlog2vbe" && echo "vlog2vbe source found"
```

如果没有输出 `vlog2vbe source found`，说明源码目录不对，或者使用了未扩展的 Alliance 源码。

## 3. 完整安装步骤

设置路径：

```sh
export ALLIANCE_TOP=${installDir}
export LD_LIBRARY_PATH=${ALLIANCE_TOP}/lib:${LD_LIBRARY_PATH}
```

生成 `configure`：

```sh
cd "${srcDir}"
./autostuff clean
./autostuff
```

配置：

```sh
mkdir -p "${buildDir}" "${installDir}"
cd "${buildDir}"
"${srcDir}/configure" --prefix="${ALLIANCE_TOP}" --enable-alc-shared
```

编译并安装：

```sh
make -j1 install
```

如果在部分 Linux/WSL 环境中遇到如下链接错误：

```text
/usr/bin/ld: cannot find -lMut: No such file or directory
```

这是旧 Alliance 构建系统在共享库路径上的问题。确认 `libMut` 已经生成后，可继续执行：

```sh
LIBRARY_PATH=${ALLIANCE_TOP}/lib:${buildDir}/mbk/src/.libs \
LD_LIBRARY_PATH=${ALLIANCE_TOP}/lib:${buildDir}/mbk/src/.libs:${LD_LIBRARY_PATH} \
make -j1 install
```

安装完成后加载环境：

```sh
export PATH=${ALLIANCE_TOP}/bin:${PATH}
export LD_LIBRARY_PATH=${ALLIANCE_TOP}/lib:${LD_LIBRARY_PATH}

if [ -f "${ALLIANCE_TOP}/etc/alc_env.sh" ]; then
  . "${ALLIANCE_TOP}/etc/alc_env.sh"
fi
```

检查工具：

```sh
which vlog2vbe
which boom
which boog
which loon
vlog2vbe -V
```

期望 `vlog2vbe -V` 输出类似：

```text
vlog2vbe 0.1
```

## 4. 前端回归测试

进入 `vlog2vbe` 目录：

```sh
cd "${srcDir}/vlog2vbe"
make -C tests VLOG2VBE="${ALLIANCE_TOP}/bin/vlog2vbe" clean check
```

当前回归会生成 13 个 VBE 文件：

- 组合逻辑：`simple_and`、`mux2`、`vector_ops`、`always_mux`、`always_default`、`case_decoder`
- 时序逻辑：`dff`、`dff_en`、`dff_async_reset`
- 层次结构：`named_two_level`、`positional_chain`、`seq_instance`、`nested_modules`

其中 10 个用例会与 `tests/expected/*.vbe` 做 `diff -u` 对比。命令正常结束且没有 diff 输出，即表示前端回归通过。

## 5. VBE 后端兼容性检查

本项目针对 BOOM/BOOG 后端兼容做了两点处理：

1. 自动为输出 VBE 增加 `vdd` 和 `vss` 电源端口。
2. 层次 flatten 时避免生成连续下划线，例如不再生成 `u_stage__nb`，而生成 `u_stage_nb`。

可用层次化组合用例检查：

```sh
cd "${srcDir}/vlog2vbe"
vlog2vbe -top top -o /tmp/nested_modules.vbe tests/hier/nested_modules.v
head -30 /tmp/nested_modules.vbe
grep -E "vdd|vss" /tmp/nested_modules.vbe
grep "__" /tmp/nested_modules.vbe && echo "unexpected double underscore" || echo "flat names ok"
```

期望能看到：

```vhdl
vdd : in BIT;
vss : in BIT
ASSERT ((vdd and not (vss)) = '1')
SIGNAL u_stage_nb : BIT;
```

并输出：

```text
flat names ok
```

## 6. 完整后端闭环测试：层次化组合用例

创建测试目录：

```sh
work=${HOME}/vlog2vbe-smoke-nested
rm -rf "${work}"
mkdir -p "${work}"
cd "${work}"
```

复制测试输入：

```sh
cp "${srcDir}/vlog2vbe/tests/hier/nested_modules.v" top.v
```

生成 VBE：

```sh
vlog2vbe -top top -o top.vbe top.v
```

设置后端环境：

```sh
export MBK_WORK_LIB=.
export MBK_CATAL_NAME=CATAL
export MBK_IN_LO=vst
export MBK_OUT_LO=vst
export MBK_TARGET_LIB=$(find "${ALLIANCE_TOP}" -type d -name sxlib | head -n 1)
export MBK_CATA_LIB=${MBK_TARGET_LIB}
```

运行综合链：

```sh
boom -V top top_o
boog top_o top_g
loon top_g top_l
```

检查输出：

```sh
ls -l top.vbe top_o.vbe top_g.vst top_l.vst
```

看到四个文件即表示层次化组合 Verilog 已经跑通：

```text
top.vbe
top_o.vbe
top_g.vst
top_l.vst
```

## 7. 完整后端闭环测试：时序层次用例

创建测试目录：

```sh
work=${HOME}/vlog2vbe-smoke-seq
rm -rf "${work}"
mkdir -p "${work}"
cd "${work}"
```

复制测试输入：

```sh
cp "${srcDir}/vlog2vbe/tests/hier/seq_instance.v" top.v
```

生成 VBE：

```sh
vlog2vbe -top top -o top.vbe top.v
head -40 top.vbe
```

期望看到寄存器结构：

```vhdl
SIGNAL q_reg : REG_BIT REGISTER;
label0 : BLOCK ((clk and not (clk'STABLE)) = '1')
```

运行后端：

```sh
boom -V top top_o
boog top_o top_g
loon top_g top_l
ls -l top.vbe top_o.vbe top_g.vst top_l.vst
```

看到 `top_l.vst` 即表示时序层次用例也完成了从 Verilog 到门级网表的闭环。

## 8. 负向测试

负向测试用于确认当前工具会拒绝尚未支持或会破坏后端约定的输入。

### 8.1 parameter 不支持

```sh
cat > unsupported_parameter.v <<'EOF'
module bad #(parameter W = 1) (input a, output y);
  assign y = a;
endmodule
EOF

vlog2vbe -o unsupported_parameter.vbe unsupported_parameter.v bad
```

期望失败，错误类似：

```text
vlog2vbe: parse error in unsupported_parameter.v: line 1, column 12: expected '(' after module name
```

### 8.2 vdd/vss 名称保留

```sh
cat > reserved_power.v <<'EOF'
module reserved_power(input vdd, output y);
  assign y = vdd;
endmodule
EOF

vlog2vbe -o reserved_power.vbe reserved_power.v reserved_power
```

期望失败，错误类似：

```text
vlog2vbe: emit error: signal names 'vdd' and 'vss' are reserved for Alliance power ports
```

## 9. 本次实测记录

本次在 WSL Ubuntu 环境中使用纯 ASCII 测试目录：

```text
/home/rmxlinux/codex-eda-alliance-current
```

完整安装结果：

- `./autostuff` 成功生成 `configure`
- `configure --prefix=... --enable-alc-shared` 成功
- 首次 `make -j1 install` 复现 `-lMut` 链接路径问题
- 补充 `LIBRARY_PATH` 和 `LD_LIBRARY_PATH` 后，`make -j1 install` 成功

工具检查结果：

```text
vlog2vbe 0.1
```

前端回归：

- 生成 `tests/generated/*.vbe` 共 13 个
- `diff -u` 对比项全部通过
- `nested_modules.vbe` 包含 `vdd/vss`
- `nested_modules.vbe` 不含连续下划线

后端闭环：

- `nested_modules.v` 通过 `vlog2vbe -> boom -> boog -> loon`
- `seq_instance.v` 通过 `vlog2vbe -> boom -> boog -> loon`
- 两个测试均生成 `top.vbe`、`top_o.vbe`、`top_g.vst`、`top_l.vst`

实测后端摘要：

```text
nested_modules: boog estimated critical path 657 ps, mapped cells total 2
seq_instance:   boog estimated critical path 505 ps, mapped cells total 3
```

负向测试：

- `parameter` 用例按预期失败
- 用户占用 `vdd` 名称按预期失败

## 10. 清理测试文件

如果使用本文档中的临时目录，可以清理：

```sh
rm -rf ${HOME}/vlog2vbe-smoke-nested
rm -rf ${HOME}/vlog2vbe-smoke-seq
```

如果使用了本文档记录的 WSL 测试目录：

```sh
rm -rf /home/rmxlinux/codex-eda-alliance-current
```

