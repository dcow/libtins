/*
 * libtins is a net packet wrapper library for crafting and
 * interpreting sniffed packets.
 *
 * Copyright (C) 2011 Nasel
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <cassert>
#include <cstring>
#include <stdexcept>
#include <algorithm>
#include <utility>
#ifndef WIN32
    #include <net/ethernet.h>
    #include <netpacket/packet.h>
    #include <netinet/in.h>
#endif
#include "ieee802-11.h"
#include "rawpdu.h"
#include "radiotap.h"
#include "sniffer.h"
#include "utils.h"
#include "snap.h"

using namespace std;

const uint8_t *Tins::IEEE802_11::BROADCAST = (const uint8_t*)"\xff\xff\xff\xff\xff\xff";

Tins::IEEE802_11::IEEE802_11(const uint8_t* dst_hw_addr, const uint8_t* src_hw_addr, PDU* child) : PDU(ETHERTYPE_IP, child), _options_size(0) {
    memset(&this->_header, 0, sizeof(ieee80211_header));
    if(dst_hw_addr)
        this->addr1(dst_hw_addr);
    if(src_hw_addr)
        this->addr2(src_hw_addr);
}

Tins::IEEE802_11::IEEE802_11(const std::string& iface, const uint8_t* dst_hw_addr, const uint8_t* src_hw_addr, PDU* child) throw (std::runtime_error) : PDU(ETHERTYPE_IP, child), _options_size(0) {
    memset(&this->_header, 0, sizeof(ieee80211_header));
    if(dst_hw_addr)
        this->addr1(dst_hw_addr);
    if(src_hw_addr)
        this->addr2(src_hw_addr);
    this->iface(iface);
}


Tins::IEEE802_11::IEEE802_11(uint32_t iface_index, const uint8_t* dst_hw_addr, const uint8_t* src_hw_addr, PDU* child) : PDU(ETHERTYPE_IP, child), _options_size(0) {
    memset(&this->_header, 0, sizeof(ieee80211_header));
    if(dst_hw_addr)
        this->addr1(dst_hw_addr);
    if(src_hw_addr)
        this->addr2(src_hw_addr);
    this->iface(iface_index);
}

Tins::IEEE802_11::IEEE802_11(const ieee80211_header *header_ptr) : PDU(ETHERTYPE_IP) {

}

Tins::IEEE802_11::IEEE802_11(const uint8_t *buffer, uint32_t total_sz) : PDU(ETHERTYPE_IP), _options_size(0) {
    if(total_sz < sizeof(_header.control))
        throw std::runtime_error("Not enough size for an IEEE 802.11 header in the buffer.");
    uint32_t sz = std::min((uint32_t)sizeof(_header), total_sz);
    std::memcpy(&_header, buffer, sz);
    buffer += sz;
    total_sz -= sz;
    if(type() == 0 && subtype() < 4) {
        // It's a data packet
        inner_pdu(new Tins::SNAP(buffer, total_sz));
    }
}

Tins::IEEE802_11::~IEEE802_11() {
    while(_options.size()) {
        delete[] _options.front().value;
        _options.pop_front();
    }
}

void Tins::IEEE802_11::parse_tagged_parameters(const uint8_t *buffer, uint32_t total_sz) {
    uint8_t opcode, length;
    while(total_sz >= 2) {
        opcode = buffer[0];
        length = buffer[1];
        buffer += 2;
        total_sz -= 2;
        if(length > total_sz)
            return; //malformed
        add_tagged_option((TaggedOption)opcode, length, buffer);
        buffer += length;
        total_sz -= length;
    }
}

Tins::IEEE802_11::IEEE802_11_Option::IEEE802_11_Option(uint8_t opt, uint8_t len, const uint8_t *val) : option(opt), length(len) {
    value = new uint8_t[len];
    std::memcpy(value, val, len);
}

void Tins::IEEE802_11::add_tagged_option(TaggedOption opt, uint8_t len, const uint8_t *val) {
    uint32_t opt_size = len + (sizeof(uint8_t) << 1);
    _options.push_back(IEEE802_11_Option((uint8_t)opt, len, val));
    _options_size += opt_size;
}

const Tins::IEEE802_11::IEEE802_11_Option *Tins::IEEE802_11::lookup_option(TaggedOption opt) const {
    for(std::list<IEEE802_11_Option>::const_iterator it = _options.begin(); it != _options.end(); ++it)
        if(it->option == (uint8_t)opt)
            return &(*it);
    return 0;
}

void Tins::IEEE802_11::protocol(uint8_t new_proto) {
    this->_header.control.protocol = new_proto;
}

void Tins::IEEE802_11::type(uint8_t new_type) {
    this->_header.control.type = new_type;
}

void Tins::IEEE802_11::subtype(uint8_t new_subtype) {
    this->_header.control.subtype = new_subtype;
}

void Tins::IEEE802_11::to_ds(bool new_value) {
    this->_header.control.to_ds = (new_value)? 1 : 0;
}

void Tins::IEEE802_11::from_ds(bool new_value) {
    this->_header.control.from_ds = (new_value)? 1 : 0;
}

void Tins::IEEE802_11::more_frag(bool new_value) {
    this->_header.control.more_frag = (new_value)? 1 : 0;
}

void Tins::IEEE802_11::retry(bool new_value) {
    this->_header.control.retry = (new_value)? 1 : 0;
}

void Tins::IEEE802_11::power_mgmt(bool new_value) {
    this->_header.control.power_mgmt = (new_value)? 1 : 0;
}

void Tins::IEEE802_11::wep(bool new_value) {
    this->_header.control.wep = (new_value)? 1 : 0;
}

void Tins::IEEE802_11::order(bool new_value) {
    this->_header.control.order = (new_value)? 1 : 0;
}

void Tins::IEEE802_11::duration_id(uint16_t new_duration_id) {
    this->_header.duration_id = Utils::net_to_host_s(new_duration_id);
}

void Tins::IEEE802_11::addr1(const uint8_t* new_addr1) {
    memcpy(this->_header.addr1, new_addr1, 6);
}

void Tins::IEEE802_11::addr2(const uint8_t* new_addr2) {
    memcpy(this->_header.addr2, new_addr2, 6);
}

void Tins::IEEE802_11::addr3(const uint8_t* new_addr3) {
    memcpy(this->_header.addr3, new_addr3, 6);
}

void Tins::IEEE802_11::frag_num(uint8_t new_frag_num) {
    this->_header.seq_control.frag_number = new_frag_num;
}

void Tins::IEEE802_11::seq_num(uint16_t new_seq_num) {
    this->_header.seq_control.seq_number = Utils::net_to_host_s(new_seq_num);
}

void Tins::IEEE802_11::addr4(const uint8_t* new_addr4) {
    memcpy(this->_addr4, new_addr4, 6);
}

void Tins::IEEE802_11::iface(uint32_t new_iface_index) {
    this->_iface_index = new_iface_index;
}

void Tins::IEEE802_11::iface(const std::string& new_iface) throw (std::runtime_error) {
    if (!Tins::Utils::interface_id(new_iface, this->_iface_index)) {
        throw std::runtime_error("Invalid interface name!");
    }
}

uint32_t Tins::IEEE802_11::header_size() const {
    uint32_t sz = sizeof(ieee80211_header) + _options_size;
    if (this->to_ds() && this->from_ds())
        sz += 6;
    return sz;
}

bool Tins::IEEE802_11::send(PacketSender* sender) {
    struct sockaddr_ll addr;

    memset(&addr, 0, sizeof(struct sockaddr_ll));

    addr.sll_family = Utils::net_to_host_s(PF_PACKET);
    addr.sll_protocol = Utils::net_to_host_s(ETH_P_ALL);
    addr.sll_halen = 6;
    addr.sll_ifindex = this->_iface_index;
    memcpy(&(addr.sll_addr), this->_header.addr1, 6);

    return sender->send_l2(this, (struct sockaddr*)&addr, (uint32_t)sizeof(addr));
}

void Tins::IEEE802_11::write_serialization(uint8_t *buffer, uint32_t total_sz, const PDU *parent) {
    uint32_t my_sz = header_size();
    assert(total_sz >= my_sz);
    memcpy(buffer, &this->_header, sizeof(ieee80211_header));
    buffer += sizeof(ieee80211_header);
    if (this->to_ds() && this->from_ds()) {
        memcpy(buffer, this->_addr4, 6);
        buffer += 6;
        total_sz -= 6;
    }

    uint32_t child_len = write_fixed_parameters(buffer, total_sz - sizeof(ieee80211_header) - _options_size);
    buffer += child_len;
    assert(total_sz > child_len + _options_size);
    for(std::list<IEEE802_11_Option>::const_iterator it = _options.begin(); it != _options.end(); ++it) {
        *(buffer++) = it->option;
        *(buffer++) = it->length;
        std::memcpy(buffer, it->value, it->length);
        buffer += it->length;
    }
}

Tins::PDU *Tins::IEEE802_11::from_bytes(const uint8_t *buffer, uint32_t total_sz) {
    // We only need the control field, the length of the PDU will depend on the flags set.
    if(total_sz < sizeof(ieee80211_header::control))
        throw std::runtime_error("Not enough size for a IEEE 802.11 header in the buffer.");
    const ieee80211_header *hdr = (const ieee80211_header*)buffer;
    PDU *ret = 0;
    if(hdr->control.type == 0 && hdr->control.subtype == 8) {
        if(total_sz < sizeof(_header))
            throw std::runtime_error("Not enough size for an IEEE 802.11 header in the buffer.");
        ret = new IEEE802_11_Beacon(buffer, total_sz);
    }
    else
        ret = new IEEE802_11(buffer, total_sz);
    return ret;
}


/*
 * ManagementFrame
 */

