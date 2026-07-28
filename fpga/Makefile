# SPPU FPGA Makefile — Open-Source Toolchain (Yosys + nextpnr-xilinx + openFPGALoader)
# Targets: sim, synth, fasm, bit, program, lint, clean
# Board: AntMiner XC7Z010 v1.0 (xc7z010clg400-1)

RTL_DIR     = rtl
SIM_DIR     = sim
BUILD_DIR   = build
SCRIPTS_DIR = scripts
CONST_DIR   = constraints
ARCH_DIR    = arch

RTL_SRC = $(RTL_DIR)/sppu_regs.v \
          $(RTL_DIR)/sppu_dotprod.v \
          $(RTL_DIR)/sppu_dma.v \
          $(RTL_DIR)/sppu_vecmem.v \
          $(RTL_DIR)/seu_tree.v \
          $(RTL_DIR)/sppu_top.v \
          $(RTL_DIR)/sppu_pynq_top.v

TB_SRC    = $(SIM_DIR)/tb_sppu.v
XDC       = $(CONST_DIR)/sppu_zynq7010.xdc
YOSYS_SRC = $(RTL_DIR)/sppu_regs.v \
            $(RTL_DIR)/sppu_dotprod.v \
            $(RTL_DIR)/sppu_dma.v \
            $(RTL_DIR)/sppu_vecmem.v \
            $(RTL_DIR)/seu_tree.v \
            $(RTL_DIR)/sppu_top.v \
            $(RTL_DIR)/sppu_pynq_top.v

CHIPDB_DIR = $(ARCH_DIR)/xc7z010clg400-1
PART_NAME  = xc7z010clg400-1
PART_YAML  = $(CHIPDB_DIR)/part.yaml

FASM   = $(BUILD_DIR)/sppu_pynq_top.fasm
JSON   = $(BUILD_DIR)/sppu_pynq_top.json

.PHONY: all sim synth fasm bit program lint clean help

all: bit

help:
	@echo "SPPU FPGA — Open-Source Toolchain Targets:"
	@echo ""
	@echo "  sim     — Run Icarus Verilog simulation"
	@echo "  synth   — Yosys synthesis → JSON netlist"
	@echo "  fasm    — nextpnr place & route → FASM bitstream"
	@echo "  bit     — Convert FASM → binary .bit (requires xc7frames2bit)"
	@echo "  program — Load .bit onto FPGA via openFPGALoader"
	@echo "  lint    — Verilator lint check"
	@echo "  clean   — Remove build artifacts"

# ---- Simulation (Icarus Verilog) ----

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

sim: $(BUILD_DIR)
	iverilog -o $(BUILD_DIR)/tb_sppu.vvp \
		-I$(RTL_DIR) \
		$(RTL_SRC) $(TB_SRC)
	vvp $(BUILD_DIR)/tb_sppu.vvp

wave: sim
	gtkwave $(BUILD_DIR)/tb_sppu.vcd &

# ---- Yosys Synthesis (xc7 / Zynq-7000) ----

$(JSON): $(YOSYS_SRC) $(XDC) $(SCRIPTS_DIR)/yosys_synth.ys
	@echo "=== Yosys synthesis ==="
	yosys -c $(SCRIPTS_DIR)/yosys_synth.ys \
		2>&1 | tee $(BUILD_DIR)/yosys.log

synth: $(JSON)
	@echo "=== Synthesis complete: $(JSON) ==="

# ---- nextpnr Place & Route → FASM ----

$(FASM): $(JSON) $(XDC) $(CHIPDB_DIR)/part.yaml $(SCRIPTS_DIR)/nextpnr_impl.sh
	@echo "=== nextpnr place & route ==="
	$(SCRIPTS_DIR)/nextpnr_impl.sh \
		$(BUILD_DIR) \
		$(XDC) \
		$(CHIPDB_DIR) \
		$(FASM) \
		$(JSON) \
		$(PART_NAME)
	@echo "=== FASM ready: $(FASM) ==="

fasm: $(FASM)

# ---- FASM → BIT (requires xc7frames2bit from prjxray-tools) ----

$(BUILD_DIR)/sppu_zynq7010.bit: $(FASM) $(PART_YAML) $(SCRIPTS_DIR)/fasm2bit.sh
	@echo "=== Converting FASM to BIT ==="
	$(SCRIPTS_DIR)/fasm2bit.sh \
		$(FASM) \
		$(PART_YAML) \
		$(PART_NAME) \
		$@ 2>&1 | tee $(BUILD_DIR)/fasm2bit.log
	@echo "=== BIT ready: $@ ==="

bit: $(BUILD_DIR)/sppu_zynq7010.bit

# ---- Program FPGA via openFPGALoader ----

program: bit
	@echo "=== Programming FPGA ==="
	$(SCRIPTS_DIR)/program_fpga.sh $(BUILD_DIR)/sppu_zynq7010.bit

# ---- Lint (Verilator) ----

lint:
	verilator --lint-only -Wall $(RTL_SRC)

# ---- Clean ----

clean:
	rm -rf $(BUILD_DIR)
	rm -f *.vvp *.vcd