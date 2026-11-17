// generado por tools/gani_cuerpo.py (no editar a mano): cara y manos de los clips propios, por cuadro (30 por segundo)
struct OwnKey { int frame; const char* face; float faceW; const char* handL; const char* handR; };
static const OwnKey kOwn_A[] = {
    { 0, "smil_talk_blink_soft", 0.80f, "relax", "relax" },
    { 20, "smil_talk_blink_soft", 0.80f, "relax", "hiraki_normal" },
    { 46, "smil_talk_blink_soft", 0.90f, "relax", "pointing" },
    { 74, "smil_talk_blink_soft", 0.80f, "relax", "hiraki_normal" },
    { 94, "smil_soft", 0.60f, "relax", "relax" },
    { 118, "pow_brwnit_wink_r", 1.00f, "relax", "relax" },
    { 140, "smil_mho", 1.00f, "open_full", "open_full" },
    { 164, "smil_soft", 0.80f, "open_full_half", "open_full_half" },
    { 190, "smil_shut_eyhc", 0.90f, "open_full", "open_full" },
    { 280, "smil_soft", 0.80f, "open_full_half", "open_full_half" },
    { 318, "smil_soft", 0.70f, "relax", "relax" },
    { 360, "smil_mho", 0.90f, "relax", "open_full" },
    { 384, "smil_smirk", 0.80f, "relax", "hiraki_normal" },
    { 418, "smil_soft", 0.60f, "relax", "relax" },
};
static const OwnKey kOwn_B[] = {
    { 0, "smil_soft", 0.60f, "relax", "relax" },
    { 70, "smil_talk_blink_soft", 0.80f, "hiraki_normal", "hiraki_normal" },
    { 122, "smil_soft", 0.70f, "relax", "relax" },
    { 142, "smil_mho", 1.00f, "open_full", "open_full" },
    { 164, "smil_soft", 0.80f, "open_full_half", "open_full_half" },
    { 190, "smil_shut_eyhc", 0.80f, "open_full", "open_full" },
    { 280, "smil_talk_blink_soft", 0.70f, "open_full_half", "open_full_half" },
    { 318, "smil_soft", 0.70f, "relax", "relax" },
    { 360, "smil_mho", 0.90f, "relax", "open_full" },
    { 384, "smil_soft", 0.90f, "relax", "hiraki_normal" },
    { 420, "smil_soft", 0.60f, "relax", "relax" },
};
struct OwnTrack { const char* file; const OwnKey* keys; int n; };
static const OwnTrack kOwnTracks[] = {
    { "zfx_saludo_abrazo_a.gani", kOwn_A, (int)(sizeof kOwn_A / sizeof kOwn_A[0]) },
    { "zfx_saludo_abrazo_b.gani", kOwn_B, (int)(sizeof kOwn_B / sizeof kOwn_B[0]) },
};
