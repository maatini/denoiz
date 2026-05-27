#include "nlm_core.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <iostream>
#include <cmath>

Image load_image(const std::string& path) {
    Image img;
    int w, h, c;
    unsigned char* pixels = stbi_load(path.c_str(), &w, &h, &c, 0);
    if (!pixels) {
        std::cerr << "Failed to load image: " << path << "\n";
        return img;
    }
    // Convert RGBA to RGB if loaded as 4-channel
    int stored_channels = (c == 4) ? 3 : c;
    img.width = w;
    img.height = h;
    img.channels = stored_channels;
    img.data.resize(w * h * stored_channels);
    if (c == 4) {
        for (size_t i = 0, j = 0; i < (size_t)(w * h * 4); i += 4, j += 3) {
            img.data[j]     = pixels[i]     / 255.0f;
            img.data[j + 1] = pixels[i + 1] / 255.0f;
            img.data[j + 2] = pixels[i + 2] / 255.0f;
        }
    } else {
        for (size_t i = 0; i < img.data.size(); ++i) {
            img.data[i] = pixels[i] / 255.0f;
        }
    }
    stbi_image_free(pixels);
    return img;
}

bool save_image(const std::string& path, const Image& img) {
    std::vector<unsigned char> pixels(img.data.size());
    for (size_t i = 0; i < img.data.size(); ++i) {
        float v = img.data[i];
        if (v < 0.0f) v = 0.0f;
        if (v > 1.0f) v = 1.0f;
        pixels[i] = static_cast<unsigned char>(v * 255.0f + 0.5f);
    }
    int result = 0;
    if (img.channels == 1) {
        result = stbi_write_png(path.c_str(), img.width, img.height, 1, pixels.data(), img.width);
    } else if (img.channels == 3) {
        result = stbi_write_png(path.c_str(), img.width, img.height, 3, pixels.data(), img.width * 3);
    } else {
        std::cerr << "Unsupported channel count: " << img.channels << "\n";
        return false;
    }
    return result != 0;
}

double psnr(const Image& a, const Image& b) {
    if (a.width != b.width || a.height != b.height || a.channels != b.channels) {
        return 0.0;
    }
    double mse = 0.0;
    size_t n = a.data.size();
    for (size_t i = 0; i < n; ++i) {
        double diff = a.data[i] - b.data[i];
        mse += diff * diff;
    }
    mse /= n;
    if (mse < 1e-10) return 100.0;
    return 10.0 * std::log10(1.0 / mse);
}
