/**
   @copyright
   Copyright (c) 2013 - 2017, INSIDE Secure Oy. All rights reserved.
*/

#include "public_defs.h"

#ifndef IP_SELECTOR_H
#define IP_SELECTOR_H

#define IP_SELECTOR_PORT_NONE -1
#define IP_SELECTOR_PORT_OPAQUE -2
#define IP_SELECTOR_PORT_MIN 0
#define IP_SELECTOR_PORT_MAX 65535


struct IPSelectorAddress
{
    uint8_t begin[16];
    uint8_t end[16];
};


struct IPSelectorPort
{
    int32_t begin;
    int32_t end;
};


struct IPSelectorEndpoint
{
    struct IPSelectorAddress address;
    struct IPSelectorPort port;
    uint8_t ip_protocol;
    uint8_t ip_version;
    uint16_t four_byte_alignment;
    uint32_t eight_byte_alignment;
};


struct IPSelector
{
    uint8_t ip_protocol;
    uint8_t ip_version;
    uint16_t four_byte_alignment;
    uint16_t source_endpoint_count;
    uint16_t destination_endpoint_count;

    uint16_t source_address_count;
    uint16_t destination_address_count;
    uint16_t source_port_count;
    uint16_t destination_port_count;

    uint32_t bytecount;
    uint32_t for_8_byte_alignment;
};


struct IPSelectorGroup
{
    uint32_t selector_count;
    uint32_t bytecount;
};


struct IPSelectorFields
{
    uint8_t source_address[16];
    uint8_t destination_address[16];

    int32_t source_port;
    int32_t destination_port;

    int32_t ip_version;
    int32_t ip_protocol;
};

#endif /* IP_SELECTOR_H */
