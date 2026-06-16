module dff(input clk, input d, output reg q);
  always @(posedge clk)
    q <= d;
endmodule

module top(input clk, input a, input b, output q);
  wire d;

  assign d = a ^ b;
  dff u0(.clk(clk), .d(d), .q(q));
endmodule
