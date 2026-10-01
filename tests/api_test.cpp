// api_test.cpp
// DSAemuEngine の FmEngineApi を DLL 越しに叩く試験。
//   api_test <DSAemuEngine の DLL / .so のパス>
// 失敗した項目があれば終了コードが 0 以外になる。
//
// 新しいチップの振る舞いは、できるかぎり素のコアのチップと同じ書き込みを
// して出力を突き合わせる形で確かめる (OPL2EX と OPL2 / Y8950、OPLLEX と OPLL)。
// 突き合わせる差分そのものが効いていることも、同じ試験の中で確かめる。

#include "FmEngineApi.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <dlfcn.h>
#endif

namespace {

// ---------------------------------------------------------
//  DLL の読み込み
// ---------------------------------------------------------
struct Api {
    FmEngineHandle (FMENGINE_CALL *Create)(uint32_t);
    void        (FMENGINE_CALL *Destroy)(FmEngineHandle);
    uint32_t    (FMENGINE_CALL *Inquiry)(FmEngineHandle);
    const char* (FMENGINE_CALL *GetSupportedChip)(FmEngineHandle, uint32_t);
    FmResult    (FMENGINE_CALL *AddChip)(FmEngineHandle, const char*, uint32_t, uint32_t*);
    const char* (FMENGINE_CALL *GetChipName)(FmEngineHandle, uint32_t);
    uint32_t    (FMENGINE_CALL *GetNativeRate)(FmEngineHandle, uint32_t);
    uint32_t    (FMENGINE_CALL *GetSampleRate)(FmEngineHandle);
    FmResult    (FMENGINE_CALL *Write)(FmEngineHandle, uint32_t, uint8_t, uint8_t, uint32_t);
    FmResult    (FMENGINE_CALL *SetGain)(FmEngineHandle, uint32_t, float, float);
    FmResult    (FMENGINE_CALL *GetGain)(FmEngineHandle, uint32_t, float*, float*);
    FmResult    (FMENGINE_CALL *SetPartGain)(FmEngineHandle, uint32_t, FmPart, float, float);
    FmResult    (FMENGINE_CALL *GetPartGain)(FmEngineHandle, uint32_t, FmPart, float*, float*);
    FmResult    (FMENGINE_CALL *GetPartMask)(FmEngineHandle, uint32_t, uint32_t*);
    FmResult    (FMENGINE_CALL *SetMemory)(FmEngineHandle, uint32_t, FmMemoryType, const uint8_t*, uint32_t);
    uint32_t    (FMENGINE_CALL *GetMemorySize)(FmEngineHandle, uint32_t, FmMemoryType);
    FmResult    (FMENGINE_CALL *Generate)(FmEngineHandle, float*, float*, uint32_t);
};

void* openLibrary(const char* path) {
#ifdef _WIN32
    return (void*)LoadLibraryA(path);
#else
    return dlopen(path, RTLD_NOW);
#endif
}

void* findSymbol(void* lib, const char* name) {
#ifdef _WIN32
    return (void*)GetProcAddress((HMODULE)lib, name);
#else
    return dlsym(lib, name);
#endif
}

bool loadApi(const char* path, Api& api) {
    void* lib = openLibrary(path);
    if (!lib) {
        std::printf("cannot load %s\n", path);
        return false;
    }
    bool ok = true;
    auto bind = [&](auto& fn, const char* name) {
        void* p = findSymbol(lib, name);
        if (!p) { std::printf("missing export: %s\n", name); ok = false; }
        std::memcpy(&fn, &p, sizeof(p));
    };
    bind(api.Create,           "FmEngine_Create");
    bind(api.Destroy,          "FmEngine_Destroy");
    bind(api.Inquiry,          "FmEngine_Inquiry");
    bind(api.GetSupportedChip, "FmEngine_GetSupportedChip");
    bind(api.AddChip,          "FmEngine_AddChip");
    bind(api.GetChipName,      "FmEngine_GetChipName");
    bind(api.GetNativeRate,    "FmEngine_GetNativeRate");
    bind(api.GetSampleRate,    "FmEngine_GetSampleRate");
    bind(api.Write,            "FmEngine_Write");
    bind(api.SetGain,          "FmEngine_SetGain");
    bind(api.GetGain,          "FmEngine_GetGain");
    bind(api.SetPartGain,      "FmEngine_SetPartGain");
    bind(api.GetPartGain,      "FmEngine_GetPartGain");
    bind(api.GetPartMask,      "FmEngine_GetPartMask");
    bind(api.SetMemory,        "FmEngine_SetMemory");
    bind(api.GetMemorySize,    "FmEngine_GetMemorySize");
    bind(api.Generate,         "FmEngine_Generate");
    return ok;
}

Api A;
int g_fails = 0;

void check(const std::string& what, bool ok) {
    std::printf("%-72s %s\n", what.c_str(), ok ? "ok" : "FAIL");
    if (!ok) ++g_fails;
}

// ---------------------------------------------------------
//  1 チップだけのエンジンを作り、書き込んで鳴らす
// ---------------------------------------------------------
constexpr uint32_t kRate = 48000;
constexpr uint32_t kFrames = 4800;  // 0.1 秒

struct Reg { uint8_t reg, val; };
using Regs = std::vector<Reg>;

struct Out {
    std::vector<float> l, r;
};

struct Engine {
    FmEngineHandle h = nullptr;
    explicit Engine(uint32_t rate = kRate) : h(A.Create(rate)) {}
    ~Engine() { A.Destroy(h); }
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    uint32_t add(const char* name, uint32_t clock = 0) {
        uint32_t id = 0xFFFFFFFFu;
        if (A.AddChip(h, name, clock, &id) != FM_OK) {
            std::printf("AddChip(%s) failed\n", name);
            ++g_fails;
        }
        return id;
    }
    void write(uint32_t id, const Regs& regs) {
        for (const auto& r : regs) A.Write(h, id, r.reg, r.val, 0);
    }
    Out render(uint32_t frames = kFrames) {
        Out o;
        o.l.resize(frames);
        o.r.resize(frames);
        A.Generate(h, o.l.data(), o.r.data(), frames);
        return o;
    }
};

// setup はチップを足したあとの書き込みなどを行う
Out play(const char* chip, const std::function<void(Engine&, uint32_t)>& setup) {
    Engine e;
    uint32_t id = e.add(chip);
    setup(e, id);
    return e.render();
}

Out play(const char* chip, const Regs& regs) {
    return play(chip, [&](Engine& e, uint32_t id) { e.write(id, regs); });
}

Out play(const char* chip, uint32_t clock, const Regs& regs) {
    Engine e;
    uint32_t id = e.add(chip, clock);
    e.write(id, regs);
    return e.render();
}

double rms(const std::vector<float>& v) {
    double acc = 0.0;
    for (float s : v) acc += (double)s * s;
    return std::sqrt(acc / (double)v.size());
}

double maxDiff(const std::vector<float>& a, const std::vector<float>& b) {
    double m = 0.0;
    for (size_t i = 0; i < a.size() && i < b.size(); ++i)
        m = std::fmax(m, std::fabs((double)a[i] - (double)b[i]));
    return m;
}

bool same(const Out& a, const Out& b) {
    return a.l == b.l && a.r == b.r;
}

// 3/32768: 振幅で 1 LSB 未満の差を取りこぼさない程度の無音判定
constexpr double kSilent = 1e-4;

bool audible(const Out& o) { return rms(o.l) > kSilent && rms(o.r) > kSilent; }
bool silent(const Out& o) { return rms(o.l) <= kSilent && rms(o.r) <= kSilent; }

// ---------------------------------------------------------
//  レジスタ列
// ---------------------------------------------------------
// YM2413: ch0 にプリセット音色を出す
Regs opllNote(int ch, int voice) {
    return {
        { (uint8_t)(0x30 + ch), (uint8_t)(voice << 4) },  // 音色, 音量最大
        { (uint8_t)(0x10 + ch), 0x40 },
        { (uint8_t)(0x20 + ch), 0x15 },                   // key on, block 2
    };
}

// YM2413: リズムモードで 5 音すべてを鳴らす
Regs opllRhythm() {
    return {
        { 0x16, 0x20 }, { 0x26, 0x05 },
        { 0x17, 0x50 }, { 0x27, 0x05 },
        { 0x18, 0xC0 }, { 0x28, 0x01 },
        { 0x36, 0x00 }, { 0x37, 0x00 }, { 0x38, 0x00 },
        { 0x0E, 0x20 }, { 0x0E, 0x3F },
    };
}

// YM2413: ユーザー音色
Regs opllUserVoice() {
    return {
        { 0x00, 0x21 }, { 0x01, 0x21 }, { 0x02, 0x1E }, { 0x03, 0x0F },
        { 0x04, 0xF3 }, { 0x05, 0xE4 }, { 0x06, 0x24 }, { 0x07, 0x13 },
    };
}

Regs concat(std::initializer_list<Regs> parts) {
    Regs all;
    for (const auto& p : parts) all.insert(all.end(), p.begin(), p.end());
    return all;
}

// OPL 系: 全スロットに音色を入れ、ch0-5 とリズム 5 音を鳴らす。
// wse は 01h の b5 (波形選択の許可)、E0h 系には常にスロットごとに違う値を書く
Regs oplNotes(bool wse) {
    static const uint8_t slotOffsets[] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x08, 0x09, 0x0A,
        0x0B, 0x0C, 0x0D, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
    };
    Regs r = { { 0x01, (uint8_t)(wse ? 0x20 : 0x00) } };
    int i = 0;
    for (uint8_t off : slotOffsets) {
        r.push_back({ (uint8_t)(0x20 + off), 0x01 });
        r.push_back({ (uint8_t)(0x40 + off), 0x10 });
        r.push_back({ (uint8_t)(0x60 + off), 0xF4 });
        r.push_back({ (uint8_t)(0x80 + off), 0x24 });
        r.push_back({ (uint8_t)(0xE0 + off), (uint8_t)(i++ % 4) });
    }
    for (uint8_t ch = 0; ch < 9; ++ch) {
        r.push_back({ (uint8_t)(0xC0 + ch), 0x08 });
        r.push_back({ (uint8_t)(0xA0 + ch), (uint8_t)(0x41 + ch * 0x10) });
    }
    for (uint8_t ch = 0; ch < 6; ++ch)
        r.push_back({ (uint8_t)(0xB0 + ch), 0x31 });      // key on, block 4
    for (uint8_t ch = 6; ch < 9; ++ch)
        r.push_back({ (uint8_t)(0xB0 + ch), 0x11 });      // block 4 (リズムは BDh で鳴らす)
    r.push_back({ 0xBD, 0x3F });
    return r;
}

