//
// Created by Valentina Bujic on 21.8.23..
//

#ifndef OPENFPGALOADER_ATSERIALCOMMUNICATION_HPP
#define OPENFPGALOADER_ATSERIALCOMMUNICATION_HPP

#include "ftdipp_mpsse.hpp"

class ATSerialCommunication {
public:
    ATSerialCommunication(const cable_t &cable, int verbose);
    ~ATSerialCommunication();
    std::string write_command(unsigned char* command, int len, int verbose);

protected:
    struct ftdi_context *_ftdi;

    static const int MESSAGE_SIZE;
    static const int BAUDRATE;
    static const int LATENCY;

};


#endif //OPENFPGALOADER_ATSERIALCOMMUNICATION_HPP
