VLOG2VBE ?= ../src/vlog2vbe
GENERATED_DIR = generated

COMB_CASES = simple_and mux2 vector_ops arithmetic always_mux always_default case_decoder
SEQ_CASES = dff dff_en dff_async_reset
HIER_CASES = named_two_level positional_chain seq_instance nested_modules parameterized generate_for
CASES = $(COMB_CASES) $(SEQ_CASES) $(HIER_CASES)
GENERATED = $(addprefix $(GENERATED_DIR)/,$(addsuffix .vbe,$(CASES)))

.PHONY: all check clean

all: $(GENERATED)

$(GENERATED_DIR):
	mkdir -p $(GENERATED_DIR)

$(GENERATED_DIR)/%.vbe: comb/%.v | $(GENERATED_DIR)
	$(VLOG2VBE) -o $@ $< $*

$(GENERATED_DIR)/%.vbe: seq/%.v | $(GENERATED_DIR)
	$(VLOG2VBE) -o $@ $< $*

$(GENERATED_DIR)/%.vbe: hier/%.v | $(GENERATED_DIR)
	$(VLOG2VBE) -top top -o $@ $<

check: all
	diff -u expected/simple_and.vbe generated/simple_and.vbe
	diff -u expected/arithmetic.vbe generated/arithmetic.vbe
	diff -u expected/always_mux.vbe generated/always_mux.vbe
	diff -u expected/always_default.vbe generated/always_default.vbe
	diff -u expected/dff.vbe generated/dff.vbe
	diff -u expected/dff_en.vbe generated/dff_en.vbe
	diff -u expected/dff_async_reset.vbe generated/dff_async_reset.vbe
	diff -u expected/named_two_level.vbe generated/named_two_level.vbe
	diff -u expected/positional_chain.vbe generated/positional_chain.vbe
	diff -u expected/seq_instance.vbe generated/seq_instance.vbe
	diff -u expected/nested_modules.vbe generated/nested_modules.vbe
	diff -u expected/parameterized.vbe generated/parameterized.vbe
	diff -u expected/generate_for.vbe generated/generate_for.vbe

clean:
	rm -rf $(GENERATED_DIR)
