# SPU FPGA Makefile
# Targets: sim (iverilog), synth (Vivado Artix-7), lint (verilator)

RTL_DIR   = rtl
SIM_DIR   = sim
BUILD_DIR = build
SCRIPTS   = scripts
CONST_DIR = constraints

RTL_SRC = $(RTL_DIR)/spu_regs.v \
          $(RTL_DIR)/spu_dotprod.v \
          $(RTL_DIR)/spu_dma.v \
          $(RTL_DIR)/spu_vecmem.v \
          $(RTL_DIR)/seu_tree.v \
          $(RTL_DIR)/spu_top.v \
          $(RTL_DIR)/spu_pynq_top.v

TB_SRC = $(SIM_DIR)/tb_spu.v
XDC    = $(CONST_DIR)/spu_artix7.xdc

.PHONY: all sim wave clean synth lint program

all: sim

# ---- Simulation (Icarus Verilog) ----

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

sim: $(BUILD_DIR)
	iverilog -o $(BUILD_DIR)/tb_spu.vvp \
		-I$(RTL_DIR) \
		$(RTL_SRC) $(TB_SRC)
	vvp $(BUILD_DIR)/tb_spu.vvp

wave: sim
	gtkwave $(BUILD_DIR)/tb_spu.vcd &

# ---- Vivado Synthesis (Artix-7) ----

$(BUILD_DIR)/spu_artix7.bit: $(RTL_SRC) $(XDC) $(SCRIPTS)/vivado_synth.tcl
	vivado -mode batch -source $(SCRIPTS)/vivado_synth.tcl \
		-tclargs "$(RTL_SRC)" "$(XDC)" "$(BUILD_DIR)" \
		2>&1 | tee $(BUILD_DIR)/vivado.log

synth: $(BUILD_DIR) $(BUILD_DIR)/spu_artix7.bit
	@echo "=== Synthesis complete ==="
	@echo "Bitstream: $(BUILD_DIR)/spu_artix7.bit"

# Program via Vivado Hardware Manager
program: synth
	vivado -mode batch -source $(SCRIPTS)/vivado_program.tcl \
		-tclargs "$(BUILD_DIR)/spu_artix7.bit" \
		2>&1 | tee $(BUILD_DIR)/program.log

# Program via openFPGALoader (if JTAG adapter available)
program-ocd: synth
	openFPGALoader -b digilent_a $(BUILD_DIR)/spu_artix7.bit

# ---- Lint (Verilator) ----

lint:
	verilator --lint-only -Wall $(RTL_SRC)

clean:
	rm -rf $(BUILD_DIR) *.vvp *.vcd