// ADPCM の試験データ。中身に意味は無く、鳴りさえすればよい
std::vector<uint8_t> adpcmData(size_t size) {
    std::vector<uint8_t> d(size);
    uint32_t x = 12345;
    for (auto& b : d) { x = x * 1103515245u + 12345u; b = (uint8_t)(x >> 16); }
    return d;
}

constexpr uint32_t kAdpcmBytes = 4096;

// reg08 を書いてから開始・終了アドレスを書き、再生を始める
Regs adpcmPlay(uint8_t reg08) {
    const uint32_t stop = kAdpcmBytes / 4 - 1;  // 256K RAM モードは 4 バイト単位
    return {
        { 0x08, reg08 },
        { 0x09, 0x00 }, { 0x0A, 0x00 },
        { 0x0B, (uint8_t)(stop & 0xFF) }, { 0x0C, (uint8_t)(stop >> 8) },
        { 0x10, 0x00 }, { 0x11, 0x80 },
        { 0x12, 0xFF },
        { 0x07, 0x80 },
    };
}

std::function<void(Engine&, uint32_t)> withAdpcm(const Regs& regs, bool load = true) {
    return [regs, load](Engine& e, uint32_t id) {
        if (load) {
            auto data = adpcmData(kAdpcmBytes);
            A.SetMemory(e.h, id, FM_MEM_ADPCM_B, data.data(), (uint32_t)data.size());
        }
        e.write(id, regs);
    };
}

