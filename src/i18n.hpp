#pragma once

#include <string>

enum class Language { French = 0, English = 1, Spanish = 2, German = 3 };

Language currentLanguage();
void setLanguage(Language language);
bool isEnglish();

std::string tr(const std::string& text);
std::string tr(const char* text);
