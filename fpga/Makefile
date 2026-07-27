# SPPU FPGA Makefile
# Targets: sim (iverilog), synth (Vivado Zynq-7010), lint (verilator)
# Board: AntMiner XC7Z010 v1.0 (xc7z010clg400-1)

RTL_DIR   = rtl
SIM_DIR   = sim
BUILD_DIR = build
SCRIPTS   = scripts
CONST_DIR = constraints

RTL_SRC = $(RTL_DIR)/sppu_regs.v \
          $(RTL_DIR)/sppu_dotprod.v \
          $(RTL_DIR)/sppu_dma.v \
          $(RTL_DIR)/sppu_vecmem.v \
          $(RTL_DIR)/seu_tree.v \
          $(RTL_DIR)/sppu_top.v \
          $(RTL_DIR)/sppu_pynq_top.v

TB_SRC = $(SIM_DIR)/tb_sppu.v
XDC    = $(CONST_DIR)/sppu_zynq7010.xdc

.PHONY: all sim wave clean synth lint program

all: sim

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

# ---- Vivado Synthesis (Zynq-7010) ----

$(BUILD_DIR)/sppu_zynq7010.bit: $(RTL_SRC) $(XDC) $(SCRIPTS)/vivado_synth.tcl
	vivado -mode batch -source $(SCRIPTS)/vivado_synth.tcl \
		-tclargs "$(RTL_SRC)" "$(XDC)" "$(BUILD_DIR)" \
		2>&1 | tee $(BUILD_DIR)/vivado.log

synth: $(BUILD_DIR) $(BUILD_DIR)/sppu_zynq7010.bit
	@echo "=== Synthesis complete ==="
	@echo "Bitstream: $(BUILD_DIR)/sppu_zynq7010.bit"

# Program via Vivado Hardware Manager
program: synth
	vivado -mode batch -source $(SCRIPTS)/vivado_program.tcl \
		-tclargs "$(BUILD_DIR)/sppu_zynq7010.bit" \
		2>&1 | tee $(BUILD_DIR)/program.log

# Program via openFPGALoader (if JTAG adapter available)
program-ocd: synth
	openFPGALoader -b digilent_a $(BUILD_DIR)/sppu_zynq7010.bit

# ---- Lint (Verilator) ----

lint:
	verilator --lint-only -Wall $(RTL_SRC)

clean:
	rm -rf $(BUILD_DIR) *.vvp *.vcd
