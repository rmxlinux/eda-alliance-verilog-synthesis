# WSL Headless Verilog 支持部署与测试指南

本文档记录本项目在 WSL 中采用 headless 命令行子集的部署和测试方式。目标是验证新增 Verilog 前端 `vlog2vbe` 能正常把 Verilog 输入转换为 Alliance `.vbe`，并继续进入 `boom -> boog -> loon` 后端综合链。

## 1. 目录约定

以下命令默认在项目根目录执行：

```sh
cd /home/jiabingyu/prj/26_myprj/eda-alliance-verilog-synthesis
```

使用固定目录：

```sh
projectRoot=$(pwd)
srcDir=${projectRoot}/alliance/src
buildDir=${projectRoot}/alliance-build
installDir=${projectRoot}/alliance-install
```

主要产物目录：

```text
alliance-build/      # out-of-tree 构建目录
alliance-install/    # 本地安装目录，包含 bin/lib/cells
test-runs/           # 手工闭环测试输出
```

这些目录都是本地运行产物，已加入 `.gitignore`。

## 2. 生成 headless 构建系统

完整 Alliance GUI 工具依赖 Motif/Xpm。WSL 环境如果没有 `libmotif-dev`，顶层 configure 会卡在 `-lXm` 检查。课程验收重点是 Verilog 前端和命令行综合链，因此只构建必要子集：

```sh
cd "${srcDir}"
sh autostuff mbk aut abl bdd abt abe abv boom boog loon cells distrib vlog2vbe
```

说明：

- `vlog2vbe`：新增 Verilog 输入前端。
- `boom/boog/loon`：Alliance 原有行为优化、门级映射、本地网表优化链路。
- `cells`：安装 `sxlib` 等标准单元库。
- `mbk/aut/abl/bdd/abt/abe/abv`：后端依赖的核心库。

旧 `autostuff` 会因为目录名匹配方式额外带上 `log` 目录，这是无害的核心库构建项。

## 3. 配置与安装

创建构建和安装目录：

```sh
mkdir -p "${buildDir}" "${installDir}"
cd "${buildDir}"
```

如果系统已安装 Motif/Xpm 开发包，可直接配置：

```sh
"${srcDir}/configure" --prefix="${installDir}" --enable-alc-shared
```

如果缺少 Motif/Xpm 开发包，使用 headless 配置方式跳过全局 GUI 探测：

```sh
ac_cv_lib_Xm_XmCreateOptionMenu=yes \
ac_cv_lib_Xm_xmUseVersion=yes \
ac_cv_lib_Xm_XmInstallImage=yes \
ac_cv_lib_Xm_Xm21InstallImage=yes \
ac_cv_lib_Xpm_XpmCreatePixmapFromXpmImage=yes \
"${srcDir}/configure" --prefix="${installDir}" --enable-alc-shared
```

安装：

```sh
make -j1 install
```

如果遇到：

```text
/usr/bin/ld: cannot find -lMut: No such file or directory
```

继续执行：

```sh
LIBRARY_PATH=${installDir}/lib:${buildDir}/mbk/src/.libs \
LD_LIBRARY_PATH=${installDir}/lib:${buildDir}/mbk/src/.libs:${LD_LIBRARY_PATH} \
make -j1 install
```

安装成功后应得到：

```text
alliance-install/bin/vlog2vbe
alliance-install/bin/boom
alliance-install/bin/boog
alliance-install/bin/loon
alliance-install/cells/sxlib/
```

## 4. 加载运行环境

每次打开新 shell 后执行：

```sh
export PATH=${installDir}/bin:${PATH}
export LD_LIBRARY_PATH=${installDir}/lib:${LD_LIBRARY_PATH}
export ALLIANCE_TOP=${installDir}
```

检查工具：

```sh
which vlog2vbe
which boom
which boog
which loon
vlog2vbe -V
```

期望：

```text
vlog2vbe 0.1
```

## 5. 前端回归测试

运行安装版 `vlog2vbe` 的回归：

```sh
cd "${srcDir}/vlog2vbe"
make -C tests VLOG2VBE="${installDir}/bin/vlog2vbe" clean check
```

通过标准：

- 生成 `tests/generated/*.vbe` 共 18 个。
- 18 个生成文件全部和 `tests/expected/*.vbe` 做 `diff -u` 对比。
- 命令以 exit code 0 结束，且没有 diff 差异输出。

覆盖用例：

```text
组合逻辑：
simple_and, mux2, vector_ops, arithmetic, div_mod_signed_tristate,
always_mux, always_default, case_decoder

时序逻辑：
dff, dff_en, dff_async_reset

层次结构：
named_two_level, positional_chain, seq_instance, nested_modules,
parameterized, generate_for, generate_if_case
```

前端输出文件含义：

```text
tests/generated/*.vbe    # 本次由 Verilog 生成的 VBE
tests/expected/*.vbe     # golden 参考输出
```

## 6. 后端闭环测试：层次化组合用例

创建测试目录：