Tins::ManagementFrame::ManagementFrame(const uint8_t *buffer, uint32_t total_sz) : IEEE802_11(buffer, total_sz) {

}

Tins::ManagementFrame::ManagementFrame(const uint8_t *dst_hw_addr, const uint8_t *src_hw_addr) : IEEE802_11(dst_hw_addr, src_hw_addr) {
    this->type(IEEE802_11::MANAGEMENT);
}

Tins::ManagementFrame::ManagementFrame(const std::string &iface,
                                       const uint8_t *dst_hw_addr,
                                       const uint8_t *src_hw_addr) throw (std::runtime_error) : IEEE802_11(iface, dst_hw_addr, src_hw_addr) {
    this->type(IEEE802_11::MANAGEMENT);
}

void Tins::ManagementFrame::ssid(const std::string &new_ssid) {
    add_tagged_option(IEEE802_11::SSID, new_ssid.size(), (const uint8_t*)new_ssid.c_str());
}

void Tins::ManagementFrame::rates(const std::list<float> &new_rates) {
    uint8_t *buffer = new uint8_t[new_rates.size()], *ptr = buffer;
    for(std::list<float>::const_iterator it = new_rates.begin(); it != new_rates.end(); ++it) {
        uint8_t result = 0x80, left = *it / 0.5;
        if(*it - left > 0)
            left++;
        *(ptr++) = (result | left);
    }
    add_tagged_option(SUPPORTED_RATES, new_rates.size(), buffer);
    delete[] buffer;
}