// ---------------------------------------------------------
//  試験
// ---------------------------------------------------------
void testChipList() {
    static const char* expected[] = {
        "SSG", "OPLL", "OPLLP", "OPLLX", "VRC7", "Y8950", "OPL", "OPL2",
        "SCC", "SCCP", "DCSG", "OPL2EX", "OPLLEX", "SSGS",
    };
    Engine e;
    const uint32_t n = A.Inquiry(e.h);
    bool order = (n == sizeof(expected) / sizeof(expected[0]));
    for (uint32_t i = 0; order && i < n; ++i)
        order = std::strcmp(A.GetSupportedChip(e.h, i), expected[i]) == 0;
    check("chip list keeps the old order and appends OPL2EX, OPLLEX, SSGS", order);

    uint32_t opl2ex = e.add("OPL2EX");
    uint32_t opllex = e.add("OPLLEX");
    check("OPL2EX native rate is clock/72",
          A.GetNativeRate(e.h, opl2ex) == 3579545 / 72);
    check("OPLLEX native rate is clock/72",
          A.GetNativeRate(e.h, opllex) == 3579545 / 72);
    check("GetChipName returns the added names",
          std::strcmp(A.GetChipName(e.h, opl2ex), "OPL2EX") == 0 &&
          std::strcmp(A.GetChipName(e.h, opllex), "OPLLEX") == 0);
}

