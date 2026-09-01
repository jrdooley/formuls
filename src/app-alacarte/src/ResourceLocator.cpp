/*
 * ResourceLocator.cpp -- see ResourceLocator.h for an overview.
 */

#include "ResourceLocator.h"

#include <cstdlib>
#include <mach-o/dyld.h>
#include <vector>

namespace formuls
{

namespace fs = std::filesystem;

static fs::path executablePath()
{
    uint32_t size = 0;
    _NSGetExecutablePath (nullptr, &size);          // ask for the needed size

    std::vector<char> buffer (size + 1, 0);

    if (_NSGetExecutablePath (buffer.data(), &size) != 0)
        return {};

    std::error_code ec;
    auto resolved = fs::canonical (fs::path (buffer.data()), ec);
    return ec ? fs::path (buffer.data()) : resolved;
}

static bool containsResources (const fs::path& dir)
{
    std::error_code ec;
    return fs::is_regular_file (dir / "pd" / "_main.pd", ec);
}

fs::path findResourceRoot()
{
    // 1. explicit override
    if (const char* env = std::getenv ("FORMULS_RESOURCE_ROOT"))
        if (containsResources (env))
            return env;

    const auto exe = executablePath();

    if (exe.empty())
        return {};

    // 2. app bundle: <...>.app/Contents/MacOS/<exe> -> ../Resources
    if (auto bundleResources = exe.parent_path().parent_path() / "Resources";
        containsResources (bundleResources))
        return bundleResources;

    // 3. development fallback: walk up from the executable
    auto dir = exe.parent_path();

    for (int i = 0; i < 10 && ! dir.empty(); ++i)
    {
        if (containsResources (dir))
            return dir;

        if (containsResources (dir / "src"))
            return dir / "src";

        auto parent = dir.parent_path();

        if (parent == dir)   // reached the filesystem root
            break;

        dir = parent;
    }

    return {};
}

} // namespace formuls