void Tins::ManagementFrame::channel(uint8_t new_channel) {
    add_tagged_option(DS_SET, 1, &new_channel);
}

void Tins::ManagementFrame::rsn_information(const RSNInformation& info) {
    uint32_t size;
    uint8_t *buffer = info.serialize(size);
    add_tagged_option(RSN, size, buffer);
    delete[] buffer;
}

void Tins::ManagementFrame::supported_rates(const std::list<float> &new_rates) {
    uint8_t *buffer = new uint8_t[new_rates.size()], *ptr = buffer;
    for(std::list<float>::const_iterator it = new_rates.begin(); it != new_rates.end(); ++it) {
        uint8_t result = 0x80, left = *it / 0.5;
        if(*it - left > 0)
            left++;
        *(ptr++) = (result | left);
    }
    add_tagged_option(SUPPORTED_RATES, new_rates.size(), buffer);
    delete[] buffer;
}

void Tins::ManagementFrame::extended_supported_rates(const std::list<float> &new_rates) {
    uint8_t *buffer = new uint8_t[new_rates.size()], *ptr = buffer;
    for(std::list<float>::const_iterator it = new_rates.begin(); it != new_rates.end(); ++it) {
        uint8_t result = 0x80, left = *it / 0.5;
        if(*it - left > 0)
            left++;
        *(ptr++) = (result | left);
    }
    add_tagged_option(EXT_SUPPORTED_RATES, new_rates.size(), buffer);
    delete[] buffer;
}

