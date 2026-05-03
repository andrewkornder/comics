#pragma once

#include <thread>


struct Image {
    int w, h, bands, up;
    std::uint32_t id;
    unsigned char* data;
    unsigned char* upscaled;
};

bool load_image_from_buffer(std::stop_token& token, Image& info, const void* buf, std::size_t size);
void load_image_from_file(Image& info, std::string_view path);
