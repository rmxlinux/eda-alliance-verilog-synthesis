module always_default(input sel, input a, input b, output reg y);
  always @(*) begin
    y = b;
    if (sel)
      y = a;
  end
endmodule