void Tins::ManagementFrame::qos_capabilities(uint8_t new_qos_capabilities) {
    add_tagged_option(QOS_CAPABILITY, 1, &new_qos_capabilities);
}

void Tins::ManagementFrame::power_capabilities(uint8_t min_power, uint8_t max_power) {
    uint8_t buffer[2];
    buffer[0] = min_power;
    buffer[1] = max_power;
    add_tagged_option(POWER_CAPABILITY, 2, buffer);
}

void Tins::ManagementFrame::supported_channels(const std::list<std::pair<uint8_t, uint8_t> > &new_channels) {
    uint8_t* buffer = new uint8_t[new_channels.size() * 2];
    uint8_t* ptr = buffer;
    for(std::list<pair<uint8_t, uint8_t> >::const_iterator it = new_channels.begin(); it != new_channels.end(); ++it) {
        *(ptr++) = it->first;
        *(ptr++) = it->second;
    }
    add_tagged_option(SUPPORTED_CHANNELS, new_channels.size() * 2, buffer);
    delete[] buffer;
}

void Tins::ManagementFrame::edca_parameter_set(uint32_t ac_be, uint32_t ac_bk, uint32_t ac_vi, uint32_t ac_vo) {
    uint8_t* buffer = new uint8_t[18];
    buffer[0] = 0;
    uint32_t* ptr = (uint32_t*)(buffer + 1);
    *(ptr++) = ac_be;
    *(ptr++) = ac_bk;
    *(ptr++) = ac_vi;
    *(ptr++) = ac_vo;
    add_tagged_option(EDCA, 18, buffer);
    delete[] buffer;
}

/*
 * Beacon
 */

Tins::IEEE802_11_Beacon::IEEE802_11_Beacon(const uint8_t* dst_hw_addr, const uint8_t* src_hw_addr) : ManagementFrame() {
    this->subtype(IEEE802_11::BEACON);
    memset(&_body, 0, sizeof(_body));
}

Tins::IEEE802_11_Beacon::IEEE802_11_Beacon(const std::string& iface,
                                           const uint8_t* dst_hw_addr,
                                           const uint8_t* src_hw_addr) throw (std::runtime_error) : ManagementFrame(iface, dst_hw_addr, src_hw_addr){
    this->subtype(IEEE802_11::BEACON);
    memset(&_body, 0, sizeof(_body));
}

