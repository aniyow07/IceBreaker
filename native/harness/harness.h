#pragma once
#include <string>
#include <vector>

namespace compat {
bool harness_gl_create(int w, int h);
void harness_gl_read(std::vector<unsigned char>& rgba);  // bottom-up RGBA rows
bool png_write(const std::string& path, int w, int h, const std::vector<unsigned char>& rgba_bottom_up);
}  // namespace compat