void testPartApi() {
    static const char* chips[] = {
        "SSG", "OPLL", "OPLLP", "OPLLX", "VRC7", "Y8950", "OPL", "OPL2",
        "SCC", "SCCP", "DCSG", "OPL2EX", "OPLLEX", "SSGS",
    };
    const uint32_t opllParts = (1u << FM_PART_OPLL_MELODY) | (1u << FM_PART_OPLL_RHYTHM);
    Engine e;
    bool masks = true;
    for (const char* name : chips) {
        uint32_t id = e.add(name);
        uint32_t mask = 0xDEADBEEF;
        bool isOpll = !std::strcmp(name, "OPLL") || !std::strcmp(name, "OPLLP") ||
                      !std::strcmp(name, "OPLLX") || !std::strcmp(name, "VRC7");
        if (A.GetPartMask(e.h, id, &mask) != FM_OK || mask != (isOpll ? opllParts : 0u)) {
            std::printf("  part mask of %s = 0x%08X\n", name, mask);
            masks = false;
        }
    }
    check("part masks: melody+rhythm for the OPLL family, none elsewhere", masks);

    uint32_t mask = 0;
    check("GetPartMask rejects an unknown chip_id",
          A.GetPartMask(e.h, 1000, &mask) == FM_ERR_INVALID_ARG);

    const uint32_t opll = 1, opl2 = 7;
    float l = -1, r = -1;
    check("melody gain defaults to 1.0",
          A.GetPartGain(e.h, opll, FM_PART_OPLL_MELODY, &l, &r) == FM_OK && l == 1.0f && r == 1.0f);
    check("rhythm gain defaults to 1.0",
          A.GetPartGain(e.h, opll, FM_PART_OPLL_RHYTHM, &l, &r) == FM_OK && l == 1.0f && r == 1.0f);
    check("SetPartGain rejects a part the chip does not have",
          A.SetPartGain(e.h, opll, FM_PART_OPN_SSG, 0.5f, 0.5f) == FM_ERR_INVALID_ARG &&
          A.SetPartGain(e.h, opl2, FM_PART_OPLL_MELODY, 0.5f, 0.5f) == FM_ERR_INVALID_ARG &&
          A.SetPartGain(e.h, opll, (FmPart)40, 0.5f, 0.5f) == FM_ERR_INVALID_ARG);
    check("SetPartGain then GetPartGain returns the values",
          A.SetPartGain(e.h, opll, FM_PART_OPLL_RHYTHM, 0.25f, 0.75f) == FM_OK &&
          A.GetPartGain(e.h, opll, FM_PART_OPLL_RHYTHM, &l, &r) == FM_OK && l == 0.25f && r == 0.75f);
}

