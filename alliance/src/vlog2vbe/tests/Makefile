VLOG2VBE ?= ../src/vlog2vbe
GENERATED_DIR = generated

CASES = simple_and mux2 vector_ops
GENERATED = $(addprefix $(GENERATED_DIR)/,$(addsuffix .vbe,$(CASES)))

.PHONY: all check clean

all: $(GENERATED)

$(GENERATED_DIR):
	mkdir -p $(GENERATED_DIR)

$(GENERATED_DIR)/%.vbe: comb/%.v | $(GENERATED_DIR)
	$(VLOG2VBE) -o $@ $< $*

check: all
	diff -u expected/simple_and.vbe generated/simple_and.vbe

clean:
	rm -rf $(GENERATED_DIR)
