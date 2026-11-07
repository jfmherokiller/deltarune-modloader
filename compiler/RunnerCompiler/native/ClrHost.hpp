#pragma once
// Minimal in-process .NET host (hostfxr) for a *self-contained* app directory: initializes the
// runtime the app ships (no global .NET install needed) and resolves [UnmanagedCallersOnly]
// methods in the app's own assemblies. Data-only: no YYToolkit dependency. Windows and Linux.
#include <string>

namespace RunnerCompiler {
#ifdef _WIN32
using PathString = std::wstring;
#else
using PathString = std::string;
#endif

class ClrHost {
public:
    // appDir: folder with hostfxr + coreclr + <app>.dll + <app>.runtimeconfig.json (dotnet publish
    // --self-contained output). appName: assembly name without extension.
    bool Start(const PathString& appDir, const PathString& appName);
    // typeName: "Namespace.Type, Assembly"; method must be [UnmanagedCallersOnly].
    void* Function(const char* typeName, const char* method);
    const std::string& Error() const { return error_; }
private:
    void* getFunctionPointer_ = nullptr;
    std::string error_;
};
}
