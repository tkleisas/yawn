#pragma once
// IconLoader — Load PNG icon for window icon and GL texture using stb_image.

#include <string>
#include <vector>
#include <cstdint>
#include <glad/gl.h>

namespace yawn {
namespace util {

struct IconData {
    std::vector<uint8_t> pixels;  // RGBA
    int width  = 0;
    int height = 0;
};

// Load a PNG file into raw RGBA pixels. Implemented in IconLoader.cpp.
IconData loadIcon(const std::string& path);

// Create an OpenGL RGBA texture from icon data.
inline GLuint createGLTexture(const IconData& icon) {
    if (icon.pixels.empty()) return 0;
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, icon.width, icon.height,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, icon.pixels.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

} // namespace util
} // namespace yawn
