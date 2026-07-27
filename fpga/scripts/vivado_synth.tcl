# Vivado synthesis script for SPPU on Zynq-7010 (AntMiner XC7Z010 v1.0)
# Usage: vivado -mode batch -source scripts/vivado_synth.tcl \
#        -tclargs "rtl_src" "xdc_file" "build_dir"

set rtl_files [lindex $argv 0]
set xdc_file  [lindex $argv 1]
set build_dir [lindex $argv 2]

# Parse RTL file list
set src_files [split $rtl_files " "]

# Create project for XC7Z010-1CLG400C
create_project sppu_project ${build_dir}/vivado_project -part xc7z010clg400-1 -force

# Add RTL sources
foreach f $src_files {
    add_files -norecurse $f
}

# Add constraints
add_files -fileset constrs_1 -norecurse $xdc_file

# Set top module
set_property top sppu_top [current_fileset]

# Synthesis for Zynq-7010
synth_design -top sppu_top -part xc7z010clg400-1 -flatten_hierarchy rebuilt

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
write_bitstream -force ${build_dir}/sppu_zynq7010.bit

# Export hardware platform (for PS integration via XSA)
write_hw_platform -fixed -include_bit -force ${build_dir}/sppu_zynq7010.xsa

puts "=== Synthesis complete: ${build_dir}/sppu_zynq7010.bit ==="
puts "=== HW platform: ${build_dir}/sppu_zynq7010.xsa ==="
puts "=== Import into Vitis/PetaLinux for PS software ==="
