# Vivado hardware programming script
# Usage: vivado -mode batch -source scripts/vivado_program.tcl \
#        -tclargs "bitstream_file"

set bit_file [lindex $argv 0]

# Open hardware manager
open_hw_manager

# Connect to hardware (auto-detect JTAG)
connect_hw_server
open_hw_target

# Find the FPGA device
set hw_device [lindex [get_hw_devices xc7a*] 0]
current_hw_device $hw_device

# Set bitstream
set_property PROGRAM.FILE $bit_file $hw_device
set_property PROBES.FILE {} $hw_device

# Program
program_hw_devices $hw_device
puts "=== Programmed: $bit_file ==="

# Disconnect
close_hw_target
disconnect_hw_server