```sh
cd "${projectRoot}"
mkdir -p test-runs/nested
cp "${srcDir}/vlog2vbe/tests/hier/nested_modules.v" test-runs/nested/top.v
cd test-runs/nested
```

设置后端环境：

```sh
export MBK_WORK_LIB=.
export MBK_CATAL_NAME=CATAL
export MBK_IN_LO=vst
export MBK_OUT_LO=vst
export MBK_TARGET_LIB=${installDir}/cells/sxlib
export MBK_CATA_LIB=${MBK_TARGET_LIB}
```

运行闭环：

```sh
vlog2vbe -top top -o top.vbe top.v
boom -V top top_o
boog top_o top_g
loon top_g top_l
ls -l top.vbe top_o.vbe top_g.vst top_l.vst
```

通过标准：

```text
top.vbe      # vlog2vbe 生成的 Alliance 行为描述
top_o.vbe    # boom 优化后的行为描述
top_g.vst    # boog 映射后的门级结构网表
top_l.vst    # loon 优化后的门级结构网表
```

本次实测摘要：

```text
boog: critical path about 506 ps, mapped cells total 1
loon: generated top_l.vst successfully
```

## 7. 后端闭环测试：时序层次用例

创建测试目录：

```sh
cd "${projectRoot}"
mkdir -p test-runs/seq
cp "${srcDir}/vlog2vbe/tests/hier/seq_instance.v" test-runs/seq/top.v
cd test-runs/seq
```

设置后端环境：

```sh
export MBK_WORK_LIB=.
export MBK_CATAL_NAME=CATAL
export MBK_IN_LO=vst
export MBK_OUT_LO=vst
export MBK_TARGET_LIB=${installDir}/cells/sxlib
export MBK_CATA_LIB=${MBK_TARGET_LIB}
```

运行闭环：

```sh
vlog2vbe -top top -o top.vbe top.v
boom -V top top_o
boog top_o top_g
loon top_g top_l
ls -l top.vbe top_o.vbe top_g.vst top_l.vst
```

通过标准：

```text
top.vbe
top_o.vbe
top_g.vst
top_l.vst
```

本次实测摘要：

```text
boog: critical path about 505 ps, mapped cells total 3
loon: final critical path about 882 ps, mapped cells total 3
```

## 8. 负向测试

负向测试确认工具会拒绝不支持或破坏 Alliance 约定的输入。

```sh
cd "${projectRoot}"
mkdir -p test-runs/negative
cd test-runs/negative
```

四态 `x` 常量：

```sh
cat > x_const.v <<'EOF'
module bad_x(input a, output y);
  assign y = 1'bx;
endmodule
EOF

vlog2vbe -o x_const.vbe x_const.v bad_x
```

期望失败：

```text
vlog2vbe: emit error: constant '1'bx' contains x/z; first version only supports 0/1
```

占用 Alliance 电源名：

```sh
cat > reserved_power.v <<'EOF'
module reserved_power(input vdd, output y);
  assign y = vdd;
endmodule
EOF

vlog2vbe -o reserved_power.vbe reserved_power.v reserved_power
```

期望失败：

```text
vlog2vbe: emit error: signal names 'vdd' and 'vss' are reserved for Alliance power ports
```

## 9. 输出文件总览

部署相关：

```text
alliance-build/                  # 编译中间目录
alliance-install/bin/vlog2vbe    # Verilog -> VBE 前端
alliance-install/bin/boom        # VBE 行为优化
alliance-install/bin/boog        # 门级映射
alliance-install/bin/loon        # 门级网表本地优化
alliance-install/lib/            # Alliance 共享库
alliance-install/cells/sxlib/    # 标准单元库
```

前端回归：

```text
alliance/src/vlog2vbe/tests/generated/*.vbe
alliance/src/vlog2vbe/tests/expected/*.vbe
```

后端闭环：

```text
test-runs/nested/top.v       # 输入 Verilog
test-runs/nested/top.vbe     # vlog2vbe 输出
test-runs/nested/top_o.vbe   # boom 输出
test-runs/nested/top_g.vst   # boog 输出
test-runs/nested/top_l.vst   # loon 输出
test-runs/nested/*.xsc       # critical path 颜色标记文件

test-runs/seq/top.v
test-runs/seq/top.vbe
test-runs/seq/top_o.vbe
test-runs/seq/top_g.vst
test-runs/seq/top_l.vst
test-runs/seq/*.xsc
```

负向测试：

```text
test-runs/negative/*.v       # 临时负向输入
test-runs/negative/*.log     # 如重定向保存，可记录失败信息
```

## 10. 清理

只清理测试运行输出：

```sh
rm -rf "${projectRoot}/test-runs"
rm -rf "${srcDir}/vlog2vbe/tests/generated"
```

清理本地部署：

```sh
rm -rf "${projectRoot}/alliance-build"
rm -rf "${projectRoot}/alliance-install"
```

如果需要重新生成构建系统，可再次从第 2 节开始执行。
