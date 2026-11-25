#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include "draw_vert.inc"
#include "draw_frag.inc"

static bool writeWords(const std::filesystem::path& path, std::span<const uint32_t> words) {
    std::ofstream out(path, std::ios::binary);
    for (auto word : words)
        for (unsigned shift = 0; shift < 32; shift += 8) out.put(static_cast<char>(word >> shift));
    out.close();
    return out.good();
}
int main(int argc, char** argv) {
    if (argc == 3 && std::string_view(argv[1]) == "--skip") {
        std::cout << argv[2] << '\n';
        return 77;
    }
    if (argc != 2) return 1;
    std::filesystem::path dir(argv[1]);
    return writeWords(dir / "draw.vert.spv", draw_vert) && writeWords(dir / "draw.frag.spv", draw_frag) ? 0 : 1;
}
