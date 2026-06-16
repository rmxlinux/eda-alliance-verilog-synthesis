module vector_ops(input [3:0] a, input [3:0] b, output [3:0] y, output y0);
  wire [3:0] n;

  assign n = a ^ b;
  assign y = n | 4'b0011;
  assign y0 = n[0];
endmodule
