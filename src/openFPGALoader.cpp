// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (C) 2019 Gwenhael Goavec-Merou <gwenhael.goavec-merou@trabucayre.com>
 */

#include <string.h>
#include <unistd.h>

#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <vector>

#include "openFPGALoader.h"
#include "verbose.hpp"

#ifdef ENABLE_XVC
#include "xvc_server.hpp"
#endif

#define DEFAULT_FREQ    6000000

using namespace std;

struct arguments {
    int8_t verbose;
    bool reset, detect, verify, scan_usb;
    unsigned int offset;
    string bit_file;
    string secondary_bit_file;
    string device;
    string cable;
    string ftdi_serial;
    int ftdi_channel;
    uint32_t freq;
    bool invert_read_edge;
    string board;
    bool pin_config;
    bool list_cables;
    bool list_boards;
    bool list_fpga;
    Device::prog_type_t prg_type;
    bool is_list_command;
    bool spi;
    bool dfu;
    string file_type;
    string fpga_part;
    string bridge_path;
    string probe_firmware;
    int index_chain;
    unsigned int file_size;
    string target_flash;
    bool external_flash;
    int16_t altsetting;
    uint16_t vid;
    uint16_t pid;
    int16_t cable_index;
    uint8_t bus_addr;
    uint8_t device_addr;
    string ip_adr;
    uint32_t protect_flash;
    bool unprotect_flash;
    bool bulk_erase_flash;
    string flash_sector;
    bool skip_load_bridge;
    bool skip_reset;
    /* xvc server */
    bool xvc;
    int port;
    string interface;
    string mcufw;
    bool conmcu;
};

int run_xvc_server(const struct arguments &args, const cable_t &cable,
                   const jtag_pins_conf_t *pins_config);

int parse_opt(int argc, char **argv, struct arguments *args,
              jtag_pins_conf_t *pins_config);

void displaySupported(const struct arguments &args, int8_t verbose_level);

usb_scan_item ** FPGALoader::scan_usb(int8_t verbose_level) {
    libusb_ll usb(0, 0);
    usb_scan_item **items = usb.scan(verbose_level);
    return items;
}

