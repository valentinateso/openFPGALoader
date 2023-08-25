//
// Created by Valentina Bujic on 21.8.23..
//

#include "atSerialCommunication.hpp"

ATSerialCommunication::ATSerialCommunication(const cable_t &cable, bool verbose) {
    _ftdi = ftdi_new();
    if (_ftdi == NULL) {
        printf("Unable to create ftdi\r\n");
        return;
    }
    int interface = ftdi_set_interface(_ftdi, (ftdi_interface) cable.config.interface);
    if (interface < 0) {
        printf("Unable to set interface\r\n");
        return;
    }
    int latency = ftdi_set_latency_timer(_ftdi, 255);

    int ftStatus = ftdi_usb_open(_ftdi, cable.vid, cable.pid);
    if (ftStatus < 0) {
        printf("Error opening usb device: %s\r\n", ftdi_get_error_string(_ftdi));
        return;
    }
    if (ftdi_set_baudrate(_ftdi, 115200) < 0) {
        printf("Error setting baud rate! \n");
        return;
    }
}

ATSerialCommunication::~ATSerialCommunication() {
    ftdi_usb_close(_ftdi);
    ftdi_deinit(_ftdi);
}

std::string ATSerialCommunication::write_command(unsigned char *command, int len, bool verbose) {
    if (verbose) {
        printf("Sending: %s\n", command);
    }
    int ret = ftdi_write_data(_ftdi, command, len);
    if (ret != len) {
        printf("Write error: %d (written %d)\n", ret, len);
        return "NULL";
    }


    unsigned char u_response[255];
    ret = ftdi_read_data(_ftdi, u_response, 255);
    if (ret < 0) {
        printf("Read error: %s\n", std::to_string(ret).c_str());
        return "NULL";
    }

    if (verbose) {
        printf("Response: %s\n", u_response);
    }

    char response[255];
    snprintf(response, sizeof u_response, "%s", u_response);
    return response;
}