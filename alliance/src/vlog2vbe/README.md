# vlog2vbe

`vlog2vbe` is the first-stage Verilog input front-end for the Alliance
synthesis flow.  It translates a small synthesizable Verilog subset into an
Alliance `.vbe` behavioral description so the existing tools can keep using:

```sh
vlog2vbe simple_and.v simple_and
boom simple_and simple_and_o
boog simple_and_o simple_and_g
loon simple_and_g simple_and_l
```

## First supported subset

- one or more Verilog modules per file;
- ANSI and non-ANSI port declarations;
- `input`, `output`, `inout`, `wire`, and `reg` declarations;
- scalar and packed vector ranges using decimal bounds or integer parameters,
  such as `[3:0]` and `[W-1:0]`;
- integer `parameter` declarations in module headers or module bodies;
- continuous assignments, `assign y = expr;`;
- combinational `always @*` / `always @(*)` with blocking assignments,
  `if/else`, and `case/default`;
- edge-triggered `always @(posedge clk)` / `always @(negedge clk)` with
  blocking or nonblocking assignments;
- one common asynchronous reset template, such as
  `always @(posedge clk or negedge rst_n) if (!rst_n) q <= 0; else q <= d;`;
- simple module instances using named or positional port connections;
- named and positional parameter overrides, such as `#(.W(4))` and `#(4)`;
- hierarchy flattening into the selected top module before VBE emission, using
  BOOM-compatible flattened signal names;
- automatic `vdd`/`vss` power ports and the matching Alliance power-supply
  assertion in emitted VBE;
- top selection with `-top module`, a positional module argument, or automatic
  inference when exactly one module is not instantiated by another module;
- identifiers, constants, bit-selects, part-selects, concatenation;
- `~`, `!`, `&`, `|`, `^`, `&&`, `||`, `==`, `!=`, and `?:`.

This version deliberately rejects `localparam`, generate blocks, delays,
multiple asynchronous reset signals in one always block, four-state constants
containing `x/z`, general RTL arithmetic operators, and complex instance
connections such as output ports connected to non-assignable expressions.
Those are planned for later milestones.

## Examples

```sh
cd alliance/src/vlog2vbe
cc -std=c99 -Wall -Wextra -o src/vlog2vbe src/vlog_*.c
src/vlog2vbe -o tests/expected/simple_and.vbe tests/comb/simple_and.v simple_and
```

For hierarchical input:

```sh
src/vlog2vbe -top top -o top.vbe tests/hier/named_two_level.v
```

Or, after the tool has been built:

```sh
make -C tests VLOG2VBE=../src/vlog2vbe
```

The generated `.vbe` can then be read by the existing Alliance behavioral
tools in a full Alliance installation.