std::string main_fpga(int argc, char **argv) {
    cable_t cable;
    target_board_t *board = NULL;
    jtag_pins_conf_t pins_config = {0, 0, 0, 0};

    /* command line args. */
    struct arguments args = {0, false, false, false, false, 0, "", "", "", "-", "", -1,
                             0, false, "-", false, false, false, false, Device::PRG_NONE, false,
            /* spi dfu    file_type fpga_part bridge_path probe_firmware */
                             false, false, "", "", "", "",
            /* index_chain file_size target_flash external_flash altsetting */
                             -1, 0, "primary", false, -1,
            /* vid, pid, index bus_addr, device_addr */
                             0, 0, -1, 0, 0,
                             "127.0.0.1", 0, false, false, "", false, false,
            /* xvc server */
                             false, 3721, "-",
                             "", false,  // mcufw conmcu
    };
    /* parse arguments */
    try {
        if (parse_opt(argc, argv, &args, &pins_config))
            return "";
    } catch (std::exception &e) {
        return "Error in parse arg step";
    }

    if (args.is_list_command) {
        displaySupported(args, args.verbose);
        return "";
    }

    if (args.prg_type == Device::WR_SRAM)
        LOG_INFO("write to ram");
    if (args.prg_type == Device::WR_FLASH)
        LOG_INFO("write to flash");

    if (args.board[0] != '-') {
        if (board_list.find(args.board) != board_list.end()) {
            board = &(board_list[args.board]);
        } else {
            return "Error: cannot find board \'" + args.board + "\'";
        }
    }

    /* if a board name is specified try to use this to determine cable */
    if (board) {
        /* set pins config (only when user has not already provided
         * configuration
         */
        if (!args.pin_config) {
            pins_config.tdi_pin = board->jtag_pins_config.tdi_pin;
            pins_config.tdo_pin = board->jtag_pins_config.tdo_pin;
            pins_config.tms_pin = board->jtag_pins_config.tms_pin;
            pins_config.tck_pin = board->jtag_pins_config.tck_pin;
        }
        /* search for cable */
        auto t = cable_list.find(board->cable_name);
        if (t != cable_list.end()) {
            if (args.cable[0] == '-') {  // no user selection
                args.cable = (*t).first;  // use board default cable
            } else {
                LOG_INFO("Board default cable overridden with %s", args.cable.c_str());
            }
        }

        /* Xilinx only: to write flash exact fpga model must be provided */
        if (!board->fpga_part.empty() && !args.fpga_part.empty())
            LOG_INFO("Board default fpga part overridden with %s", args.fpga_part.c_str());
        else if (!board->fpga_part.empty() && args.fpga_part.empty())
            args.fpga_part = board->fpga_part;

        /* Some boards can override the default clock speed
         * if args.freq == 0: the `--freq` arg has not been used
         * => apply board->default_freq with a value of
         * 0 (no default frequency) or > 0 (board has a default frequency)
         */
        if (args.freq == 0)
            args.freq = board->default_freq;
    }

    if (args.cable[0] == '-') { /* if no board and no cable */
        LOG_WARNING("No cable or board specified: using direct ft2232 interface");
        args.cable = "ft2232";
    }

    /* if args.freq == 0: no user requirement nor board default
     * clock speed => set default frequency
     */
    if (args.freq == 0)
        args.freq = DEFAULT_FREQ;

    auto select_cable = cable_list.find(args.cable);
    if (select_cable == cable_list.end()) {
        return "Error : " + args.cable + " not found";
    }
    cable = select_cable->second;

    if (args.ftdi_channel != -1) {
        if (cable.type != MODE_FTDI_SERIAL && cable.type != MODE_FTDI_BITBANG) {
            return "Error: FTDI channel param is for FTDI cables.";
        }

        const int mapping[] = {INTERFACE_A, INTERFACE_B, INTERFACE_C,
                               INTERFACE_D};
        cable.config.interface = mapping[args.ftdi_channel];
    }

    if (!args.ftdi_serial.empty()) {
        if (cable.type != MODE_FTDI_SERIAL && cable.type != MODE_FTDI_BITBANG) {
            return "Error: FTDI serial param is for FTDI cables.";
        }
    }

    if (args.vid != 0) {
        LOG_INFO("Cable VID overridden");
        cable.vid = args.vid;
    }
    if (args.pid != 0) {
        LOG_INFO("Cable PID overridden");
        cable.pid = args.pid;
    }

    cable.bus_addr = args.bus_addr;
    cable.device_addr = args.device_addr;

    // always set this
    cable.config.index = args.cable_index;

    /* FLASH direct access */
    if (args.spi || (board && board->mode == COMM_SPI)) {
        /* if no instruction from user -> select flash mode */
        if (args.prg_type == Device::PRG_NONE)
            args.prg_type = Device::WR_FLASH;

        FtdiSpi *spi = NULL;
        spi_pins_conf_t pins_config;
        if (board)
            pins_config = board->spi_pins_config;

        try {
            spi = new FtdiSpi(cable, pins_config, args.freq, args.verbose > 0);
        } catch (std::exception &e) {
            return "Error: Failed to claim cable";
        }

        int spi_ret = EXIT_SUCCESS;

        if (board && board->manufacturer != "none") {
            Device *target;
            if (board->manufacturer == "efinix") {
                target = new Efinix(spi, args.bit_file, args.file_type,
                                    board->reset_pin, board->done_pin, board->oe_pin,
                                    args.verify, args.verbose);
            } else if (board->manufacturer == "lattice") {
                target = new Ice40(spi, args.bit_file, args.file_type,
                                   args.prg_type,
                                   board->reset_pin, board->done_pin, args.verify, args.verbose);
            } else if (board->manufacturer == "colognechip") {
                target = new CologneChip(spi, args.bit_file, args.file_type, args.prg_type,
                                         board->reset_pin, board->done_pin, DBUS6, board->oe_pin,
                                         args.verify, args.verbose);
            }
            if (args.prg_type == Device::RD_FLASH) {
                if (args.file_size == 0) {
                    LOG_ERR("0 size for dump");
                } else {
                    target->dumpFlash(args.offset, args.file_size);
                }
            } else if ((args.prg_type == Device::WR_FLASH ||
                        args.prg_type == Device::WR_SRAM) ||
                       !args.bit_file.empty() || !args.file_type.empty()) {
                target->program(args.offset, args.unprotect_flash);
            }
            if (args.unprotect_flash && args.bit_file.empty())
                if (!target->unprotect_flash())
                    spi_ret = EXIT_FAILURE;
            if (args.bulk_erase_flash && args.bit_file.empty())
                if (!target->bulk_erase_flash())
                    spi_ret = EXIT_FAILURE;
            if (args.protect_flash)
                if (!target->protect_flash(args.protect_flash))
                    spi_ret = EXIT_FAILURE;
        } else {
            RawParser *bit = NULL;
            if (board && board->reset_pin) {
                spi->gpio_set_output(board->reset_pin, true);
                spi->gpio_clear(board->reset_pin, true);
            }

            SPIFlash flash((SPIInterface *) spi, args.unprotect_flash, args.verbose);
            flash.display_status_reg();

            if (args.prg_type != Device::RD_FLASH &&
                (!args.bit_file.empty() || !args.file_type.empty())) {
                LOG_INFO("Open file %s %s", args.bit_file.c_str(), "false");
                try {
                    bit = new RawParser(args.bit_file, false);
                    LOG_INFO("DONE");
                } catch (std::exception &e) {
                    delete spi;
                    return "FAIL";
                }

                LOG_INFO("Parse file false");
                if (bit->parse() == EXIT_FAILURE) {
                    delete spi;
                    return "FAIL";
                } else {
                    LOG_INFO("DONE");
                }

                try {
                    flash.erase_and_prog(args.offset, bit->getData(), bit->getLength() / 8);
                } catch (std::exception &e) {
                    LOG_ERR("FAIL: %s", e.what());
                }

                if (args.verify)
                    flash.verify(args.offset, bit->getData(), bit->getLength() / 8);

                delete bit;
            } else if (args.prg_type == Device::RD_FLASH) {
                flash.dump(args.bit_file, args.offset, args.file_size);
            }

            if (args.unprotect_flash && args.bit_file.empty())
                if (!flash.disable_protection())
                    spi_ret = EXIT_FAILURE;
            if (args.bulk_erase_flash && args.bit_file.empty())
                if (!flash.bulk_erase())
                    spi_ret = EXIT_FAILURE;
            if (args.protect_flash)
                if (!flash.enable_protection(args.protect_flash))
                    spi_ret = EXIT_FAILURE;

            if (board && board->reset_pin)
                spi->gpio_set(board->reset_pin, true);
        }

        delete spi;

        return "FAIL";
    }

    /* ------------------- */
    /* DFU access          */
    /* ------------------- */
    if (args.dfu || (board && board->mode == COMM_DFU)) {
        /* try to init DFU probe */
        DFU *dfu = NULL;
        uint16_t vid = 0, pid = 0;
        int altsetting = -1;
        if (board) {
            vid = board->vid;
            pid = board->pid;
            altsetting = board->altsetting;
        }
        if (args.altsetting != -1) {
            if (altsetting != -1)
                LOG_INFO("Board altsetting overridden");
            altsetting = args.altsetting;
        }

        if (args.vid != 0) {
            if (vid != 0)
                LOG_INFO("Board VID overridden");
            vid = args.vid;
        }
        if (args.pid != 0) {
            if (pid != 0)
                LOG_INFO("Board PID overridden");
            pid = args.pid;
        }

        try {
            dfu = new DFU(args.bit_file, args.detect, vid, pid, altsetting,
                          args.verbose);
        } catch (std::exception &e) {
            return "DFU init failed with: " + string(e.what());
        }
        /* if verbose or detect: display device */
        if (args.verbose > 0 || args.detect)
            dfu->displayDFU();

        /* if detect: stop */
        if (args.detect)
            return "";

        try {
            dfu->download();
        } catch (std::exception &e) {
            return "DFU download failed with: " + string(e.what());
        }

        return "";
    }

#ifdef ENABLE_XVC
    /* ------------------- */
    /*      XVC server     */
    /* ------------------- */
    if (args.xvc) {
        try {
            return run_xvc_server(args, cable, &pins_config);
        } catch (std::exception &e) {
            return "FAILURE";
        }
    }
#endif

    /* jtag base */


    /* if no instruction from user -> select load */
    if (args.prg_type == Device::PRG_NONE)
        args.prg_type = Device::WR_SRAM;

    Jtag *jtag;
    try {
        jtag = new Jtag(cable, &pins_config, args.device, args.ftdi_serial,
                        args.freq, args.verbose, args.ip_adr, args.port,
                        args.invert_read_edge, args.probe_firmware);
    } catch (std::exception &e) {
        return "JTAG init failed with: " + string(e.what());
    }

    /* chain detection */
    vector<int> listDev = jtag->get_devices_list();
    int found = listDev.size();
    int idcode = -1, index = 0;

    if (args.verbose > normal)
        LOG_INFO("found %d devices", found);

    /* in verbose mode or when detect
     * display full chain with details
     */
    if (args.verbose > normal || args.detect) {
        for (int i = 0; i < found; i++) {
            int t = listDev[i];
            LOG_INFO("index %d:\n", i);
            if (fpga_list.find(t) != fpga_list.end()) {
                LOG_INFO("\tidcode 0x%x\n\tmanufacturer %s\n\tfamily %s\n\tmodel  %s\n",
                       t,
                       fpga_list[t].manufacturer.c_str(),
                       fpga_list[t].family.c_str(),
                       fpga_list[t].model.c_str());
                LOG_INFO("\tirlength %d\n", fpga_list[t].irlength);
            } else if (misc_dev_list.find(t) != misc_dev_list.end()) {
                LOG_INFO("\tidcode   0x%x\n\ttype     %s\n\tirlength %d\n",
                       t,
                       misc_dev_list[t].name.c_str(),
                       misc_dev_list[t].irlength);
            }
        }
        if (args.detect == true) {
            delete jtag;
            return "";
        }
    }

    if (found != 0) {
        if (args.index_chain == -1) {
            for (int i = 0; i < found; i++) {
                if (fpga_list.find(listDev[i]) != fpga_list.end()) {
                    index = i;
                    if (idcode != -1) {
                        for (int i = 0; i < found; i++)
                            LOG_INFO("0x%08x\n", listDev[i]);
                        delete (jtag);
                        return "Error: more than one FPGA found. Use --index-chain to force selection";
                    } else {
                        idcode = listDev[i];
                    }
                }
            }
        } else {
            index = args.index_chain;
            if (index > found || index < 0) {
                delete (jtag);
                return "Error: wrong index for device in JTAG chain";
            }
            idcode = listDev[index];
        }
    } else {
        delete (jtag);
        return "Error: no device found";
    }

    jtag->device_select(index);

    /* detect svf file and program the device */
    if (!args.file_type.compare("svf") ||
        args.bit_file.find(".svf") != string::npos) {
        SVF_jtag *svf = new SVF_jtag(jtag, args.verbose);
        try {
            svf->parse(args.bit_file);
        } catch (std::exception &e) {
            return "Error: Failed to detect svf file and program the device";
        }
        return "";
    }

    /* check if selected device is supported
	 * mainly used in conjunction with --index-chain
	 */
    if (fpga_list.find(idcode) == fpga_list.end()) {
        LOG_ERR("Device 0x%x not supported", idcode);
        delete (jtag);
        return "Error: device not supported";
    }

    string fab = fpga_list[idcode].manufacturer;


    Device *fpga;
    try {
        if (fab == "xilinx") {
            fpga = new Xilinx(jtag, args.bit_file, args.secondary_bit_file,
                              args.file_type, args.prg_type, args.fpga_part, args.bridge_path,
                              args.target_flash, args.verify, args.verbose, args.skip_load_bridge, args.skip_reset);
        } else if (fab == "altera") {
            fpga = new Altera(jtag, args.bit_file, args.file_type,
                              args.prg_type, args.fpga_part, args.bridge_path, args.verify,
                              args.verbose, args.skip_load_bridge, args.skip_reset);
        } else if (fab == "anlogic") {
            fpga = new Anlogic(jtag, args.bit_file, args.file_type,
                               args.prg_type, args.verify, args.verbose);
        } else if (fab == "efinix") {
            fpga = new Efinix(jtag, args.bit_file, args.file_type,
                    /*DBUS4 | DBUS7, DBUS5*/args.board, args.verify, args.verbose);
        } else if (fab == "Gowin") {
            fpga = new Gowin(jtag, args.bit_file, args.file_type, args.mcufw,
                             args.prg_type, args.external_flash, args.verify, args.verbose);
        } else if (fab == "lattice") {
            fpga = new Lattice(jtag, args.bit_file, args.file_type,
                               args.prg_type, args.flash_sector, args.verify, args.verbose);
        } else if (fab == "colognechip") {
            fpga = new CologneChip(jtag, args.bit_file, args.file_type,
                                   args.prg_type, args.board, args.cable, args.verify, args.verbose);
        } else {
            delete (jtag);
            return "Error: manufacturer " + fab + " not supported";
        }
    } catch (std::exception &e) {
        delete (jtag);
        return "Error: Failed to claim FPGA device: " + string(e.what());
    }

    if ((!args.bit_file.empty() ||
         !args.secondary_bit_file.empty() ||
         !args.file_type.empty())
        && args.prg_type != Device::RD_FLASH) {
        try {
            fpga->program(args.offset, args.unprotect_flash);
        } catch (std::exception &e) {
            delete (fpga);
            delete (jtag);
            return "Error: Failed to program FPGA: " + string(e.what());
        }
    }

    if (args.conmcu == true) {
        fpga->connectJtagToMCU();
    }

    /* unprotect SPI flash */
    if (args.unprotect_flash && args.bit_file.empty()) {
        fpga->unprotect_flash();
    }

    /* bulk erase SPI flash */
    if (args.bulk_erase_flash && args.bit_file.empty()) {
        fpga->bulk_erase_flash();
    }

    /* protect SPI flash */
    if (args.protect_flash != 0) {
        fpga->protect_flash(args.protect_flash);
    }

    if (args.prg_type == Device::RD_FLASH) {
        if (args.file_size == 0) {
            LOG_ERR("0 size for dump");
        } else {
            fpga->dumpFlash(args.offset, args.file_size);
        }
    }

    if (args.reset)
        fpga->reset();

    delete (fpga);
    delete (jtag);

    return "";
}


