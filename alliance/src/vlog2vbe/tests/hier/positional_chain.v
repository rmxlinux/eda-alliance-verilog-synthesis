module inv1(input a, output y);
  assign y = ~a;
endmodule

module xor2(input a, input b, output y);
  assign y = a ^ b;
endmodule

module top(input a, input b, output y);
  wire na;

  inv1 u0(a, na);
  xor2 u1(na, b, y);
endmodule
