# Vivado synthesis script for SPU on Artix-7
# Usage: vivado -mode batch -source scripts/vivado_synth.tcl \
#        -tclargs "rtl_src" "xdc_file" "build_dir"

set rtl_files [lindex $argv 0]
set xdc_file  [lindex $argv 1]
set build_dir [lindex $argv 2]

# Parse RTL file list
set src_files [split $rtl_files " "]

# Create project
create_project spu_project ${build_dir}/vivado_project -part xc7a35tcpg236-1 -force

# Add RTL sources
foreach f $src_files {
    add_files -norecurse $f
}

# Add constraints
add_files -fileset constrs_1 -norecurse $xdc_file

# Set top module
set_property top spu_top [current_fileset]

# Synthesis
synth_design -top spu_top -part xc7a35tcpg236-1 -flatten_hierarchy rebuilt

# Report utilization
report_utilization -file ${build_dir}/utilization_report.txt
report_timing_summary -file ${build_dir}/timing_report.txt

# Place & Route
opt_design
place_design
route_design

# Post-route reports
report_utilization -file ${build_dir}/utilization_post_route.txt
report_timing_summary -file ${build_dir}/timing_post_route.txt

# Generate bitstream
write_bitstream -force ${build_dir}/spu_artix7.bit

# Export hardware (for PYNQ or PS integration)
write_hw_platform -fixed -include_bit -force ${build_dir}/spu_artix7.xsa

puts "=== Synthesis complete: ${build_dir}/spu_artix7.bit ==="