Tins::IEEE802_11_Beacon::IEEE802_11_Beacon(const uint8_t *buffer, uint32_t total_sz) : ManagementFrame(buffer, total_sz) {
    buffer += sizeof(ieee80211_header);
    total_sz -= sizeof(ieee80211_header);
    if(total_sz < sizeof(_body))
        throw std::runtime_error("Not enough size for a IEEE 802.11 beacon header in the buffer.");
    memcpy(&_body, buffer, sizeof(_body));
    buffer += sizeof(_body);
    total_sz -= sizeof(_body);
    parse_tagged_parameters(buffer, total_sz);
}

void Tins::IEEE802_11_Beacon::timestamp(uint64_t new_timestamp) {
    this->_body.timestamp = new_timestamp;
}

void Tins::IEEE802_11_Beacon::interval(uint16_t new_interval) {
    this->_body.interval = Utils::net_to_host_s(new_interval);
}

void Tins::IEEE802_11_Beacon::essid(const std::string &new_essid) {
    ManagementFrame::ssid(new_essid);
}

void Tins::IEEE802_11_Beacon::rates(const std::list<float> &new_rates) {
    ManagementFrame::rates(new_rates);
}

void Tins::IEEE802_11_Beacon::channel(uint8_t new_channel) {
    ManagementFrame::channel(new_channel);
}

void Tins::IEEE802_11_Beacon::rsn_information(const RSNInformation& info) {
    ManagementFrame::rsn_information(info);
}

string Tins::IEEE802_11_Beacon::essid() const {
    const IEEE802_11::IEEE802_11_Option *option = lookup_option(SSID);
    return (option) ? string((const char*)option->value, option->length) : 0;
}

bool Tins::IEEE802_11_Beacon::rsn_information(RSNInformation *rsn) {
    const IEEE802_11::IEEE802_11_Option *option = lookup_option(RSN);
    if(!option || option->length < (sizeof(uint16_t) << 1) + sizeof(uint32_t))
        return false;
    const uint8_t *buffer = option->value;
    uint32_t bytes_left = option->length;
    rsn->version(*(uint16_t*)buffer);
    buffer += sizeof(uint16_t);
    rsn->group_suite((RSNInformation::CypherSuites)*(uint32_t*)buffer);
    buffer += sizeof(uint32_t);

    bytes_left -= (sizeof(uint16_t) << 1) + sizeof(uint32_t);
    if(bytes_left < sizeof(uint16_t))
        return false;
    uint16_t count = *(uint16_t*)buffer;
    buffer += sizeof(uint16_t);
    if(count * sizeof(uint32_t) > bytes_left)
        return false;
    bytes_left -= count * sizeof(uint32_t);
    while(count--) {
        rsn->add_pairwise_cypher((RSNInformation::CypherSuites)*(uint32_t*)buffer);
        buffer += sizeof(uint32_t);
    }
    if(bytes_left < sizeof(uint16_t))
        return false;
    count = *(uint16_t*)buffer;
    buffer += sizeof(uint16_t);
    bytes_left -= sizeof(uint16_t);
    if(count * sizeof(uint32_t) > bytes_left)
        return false;
    bytes_left -= count * sizeof(uint32_t);
    while(count--) {
        rsn->add_akm_cypher((RSNInformation::AKMSuites)*(uint32_t*)buffer);
        buffer += sizeof(uint32_t);
    }
    if(bytes_left < sizeof(uint16_t))
        return false;
    rsn->capabilities(*(uint16_t*)buffer);
    return true;
}

uint32_t Tins::IEEE802_11_Beacon::header_size() const {
    return IEEE802_11::header_size() + sizeof(BeaconBody);
}

uint32_t Tins::IEEE802_11_Beacon::write_fixed_parameters(uint8_t *buffer, uint32_t total_sz) {
    uint32_t sz = sizeof(BeaconBody);
    assert(sz <= total_sz);
    memcpy(buffer, &this->_body, sz);
    return sz;
}

Tins::IEEE802_11_Disassoc::IEEE802_11_Disassoc() : ManagementFrame() {
    this->subtype(IEEE802_11::DISASSOC);
    memset(&_body, 0, sizeof(_body));
}

