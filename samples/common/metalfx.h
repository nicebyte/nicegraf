#include <nicegraf.h>
#include <utility>

struct mtlfx_scaler_info {
    uint32_t input_width;
    uint32_t input_height;
    uint32_t output_width;
    uint32_t output_height;
    ngf_image_format input_format;
    ngf_image_format output_format;
};

struct mtlfx_encode_info {
    ngf_image in_img;
    ngf_image out_img;
    ngf_cmd_buffer cmd_buf;
};

class mtlfx_scaler {
private:
    struct mtlfx_scaler_impl;
    mtlfx_scaler_impl* impl_ = nullptr;
    mtlfx_scaler() = default;
    void destroy();
    
public:
    mtlfx_scaler(mtlfx_scaler&& other) {
        *this = std::move(other);
    }
    ~mtlfx_scaler() { destroy(); }
    
    mtlfx_scaler& operator=(mtlfx_scaler&& other) {
        destroy();
        impl_ = other.impl_;
        other.impl_ = nullptr;
        return *this;
    }
    static mtlfx_scaler create(const mtlfx_scaler_info& info);
    
    void encode(const mtlfx_encode_info& info);
};
