module arithmetic(input [2:0] a,
                  input [2:0] b,
                  output [2:0] sum,
                  output [2:0] diff,
                  output [3:0] prod,
                  output bit0);
  assign sum = a + b;
  assign diff = a - b;
  assign prod = a[1:0] * b[1:0];
  assign bit0 = a + b;
endmodule
