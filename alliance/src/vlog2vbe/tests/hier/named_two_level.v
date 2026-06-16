module and2(input a, input b, output y);
  assign y = a & b;
endmodule

module top(input a, input b, input c, input d, output y);
  wire n1, n2;

  and2 u0(.a(a), .b(b), .y(n1));
  and2 u1(.a(c), .b(d), .y(n2));
  assign y = n1 | n2;
endmodule
