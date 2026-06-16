VLOG2VBE ?= ../src/vlog2vbe
GENERATED_DIR = generated

CASES = simple_and mux2 vector_ops always_mux always_default case_decoder
GENERATED = $(addprefix $(GENERATED_DIR)/,$(addsuffix .vbe,$(CASES)))

.PHONY: all check clean

all: $(GENERATED)

$(GENERATED_DIR):
	mkdir -p $(GENERATED_DIR)

$(GENERATED_DIR)/%.vbe: comb/%.v | $(GENERATED_DIR)
	$(VLOG2VBE) -o $@ $< $*

check: all
	diff -u expected/simple_and.vbe generated/simple_and.vbe
	diff -u expected/always_mux.vbe generated/always_mux.vbe
	diff -u expected/always_default.vbe generated/always_default.vbe

clean:
	rm -rf $(GENERATED_DIR)
