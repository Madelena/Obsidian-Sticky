// =============================================================================
// TEXT
// =============================================================================
// String preparation for the ASCII-only bitmap fonts: UTF-8 folding and word
// wrapping. Used by screen.cpp before anything reaches the canvas.
#pragma once

#include <string>
#include <vector>

struct Font;

namespace text {

// UTF-8 FOLDER
// Returns an ASCII approximation of a UTF-8 string: curly quotes, dashes and
// ellipses become their ASCII forms, accented Latin letters lose their marks,
// and anything else becomes '?'.
std::string fold_to_ascii(const std::string &utf8);

// WORD WRAPPER
// Splits folded text into lines no wider than max_width pixels, breaking at
// spaces and newlines, or inside a word only when it cannot fit on a line.
std::vector<std::string> wrap(const Font &font, const std::string &ascii, int max_width);

}  // namespace text
