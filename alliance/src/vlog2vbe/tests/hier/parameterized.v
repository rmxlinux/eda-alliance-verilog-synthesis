module pass #(parameter W = 1)(input [W-1:0] a, output [W-1:0] y);
  assign y = a;
endmodule

module bit_pick #(parameter integer IDX = 0)(input [3:0] a, output y);
  assign y = ~a[IDX];
endmodule

module top(input [3:0] a, output [3:0] y, output z);
  parameter TOPW = 4;
  wire [TOPW-1:0] t;

  pass #(.W(TOPW)) u_pass(.a(a), .y(t));
  bit_pick #(2) u_bit(.a(t), .y(z));
  assign y = t;
endmodule