#ifdef ENABLE_XVC
int run_xvc_server(const struct arguments &args, const cable_t &cable,
    const jtag_pins_conf_t *pins_config)
{
    // create XVC instance
    try {
        XVC_server *xvc = NULL;
        xvc = new XVC_server(args.port, cable, pins_config, args.device,
                args.ftdi_serial, args.freq, args.verbose, args.ip_adr,
                args.invert_read_edge, args.probe_firmware);
        /* create connection */
        xvc->open_connection();
        /* start loop */
        xvc->listen_loop();
        /* close connection */
        xvc->close_connection();
        delete xvc;
    } catch (std::exception &e) {
        LOG_ERR("XVC_server failed with %s", e.what());
        return EXIT_FAILURE;
    }
    LOG_INFO("Xilinx Virtual Cable Stopped! ");
    return EXIT_SUCCESS;
}
#endif

// parse double from string in engineering notation
// can deal with postfixes k and m, add more when required
static int parse_eng(string arg, double *dst) {
    try {
        size_t end;
        double base = stod(arg, &end);
        if (end == arg.size()) {
            *dst = base;
            return 0;
        } else if (end == (arg.size() - 1)) {
            switch (arg.back()) {
                case 'k':
                case 'K':
                    *dst = (uint32_t) (1e3 * base);
                    return 0;
                case 'm':
                case 'M':
                    *dst = (uint32_t) (1e6 * base);
                    return 0;
                default:
                    return EINVAL;
            }
        } else {
            return EINVAL;
        }
    } catch (...) {
        LOG_ERR("speed: invalid format");
        return EINVAL;
    }
}

