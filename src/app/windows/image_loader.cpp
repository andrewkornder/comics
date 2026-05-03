#include "image_loader.h"

#include <cassert>

#include <stb/stb_image.h>
#include <stb/stb_image_resize2.h>
#include <vips/vips8>


// #define IMAGE_LOAD_USE_STB_IMAGE


bool load_image_from_buffer(std::stop_token& token, Image& info, const void* buf, std::size_t size) {
#ifndef IMAGE_LOAD_USE_STB_IMAGE
    vips::VImage img = vips::VImage::new_from_buffer(buf, size, NULL,
        vips::VImage::option()->set("access", VIPS_ACCESS_RANDOM)
    );
    if (token.stop_requested()) {
        return false;
    }

    std::size_t dummy_size;
    info.w = img.width();
    info.h = img.height();
    info.bands = img.bands();
    info.data = (unsigned char*) img.write_to_memory(&dummy_size);

    assert(dummy_size == (std::size_t) info.w * info.h * info.bands);

    if (token.stop_requested()) { 
        free(info.data);
        return false; 
    }

    if (info.up > 1) {
        auto upscaled = img.thumbnail_image(info.w * info.up);
        info.upscaled = (unsigned char*) upscaled.write_to_memory(&dummy_size);

        assert(dummy_size == (std::size_t) info.w * info.h * info.bands * info.up * info.up);
    } else {
        info.upscaled = nullptr;
    }
#else
    info.data = stbi_load_from_memory(
        (const unsigned char*) buf, (int) size, 
        &info.w, &info.h, NULL, 4
    );
    info.bands = 4;

    if (token.stop_requested()) {
        stbi_image_free(info.data);
        return false;
    }
    
    if (info.up > 1) {
        info.upscaled = (unsigned char*) stbir_resize(
            (void*) info.data, info.w, info.h, 0, NULL,
            info.up * info.w, info.up * info.h, 0, 
            STBIR_RGBA, STBIR_TYPE_UINT8,
            STBIR_EDGE_CLAMP, STBIR_FILTER_CUBICBSPLINE
        );
    } else {
        info.upscaled = nullptr;
    }
#endif
    return true;
}

void load_image_from_file(Image& info, std::string_view path) {
#ifdef IMAGE_LOAD_USE_STB_IMAGE
    info.data = stbi_load(path.data(), &info.w, &info.h, NULL, 4);
    info.bands = 4;
#else
    vips::VImage img = vips::VImage::new_from_file(path.data(), 
        vips::VImage::option()->set("access", VIPS_ACCESS_SEQUENTIAL)
    );

    std::size_t dummy_size;
    info.w = img.width();
    info.h = img.height();
    info.bands = img.bands();
    info.data = (unsigned char*) img.write_to_memory(&dummy_size);
#endif
}