Tins::IEEE802_11_Disassoc::IEEE802_11_Disassoc(const std::string& iface,
                                           const uint8_t* dst_hw_addr,
                                           const uint8_t* src_hw_addr) throw (std::runtime_error) : ManagementFrame(iface, dst_hw_addr, src_hw_addr){
    this->subtype(IEEE802_11::DISASSOC);
    memset(&_body, 0, sizeof(_body));
}

void Tins::IEEE802_11_Disassoc::reason_code(uint16_t new_reason_code) {
    this->_body.reason_code = new_reason_code;
}

uint32_t Tins::IEEE802_11_Disassoc::header_size() const {
    return IEEE802_11::header_size() + sizeof(DisassocBody);
}

uint32_t Tins::IEEE802_11_Disassoc::write_fixed_parameters(uint8_t *buffer, uint32_t total_sz) {
    uint32_t sz = sizeof(DisassocBody);
    assert(sz <= total_sz);
    memcpy(buffer, &this->_body, sz);
    return sz;
}

/*
 * RSNInformation class
 */
Tins::RSNInformation::RSNInformation() : _version(1), _capabilities(0) {

}

void Tins::RSNInformation::add_pairwise_cypher(CypherSuites cypher) {
    _pairwise_cyphers.push_back(cypher);
}

void Tins::RSNInformation::add_akm_cypher(AKMSuites akm) {
    _akm_cyphers.push_back(akm);
}

void Tins::RSNInformation::group_suite(CypherSuites group) {
    _group_suite = group;
}

void Tins::RSNInformation::version(uint16_t ver) {
    _version = ver;
}

void Tins::RSNInformation::capabilities(uint16_t cap) {
    _capabilities = cap;
}

uint8_t *Tins::RSNInformation::serialize(uint32_t &size) const {
    size = sizeof(_version) + sizeof(_capabilities) + sizeof(uint32_t);
    size += (sizeof(uint16_t) << 1); // 2 lists count.
    size += sizeof(uint32_t) * (_akm_cyphers.size() + _pairwise_cyphers.size());

    uint8_t *buffer = new uint8_t[size], *ptr = buffer;
    *(uint16_t*)ptr = _version;
    ptr += sizeof(_version);
    *(uint32_t*)ptr = _group_suite;
    ptr += sizeof(uint32_t);
    *(uint16_t*)ptr = _pairwise_cyphers.size();
    ptr += sizeof(uint16_t);
    for(std::list<CypherSuites>::const_iterator it = _pairwise_cyphers.begin(); it != _pairwise_cyphers.end(); ++it) {
        *(uint32_t*)ptr = *it;
        ptr += sizeof(uint32_t);
    }
    *(uint16_t*)ptr = _akm_cyphers.size();
    ptr += sizeof(uint16_t);
    for(std::list<AKMSuites>::const_iterator it = _akm_cyphers.begin(); it != _akm_cyphers.end(); ++it) {
        *(uint32_t*)ptr = *it;
        ptr += sizeof(uint32_t);
    }
    *(uint16_t*)ptr = _capabilities;
    return buffer;
}

Tins::RSNInformation Tins::RSNInformation::wpa2_psk() {
    RSNInformation info;
    info.group_suite(RSNInformation::CCMP);
    info.add_pairwise_cypher(RSNInformation::CCMP);
    info.add_akm_cypher(RSNInformation::PSK);
    return info;
}

Tins::IEEE802_11_Assoc_Request::IEEE802_11_Assoc_Request() : ManagementFrame() {
    this->subtype(IEEE802_11::ASSOC_REQ);
    memset(&_body, 0, sizeof(_body));
}