/* arguments parser */
int parse_opt(int argc, char **argv, struct arguments *args,
              jtag_pins_conf_t *pins_config) {
    string freqo;
    vector<string> pins, bus_dev_num;
    bool verbose, quiet;
    int8_t verbose_level = -2;
    try {
        cxxopts::Options options(argv[0], "openFPGALoader -- a program to flash FPGA",
                                 "<gwenhael.goavec-merou@trabucayre.com>");
        options
                .positional_help("BIT_FILE")
                .show_positional_help();

        options
                .add_options()
                        ("altsetting", "DFU interface altsetting (only for DFU mode)",
                         cxxopts::value<int16_t>(args->altsetting))
                        ("bitstream", "bitstream",
                         cxxopts::value<std::string>(args->bit_file))
                        ("secondary-bitstream", "secondary bitstream (some Xilinx"
                                                " UltraScale boards)",
                         cxxopts::value<std::string>(args->secondary_bit_file))
                        ("b,board", "board name, may be used instead of cable",
                         cxxopts::value<string>(args->board))
                        ("B,bridge", "disable spiOverJtag model detection by providing "
                                     "bitstream(intel/xilinx)",
                         cxxopts::value<string>(args->bridge_path))
                        ("c,cable", "jtag interface", cxxopts::value<string>(args->cable))
                        ("invert-read-edge",
                         "JTAG mode / FTDI: read on negative edge instead of positive",
                         cxxopts::value<bool>(args->invert_read_edge))
                        ("vid", "probe Vendor ID", cxxopts::value<uint16_t>(args->vid))
                        ("pid", "probe Product ID", cxxopts::value<uint16_t>(args->pid))
                        ("cable-index", "probe index (FTDI and cmsisDAP)",
                         cxxopts::value<int16_t>(args->cable_index))
                        ("busdev-num",
                         "select a probe by it bus and device number (bus_num:device_addr)",
                         cxxopts::value<vector<string>>(bus_dev_num))
                        ("ftdi-serial", "FTDI chip serial number",
                         cxxopts::value<string>(args->ftdi_serial))
                        ("ftdi-channel",
                         "FTDI chip channel number (channels 0-3 map to A-D)",
                         cxxopts::value<int>(args->ftdi_channel))
#if defined(USE_DEVICE_ARG)
                ("d,device",  "device to use (/dev/ttyUSBx)",
                    cxxopts::value<string>(args->device))
#endif
                ("detect", "detect FPGA",
                 cxxopts::value<bool>(args->detect))
                ("dfu", "DFU mode", cxxopts::value<bool>(args->dfu))
                ("dump-flash", "Dump flash mode")
                ("bulk-erase", "Bulk erase flash",
                 cxxopts::value<bool>(args->bulk_erase_flash))
                ("target-flash",
                 "for boards with multiple flash chips (some Xilinx UltraScale"
                 " boards), select the target flash: primary (default), secondary or both",
                 cxxopts::value<string>(args->target_flash))
                ("external-flash",
                 "select ext flash for device with internal and external storage",
                 cxxopts::value<bool>(args->external_flash))
                ("file-size",
                 "provides size in Byte to dump, must be used with dump-flash",
                 cxxopts::value<unsigned int>(args->file_size))
                ("file-type",
                 "provides file type instead of let's deduced by using extension",
                 cxxopts::value<string>(args->file_type))
                ("flash-sector", "flash sector (Lattice parts only)",
                 cxxopts::value<string>(args->flash_sector))
                ("fpga-part", "fpga model flavor + package",
                 cxxopts::value<string>(args->fpga_part))
                ("freq", "jtag frequency (Hz)", cxxopts::value<string>(freqo))
                ("f,write-flash",
                 "write bitstream in flash (default: false)")
                ("index-chain", "device index in JTAG-chain",
                 cxxopts::value<int>(args->index_chain))
                ("ip", "IP address (XVC and remote bitbang client)",
                 cxxopts::value<string>(args->ip_adr))
                ("list-boards", "list all supported boards",
                 cxxopts::value<bool>(args->list_boards))
                ("list-cables", "list all supported cables",
                 cxxopts::value<bool>(args->list_cables))
                ("list-fpga", "list all supported FPGA",
                 cxxopts::value<bool>(args->list_fpga))
                ("m,write-sram",
                 "write bitstream in SRAM (default: true)")
                ("o,offset", "Start address (in bytes) for read/write into non volatile memory (default: 0)",
                 cxxopts::value<unsigned int>(args->offset))
                ("pins", "pin config TDI:TDO:TCK:TMS",
                 cxxopts::value<vector<string>>(pins))
                ("probe-firmware", "firmware for JTAG probe (usbBlasterII)",
                 cxxopts::value<string>(args->probe_firmware))
                ("protect-flash", "protect SPI flash area",
                 cxxopts::value<uint32_t>(args->protect_flash))
                ("quiet", "Produce quiet output (no progress bar)",
                 cxxopts::value<bool>(quiet))
                ("r,reset", "reset FPGA after operations",
                 cxxopts::value<bool>(args->reset))
                ("scan-usb", "scan USB to display connected probes",
                 cxxopts::value<bool>(args->scan_usb))
                ("skip-load-bridge", "skip writing bridge to SRAM when in write-flash mode",
                 cxxopts::value<bool>(args->skip_load_bridge))
                ("skip-reset", "skip resetting the device when in write-flash mode",
                 cxxopts::value<bool>(args->skip_reset))
                ("spi", "SPI mode (only for FTDI in serial mode)",
                 cxxopts::value<bool>(args->spi))
                ("unprotect-flash", "Unprotect flash blocks",
                 cxxopts::value<bool>(args->unprotect_flash))
                ("v,verbose", "Produce verbose output", cxxopts::value<bool>(verbose))
                ("verbose-level", "verbose level -1: quiet, 0: normal, 1:verbose, 2:debug",
                 cxxopts::value<int8_t>(verbose_level))
                ("h,help", "Give this help list")
                ("verify", "Verify write operation (SPI Flash only)",
                 cxxopts::value<bool>(args->verify))
#ifdef ENABLE_XVC
                ("xvc",   "Xilinx Virtual Cable Functions",
                    cxxopts::value<bool>(args->xvc))
#endif
                ("port", "Xilinx Virtual Cable and remote bitbang Port (default 3721)",
                 cxxopts::value<int>(args->port))
                ("mcufw", "Microcontroller firmware",
                 cxxopts::value<std::string>(args->mcufw))
                ("conmcu", "Connect JTAG to MCU",
                 cxxopts::value<bool>(args->conmcu))
                ("V,Version", "Print program version");

        options.parse_positional({"bitstream"});
        auto result = options.parse(argc, argv);

        if (result.count("help")) {
            LOG_INFO("%s", options.help().c_str());
            return 1;
        }

        if (verbose && quiet) {
            LOG_ERR("Can't select quiet and verbose mode in same time");
            throw std::exception();
        }
        if (verbose)
            args->verbose = 1;
        if (quiet)
            args->verbose = -1;
        if (verbose_level != -2) {
            if ((verbose && verbose_level != 1) ||
                (quiet && verbose_level != -1)) {
                LOG_ERR("Mismatch quiet/verbose and verbose-level\n");
                throw std::exception();
            }

            args->verbose = verbose_level;
        }

        if (result.count("Version")) {
            LOG_INFO("openFPGALoader %s", VERSION);
            return 1;
        }

        if (result.count("write-flash") && result.count("write-sram") &&
            result.count("dump-flash")) {
            LOG_ERR("Both write to flash and write to ram enabled");
            throw std::exception();
        }

        if (result.count("write-flash"))
            args->prg_type = Device::WR_FLASH;
        else if (result.count("write-sram"))
            args->prg_type = Device::WR_SRAM;
        else if (result.count("dump-flash"))
            args->prg_type = Device::RD_FLASH;
        else if (result.count("external-flash"))
            args->prg_type = Device::WR_FLASH;

        if (result.count("freq")) {
            double freq;
            if (parse_eng(freqo, &freq)) {
                LOG_ERR("Invalid format for --freq");
                throw std::exception();
            }
            if (freq < 1) {
                LOG_ERR("--freq must be positive");
                throw std::exception();
            }
            args->freq = static_cast<uint32_t>(freq);
        }

        if (result.count("ftdi-channel")) {
            if (args->ftdi_channel < 0 || args->ftdi_channel > 3) {
                LOG_ERR("Valid FTDI channels are 0-3.");
                throw std::exception();
            }
        }

        if (result.count("busdev-num")) {
            if (bus_dev_num.size() != 2) {
                LOG_ERR("busdev-num must be xx:yy");
                throw std::exception();
            }
            try {
                args->bus_addr = static_cast<uint8_t>(std::stoi(bus_dev_num[0],
                                                                nullptr, 0));
                args->device_addr = static_cast<uint8_t>(
                        std::stoi(bus_dev_num[1], nullptr, 0));
            } catch (std::exception &e) {
                LOG_ERR("busdev-num invalid format: must be numeric values");
                throw std::exception();
            }
        }

        if (result.count("pins")) {
            if (pins.size() != 4) {
                LOG_ERR("pin_config need 4 pins");
                throw std::exception();
            }

            static std::map<std::string, int> pins_list = {
                    {"TXD", FT232RL_TXD},
                    {"RXD", FT232RL_RXD},
                    {"RTS", FT232RL_RTS},
                    {"CTS", FT232RL_CTS},
                    {"DTR", FT232RL_DTR},
                    {"DSR", FT232RL_DSR},
                    {"DCD", FT232RL_DCD},
                    {"RI",  FT232RL_RI}};

            for (int i = 0; i < 4; i++) {
                int pin_num;
                try {
                    pin_num = std::stoi(pins[i], nullptr, 0);
                } catch (std::exception &e) {
                    if (pins_list.find(pins[i]) == pins_list.end()) {
                        LOG_ERR("Invalid pin name");
                        throw std::exception();
                    }
                    pin_num = pins_list[pins[i]];
                }

                switch (i) {
                    case 0:
                        pins_config->tdi_pin = pin_num;
                        break;
                    case 1:
                        pins_config->tdo_pin = pin_num;
                        break;
                    case 2:
                        pins_config->tck_pin = pin_num;
                        break;
                    case 3:
                        pins_config->tms_pin = pin_num;
                        break;
                }
            }
            args->pin_config = true;
        }

        if (args->target_flash == "both" || args->target_flash == "secondary") {
            if ((args->prg_type == Device::WR_FLASH || args->prg_type == Device::RD_FLASH) &&
                args->secondary_bit_file.empty() &&
                !args->protect_flash &&
                !args->unprotect_flash &&
                !args->bulk_erase_flash
                    ) {
                LOG_ERR("Secondary bitfile not specified");
                LOG_INFO("%s", options.help().c_str());
                throw std::exception();
            }
        }

        if (args->list_cables || args->list_boards || args->list_fpga ||
            args->scan_usb)
            args->is_list_command = true;

        if (args->bit_file.empty() &&
            args->secondary_bit_file.empty() &&
            args->file_type.empty() &&
            !args->is_list_command &&
            !args->detect &&
            !args->protect_flash &&
            !args->unprotect_flash &&
            !args->bulk_erase_flash &&
            !args->xvc &&
            !args->reset &&
            !args->conmcu) {
            LOG_ERR("bitfile not specified");
            LOG_INFO("%s", options.help().c_str());
            throw std::exception();
        }
    } catch (const cxxopts::OptionException &e) {
        LOG_ERR("Parsing options: %s", e.what());
        throw std::exception();
    }

    return 0;
}

