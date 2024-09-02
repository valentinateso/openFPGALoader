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

typedef struct {
	std::string manufacturer;
	std::string family;
	std::string model;
	int irlength;
    int idcode;
} fpga_model_data;

class FPGALoader {
public:
    FPGALoader(int8_t verbose_level);

    ~FPGALoader();

    std::string write_flash(char *spi_over_jtag_file, char *mcs_file);

    std::string send_command(char *command, int len);

    std::string reset();

    std::map <uint32_t, fpga_model_data> detect_fpga();

    usb_scan_item **scan_usb();

    void set_ftdi_channel(int value);

    void select_usb(int ind);

    int find_current();
    
    int find_any();

private:
    libusb_ll usb;
    usb_scan_item **scan_items = NULL;
    std::map <uint32_t, fpga_model_data> detected_fpga;
    int8_t verbose_level;
    char verbose_level_c[3];
    int selected_usb = -1;
    int detected_ftdi_channel = -1;
    char detected_ftdi_channel_c[3];

    const char *get_cable_name();

    int detect_fpga(int ftdi_channel);

    void free_scan_items();

};
