#ifndef VISCALISTENER_H
#define VISCALISTENER_H
#include <Arduino.h>

#include "AsyncUDP.h"
#include "MoveQueue.h"
#include "mylog.h"
#include <initializer_list>
#include <memory>

namespace visca {

class ViscaParser;
class ViscaPacketParser;
class MoveParser;

static const char *TAG = "visca";

class ViscaListener {
  private:
    AsyncUDP udp;
    std::unique_ptr<ViscaParser> parser;
    movequeue::MoveQueue &queue;


  protected:
    void _visca_receiver(AsyncUDPPacket packet);
    static void _broadcast_alive_task(TimerHandle_t xTimer);
  public:
    ViscaListener(movequeue::MoveQueue &p_queue);
    ~ViscaListener();
    void start();
    void reply_visca_broadcast_inquiry();
};

enum class ActionType : uint8_t {
    MOVE_UP = 0,
    MOVE_DOWN,
    MOVE_LEFT,
    MOVE_RIGHT,
    MOVE_UPLEFT,
    MOVE_UPRIGHT,
    MOVE_DOWNLEFT,
    MOVE_DOWNRIGHT,
    MOVE_STOP,
    // Note no difference between standard/variable speed; just default speed in
    // standard case so the command is basically same
    ZOOM_IN,
    ZOOM_OUT,
    ZOOM_STOP,
    SET_SPEED,
    RECALL_PRESET,
    SET_PRESET,
    PRESET_SPEED,
    UNKNOWN
};

class ViscaCommandBase {
  protected:
    pbuf *_pb;
    ActionType _type;

  public:
    virtual uint8_t pan_speed() const { return 0; }
    virtual uint8_t tilt_speed() const { return 0; }
    virtual uint8_t zoom_speed() const { return 0; }
    virtual uint8_t get_preset() const { return 0; }

    ActionType get_action() const { return _type; }
};

class AbstractFactory {
  public:
    virtual std::shared_ptr<ViscaCommandBase> create(uint8_t *pb,
                                                     size_t len) = 0;
    virtual ~AbstractFactory() = default;
};

template <typename T> class ConcreteFactory : public AbstractFactory {
  public:
    std::shared_ptr<ViscaCommandBase> create(uint8_t *pb, size_t len) override {
        return std::make_shared<T>(pb, len);
    }
};

template <typename T>
inline bool arrcmpr(const T *a, const T *b, const int len) {
    for (int i = 0; i < len; i++) {
        if (a[i] != b[i]) {
            return false;
        }
    }
    return true;
}

class ViscaMoveCommand : public ViscaCommandBase {
  public:
    ViscaMoveCommand(uint8_t *pb, size_t length) {
        static constexpr uint8_t types[][2] = {
            {0x03, 0x01}, {0x03, 0x02}, {0x01, 0x03},
            {0x02, 0x03}, {0x01, 0x01}, {0x02, 0x01},
            {0x01, 0x02}, {0x02, 0x02}, {0x03, 0x03}};

        for (int i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
            if (arrcmpr(types[i], &pb[5], 2)) {
                // ei ole kovin ylläpidettävä systeemi tämä tässä nyt :D
                // käytä mappia tai jotai
                _type = (ActionType)i;
            }
        }
        _pan_speed = pb[3];
        _tilt_speed = pb[4];
        ESP_LOGE(TAG, "Got %d, with pan: %d, tilt: %d ", _type, _pan_speed,
                 _tilt_speed);
    }
    uint8_t pan_speed() const override { return _pan_speed; }
    uint8_t tilt_speed() const override { return _tilt_speed; }

