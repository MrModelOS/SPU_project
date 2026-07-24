# SPU FPGA Makefile
# Targets: sim (iverilog), synth-xilinx (Vivado), synth-intel (Quartus)

RTL_DIR   = rtl
SIM_DIR   = sim
BUILD_DIR = build

RTL_SRC = $(RTL_DIR)/spu_regs.v \
          $(RTL_DIR)/spu_dotprod.v \
          $(RTL_DIR)/spu_dma.v \
          $(RTL_DIR)/spu_vecmem.v \
          $(RTL_DIR)/spu_top.v

TB_SRC = $(SIM_DIR)/tb_spu.v

.PHONY: all sim wave clean synth-xilinx synth-intel

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

# ---- Xilinx Vivado Synthesis ----

synth-xilinx: $(BUILD_DIR)
	vivado -mode batch -source scripts/vivado_synth.tcl 2>&1 | tee $(BUILD_DIR)/vivado.log

# ---- Intel Quartus Synthesis ----

synth-intel: $(BUILD_DIR)
	quartus_sh --flow compile spu_quartus.qpf 2>&1 | tee $(BUILD_DIR)/quartus.log

# ---- Lint ----

lint:
	verilator --lint-only -Wall $(RTL_SRC)

clean:
	rm -rf $(BUILD_DIR) *.vvp *.vcd
