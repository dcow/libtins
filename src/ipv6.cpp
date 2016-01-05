/*
 * Copyright (c) 2014, Matias Fontanini
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above
 *   copyright notice, this list of conditions and the following disclaimer
 *   in the documentation and/or other materials provided with the
 *   distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <cstring>
#ifndef _WIN32
    #include <netinet/in.h>
    #include <sys/socket.h>
#else
    #include <ws2tcpip.h>
#endif
#include <algorithm> 
#include "ipv6.h"
#include "constants.h"
#include "packet_sender.h"
#include "rawpdu.h"
#include "exceptions.h"
#include "pdu_allocator.h"
#include "internals.h"
#include "memory_helpers.h"

using std::copy;

using Tins::Memory::InputMemoryStream;
using Tins::Memory::OutputMemoryStream;

namespace Tins {

IPv6::IPv6(address_type ip_dst, address_type ip_src, PDU *child) 
: header_(), headers_size_(0) {
    version(6);
    dst_addr(ip_dst);
    src_addr(ip_src);
}

IPv6::IPv6(const uint8_t *buffer, uint32_t total_sz) 
: headers_size_(0) {
    InputMemoryStream stream(buffer, total_sz);
    stream.read(header_);
    uint8_t current_header = header_.next_header;
    while (stream) {
        if (is_extension_header(current_header)) {
            const uint8_t ext_type = stream.read<uint8_t>();
            // every ext header is at least 8 bytes long
            // minus one, from the next_header field.
            const uint32_t ext_size = (static_cast<uint32_t>(stream.read<uint8_t>()) + 1) * 8;
            const uint32_t payload_size = ext_size - sizeof(uint8_t) * 2;
            // -1 -> next header identifier
            if (!stream.can_read(ext_size)) { 
                throw malformed_packet();
            }
            // minus one, from the size field
            add_ext_header(ext_header(ext_type, payload_size, stream.pointer()));
            current_header = ext_type;
            stream.skip(payload_size);
        }
        else {
            inner_pdu(
                Internals::pdu_from_flag(
                    static_cast<Constants::IP::e>(current_header),
                    stream.pointer(), 
                    stream.size(),
                    false
                )
            );
            if (!inner_pdu()) {
                inner_pdu(
                    Internals::allocate<IPv6>(
                        current_header,
                        stream.pointer(), 
                        stream.size()
                    )
                );
                if (!inner_pdu()) {
                    inner_pdu(new Tins::RawPDU(stream.pointer(), stream.size()));
                }
            }
            // We got to an actual PDU, we're done
            break;
        }
    }
}

bool IPv6::is_extension_header(uint8_t header_id) {
    return header_id == HOP_BY_HOP || header_id == DESTINATION_ROUTING_OPTIONS
        || header_id == ROUTING || header_id == FRAGMENT || header_id == AUTHENTICATION
        || header_id == SECURITY_ENCAPSULATION || header_id == DESTINATION_OPTIONS
        || header_id == MOBILITY || header_id == NO_NEXT_HEADER;
}

void IPv6::version(small_uint<4> new_version) {
    header_.version = new_version;
}

void IPv6::traffic_class(uint8_t new_traffic_class) {
    #if TINS_IS_LITTLE_ENDIAN
    header_.traffic_class = (new_traffic_class >> 4) & 0xf;
    header_.flow_label[0] = (header_.flow_label[0] & 0x0f) | ((new_traffic_class << 4) & 0xf0);
    #else
    header_.traffic_class = new_traffic_class;
    #endif
}

void IPv6::flow_label(small_uint<20> new_flow_label) {
    #if TINS_IS_LITTLE_ENDIAN
    uint32_t value = Endian::host_to_be<uint32_t>(new_flow_label);
    header_.flow_label[2] = (value >> 24) & 0xff;
    header_.flow_label[1] = (value >> 16) & 0xff;
    header_.flow_label[0] = ((value >> 8) & 0x0f) | (header_.flow_label[0] & 0xf0);
    #else
    header_.flow_label = new_flow_label;
    #endif
}

void IPv6::payload_length(uint16_t new_payload_length) {
    header_.payload_length = Endian::host_to_be(new_payload_length);
}

void IPv6::next_header(uint8_t new_next_header) {
    header_.next_header = new_next_header;
}

void IPv6::hop_limit(uint8_t new_hop_limit) {
    header_.hop_limit = new_hop_limit;
}

void IPv6::src_addr(const address_type &new_src_addr) {
    new_src_addr.copy(header_.src_addr);
}

void IPv6::dst_addr(const address_type &new_dst_addr) {
    new_dst_addr.copy(header_.dst_addr);
}

uint32_t IPv6::header_size() const {
    return sizeof(header_) + headers_size_;
}

bool IPv6::matches_response(const uint8_t *ptr, uint32_t total_sz) const {
    if (total_sz < sizeof(ipv6_header)) {
        return false;
    }
    const ipv6_header *hdr_ptr = (const ipv6_header*)ptr;
    // checks for ff02 multicast
    if (src_addr() == hdr_ptr->dst_addr && 
        (dst_addr() == hdr_ptr->src_addr || (header_.dst_addr[0] == 0xff && header_.dst_addr[1] == 0x02))) {
        // is this OK? there's no inner pdu, simple dst/src addr match should suffice
        if (!inner_pdu()) {
            return true;
        }
        ptr += sizeof(ipv6_header);
        total_sz -= sizeof(ipv6_header);
        uint8_t current = hdr_ptr->next_header;
        // 8 == minimum header size
        while (total_sz > 8 && is_extension_header(current)) {
            if (static_cast<uint32_t>(ptr[1] + 1) * 8 > total_sz) {
                return false;
            }
            current = ptr[0];
            total_sz -= (ptr[1] + 1) * 8;
            ptr += (ptr[1] + 1) * 8;
        }
        if (!is_extension_header(current)) {
            return inner_pdu()->matches_response(ptr, total_sz);
        }
    }
    return false;
}

void IPv6::write_serialization(uint8_t *buffer, uint32_t total_sz, const PDU *parent) {
    OutputMemoryStream stream(buffer, total_sz);
    if (inner_pdu()) {
        uint8_t new_flag = Internals::pdu_flag_to_ip_type(inner_pdu()->pdu_type());
        if (new_flag == 0xff && Internals::pdu_type_registered<IPv6>(inner_pdu()->pdu_type())) {
            new_flag = static_cast<Constants::IP::e>(
                Internals::pdu_type_to_id<IPv6>(inner_pdu()->pdu_type())
            );
        }
        set_last_next_header(new_flag);
    }
    payload_length(static_cast<uint16_t>(total_sz - sizeof(header_)));
    stream.write(header_);
    for (headers_type::const_iterator it = ext_headers_.begin(); it != ext_headers_.end(); ++it) {
        write_header(*it, stream);
    }
}

#ifndef BSD
void IPv6::send(PacketSender &sender, const NetworkInterface &) {
    struct sockaddr_in6 link_addr;
    PacketSender::SocketType type = PacketSender::IPV6_SOCKET;
    link_addr.sin6_family = AF_INET6;
    link_addr.sin6_port = 0;
    copy(header_.dst_addr, header_.dst_addr + address_type::address_size, (uint8_t*)&link_addr.sin6_addr);
    if (inner_pdu() && inner_pdu()->pdu_type() == PDU::ICMP) {
        type = PacketSender::ICMP_SOCKET;
    }

    sender.send_l3(*this, (struct sockaddr*)&link_addr, sizeof(link_addr), type);
}
#endif

void IPv6::add_ext_header(const ext_header &header) {
    ext_headers_.push_back(header);
    headers_size_ += static_cast<uint32_t>(header.data_size() + sizeof(uint8_t) * 2);
}

const IPv6::ext_header *IPv6::search_header(ExtensionHeader id) const {
    uint8_t current_header = header_.next_header;
    headers_type::const_iterator it = ext_headers_.begin();
    while (it != ext_headers_.end() && current_header != id) {
        current_header = it->option();
        ++it;
    }
    if (it == ext_headers_.end()) {
        return 0;
    }
    return &*it;
}

void IPv6::set_last_next_header(uint8_t value) {
    if (ext_headers_.empty()) {
        header_.next_header = value;
    }
    else {
        ext_headers_.back().option(value);
    }
}

void IPv6::write_header(const ext_header &header, OutputMemoryStream& stream) {
    const uint8_t length = header.length_field() / 8;
    stream.write(header.option());
    stream.write(length);
    stream.write(header.data_ptr(), header.data_size());
}

} // Tins
