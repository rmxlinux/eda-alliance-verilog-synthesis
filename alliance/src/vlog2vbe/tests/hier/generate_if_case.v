module top #(parameter ENABLE = 1, parameter MODE = 2)
          (input a, input b, input c, output y, output z);
  generate
    if (ENABLE) begin : enabled
      assign y = a;
    end else begin : disabled
      assign y = b;
    end

    case (MODE)
      0: assign z = a;
      1: assign z = b;
      2: begin : mode_two
        assign z = c;
      end
      default: assign z = b;
    endcase
  endgenerate
endmodule