Tins::IEEE802_11_Assoc_Request::IEEE802_11_Assoc_Request(const std::string& iface,
                                           const uint8_t* dst_hw_addr,
                                           const uint8_t* src_hw_addr) throw (std::runtime_error) : ManagementFrame(iface, dst_hw_addr, src_hw_addr){
    this->subtype(IEEE802_11::ASSOC_REQ);
    memset(&_body, 0, sizeof(_body));
}

Tins::IEEE802_11_Assoc_Request::IEEE802_11_Assoc_Request(const uint8_t *buffer, uint32_t total_sz) : ManagementFrame(buffer, total_sz) {
    buffer += sizeof(ieee80211_header);
    total_sz -= sizeof(ieee80211_header);
    if(total_sz < sizeof(_body))
        throw std::runtime_error("Not enough size for an IEEE 802.11 association header in the buffer.");
    memcpy(&_body, buffer, sizeof(_body));
    buffer += sizeof(_body);
    total_sz -= sizeof(_body);
    parse_tagged_parameters(buffer, total_sz);
}

void Tins::IEEE802_11_Assoc_Request::listen_interval(uint16_t new_listen_interval) {
    this->_body.listen_interval = new_listen_interval;
}

void Tins::IEEE802_11_Assoc_Request::ssid(const std::string &new_ssid) {
    ManagementFrame::ssid(new_ssid);
}

void Tins::IEEE802_11_Assoc_Request::supported_rates(const std::list<float> &new_rates) {
    ManagementFrame::supported_rates(new_rates);
}

void Tins::IEEE802_11_Assoc_Request::extended_supported_rates(const std::list<float> &new_rates) {
    ManagementFrame::extended_supported_rates(new_rates);
}

void Tins::IEEE802_11_Assoc_Request::power_capabilities(uint8_t min_power, uint8_t max_power) {
    ManagementFrame::power_capabilities(min_power, max_power);
}

void Tins::IEEE802_11_Assoc_Request::supported_channels(const std::list<pair<uint8_t, uint8_t> > &new_channels) {
    ManagementFrame::supported_channels(new_channels);
}

void Tins::IEEE802_11_Assoc_Request::rsn_information(const RSNInformation& info) {
    ManagementFrame::rsn_information(info);
}

void Tins::IEEE802_11_Assoc_Request::qos_capabilities(uint8_t new_qos_capabilities) {
    ManagementFrame::qos_capabilities(new_qos_capabilities);
}

uint32_t Tins::IEEE802_11_Assoc_Request::header_size() const {
    return IEEE802_11::header_size() + sizeof(AssocReqBody);
}

uint32_t Tins::IEEE802_11_Assoc_Request::write_fixed_parameters(uint8_t *buffer, uint32_t total_sz) {
    uint32_t sz = sizeof(AssocReqBody);
    assert(sz <= total_sz);
    memcpy(buffer, &this->_body, sz);
    return sz;
}

Tins::IEEE802_11_Assoc_Response::IEEE802_11_Assoc_Response() : ManagementFrame() {
    this->subtype(IEEE802_11::ASSOC_RESP);
    memset(&_body, 0, sizeof(_body));
}

Tins::IEEE802_11_Assoc_Response::IEEE802_11_Assoc_Response(const std::string& iface,
                                                           const uint8_t* dst_hw_addr,
                                                           const uint8_t* src_hw_addr) throw (std::runtime_error) : ManagementFrame(iface, dst_hw_addr, src_hw_addr) {
    this->subtype(IEEE802_11::ASSOC_RESP);
    memset(&_body, 0, sizeof(_body));
}

Tins::IEEE802_11_Assoc_Response::IEEE802_11_Assoc_Response(const uint8_t *buffer, uint32_t total_sz) : ManagementFrame(buffer, total_sz) {
    buffer += sizeof(ieee80211_header);
    total_sz -= sizeof(ieee80211_header);
    if(total_sz < sizeof(_body))
        throw std::runtime_error("Not enough size for an IEEE 802.11 association response header in the buffer.");
    memcpy(&_body, buffer, sizeof(_body));
    buffer += sizeof(_body);
    total_sz -= sizeof(_body);
    parse_tagged_parameters(buffer, total_sz);
}

