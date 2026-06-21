module div_mod_signed_tristate(input [3:0] a,
                               input [3:0] b,
                               input en0,
                               input en1,
                               input signed [2:0] sa,
                               input signed [2:0] sb,
                               output [3:0] quot,
                               output [3:0] rem,
                               output signed [3:0] ssum,
                               output [3:0] bus);
  assign quot = a / b;
  assign rem = a % b;
  assign ssum = sa + sb;
  assign bus = en0 ? a : 4'bzzzz;
  assign bus = en1 ? b : 4'bzzzz;
endmodule
