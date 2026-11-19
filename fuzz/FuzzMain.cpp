// Corpus replay driver for builds without libFuzzer (see FuzzMain.hpp).
// Usage: fuzz_<name> <file-or-dir>...   Exit status 0 when every input ran.
#include "FuzzMain.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

namespace fs = std::filesystem;

static bool runFile(const fs::path &p)
{
    std::ifstream in(p, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "can't read %s\n", p.string().c_str());
        return false;
    }
    const std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    LLVMFuzzerTestOneInput(reinterpret_cast<const uint8_t *>(bytes.data()), bytes.size());
    return true;
}

int main(int argc, char **argv)
{
    if (LLVMFuzzerInitialize(&argc, &argv) != 0) {
        return 1;
    }
    int ran = 0;
    for (int i = 1; i < argc; ++i) {
        const fs::path arg(argv[i]);
        if (fs::is_directory(arg)) {
            for (const auto &e : fs::directory_iterator(arg)) {
                if (e.is_regular_file()) {
                    if (!runFile(e.path())) {
                        return 1;
                    }
                    ++ran;
                }
            }
        } else if (!runFile(arg)) {
            return 1;
        } else {
            ++ran;
        }
    }
    std::printf("%s: %d inputs OK\n", argv[0], ran);
    return ran > 0 ? 0 : 1;
}