void testOpllParts() {
    const Regs regs = concat({ opllNote(0, 1), opllRhythm() });
    auto withGains = [&](float ml, float mr, float rl, float rr) {
        return play("OPLL", [&](Engine& e, uint32_t id) {
            A.SetPartGain(e.h, id, FM_PART_OPLL_MELODY, ml, mr);
            A.SetPartGain(e.h, id, FM_PART_OPLL_RHYTHM, rl, rr);
            e.write(id, regs);
        });
    };
    Out both    = withGains(1, 1, 1, 1);
    Out melody  = withGains(1, 1, 0, 0);
    Out rhythm  = withGains(0, 0, 1, 1);
    // 片方の部位を 0 にした出力は、もう片方だけを鳴らしたチップの出力と一致する
    Out melodyChip = play("OPLL", opllNote(0, 1));
    Out rhythmChip = play("OPLL", opllRhythm());
    check("OPLL: melody gain alone gives exactly the melody",
          audible(melody) && same(melody, melodyChip));
    check("OPLL: rhythm gain alone gives exactly the rhythm",
          audible(rhythm) && same(rhythm, rhythmChip));
    double d = 0.0;
    for (size_t i = 0; i < both.l.size(); ++i)
        d = std::fmax(d, std::fabs((double)both.l[i] - ((double)melodyChip.l[i] + rhythmChip.l[i])));
    check("OPLL: with default gains the output is melody + rhythm", d < 1e-6);

    Out leftOnlyRhythm = withGains(1, 1, 1, 0);
    check("OPLL: part gains act on L and R separately",
          leftOnlyRhythm.l == both.l && leftOnlyRhythm.r == melody.r);
}

void testOpllex() {
    // バンクごとに音色 1 を鳴らす
    Out bank[4];
    for (int b = 0; b < 4; ++b)
        bank[b] = play("OPLLEX", concat({ { { 0x40, (uint8_t)b } }, opllNote(0, 1) }));
    bool allAudible = true, allDiffer = true;
    for (int b = 0; b < 4; ++b) {
        allAudible = allAudible && audible(bank[b]);
        for (int c = b + 1; c < 4; ++c)
            allDiffer = allDiffer && maxDiff(bank[b].l, bank[c].l) > kSilent;
    }
    check("OPLLEX: voice 1 sounds in every bank", allAudible);
    check("OPLLEX: voice 1 sounds different in each of the four banks", allDiffer);

    check("OPLLEX: bits above b1:b0 of the bank register are ignored",
          same(play("OPLLEX", concat({ { { 0x40, 0xFD } }, opllNote(0, 1) })), bank[1]));

    // ch0 にバンク 1、ch1 にバンク 2 を同時に持たせる。チャンネルの出力は
    // 足し合わされるだけなので、別々に鳴らした和と (丸めの範囲で) 一致する
    Out ch0 = play("OPLLEX", concat({ { { 0x40, 1 } }, opllNote(0, 1) }));
    Out ch1 = play("OPLLEX", concat({ { { 0x41, 2 } }, opllNote(1, 1) }));
    Out both = play("OPLLEX", concat({ { { 0x40, 1 }, { 0x41, 2 } }, opllNote(0, 1), opllNote(1, 1) }));
    Out lastWins = play("OPLLEX", concat({ { { 0x40, 2 }, { 0x41, 2 } }, opllNote(0, 1), opllNote(1, 1) }));
    double d = 0.0;
    for (size_t i = 0; i < both.l.size(); ++i)
        d = std::fmax(d, std::fabs((double)both.l[i] - ((double)ch0.l[i] + ch1.l[i])));
    check("OPLLEX: two channels hold different banks at the same time",
          d < 4.0 / 32768 && maxDiff(both.l, lastWins.l) > kSilent);

    Out playing = play("OPLLEX", [](Engine& e, uint32_t id) {
        e.write(id, opllNote(0, 1));
        e.render(100);
        e.write(id, { { 0x40, 3 } });
    });
    Out untouched = play("OPLLEX", [](Engine& e, uint32_t id) {
        e.write(id, opllNote(0, 1));
        e.render(100);
    });
    check("OPLLEX: writing the bank register re-points a channel already playing",
          maxDiff(playing.l, untouched.l) > kSilent);

    // ユーザー音色はバンクに属さず、素の OPLL と同じ音になる
    Out user0 = play("OPLLEX", concat({ opllUserVoice(), { { 0x40, 0 } }, opllNote(0, 0) }));
    Out user3 = play("OPLLEX", concat({ opllUserVoice(), { { 0x40, 3 } }, opllNote(0, 0) }));
    Out opll  = play("OPLL",   concat({ opllUserVoice(), opllNote(0, 0) }));
    check("OPLLEX: the user voice does not change with the bank", audible(user0) && same(user0, user3));
    check("OPLLEX: the user voice sounds exactly as on the plain OPLL", same(user0, opll));

    Regs junk;
    for (int r = 0x49; r < 0x100; ++r) junk.push_back({ (uint8_t)r, 0xFF });
    check("OPLLEX: registers past 48h change nothing",
          same(play("OPLLEX", concat({ junk, { { 0x40, 1 } }, opllNote(0, 1) })), bank[1]));

    check("OPLLEX: has no parts, so its output stays mono (L == R)",
          bank[0].l == bank[0].r);
}