void Tins::IEEE802_11_Assoc_Response::status_code(uint16_t new_status_code) {
    this->_body.status_code = new_status_code;
}

void Tins::IEEE802_11_Assoc_Response::aid(uint16_t new_aid) {
    this->_body.aid = new_aid;
}

void Tins::IEEE802_11_Assoc_Response::supported_rates(const std::list<float> &new_rates) {
    ManagementFrame::supported_rates(new_rates);
}

void Tins::IEEE802_11_Assoc_Response::extended_supported_rates(const std::list<float> &new_rates) {
    ManagementFrame::extended_supported_rates(new_rates);
}

void Tins::IEEE802_11_Assoc_Response::edca_parameter_set(uint32_t ac_be, uint32_t ac_bk, uint32_t ac_vi, uint32_t ac_vo) {
    ManagementFrame::edca_parameter_set(ac_be, ac_bk, ac_vi, ac_vo);
}

uint32_t Tins::IEEE802_11_Assoc_Response::header_size() const {
    return IEEE802_11::header_size() + sizeof(AssocRespBody);
}

uint32_t Tins::IEEE802_11_Assoc_Response::write_fixed_parameters(uint8_t *buffer, uint32_t total_sz) {
    uint32_t sz = sizeof(AssocRespBody);
    assert(sz <= total_sz);
    memcpy(buffer, &this->_body, sz);
    return sz;
}

Tins::IEEE802_11_QoS_Data::IEEE802_11_QoS_Data(const uint8_t* dst_hw_addr, const uint8_t* src_hw_addr, PDU* child) : IEEE802_11(dst_hw_addr, src_hw_addr, child) {
    this->type(IEEE802_11::DATA);
    this->subtype(IEEE802_11::QOS_DATA_DATA);
    this->_qos_control = 0;
}

Tins::IEEE802_11_QoS_Data::IEEE802_11_QoS_Data(const std::string& iface, const uint8_t* dst_hw_addr, const uint8_t* src_hw_addr, PDU* child) throw (std::runtime_error) : IEEE802_11(iface, dst_hw_addr, src_hw_addr, child) {
    this->type(IEEE802_11::DATA);
    this->subtype(IEEE802_11::QOS_DATA_DATA);
    this->_qos_control = 0;
}

Tins::IEEE802_11_QoS_Data::IEEE802_11_QoS_Data(uint32_t iface_index, const uint8_t* dst_hw_addr, const uint8_t* src_hw_addr, PDU* child) : IEEE802_11(iface_index, dst_hw_addr, src_hw_addr, child) {
    this->type(IEEE802_11::DATA);
    this->subtype(IEEE802_11::QOS_DATA_DATA);
    this->_qos_control = 0;
}

Tins::IEEE802_11_QoS_Data::IEEE802_11_QoS_Data(const uint8_t *buffer, uint32_t total_sz) : IEEE802_11(buffer, total_sz) {
    buffer += sizeof(ieee80211_header);
    total_sz -= sizeof(ieee80211_header);
    assert(total_sz >= sizeof(this->_qos_control));
    this->_qos_control = *(uint16_t*)buffer;

}

void Tins::IEEE802_11_QoS_Data::qos_control(uint16_t new_qos_control) {
    this->_qos_control = new_qos_control;
}

uint32_t Tins::IEEE802_11_QoS_Data::header_size() const {
    return IEEE802_11::header_size() + sizeof(this->_qos_control);
}

uint32_t Tins::IEEE802_11_QoS_Data::write_fixed_parameters(uint8_t *buffer, uint32_t total_sz) {
    uint32_t sz = sizeof(this->_qos_control);
    assert(sz <= total_sz);
    *(uint16_t*)buffer = this->_qos_control;
    return sz;
}