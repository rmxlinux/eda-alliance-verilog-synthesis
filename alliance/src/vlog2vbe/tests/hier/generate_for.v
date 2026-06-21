module bit_and(input a, input b, output y);
  assign y = a & b;
endmodule

module top #(parameter W = 4)(input [W-1:0] a,
                              input [W-1:0] b,
                              output [W-1:0] y,
                              output [W-1:0] n);
  localparam LAST = W - 1;
  genvar i;

  generate
    for (i = 0; i < W; i = i + 1) begin : g
      bit_and u_and(.a(a[i]), .b(b[i]), .y(y[i]));
      assign n[i] = (i == LAST) ? ~a[i] : a[i];
    end
  endgenerate
endmodule
