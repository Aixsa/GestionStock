#pragma once

#include <string>

std::string hashPassword(const std::string& password);
bool verifyPassword(const std::string& password, const std::string& storedHash);
bool passwordMeetsMinimum(const std::string& password);
