// kk4s E-mote runtime: the cipher profiles shipped with kk4s.
//
// Add a profile here only after a real sample of that build passed structure,
// byte-exact round-trip and archive adler32 checks (see
// docs/universal_emote_runtime.md, "Adding a cipher profile").
#include "emote/cipher_profile.h"

namespace kk4s {
namespace emote {

std::unique_ptr<CipherProfile> MakeNekoParaVol1Profile();

void RegisterBuiltinProfiles(CipherRegistry& r) {
    r.Register(MakeNekoParaVol1Profile());
}

}  // namespace emote
}  // namespace kk4s
