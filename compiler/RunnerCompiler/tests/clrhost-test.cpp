// Host test: start the self-contained compiler runtime in this process and call its exports.
// usage: clrhost-test <compiler dir> [data.win code_name gml_file [object]]
#include "../native/ClrHost.hpp"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>
using namespace RunnerCompiler;
using PingFn = int (*)(unsigned char*, int);
using CompileFn = int (*)(const char*, const char*, const char*, unsigned char*, int);

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <compiler dir> [data.win code_name gml_file]\n", argv[0]); return 2; }
    auto t0 = std::chrono::steady_clock::now();
    ClrHost host;
#ifdef _WIN32
    std::wstring dir(argv[1], argv[1] + strlen(argv[1]));
    if (!host.Start(dir, L"LooseModCompiler")) { fprintf(stderr, "FAIL start: %s\n", host.Error().c_str()); return 1; }
#else
    if (!host.Start(argv[1], "LooseModCompiler")) { fprintf(stderr, "FAIL start: %s\n", host.Error().c_str()); return 1; }
#endif
    auto ping = reinterpret_cast<PingFn>(host.Function("LooseModCompiler.RuntimeExports, LooseModCompiler", "Ping"));
    if (!ping) { fprintf(stderr, "FAIL: %s\n", host.Error().c_str()); return 1; }
    std::vector<unsigned char> out(1 << 20);
    ping(out.data(), (int)out.size());
    auto ms = [&] { return (long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count(); };
    printf("PING after %ld ms: %s\n", ms(), out.data());
    if (argc >= 5) {
        // 5th argument "object": CompileObject (.gmlo text, what gmlrt::Runtime calls) instead of Compile.
        const bool object = argc >= 6 && strcmp(argv[5], "object") == 0;
        auto compile = reinterpret_cast<CompileFn>(host.Function("LooseModCompiler.RuntimeExports, LooseModCompiler", object ? "CompileObject" : "Compile"));
        if (!compile) { fprintf(stderr, "FAIL: %s\n", host.Error().c_str()); return 1; }
        std::ifstream f(argv[4], std::ios::binary); std::stringstream src; src << f.rdbuf();
        for (int round = 0; round < 2; ++round) {
            t0 = std::chrono::steady_clock::now();
            int n = compile(argv[2], argv[3], src.str().c_str(), out.data(), (int)out.size());
            printf("COMPILE round %d rc=%d after %ld ms\n%.*s\n", round, n, ms(), object ? (int)out.size() : 600, out.data());
            if (n < 0) return 1;
        }
    }
    puts("CLR host test passed");
    return 0;
}
