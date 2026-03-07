// Himmel — 欣梅爾 (Hero — HintCache)
// Persists AOB pattern match results keyed by PE hash.

#include "Himmel.h"
#include "Solliter.h"
#include "Grimoire.h"
#include <Windows.h>
#include <unordered_map>

namespace Zolt::Himmel {

// In-memory cache: peHash -> patternId
static std::unordered_map<uint64_t, std::string> s_cache;
static bool s_loaded = false;

void Load() {
    // TODO: Build path: %LOCALAPPDATA%\ZoltDump\hints.dat
    // TODO: Read binary file: [count][peHash(8) + patternIdLen(4) + patternId(N)] ...
    // TODO: Populate s_cache
    Solliter::LogInit("Himmel::Load: TODO — not yet implemented");
    s_loaded = true;
}

void Save() {
    // TODO: Write s_cache to hints.dat in binary format
    // TODO: Create directory if it does not exist
    Solliter::LogInit("Himmel::Save: TODO — not yet implemented");
}

std::string GetHint(uint64_t peHash) {
    if (!s_loaded) Load();

    auto it = s_cache.find(peHash);
    if (it != s_cache.end()) {
        return it->second;
    }
    return "";
}

void SetHint(uint64_t peHash, const char* patternId) {
    s_cache[peHash] = patternId ? patternId : "";
    // TODO: Auto-save on update (or defer to explicit Save call)
}

} // namespace Zolt::Himmel
