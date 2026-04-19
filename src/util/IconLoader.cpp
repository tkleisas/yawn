#define STB_IMAGE_IMPLEMENTATION
// PNG (for the app icon) + JPEG (for video thumbnails). No other formats.
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include "stb_image.h"
#undef STB_IMAGE_IMPLEMENTATION

#include "IconLoader.h"
#include <cstdio>

namespace yawn {
namespace util {

IconData loadIcon(const std::string& path) {
    IconData data;
    int w = 0, h = 0, channels = 0;
    unsigned char* img = stbi_load(path.c_str(), &w, &h, &channels, 4);
    if (!img) return data;

    data.width  = w;
    data.height = h;
    data.pixels.assign(img, img + w * h * 4);
    stbi_image_free(img);
    return data;
}

} // namespace util
} // namespace yawn
