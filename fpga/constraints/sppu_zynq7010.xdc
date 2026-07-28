# SPPU FPGA — Xilinx Zynq-7010 (XC7Z010-1CLG400C) Constraints
# Target: AntMiner XC7Z010 v1.0
# PS-PL AXI interfaces are hardcoded by the Zynq fabric (no pin constraints needed).
# Only PL-side I/O pins are constrained below.

# ---- PL System Clock (50 MHz on MRCC pin K17) ----
set_property PACKAGE_PIN K17 [get_ports sys_clk]
set_property IOSTANDARD LVCMOS33 [get_ports sys_clk]
create_clock -name sys_clk -period 20.0 [get_ports sys_clk]

# ---- PL Reset ----
set_property IOSTANDARD LVCMOS33 [get_ports sys_rst_n]

# ---- LEDs (active-high, directly on PL bank) ----
set_property PACKAGE_PIN H17 [get_ports {led[0]}]
set_property IOSTANDARD LVCMOS33 [get_ports {led[0]}]

set_property PACKAGE_PIN K18 [get_ports {led[1]}]
set_property IOSTANDARD LVCMOS33 [get_ports {led[1]}]

set_property PACKAGE_PIN J14 [get_ports {led[2]}]
set_property IOSTANDARD LVCMOS33 [get_ports {led[2]}]

set_property PACKAGE_PIN M14 [get_ports {led[3]}]
set_property IOSTANDARD LVCMOS33 [get_ports {led[3]}]

set_property PACKAGE_PIN L16 [get_ports {led[4]}]
set_property IOSTANDARD LVCMOS33 [get_ports {led[4]}]

set_property IOSTANDARD LVCMOS33 [get_ports irq]

# ---- UART (unconstrained — auto-assign to free PL pad) ----
set_property IOSTANDARD LVCMOS33 [get_ports uart_tx]
set_property IOSTANDARD LVCMOS33 [get_ports uart_rx]

# ---- PS-PL Configuration ----
set_property CFGBVS VCCO [current_design]
set_property CONFIG_VOLTAGE 3.3 [current_design]

# ---- Bitstream configuration ----
set_property BITSTREAM.GENERAL.COMPRESS TRUE [current_design]
set_property BITSTREAM.CONFIG.CONFIGRATE 33 [current_design]