/* display list of cables, boards and devices supported */
void displaySupported(const struct arguments &args, int8_t verbose_level) {
    if (args.list_cables == true) {
        stringstream t;
        t << setw(25) << left << "cable name" << "vid:pid";
        LOG_INFO("%s", t.str().c_str());
        for (auto b = cable_list.begin(); b != cable_list.end(); b++) {
            cable_t c = (*b).second;
            stringstream ss;
            ss << setw(25) << left << (*b).first;
            ss << "0x" << hex << right << setw(4) << setfill('0') << c.vid
               << ":" << setw(4) << c.pid;
            LOG_INFO("%s", ss.str().c_str());
        }
    }

    if (args.list_boards) {
        stringstream t;
        t << setw(25) << left << "board name" << "cable_name";
        LOG_INFO("%s", t.str().c_str());
        for (auto b = board_list.begin(); b != board_list.end(); b++) {
            stringstream ss;
            target_board_t c = (*b).second;
            ss << setw(25) << left << (*b).first << c.cable_name;
            LOG_INFO("%s", ss.str().c_str());
        }
    }

    if (args.list_fpga) {
        stringstream t;
        t << setw(12) << left << "IDCode" << setw(14) << "manufacturer";
        t << setw(16) << "family" << setw(20) << "model";
        LOG_INFO("%s", t.str().c_str());
        for (auto b = fpga_list.begin(); b != fpga_list.end(); b++) {
            fpga_model fpga = (*b).second;
            stringstream ss, idCode;
            idCode << "0x" << hex << setw(8) << setfill('0') << (*b).first;
            ss << setw(12) << left << idCode.str();
            ss << setw(14) << fpga.manufacturer << setw(16) << fpga.family;
            ss << setw(20) << fpga.model;
            LOG_INFO("%s", ss.str().c_str());
        }
    }

    if (args.scan_usb) {
        libusb_ll usb(0, 0);
        usb.scan(verbose_level);
    }
}

