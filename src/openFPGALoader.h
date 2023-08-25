// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (C) 2019 Gwenhael Goavec-Merou <gwenhael.goavec-merou@trabucayre.com>
 */

#include "altera.hpp"
#include "anlogic.hpp"
#include "board.hpp"
#include "cable.hpp"
#include "colognechip.hpp"
#include "cxxopts.hpp"
#include "device.hpp"
#include "dfu.hpp"
#include "display.hpp"
#include "efinix.hpp"
#include "ftdispi.hpp"
#include "gowin.hpp"
#include "ice40.hpp"
#include "lattice.hpp"
#include "libusb_ll.hpp"
#include "jtag.hpp"
#include "part.hpp"
#include "spiFlash.hpp"
#include "rawParser.hpp"
#include "xilinx.hpp"
#include "svf_jtag.hpp"
#include "atSerialCommunication.hpp"

class FPGALoader {
public:
    std::string write_flash(char* verbose_level, char* cable, char* ftdi_channel, char* spi_over_jtag_file, char* mcs_file);
    std::string send_command(int verbose_level, char* cable, char* command, int len);
    std::map<uint32_t, fpga_model> detect_fpga(int8_t verbose_level, usb_scan_item item, const char* cable_name, int ftdi_channel);
    usb_scan_item **scan_usb(int8_t verbose_level);
};
