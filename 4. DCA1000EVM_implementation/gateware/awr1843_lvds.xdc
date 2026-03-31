//==============================================================================
// awr1843_lvds.xdc
// Xilinx Artix-7 constraints for AWR1843BOOST LVDS capture
//
// EDIT: Pin locations must match your actual PCB / breakout wiring.
//       Bank voltage: AWR1843 drives 1.8V LVDS → use LVDS_25 standard
//       (Artix-7 HR banks support 1.8V with correct VCCO, need to change LDO resistor value to supply 1.8V instead of 3.3V).
//       If using HP bank at 1.8V use LVDS instead of LVDS_25.
//==============================================================================

# -----------------------------------------------------------------------
# LVDS Clock (from AWR1843 LVDS_CLK+/-)
# -----------------------------------------------------------------------
set_property PACKAGE_PIN  U18        [get_ports LVDS_CLK_P]
set_property PACKAGE_PIN  V18        [get_ports LVDS_CLK_N]
set_property IOSTANDARD   LVDS_25    [get_ports LVDS_CLK_P]
set_property IOSTANDARD   LVDS_25    [get_ports LVDS_CLK_N]
set_property DIFF_TERM    TRUE       [get_ports LVDS_CLK_P]

# -----------------------------------------------------------------------
# LVDS Data Lanes (Lane 0..3, P and N)
# EDIT these pin assignments to match your layout.
# -----------------------------------------------------------------------
set_property PACKAGE_PIN  W18        [get_ports {LVDS_DATA_P[0]}]
set_property PACKAGE_PIN  W19        [get_ports {LVDS_DATA_N[0]}]

set_property PACKAGE_PIN  Y18        [get_ports {LVDS_DATA_P[1]}]
set_property PACKAGE_PIN  Y19        [get_ports {LVDS_DATA_N[1]}]

set_property PACKAGE_PIN  AA18       [get_ports {LVDS_DATA_P[2]}]
set_property PACKAGE_PIN  AB18       [get_ports {LVDS_DATA_N[2]}]

set_property PACKAGE_PIN  AA19       [get_ports {LVDS_DATA_P[3]}]
set_property PACKAGE_PIN  AB19       [get_ports {LVDS_DATA_N[3]}]

set_property IOSTANDARD   LVDS_25    [get_ports {LVDS_DATA_P[*]}]
set_property IOSTANDARD   LVDS_25    [get_ports {LVDS_DATA_N[*]}]
set_property DIFF_TERM    TRUE       [get_ports {LVDS_DATA_P[*]}]

# -----------------------------------------------------------------------
# User clock (e.g. 100 MHz from on-board oscillator)
# -----------------------------------------------------------------------
set_property PACKAGE_PIN  E3         [get_ports user_clk]
set_property IOSTANDARD   LVCMOS33   [get_ports user_clk]
create_clock -period 10.000 -name user_clk [get_ports user_clk]

# -----------------------------------------------------------------------
# LVDS source-synchronous clock constraint
# AWR1843 LVDS bit clock = adcSampleRate × 2 (DDR)
# Example: 37.5 MSps → 75 MHz bit clock → period = 13.333 ns
# Change -period to match your mmWave profile adcSampleRate.
# -----------------------------------------------------------------------
create_clock -period 13.333 -name lvds_clk [get_ports LVDS_CLK_P]

# Divided clock (BUFR output = bit_clk / SERDES_RATIO)
# 75 MHz / 8 = 9.375 MHz → period = 106.667 ns
create_generated_clock -name lvds_div_clk \
    -source [get_ports LVDS_CLK_P] \
    -divide_by 8 \
    [get_pins u_clk_rx/u_bufr/O]

# -----------------------------------------------------------------------
# Source-synchronous input timing
# The AWR1843 launches data on the LVDS clock edges.
# Typical setup/hold from TI datasheet: tSU=0.3ns, tH=0.3ns
# -----------------------------------------------------------------------
set_input_delay -clock lvds_clk -max  0.5 [get_ports {LVDS_DATA_P[*]}]
set_input_delay -clock lvds_clk -min -0.5 [get_ports {LVDS_DATA_P[*]}]
set_input_delay -clock lvds_clk -max  0.5 \
    -clock_fall [get_ports {LVDS_DATA_P[*]}]
set_input_delay -clock lvds_clk -min -0.5 \
    -clock_fall [get_ports {LVDS_DATA_P[*]}]

# -----------------------------------------------------------------------
# Async FIFO CDC paths — declare as false paths (Gray pointers are safe)
# -----------------------------------------------------------------------
set_false_path -from [get_clocks lvds_div_clk] -to [get_clocks user_clk]
set_false_path -from [get_clocks user_clk]     -to [get_clocks lvds_div_clk]

# -----------------------------------------------------------------------
# Reset (active low, from push button or external)
# -----------------------------------------------------------------------
set_property PACKAGE_PIN  C2         [get_ports rst_n]
set_property IOSTANDARD   LVCMOS33   [get_ports rst_n]
set_false_path -from [get_ports rst_n]