// emu2413 / emu8950 のレート変換器は L と R で補間位置がずれる作りなので、
// エンジンが揃えていなければ、中央定位のチップでも L と R が食い違う。
// OPLL 系は L と R にメロディとリズムを振り分けているので、この形では見られない
void testConverterPhase() {
    static const char* chips[] = { "Y8950", "OPL", "OPL2", "OPL2EX" };
    for (const char* chip : chips) {
        Out o = play(chip, oplNotes(true));
        check(std::string(chip) + ": L == R at 48 kHz (converter phase aligned)",
              audible(o) && o.l == o.r);
    }
}

void testOpl2exFm() {
    Out opl2   = play("OPL2",   oplNotes(true));
    Out opl2ex = play("OPL2EX", oplNotes(true));
    Out plain  = play("OPL2",   oplNotes(false));
    check("OPL2EX: FM with waveform select sounds exactly as on the plain OPL2",
          audible(opl2ex) && same(opl2ex, opl2));
    check("  (the waveform select is in effect in that comparison)",
          maxDiff(opl2.l, plain.l) > kSilent);
    check("OPL2EX: with 01h b5 clear it also matches the plain OPL2",
          same(play("OPL2EX", oplNotes(false)), plain));
    check("  (the Y8950 ignores waveform select, so it differs there)",
          maxDiff(play("Y8950", oplNotes(true)).l, opl2.l) > kSilent);
}

void testOpl2exAdpcm() {
    Out y8950  = play("Y8950",  withAdpcm(adpcmPlay(0x00)));
    Out opl2ex = play("OPL2EX", withAdpcm(adpcmPlay(0x00)));
    check("OPL2EX: ADPCM plays exactly as on the Y8950", audible(opl2ex) && same(opl2ex, y8950));
    check("  (the plain OPL2 has no ADPCM)", silent(play("OPL2", withAdpcm(adpcmPlay(0x00)))));

    check("OPL2EX: bit 0 of 08h (ROM) still plays from RAM",
          same(play("OPL2EX", withAdpcm(adpcmPlay(0x01))), opl2ex));
    // 空の ROM を ADPCM として復号すると直流が積み上がるので、無音ではなく差で見る
    check("  (on the Y8950 the same bit switches to the empty ROM)",
          maxDiff(play("Y8950", withAdpcm(adpcmPlay(0x01))).l, y8950.l) > kSilent);

    // 07h の REC と MEMORY DATA を立てて 0Fh から書き込む経路
    auto byRegisters = [](Engine& e, uint32_t id) {
        Regs r = { { 0x07, 0x60 } };
        for (uint8_t b : adpcmData(kAdpcmBytes)) r.push_back({ 0x0F, b });
        r.push_back({ 0x07, 0x00 });
        e.write(id, r);
        e.write(id, adpcmPlay(0x00));
    };
    check("OPL2EX: sample RAM written through 0Fh plays like SetMemory",
          same(play("OPL2EX", byRegisters), opl2ex));

    check("OPL2EX: 07h b3 (SP-OFF) mutes the ADPCM, as on the Y8950",
          silent(play("OPL2EX", withAdpcm(concat({ adpcmPlay(0x00), { { 0x07, 0x88 } } })))));

    // 空の RAM も直流を積み上げるので、データを入れずに鳴らしたものと比べる
    Out empty = play("OPL2EX", withAdpcm(adpcmPlay(0x00), false));
    Out other = play("OPL2EX", [](Engine& e, uint32_t id) {
        uint32_t second = e.add("OPL2EX");
        auto data = adpcmData(kAdpcmBytes);
        A.SetMemory(e.h, id, FM_MEM_ADPCM_B, data.data(), (uint32_t)data.size());
        e.write(second, adpcmPlay(0x00));
    });
    check("OPL2EX: each chip has its own sample RAM",
          same(other, empty) && maxDiff(empty.l, opl2ex.l) > kSilent);

    Engine e;
    uint32_t id = e.add("OPL2EX");
    std::vector<uint8_t> big(300 * 1024, 0x77);
    check("OPL2EX: SetMemory larger than 256KB is clipped, not rejected",
          A.SetMemory(e.h, id, FM_MEM_ADPCM_B, big.data(), (uint32_t)big.size()) == FM_OK);
    check("OPL2EX: SetMemory rejects memory types other than ADPCM-B",
          A.SetMemory(e.h, id, FM_MEM_PCM, big.data(), 16) == FM_ERR_UNAVAILABLE);
}