std::map<uint32_t, fpga_model> FPGALoader::detect_fpga(int8_t verbose_level, usb_scan_item item, const char* cable_name, int ftdi_channel) {

    auto fpga_list_ret = std::map<uint32_t, fpga_model>();

    cable_t cable;
    target_board_t *board = NULL;
    jtag_pins_conf_t pins_config = {0, 0, 0, 0};

    /* command line args. */
    struct arguments args = {verbose_level, false, true, false, false, 0, "", "", "", cable_name, "", ftdi_channel,
                             0, false, "-", false, false, false, false, Device::PRG_NONE, false,
            /* spi dfu    file_type fpga_part bridge_path probe_firmware */
                             false, false, "", "", "", "",
            /* index_chain file_size target_flash external_flash altsetting */
                             -1, 0, "primary", false, -1,
            /* vid, pid, index bus_addr, device_addr */
                             item.vid, item.pid, -1, item.bus_addr, item.dev_addr,
                             "127.0.0.1", 0, false, false, "", false, false,
            /* xvc server */
                             false, 3721, "-",
                             "", false,  // mcufw conmcu
    };

    if (args.cable[0] == '-') { /* if no board and no cable */
        LOG_WARNING("No cable or board specified: using direct ft2232 interface");
        args.cable = "ft2232";
    }

    if (args.freq == 0)
        args.freq = DEFAULT_FREQ;

    auto select_cable = cable_list.find(args.cable);
    if (select_cable == cable_list.end()) {
        LOG_ERR("Cable %s not found", args.cable.c_str());
        return fpga_list_ret;
    }
    cable = select_cable->second;

    if (args.ftdi_channel != -1) {
        if (cable.type != MODE_FTDI_SERIAL && cable.type != MODE_FTDI_BITBANG) {
            LOG_ERR("FTDI channel param is for FTDI cables.");
            return fpga_list_ret;
        }

        const int mapping[] = {INTERFACE_A, INTERFACE_B, INTERFACE_C,
                               INTERFACE_D};
        cable.config.interface = mapping[args.ftdi_channel];
    }

    if (args.vid != 0) {
        LOG_INFO("Cable VID overridden");
        cable.vid = args.vid;
    }
    if (args.pid != 0) {
        LOG_INFO("Cable PID overridden");
        cable.pid = args.pid;
    }

    cable.bus_addr = args.bus_addr;
    cable.device_addr = args.device_addr;

    // always set this
    cable.config.index = args.cable_index;

    /* jtag base */


    /* if no instruction from user -> select load */
    if (args.prg_type == Device::PRG_NONE)
        args.prg_type = Device::WR_SRAM;

    Jtag *jtag;
    try {
        jtag = new Jtag(cable, &pins_config, args.device, args.ftdi_serial,
                        args.freq, args.verbose, args.ip_adr, args.port,
                        args.invert_read_edge, args.probe_firmware);
    } catch (std::exception &e) {
        LOG_ERR("JTAG init failed with: %s", e.what());
        return fpga_list_ret;
    }

    /* chain detection */
    vector<int> listDev = jtag->get_devices_list();
    int found = listDev.size();
    int idcode = -1, index = 0;

    for (int i = 0; i < found; i++) {
        int t = listDev[i];
        if (fpga_list.find(t) != fpga_list.end()) {
            fpga_list_ret.insert({t, fpga_list[t]});
        }
    }

    delete jtag;
    return fpga_list_ret;
}

