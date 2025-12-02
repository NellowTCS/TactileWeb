#pragma once

#include <string>

// C-style function that returns allocated string (caller must free())
char* html2text_c(const char* html);

// C++ wrapper for compatibility
std::string html2text(const std::string& html);
