module always_mux(input sel, input a, input b, output reg y);
  always @* begin
    if (sel)
      y = a;
    else
      y = b;
  end
endmodule
