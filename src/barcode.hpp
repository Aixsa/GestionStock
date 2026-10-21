#pragma once

#include <string>

std::string barcodeSvg(const std::string& value, int height = 48, bool showText = true);
std::string barcodeSvg(const std::string& value, double moduleWidth, int height, bool showText = true);