std::string FPGALoader::write_flash(char* verbose_level, char* cable, char* ftdi_channel, char* spi_over_jtag_file, char* mcs_file) {
    char *arguments[] = {"openFPGALoader", "--verbose-level", verbose_level,"-c", cable, "--ftdi-channel",  ftdi_channel,
                         "-B", spi_over_jtag_file, "-f", mcs_file,
                         "--verify", "--reset",
                         NULL};
    return main_fpga(13, arguments);
}

std::string FPGALoader::send_command(int verbose_level, char* cable, char* command, int len) {
    /* search for cable */
    auto select_cable = cable_list.find(cable);
    if (select_cable == cable_list.end()) {
        return "Error : cable not found";
    }
    ATSerialCommunication* at_communication = new ATSerialCommunication(select_cable->second, verbose_level);
    auto ret = at_communication->write_command(reinterpret_cast<unsigned char*>(command), len, verbose_level);
    delete(at_communication);
    return ret;
}

std::string FPGALoader::reset(char *verbose_level, char *cable, char *ftdi_channel) {
    char *arguments[] = {"openFPGALoader", "--verbose-level", verbose_level, "-c", cable, "--ftdi-channel",
                         ftdi_channel, "--reset",
                         NULL};
    return main_fpga(8, arguments);
}