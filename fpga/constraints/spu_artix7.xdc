# SPU FPGA — Xilinx Artix-7 Pin Constraints (XDC)
# Target: generic Artix-7 board (ATMiner / similar)
# Adjust pin assignments for your specific board's schematic.

# ---- Clock (directly from oscillator) ----
set_property PACKAGE_PIN E3 [get_ports sys_clk]
set_property IOSTANDARD LVCMOS33 [get_ports sys_clk]
create_clock -add -name sys_clk -period 10.00 -waveform {0 5} [get_ports sys_clk]

# ---- Reset (active-low button) ----
set_property PACKAGE_PIN C12 [get_ports sys_rst_n]
set_property IOSTANDARD LVCMOS33 [get_ports sys_rst_n]

# ---- LEDs ----
set_property PACKAGE_PIN H17 [get_ports {led[0]}]
set_property IOSTANDARD LVCMOS33 [get_ports {led[0]}]

set_property PACKAGE_PIN K15 [get_ports {led[1]}]
set_property IOSTANDARD LVCMOS33 [get_ports {led[1]}]

set_property PACKAGE_PIN J13 [get_ports {led[2]}]
set_property IOSTANDARD LVCMOS33 [get_ports {led[2]}]

set_property PACKAGE_PIN N14 [get_ports {led[3]}]
set_property IOSTANDARD LVCMOS33 [get_ports {led[3]}]

set_property PACKAGE_PIN R18 [get_ports {led[4]}]
set_property IOSTANDARD LVCMOS33 [get_ports {led[4]}]

# ---- UART (register access from host) ----
set_property PACKAGE_PIN D10 [get_ports uart_tx]
set_property IOSTANDARD LVCMOS33 [get_ports uart_tx]

set_property PACKAGE_PIN A9  [get_ports uart_rx]
set_property IOSTANDARD LVCMOS33 [get_ports uart_rx]

# ---- SPI Slave (vector data from host) ----
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

set_property PACKAGE_PIN G3  [get_ports {gpio[2]}]
set_property IOSTANDARD LVCMOS33 [get_ports {gpio[2]}]

set_property PACKAGE_PIN F6  [get_ports {gpio[3]}]
set_property IOSTANDARD LVCMOS33 [get_ports {gpio[3]}]