  private:
    uint8_t _pan_speed{0};
    uint8_t _tilt_speed{0};
};

class ViscaZoomCommand : public ViscaCommandBase {
  public:
    ViscaZoomCommand(uint8_t *pb, size_t length) {
        uint8_t cnt_byte = pb[3];
        if (cnt_byte == 0x02) {
            _type = ActionType::ZOOM_IN;
        } else if (cnt_byte == 0x03) {
            _type = ActionType::ZOOM_OUT;
        } else if ((cnt_byte & 0x30) == 0x20) {
            _type = ActionType::ZOOM_IN;
            _zoom_speed = cnt_byte ^ (1 << 5);
        } else if ((cnt_byte & 0x30) == 0x30) {
            _type = ActionType::ZOOM_OUT;
            _zoom_speed = cnt_byte ^ 0x30;
        } else {
            _type = ActionType::ZOOM_STOP;
        }
        //ESP_LOG_BUFFER_HEX_LEVEL(TAG, pb, 4, ESP_LOG_ERROR);
        ESP_LOGE(TAG, "Got zoom %d, with speed: %d", _type, _zoom_speed);
    }
    uint8_t zoom_speed() const override { return _zoom_speed; }

  private:
    uint8_t _zoom_speed{0};
};

class ViscaPresetCommand : public ViscaCommandBase {
  public:
    ViscaPresetCommand(uint8_t *pb, size_t length) {
        // speed and preset creation are separate events
        uint8_t cnt_byte = pb[3];
        if (cnt_byte == 0x02) {
            _type = ActionType::RECALL_PRESET;
            _preset = pb[4];
        } else if (cnt_byte == 0x01) {
            _type = ActionType::SET_PRESET;
            _preset = pb[4];
        } else if (cnt_byte == 0x0b) {
            _type = ActionType::PRESET_SPEED;
            _preset = pb[4];
            _speed = pb[5];

        }
        ESP_LOGE(TAG, "Got %d, with preset: %d, speed %d", _type, _preset, _speed);
    }
    uint8_t get_preset() const override { return _preset; }
    uint8_t pan_speed() const override { return _speed; }
    uint8_t tilt_speed() const override { return _speed; }

  private:
    uint8_t _preset;
    uint8_t _speed{1};
};

struct parser_t {
  public:
    std::unique_ptr<AbstractFactory> parser;
    uint8_t code[5];
    uint8_t length{0};
    parser_t(const uint8_t *p_buf, size_t p_length,
             std::unique_ptr<AbstractFactory> p_parser)
        : length(p_length), parser(std::move(p_parser)) {
        if (p_buf != nullptr && p_length > 0 && p_length <= 5) {
            for (int i = 0; i < 5; i++) {
                code[i] = 0;
            }
            memcpy(code, p_buf, p_length);
        } else {
            ESP_LOGW(TAG, "Invalid visca code registered");
        }
    }
    parser_t() : parser(nullptr), length(0) { memset(code, 0, sizeof(code)); }
    parser_t &operator=(parser_t &other) {
        if (this != &other && other.parser != nullptr) {
            for (int i = 0; i < 5; i++) {
                code[i] = 0;
            }
            length = other.length;
            memcpy(code, other.code, length);
            parser = std::move(other.parser);
        }
        return *this;
    }
    parser_t(const parser_t &) = delete;
    parser_t &operator=(const parser_t &) = delete;
    ~parser_t() = default;

    bool operator==(const parser_t &cmp) {
        return length == cmp.length &&
               strcmp((const char *)code, (const char *)cmp.code) == 0;
    }
};

class ViscaParser {
  public:
    ViscaParser() = default;
    virtual ~ViscaParser() = default;
    template <typename T>
    void register_parser(std::initializer_list<uint8_t> code, size_t length);
    std::shared_ptr<ViscaCommandBase> get_parser(uint8_t *code, size_t length);

  protected:
  private:
    parser_t parsers[20];
    uint8_t register_cnt{0};
};

class ViscaPacketParser {
  protected:
    pbuf *_pb;
    ActionType _type;

  public:
    ViscaPacketParser() {} // ViscaPacket &packet);
    ViscaPacketParser(uint8_t *, size_t length) {}
    virtual ~ViscaPacketParser() = default;
    virtual bool parse(uint8_t *pb) = 0;
};

class MoveParser : public ViscaPacketParser {
    bool parse(uint8_t *pb) override {
        ESP_LOGE(TAG, "Move parser called");
        return true;
    }
};

} // namespace visca

#endif