# SPPU FPGA — Xilinx Zynq-7010 (XC7Z010-1CLG400C) Constraints
# Target: AntMiner XC7Z010 v1.0
# PS-PL AXI interfaces are hardcoded by the Zynq fabric (no pin constraints needed).
# Only PL-side I/O pins are constrained below.

# ---- PL System Clock (50 MHz oscillator) ----
set_property PACKAGE_PIN E3 [get_ports sys_clk]
set_property IOSTANDARD LVCMOS33 [get_ports sys_clk]
create_clock -add -name sys_clk -period 20.00 -waveform {0 10} [get_ports sys_clk]

# ---- PL Reset (active-low, directly from PS MIO or button) ----
set_property PACKAGE_PIN C12 [get_ports sys_rst_n]
set_property IOSTANDARD LVCMOS33 [get_ports sys_rst_n]

# ---- LEDs (active-high, directly on PL bank) ----
set_property PACKAGE_PIN H17 [get_ports {led[0]}]
set_property IOSTANDARD LVCMOS33 [get_ports {led[0]}]

set_property PACKAGE_PIN K15 [get_ports {led[1]}]
set_property IOSTANDARD LVCMOS33 [get_ports {led[1]}]

set_property PACKAGE_PIN J13 [get_ports {led[2]}]
set_property IOSTANDARD LVCMOS33 [get_ports {led[2]}]

set_property PACKAGE_PIN N14 [get_ports {led[3]}]
set_property IOSTANDARD LVCMOS33 [get_ports {led[3]}]

# ---- UART (directly on PL bank for debug — main UART is on PS MIO) ----
set_property PACKAGE_PIN D10 [get_ports uart_tx]
set_property IOSTANDARD LVCMOS33 [get_ports uart_tx]

set_property PACKAGE_PIN A9  [get_ports uart_rx]
set_property IOSTANDARD LVCMOS33 [get_ports uart_rx]

# ---- SPI Slave (vector data from external host CPU) ----
set_property PACKAGE_PIN B1  [get_ports spi_sck]
set_property IOSTANDARD LVCMOS33 [get_ports spi_sck]

set_property PACKAGE_PIN A1  [get_ports spi_csn]
set_property IOSTANDARD LVCMOS33 [get_ports spi_csn]

set_property PACKAGE_PIN C1  [get_ports spi_mosi]
set_property IOSTANDARD LVCMOS33 [get_ports spi_mosi]

set_property PACKAGE_PIN C2  [get_ports spi_miso]
set_property IOSTANDARD LVCMOS33 [get_ports spi_miso]

# ---- GPIO (debug / expansion) ----
set_property PACKAGE_PIN H1  [get_ports {gpio[0]}]
set_property IOSTANDARD LVCMOS33 [get_ports {gpio[0]}]

set_property PACKAGE_PIN G1  [get_ports {gpio[1]}]
set_property IOSTANDARD LVCMOS33 [get_ports {gpio[1]}]

# ---- PS-PL Configuration ----
# Prevent unused PS-PL configuration ports from generating warnings
set_property CFGBVS VCCO [current_design]
set_property CONFIG_VOLTAGE 3.3 [current_design]

# ---- Bitstream configuration ----
set_property BITSTREAM.GENERAL.COMPRESS TRUE [current_design]
set_property BITSTREAM.CONFIG.CONFIGRATE 33 [current_design]
