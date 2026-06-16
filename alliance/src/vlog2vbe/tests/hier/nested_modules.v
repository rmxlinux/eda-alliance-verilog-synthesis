module inv1(input a, output y);
  assign y = ~a;
endmodule

module mux2(input sel, input a, input b, output y);
  assign y = sel ? b : a;
endmodule

module stage(input sel, input a, input b, output y);
  wire nb;

  inv1 u_inv(.a(b), .y(nb));
  mux2 u_mux(.sel(sel), .a(a), .b(nb), .y(y));
endmodule

module top(input sel, input a, input b, input c, output y);
  wire t;

  stage u_stage(.sel(sel), .a(a), .b(b), .y(t));
  mux2 u_out(.sel(sel), .a(t), .b(c), .y(y));
endmodule