// YM2149 1 系統ぶん。base は SSGS では 0x00 (SSG-1) か 0x20 (SSG-2)
// A: トーン、B: トーン + エンベロープ、C: ノイズ
Regs ssgNotes(uint8_t base) {
    auto r = [base](uint8_t reg, uint8_t val) { return Reg{ (uint8_t)(base + reg), val }; };
    return {
        r(0x00, 0x00), r(0x01, 0x01),
        r(0x02, 0xC0), r(0x03, 0x00),
        r(0x06, 0x10),
        r(0x07, 0x1C),                  // トーン A/B、ノイズ C
        r(0x08, 0x0F), r(0x09, 0x10), r(0x0A, 0x0C),
        r(0x0B, 0x00), r(0x0C, 0x02), r(0x0D, 0x0E),
    };
}

// ch のトーンだけを鳴らす
Regs ssgTone(uint8_t base, int ch) {
    return {
        { (uint8_t)(base + ch * 2), 0x00 }, { (uint8_t)(base + ch * 2 + 1), 0x01 },
        { (uint8_t)(base + 0x07), (uint8_t)(0x3F & ~(1 << ch)) },
        { (uint8_t)(base + 0x08 + ch), 0x0F },
    };
}

Regs ssgPan(uint8_t base, int ch, uint8_t pan) {
    return { { (uint8_t)(base + 0x10 + ch), pan } };
}

bool allZero(const std::vector<float>& v) {
    for (float s : v) if (s != 0.0f) return false;
    return true;
}

