// kk4s E-mote runtime: cipher profile recovered from NekoPara Vol.1 (x86).
//
// This is the only file in the E-mote runtime that holds key material for a
// specific build. Evidence: docs/emote_psb_research.md,
// docs/emote_psb_validation.md, docs/emote_crossgame_compatibility.md.
//
//   emoteplayer.dll  VA 0x1004BF6D  push 0x100D4394 ("742877301"), parsed with
//                    strtol, stored as the fourth xorshift128 state word;
//                    seeds are u32 immediates at VA 0x1004BF99/A3/AD.
//   region           [u32 @0x08 (header length), u32 @0x18 (chunk offsets))
//   verified         157/157 PSB (emote.xp3, emotewin.xp3, emotewin_low.xp3):
//                    structure + byte-exact re-encryption + adler32.
//
// Whether any other game or build uses the same parameters is NOT known.
#include "emote/cipher_profile.h"

namespace kk4s {
namespace emote {

const char* const kNekoParaVol1ProfileId = "emote.xorshift128.nekopara-vol1";

std::unique_ptr<CipherProfile> MakeNekoParaVol1Profile() {
    ProfileInfo info;
    info.id = kNekoParaVol1ProfileId;
    info.description =
        "E-mote PSB inner cipher of NekoPara Vol.1 x86 (emoteplayer.dll / emotedriver.dll): "
        "xorshift128, seeds 123456789/362436069/521288629, fourth word = strtol(\"742877301\")";
    info.psb_versions = {2};
    info.evidence.binary_sha256 = {
        // plugin/emoteplayer.dll, 1,142,784 bytes
        "536361228be24fb47de73e7c72409060577e8b926fefa24e1ffd5854e6779541",
        // emotedriver.dll, 574,464 bytes
        "a3b693b605d67812e489b1fb62cd341012b5514fc9114a032fee4685e70b3a86",
    };
    info.evidence.source = "recovered from machine code; docs/emote_psb_research.md";
    info.evidence.real_files_verified = 157;

    Xorshift128Params p;
    p.seed_x = 123456789u;  // 0x075BCD15
    p.seed_y = 362436069u;  // 0x159A55E5
    p.seed_z = 521288629u;  // 0x1F123BB5
    p.key_w = 742877301u;   // 0x2C46C275, from the ASCII string in .rdata
    p.region = Xorshift128Params::RegionRule::HeaderLenToChunkOffsets;
    p.require_header_len_equals_names = true;  // true in 157/157 files; others refused
    return std::unique_ptr<CipherProfile>(new Xorshift128RegionProfile(std::move(info), p));
}

}  // namespace emote
}  // namespace kk4s
