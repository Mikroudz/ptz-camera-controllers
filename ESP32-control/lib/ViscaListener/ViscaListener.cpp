#include "ViscaListener.h"
#include <WiFi.h>

#define VISCA_BROADCAST_PORT 52380
#define VISCA_PACKET_MAX_LENGHT 16

namespace visca {

static uint8_t _mac_addr[6];

ViscaListener::ViscaListener(movequeue::MoveQueue &p_queue)
    : parser(std::make_unique<ViscaParser>()), queue(p_queue) {

    parser->register_parser<ViscaMoveCommand>({0x01, 0x06, 0x01},
                                              3); // pan-tilt
    parser->register_parser<ViscaZoomCommand>({0x01, 0x04, 0x07}, 3); // zoom
    parser->register_parser<ViscaPresetCommand>({0x01, 0x04, 0x3f},
                                                3); // preset
    parser->register_parser<ViscaPresetCommand>({0x01, 0x7e, 0x01},
                                                3); // preset speed commands
    WiFi.macAddress(_mac_addr);                     // get mac

    esp_log_level_set(TAG, ESP_LOG_VERBOSE);
}

ViscaListener::~ViscaListener() {}

void ViscaListener::start() {
    const auto port = 52381;
    if (udp.listen(port) && udp.listen(VISCA_BROADCAST_PORT)) {
        ESP_LOGI(TAG, "Started UDP listener on port %d", port);
        udp.onPacket(
            [this](AsyncUDPPacket packet) { this->_visca_receiver(packet); });
    }
}

void ViscaListener::reply_visca_broadcast_inquiry(){
    udp.writeTo((uint8_t *)"testi", 5, IPAddress(192,168,1,255), VISCA_BROADCAST_PORT);

}

void ViscaListener::_visca_receiver(AsyncUDPPacket packet) {
    // ESP_LOGW(TAG, "%d bytes received!", packet.length());
    //  ESP_LOG_BUFFER_HEX_LEVEL(TAG, packet.data(), packet.length(),
    //                           ESP_LOG_ERROR);
    // broadcasts are inquiries, different handling
    if(packet.isBroadcast()){
        ESP_LOG_BUFFER_CHAR_LEVEL(TAG, packet.data(), packet.length(),
        ESP_LOG_ERROR);
        reply_visca_broadcast_inquiry();
        return;
    }
    uint8_t visca_data[VISCA_PACKET_MAX_LENGHT] = {0};
    uint16_t payload_len{0};
    const int visca_packet_len = (packet.length() - 9) <= VISCA_PACKET_MAX_LENGHT
                               ? packet.length() - 9
                               : VISCA_PACKET_MAX_LENGHT;

    for (int i = 0; i < visca_packet_len; i++) {
        visca_data[i] = packet.data()[i + 9];
    }
    payload_len = packet.data()[2] << 8 | packet.data()[3];
    // ESP_LOG_BUFFER_HEX_LEVEL(TAG, visca_data, payload_len, ESP_LOG_ERROR);
    std::shared_ptr<ViscaCommandBase> res_ptr{
        parser->get_parser(visca_data, 3)};
    if (res_ptr) {
        queue.create_msg<ViscaCommandBase>(res_ptr);
    } else {
        ESP_LOGW(TAG, "Unknown VISCA(tm) message received");
    }
}

template <typename T>
void ViscaParser::register_parser(std::initializer_list<uint8_t> code,
                                  size_t length) {
    uint8_t tmp[5] = {0};
    int i{0};
    for (const auto &val : code) {
        tmp[i] = val;
        i++;
        if (i >= 5) {
            break;
        }
    }
    parser_t st(tmp, length, std::make_unique<ConcreteFactory<T>>());
    if (register_cnt < 20) {
        parsers[register_cnt] = st;
        register_cnt++;
    } else {
        ESP_LOGW(TAG, "Cannot register more parsers!");
    }
}

std::shared_ptr<ViscaCommandBase> ViscaParser::get_parser(uint8_t *code,
                                                          size_t length) {
    parser_t tmp(code, length, nullptr);
    for (int i = 0; i < register_cnt; i++) {
        if (parsers[i] == tmp) {
            return parsers[i].parser->create(code, length);
        }
    }
    return nullptr;
}

} // namespace visca