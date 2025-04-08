#include "packet.hpp"

Packet make_packet(uint16_t in_type, uint16_t in_seqn, uint32_t in_total_size, uint16_t in_length, std::string in_payload)
{
    Packet pkt;
    pkt.type = in_type;
    pkt.seqn = in_seqn;
    pkt.total_size = in_total_size;
    pkt.length = std::min((uint16_t)in_payload.size(), (uint16_t)(PAYLOAD_SIZE - 1)); // segurança

    strncpy(pkt._payload, in_payload.c_str(), pkt.length);
    //pkt._payload[pkt.length] = '\0'; // null-terminate (opcional, para strings)
    return pkt;
}
