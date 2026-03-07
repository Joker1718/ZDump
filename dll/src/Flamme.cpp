// Flamme — 芙拉梅 (Great Mage Master — StringName Resolver)
// Resolves Godot StringName pointers to readable UTF-8 strings.

#include "Flamme.h"
#include "Zoltraak.h"
#include "Solliter.h"
#include "Grimoire.h"

namespace Zolt::Flamme {

std::string Resolve(uint64_t stringNameAddr) {
    if (!stringNameAddr) return "";

    // TODO: Read _Data* from StringName (at DynOff::STRINGNAME_DATA_PTR)
    // TODO: From _Data, read the String field (at DynOff::SDATA_NAME)
    // TODO: String is a CowData<char32_t> — use Zoltraak::ReadGodotString
    // TODO: Handle null _Data pointer (empty StringName)

    Solliter::LogWalk("Flamme::Resolve: TODO — not yet implemented");
    return "";
}

} // namespace Zolt::Flamme