void testSsgs() {
    // SSGS の clock はマスタークロックで、SSG は 5.12MHz 未満ならその 1/2 で動く
    Out ssg1 = play("SSGS", ssgNotes(0x00));
    Out ssg  = play("SSG", 3579545 / 2, ssgNotes(0x00));
    check("SSGS: SSG-1 sounds exactly as the plain SSG at half the clock",
          audible(ssg1) && same(ssg1, ssg));
    check("SSGS: SSG-2 (20h-3Fh) sounds exactly the same",
          same(play("SSGS", ssgNotes(0x20)), ssg));
    check("SSGS: from 5.12MHz up the SSG runs at a third of the clock",
          same(play("SSGS", 6144000, ssgNotes(0x00)), play("SSG", 2048000, ssgNotes(0x00))));
    {
        Engine e;
        uint32_t def = e.add("SSGS"), s6m = e.add("SSGS", 6144000);
        check("SSGS: native rate is the SSG clock / 8",
              A.GetNativeRate(e.h, def) == 3579545 / 2 / 8 &&
              A.GetNativeRate(e.h, s6m) == 6144000 / 3 / 8);
    }
    check("SSGS: a silent chip puts out exactly zero", [] {
        Out o = play("SSGS", Regs{});
        return allZero(o.l) && allZero(o.r);
    }());

    // パンポット。center は書かなかったときの出力
    Out center = play("SSGS", ssgTone(0x00, 0));
    auto panned = [](uint8_t pan) {
        return play("SSGS", concat({ ssgPan(0x00, 0, pan), ssgTone(0x00, 0) }));
    };
    check("SSGS: a channel sounds on both sides until a pot is written",
          audible(center) && center.l == center.r);
    check("SSGS: pan 8 is the centre", same(panned(8), center));
    Out p0 = panned(0), p1 = panned(1), p14 = panned(14), p15 = panned(15);
    check("SSGS: pan 0 sounds on the left only, at full level", p0.l == center.l && allZero(p0.r));
    check("SSGS: pan 1 is hard left as well", p1.l == center.l && allZero(p1.r));
    check("SSGS: pan 15 sounds on the right only, at full level", p15.r == center.r && allZero(p15.l));
    check("SSGS: pan 14 is not hard right", p14.r == center.r && rms(p14.l) > kSilent);
    // 片側は全開のまま、反対側が線形に絞られる。4 は (4-1)/7、11 は (15-11)/7
    Out p4 = panned(4), p11 = panned(11);
    check("SSGS: pan 4 turns the right side down to 3/7",
          p4.l == center.l && std::fabs(rms(p4.r) / rms(p4.l) - 3.0 / 7) < 0.01);
    check("SSGS: pan 11 turns the left side down to 4/7",
          p11.r == center.r && std::fabs(rms(p11.l) / rms(p11.r) - 4.0 / 7) < 0.01);
    check("SSGS: the upper bits of a pan register are ignored", same(panned(0xF0), p0));

    // チャンネルごと、系統ごとに独立している
    Out aLeft  = play("SSGS", concat({ ssgPan(0x00, 0, 0), ssgTone(0x00, 0) }));
    Out bRight = play("SSGS", concat({ ssgPan(0x00, 1, 15), ssgTone(0x00, 1) }));
    Out both   = play("SSGS", concat({ ssgPan(0x00, 0, 0), ssgPan(0x00, 1, 15), ssgTone(0x00, 0),
                                       ssgTone(0x00, 1), { { 0x07, 0x3C } } }));
    check("SSGS: each channel of a unit has its own pot",
          both.l == aLeft.l && both.r == bRight.r);
    // 2 系統は足し合わされるだけなので、同時に鳴らした出力は別々に鳴らした和になる
    Out unit1Only = play("SSGS", ssgNotes(0x00));
    Out unit2Only = play("SSGS", ssgTone(0x20, 1));
    Out together  = play("SSGS", concat({ ssgNotes(0x00), ssgTone(0x20, 1) }));
    double d = 0.0;
    for (size_t i = 0; i < together.l.size(); ++i)
        d = std::fmax(d, std::fabs((double)together.l[i] - ((double)unit1Only.l[i] + unit2Only.l[i])));
    check("SSGS: SSG-1 and SSG-2 are separate units that sound together",
          audible(unit2Only) && d < 1e-6);
    Out unit2Right = play("SSGS", concat({ ssgPan(0x20, 0, 15), ssgTone(0x20, 0) }));
    Out units = play("SSGS", concat({ ssgPan(0x00, 0, 0), ssgTone(0x00, 0),
                                      ssgPan(0x20, 0, 15), ssgTone(0x20, 0) }));
    check("SSGS: the two units sit on opposite sides at once",
          units.l == aLeft.l && units.r == unit2Right.r);

    // 持たないレジスタ: I/O ポート (0Eh/0Fh)、LED (2Fh)、13h-1Fh、33h-3Fh、40h 以降
    Regs junk;
    for (int r : { 0x0E, 0x0F, 0x2E, 0x2F }) junk.push_back({ (uint8_t)r, 0xFF });
    for (int r = 0x13; r < 0x20; ++r) junk.push_back({ (uint8_t)r, 0xFF });
    for (int r = 0x33; r < 0x100; ++r) junk.push_back({ (uint8_t)r, 0xFF });
    check("SSGS: registers it does not have change nothing",
          same(play("SSGS", concat({ junk, ssgNotes(0x00) })), ssg1));
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: api_test <path to DSAemuEngine library>\n");
        return 2;
    }
    if (!loadApi(argv[1], A)) return 1;

    testChipList();
    testPartApi();
    testOpllParts();
    testOpllex();
    testConverterPhase();
    testOpl2exFm();
    testOpl2exAdpcm();
    testSsgs();

    std::printf("%s (%d failed)\n", g_fails ? "FAILED" : "PASSED", g_fails);
    return g_fails ? 1 : 0;
}
