// api_test.cpp
// DSAemuEngine の FmEngineApi を DLL 越しに叩く試験。
//   api_test <DSAemuEngine の DLL / .so のパス>
// 失敗した項目があれば終了コードが 0 以外になる。
//
// 新しいチップの振る舞いは、できるかぎり素のコアのチップと同じ書き込みを
// して出力を突き合わせる形で確かめる
// (OPL2EX と OPL2 / Y8950、OPLLEX と OPLL、SSGS と SSG、SCCEX と SCC)。
// 突き合わせる差分そのものが効いていることも、同じ試験の中で確かめる。

#include "FmEngineApi.h"

#include <algorithm>
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
// 型はヘッダの宣言から取る。エンジンも同じヘッダの宣言に対して定義しているので、
// 引数の食い違いはどちらかのコンパイルで止まる
struct Api {
    decltype(&FmEngine_Create)           Create;
    decltype(&FmEngine_Destroy)          Destroy;
    decltype(&FmEngine_Inquiry)          Inquiry;
    decltype(&FmEngine_GetSupportedChip) GetSupportedChip;
    decltype(&FmEngine_AddChip)          AddChip;
    decltype(&FmEngine_GetChipName)      GetChipName;
    decltype(&FmEngine_GetSampleRate)    GetSampleRate;
    decltype(&FmEngine_Write)            Write;
    decltype(&FmEngine_SetGain)          SetGain;
    decltype(&FmEngine_GetGain)          GetGain;
    decltype(&FmEngine_GetPartCount)     GetPartCount;
    decltype(&FmEngine_GetPartName)      GetPartName;
    decltype(&FmEngine_SetPartGain)      SetPartGain;
    decltype(&FmEngine_GetPartGain)      GetPartGain;
    decltype(&FmEngine_GetMemoryCount)   GetMemoryCount;
    decltype(&FmEngine_GetMemoryName)    GetMemoryName;
    decltype(&FmEngine_SetMemory)        SetMemory;
    decltype(&FmEngine_SetMemoryEx)      SetMemoryEx;
    decltype(&FmEngine_Generate)         Generate;
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
    bind(api.GetSampleRate,    "FmEngine_GetSampleRate");
    bind(api.Write,            "FmEngine_Write");
    bind(api.SetGain,          "FmEngine_SetGain");
    bind(api.GetGain,          "FmEngine_GetGain");
    bind(api.GetPartCount,     "FmEngine_GetPartCount");
    bind(api.GetPartName,      "FmEngine_GetPartName");
    bind(api.SetPartGain,      "FmEngine_SetPartGain");
    bind(api.GetPartGain,      "FmEngine_GetPartGain");
    bind(api.GetMemoryCount,   "FmEngine_GetMemoryCount");
    bind(api.GetMemoryName,    "FmEngine_GetMemoryName");
    bind(api.SetMemory,        "FmEngine_SetMemory");
    bind(api.SetMemoryEx,      "FmEngine_SetMemoryEx");
    bind(api.Generate,         "FmEngine_Generate");
    // 仕様から外れた関数
    for (const char* name : { "FmEngine_GetPartMask", "FmEngine_GetMemorySize",
                              "FmEngine_GetNativeRate" }) {
        if (findSymbol(lib, name)) { std::printf("stale export: %s\n", name); ok = false; }
    }
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
// クロックを指定しない試験でチップに渡す値
constexpr uint32_t kClock = 3579545;
// Y8960 が SSGS に配るクロック
constexpr uint32_t kSsgClock = 1789772;

struct Reg { uint8_t reg, val; uint32_t port = 0; };
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

    uint32_t add(const char* name, uint32_t clock = kClock) {
        uint32_t id = 0xFFFFFFFFu;
        if (A.AddChip(h, name, clock, &id) != FM_OK) {
            std::printf("AddChip(%s) failed\n", name);
            ++g_fails;
        }
        return id;
    }
    void write(uint32_t id, const Regs& regs) {
        for (const auto& r : regs) A.Write(h, id, r.reg, r.val, r.port);
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

bool allZero(const std::vector<float>& v) {
    for (float s : v) if (s != 0.0f) return false;
    return true;
}

// 後半だけを見るのは、レート変換器に残っている止まる前のサンプルが抜けるのを待つため
bool zeroInSecondHalf(const Out& o) {
    auto half = [](const std::vector<float>& v) {
        return std::vector<float>(v.begin() + v.size() / 2, v.end());
    };
    return allZero(half(o.l)) && allZero(half(o.r));
}

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
// wse は 01h の b5 (波形選択の許可)、E0h 系には常にスロットごとに違う値を書く。
// melody が false なら ch0-5 を鳴らさない。rhythm は BDh の下位 5 ビット (鳴らすリズムの音)
Regs oplNotes(bool wse, bool melody = true, uint8_t rhythm = 0x1F) {
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
        r.push_back({ (uint8_t)(0xB0 + ch), (uint8_t)(melody ? 0x31 : 0x11) });  // key on, block 4
    for (uint8_t ch = 6; ch < 9; ++ch)
        r.push_back({ (uint8_t)(0xB0 + ch), 0x11 });      // block 4 (リズムは BDh で鳴らす)
    r.push_back({ 0xBD, (uint8_t)(0x20 | rhythm) });
    return r;
}

// OPL2EX の定位。index は 0-8 が ch0-8 (F6h-FEh)、9 が ADPCM (FFh)
Regs opl2exPan(int index, uint8_t pan) {
    return { { (uint8_t)(0xF6 + index), pan } };
}

// FM の 9 チャンネルを同じ定位にする
Regs opl2exFmPan(uint8_t pan) {
    Regs r;
    for (int ch = 0; ch < 9; ++ch) r.push_back({ (uint8_t)(0xF6 + ch), pan });
    return r;
}

// 左端の倍率はちょうど 1 なので、L が素の OPL2 / Y8950 の出力そのものになる
Regs opl2exHardLeft() {
    return concat({ opl2exFmPan(1), opl2exPan(9, 1) });
}

// ex は左端に置いた OPL2EX、plain は素のチップ (L と R に同じ音を出す)
bool sameOnLeft(const Out& ex, const Out& plain) {
    return ex.l == plain.l && allZero(ex.r);
}

// 書き込みの前に OPL2EX を左端に置く
std::function<void(Engine&, uint32_t)> hardLeft(const std::function<void(Engine&, uint32_t)>& setup) {
    return [setup](Engine& e, uint32_t id) {
        e.write(id, opl2exHardLeft());
        setup(e, id);
    };
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
// start と bytes は 256K RAM モードの番地 (4 バイト単位で数える)。
// DELTA-N が 8000h なので、既定の bytes は 0.33 秒ぶんで、kFrames では鳴り終わらない
Regs adpcmPlay(uint8_t reg08, uint32_t start = 0, uint32_t bytes = kAdpcmBytes) {
    const uint32_t from = start / 4;
    const uint32_t stop = (start + bytes) / 4 - 1;
    return {
        { 0x08, reg08 },
        { 0x09, (uint8_t)(from & 0xFF) }, { 0x0A, (uint8_t)(from >> 8) },
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
            A.SetMemory(e.h, id, "ADPCM_B", data.data(), (uint32_t)data.size());
        }
        e.write(id, regs);
    };
}

// ---------------------------------------------------------
//  試験
// ---------------------------------------------------------
const char* const kChips[] = {
    "SSG", "OPLL", "OPLLP", "OPLLX", "VRC7", "Y8950", "OPL", "OPL2",
    "SCC", "SCCP", "DCSG", "OPL2EX", "OPLLEX", "SSGS", "SCCEX",
};
constexpr uint32_t kChipCount = sizeof(kChips) / sizeof(kChips[0]);

// kChips を順に足したエンジンでの chip_id
constexpr uint32_t kOpll = 1, kY8950 = 5, kOpl2 = 7, kOpl2ex = 11, kOpllex = 12;

void testChipList() {
    Engine e;
    const uint32_t n = A.Inquiry(e.h);
    bool order = (n == kChipCount);
    for (uint32_t i = 0; order && i < n; ++i)
        order = std::strcmp(A.GetSupportedChip(e.h, i), kChips[i]) == 0;
    check("chip list keeps the old order and appends OPL2EX, OPLLEX, SSGS, SCCEX", order);

    bool rejected = true;
    for (const char* name : kChips) {
        uint32_t id = 0;
        rejected = rejected && A.AddChip(e.h, name, 0, &id) == FM_ERR_INVALID_ARG;
    }
    check("AddChip rejects clock 0 for every chip and adds nothing",
          rejected && A.GetChipName(e.h, 0) == nullptr);

    uint32_t opl2ex = e.add("OPL2EX");
    uint32_t opllex = e.add("OPLLEX");
    check("GetChipName returns the added names",
          std::strcmp(A.GetChipName(e.h, opl2ex), "OPL2EX") == 0 &&
          std::strcmp(A.GetChipName(e.h, opllex), "OPLLEX") == 0);
}

using Names = std::vector<std::string>;

// 仕様は並びを定めていないので、並べ替えて比べる
template <typename Count, typename Name>
Names listNames(Count count, Name name, Engine& e, uint32_t id) {
    Names names;
    const uint32_t n = count(e.h, id);
    for (uint32_t i = 0; i < n; ++i) {
        const char* s = name(e.h, id, i);
        names.push_back(s ? s : "(null)");
    }
    std::sort(names.begin(), names.end());
    return names;
}

bool isOneOf(const char* name, std::initializer_list<const char*> set) {
    for (const char* s : set) if (!std::strcmp(name, s)) return true;
    return false;
}

void testPartApi() {
    Engine e;
    bool lists = true;
    for (const char* name : kChips) {
        uint32_t id = e.add(name);
        const Names expected = isOneOf(name, { "OPLL", "OPLLP", "OPLLX", "VRC7" })
                             ? Names{ "MELODY", "RHYTHM" } : Names{};
        const Names parts = listNames(A.GetPartCount, A.GetPartName, e, id);
        if (parts != expected || listNames(A.GetPartCount, A.GetPartName, e, id) != parts) {
            std::printf("  %s has %u parts\n", name, A.GetPartCount(e.h, id));
            lists = false;
        }
    }
    check("parts: MELODY and RHYTHM for the OPLL family, none elsewhere", lists);
    check("GetPartCount is 0 and GetPartName is null for an unknown chip_id",
          A.GetPartCount(e.h, 1000) == 0 && A.GetPartName(e.h, 1000, 0) == nullptr);
    check("GetPartName is null past the last part",
          A.GetPartName(e.h, kOpll, 2) == nullptr && A.GetPartName(e.h, kOpl2, 0) == nullptr);

    float l = -1, r = -1;
    check("MELODY gain defaults to 1.0",
          A.GetPartGain(e.h, kOpll, "MELODY", &l, &r) == FM_OK && l == 1.0f && r == 1.0f);
    check("RHYTHM gain defaults to 1.0",
          A.GetPartGain(e.h, kOpll, "RHYTHM", &l, &r) == FM_OK && l == 1.0f && r == 1.0f);

    auto rejected = [&](uint32_t id, const char* part) {
        return A.SetPartGain(e.h, id, part, 0.5f, 0.5f) == FM_ERR_INVALID_ARG &&
               A.GetPartGain(e.h, id, part, &l, &r) == FM_ERR_INVALID_ARG;
    };
    check("part gain rejects a part the chip does not have",
          rejected(kOpll, "SSG") && rejected(kOpl2, "MELODY") && rejected(kOpllex, "MELODY"));
    check("part gain matches the whole name, case sensitively",
          rejected(kOpll, "melody") && rejected(kOpll, "MEL") && rejected(kOpll, "MELODYX") &&
          rejected(kOpll, ""));
    check("part gain rejects a null name and an unknown chip_id",
          rejected(kOpll, nullptr) && rejected(1000, "MELODY"));
    check("  (the rejected calls changed nothing)",
          A.GetPartGain(e.h, kOpll, "MELODY", &l, &r) == FM_OK && l == 1.0f && r == 1.0f &&
          A.GetPartGain(e.h, kOpll, "RHYTHM", &l, &r) == FM_OK && l == 1.0f && r == 1.0f);
    check("SetPartGain then GetPartGain returns the values",
          A.SetPartGain(e.h, kOpll, "RHYTHM", 0.25f, 0.75f) == FM_OK &&
          A.GetPartGain(e.h, kOpll, "RHYTHM", &l, &r) == FM_OK && l == 0.25f && r == 0.75f &&
          A.GetPartGain(e.h, kOpll, "MELODY", &l, &r) == FM_OK && l == 1.0f && r == 1.0f);
}

void testOpllParts() {
    const Regs regs = concat({ opllNote(0, 1), opllRhythm() });
    auto withGains = [&](float ml, float mr, float rl, float rr) {
        return play("OPLL", [&](Engine& e, uint32_t id) {
            A.SetPartGain(e.h, id, "MELODY", ml, mr);
            A.SetPartGain(e.h, id, "RHYTHM", rl, rr);
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

    // ユーザー音色はバンクに属さない。素の OPLL との突き合わせは testOpllexPan
    Out user0 = play("OPLLEX", concat({ opllUserVoice(), { { 0x40, 0 } }, opllNote(0, 0) }));
    Out user3 = play("OPLLEX", concat({ opllUserVoice(), { { 0x40, 3 } }, opllNote(0, 0) }));
    check("OPLLEX: the user voice does not change with the bank", audible(user0) && same(user0, user3));

    Regs junk;
    for (int r = 0x49; r < 0x50; ++r) junk.push_back({ (uint8_t)r, 0xFF });
    for (int r = 0x59; r < 0x100; ++r) junk.push_back({ (uint8_t)r, 0xFF });
    check("OPLLEX: registers 49h-4Fh and past 58h change nothing",
          same(play("OPLLEX", concat({ junk, { { 0x40, 1 } }, opllNote(0, 1) })), bank[1]));
}

// Y8960 の 8bit の定位 (1 が左端、128 が中央、255 が右端) を等パワーの則で
// 振ったときの、右と左の比
double panTan(uint8_t pan) {
    return std::tan((pan - 1) / 254.0 * 1.57079632679489661923);
}

// 中央の倍率 cos(pi/4)
constexpr double kCentreGain = 0.70710678118654752;

Regs opllexPan(int ch, uint8_t pan) {
    return { { (uint8_t)(0x50 + ch), pan } };
}

// リズムモードで keys (0Eh の下位 5 ビット) の音だけを鳴らす
Regs opllRhythmKeys(uint8_t keys) {
    Regs r = opllRhythm();
    r.back().val = (uint8_t)(0x20 | keys);
    return r;
}

void testOpllexPan() {
    const Regs note = concat({ opllUserVoice(), opllNote(0, 0) });
    Out center = play("OPLLEX", note);
    auto panned = [&](uint8_t pan) { return play("OPLLEX", concat({ opllexPan(0, pan), note })); };
    Out left = panned(1), right = panned(255);
    check("OPLLEX: a channel sounds on both sides alike until 50h-58h are written",
          audible(center) && center.l == center.r);
    check("OPLLEX: pan 128 is the centre, and so is the undefined 0",
          same(panned(128), center) && same(panned(0), center));
    // 左端の倍率はちょうど 1 なので、素の OPLL の出力そのものになる。
    // 素の OPLL はコアが L と R を同じ位相で取るので、レート変換の位相もここで見ている
    Out opll = play("OPLL", note);
    check("OPLLEX: pan 1 is hard left, exactly the plain OPLL's output",
          left.l == opll.l && allZero(left.r));
    check("OPLLEX: pan 255 is hard right", right.r == left.l && allZero(right.l));
    check("OPLLEX: the centre is equal power (cos 45 degrees each side)",
          std::fabs(rms(center.l) / rms(left.l) - kCentreGain) < 0.01);
    Out p64 = panned(64);
    check("OPLLEX: pan 64 leans left by the equal power law",
          std::fabs(rms(p64.r) / rms(p64.l) - panTan(64)) < 0.01 &&
          std::fabs((rms(p64.l) * rms(p64.l) + rms(p64.r) * rms(p64.r)) /
                    (rms(left.l) * rms(left.l)) - 1.0) < 0.02);

    Out ch0Left  = play("OPLLEX", concat({ opllexPan(0, 1), opllNote(0, 1) }));
    Out ch1Right = play("OPLLEX", concat({ opllexPan(1, 255), opllNote(1, 2) }));
    Out apart    = play("OPLLEX", concat({ opllexPan(0, 1), opllexPan(1, 255),
                                          opllNote(0, 1), opllNote(1, 2) }));
    check("OPLLEX: each channel has its own position",
          apart.l == ch0Left.l && apart.r == ch1Right.r);

    // リズムの音は、使うスロットのチャンネルの定位に従う
    struct Drums { int ch; uint8_t keys; const char* what; };
    const Drums drums[] = {
        { 6, 0x10, "56h places BD" },
        { 7, 0x09, "57h places HH and SD" },
        { 8, 0x06, "58h places TOM and CYM" },
    };
    for (const Drums& d : drums) {
        Regs pans;
        for (int ch = 6; ch < 9; ++ch)
            pans.push_back({ (uint8_t)(0x50 + ch), (uint8_t)(ch == d.ch ? 1 : 255) });
        Out all  = play("OPLLEX", concat({ pans, opllRhythmKeys(0x1F) }));
        Out only = play("OPLLEX", concat({ opllexPan(6, 1), opllexPan(7, 1), opllexPan(8, 1),
                                           opllRhythmKeys(d.keys) }));
        // 左端の倍率 1 は定位を持たない音の倍率と同じなので、R が 0 であることも見る
        check(std::string("OPLLEX: ") + d.what,
              rms(only.l) > kSilent && allZero(only.r) && all.l == only.l);
    }
}

// レート変換器は補間位置を全チャンネルで共有している。calcStereo が L と R を同じ
// 位置で取っていなければ、中央定位のチップでも L と R が食い違う。
// OPLL 系は L と R にメロディとリズムを振り分けているので、この形では見られない。
// OPLLEX は、左端に置いた音が素の OPLL と一致することで見ている (testOpllexPan)
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
    Out opl2ex = play("OPL2EX", concat({ opl2exHardLeft(), oplNotes(true) }));
    Out plain  = play("OPL2",   oplNotes(false));
    check("OPL2EX: FM with waveform select sounds exactly as on the plain OPL2",
          rms(opl2ex.l) > kSilent && sameOnLeft(opl2ex, opl2));
    check("  (the waveform select is in effect in that comparison)",
          maxDiff(opl2.l, plain.l) > kSilent);
    check("OPL2EX: with 01h b5 clear it also matches the plain OPL2",
          sameOnLeft(play("OPL2EX", concat({ opl2exHardLeft(), oplNotes(false) })), plain));
    check("  (the Y8950 ignores waveform select, so it differs there)",
          maxDiff(play("Y8950", oplNotes(true)).l, opl2.l) > kSilent);
}

void testOpl2exAdpcm() {
    Out y8950  = play("Y8950",  withAdpcm(adpcmPlay(0x00)));
    Out opl2ex = play("OPL2EX", withAdpcm(adpcmPlay(0x00)));
    check("OPL2EX: ADPCM plays exactly as on the Y8950",
          audible(opl2ex) && sameOnLeft(play("OPL2EX", hardLeft(withAdpcm(adpcmPlay(0x00)))), y8950));
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

    // 07h を START だけで書き直したときと同じ出力になる
    const Regs spOff = concat({ adpcmPlay(0x00), { { 0x07, 0x88 } } });
    Out spOffEx = play("OPL2EX", withAdpcm(spOff));
    check("OPL2EX: 07h b3 (SP-OFF) is ignored",
          audible(spOffEx) &&
          same(spOffEx, play("OPL2EX", withAdpcm(concat({ adpcmPlay(0x00), { { 0x07, 0x80 } } })))));
    check("  (on the Y8950 the same bit mutes the ADPCM)", silent(play("Y8950", withAdpcm(spOff))));

    // Y8960 の仕様で廃止されたレジスタ: キーボード、プリスケール、DA / AD、I/O、PCM。
    // 書く値は、試験が 12h (EG-CTRL) と 10h (DELTA-N) に書く値と違うものにする
    Regs abolished;
    for (int r : { 0x05, 0x06, 0x0D, 0x0E, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A })
        abolished.push_back({ (uint8_t)r, 0x40 });
    check("OPL2EX: the abolished registers change nothing",
          same(play("OPL2EX", withAdpcm(concat({ adpcmPlay(0x00), abolished }))), opl2ex));

    // 空の RAM も直流を積み上げるので、データを入れずに鳴らしたものと比べる
    Out empty = play("OPL2EX", withAdpcm(adpcmPlay(0x00), false));
    Out other = play("OPL2EX", [](Engine& e, uint32_t id) {
        uint32_t second = e.add("OPL2EX");
        auto data = adpcmData(kAdpcmBytes);
        A.SetMemory(e.h, id, "ADPCM_B", data.data(), (uint32_t)data.size());
        e.write(second, adpcmPlay(0x00));
    });
    check("OPL2EX: each chip has its own sample RAM",
          same(other, empty) && maxDiff(empty.l, opl2ex.l) > kSilent);

    Engine e;
    uint32_t id = e.add("OPL2EX");
    std::vector<uint8_t> big(300 * 1024, 0x77);
    check("OPL2EX: SetMemory larger than 256KB is clipped, not rejected",
          A.SetMemory(e.h, id, "ADPCM_B", big.data(), (uint32_t)big.size()) == FM_OK);
}

// OPL 系: ch だけを鳴らす
Regs oplNote(int ch) {
    const uint8_t mod = (uint8_t)((ch / 3) * 8 + ch % 3), car = (uint8_t)(mod + 3);
    return {
        { (uint8_t)(0x20 + mod), 0x01 }, { (uint8_t)(0x20 + car), 0x01 },
        { (uint8_t)(0x40 + mod), 0x10 }, { (uint8_t)(0x40 + car), 0x00 },
        { (uint8_t)(0x60 + mod), 0xF4 }, { (uint8_t)(0x60 + car), 0xF4 },
        { (uint8_t)(0x80 + mod), 0x24 }, { (uint8_t)(0x80 + car), 0x24 },
        { (uint8_t)(0xC0 + ch), 0x08 },
        { (uint8_t)(0xA0 + ch), 0x41 },
        { (uint8_t)(0xB0 + ch), 0x31 },                   // key on, block 4
    };
}

void testOpl2exPan() {
    const Regs note = oplNote(0);
    Out center = play("OPL2EX", note);
    auto panned = [&](uint8_t pan) { return play("OPL2EX", concat({ opl2exPan(0, pan), note })); };
    Out left = panned(1), right = panned(255);
    check("OPL2EX: a channel sounds on both sides alike until F6h-FEh are written",
          audible(center) && center.l == center.r);
    check("OPL2EX: pan 128 is the centre, and so is the undefined 0",
          same(panned(128), center) && same(panned(0), center));
    check("OPL2EX: pan 1 is hard left, exactly the plain OPL2's output",
          sameOnLeft(left, play("OPL2", note)));
    check("OPL2EX: pan 255 is hard right", right.r == left.l && allZero(right.l));
    check("OPL2EX: the centre is equal power (cos 45 degrees each side)",
          std::fabs(rms(center.l) / rms(left.l) - kCentreGain) < 0.01);
    Out p64 = panned(64);
    check("OPL2EX: pan 64 leans left by the equal power law",
          std::fabs(rms(p64.r) / rms(p64.l) - panTan(64)) < 0.01 &&
          std::fabs((rms(p64.l) * rms(p64.l) + rms(p64.r) * rms(p64.r)) /
                    (rms(left.l) * rms(left.l)) - 1.0) < 0.02);

    Out ch0Left  = play("OPL2EX", concat({ opl2exPan(0, 1), oplNote(0) }));
    Out ch1Right = play("OPL2EX", concat({ opl2exPan(1, 255), oplNote(1) }));
    Out apart    = play("OPL2EX", concat({ opl2exPan(0, 1), opl2exPan(1, 255), oplNote(0), oplNote(1) }));
    check("OPL2EX: each channel has its own position",
          apart.l == ch0Left.l && apart.r == ch1Right.r);

    // リズムの音は、使うスロットのチャンネルの定位に従う。keys は BDh の下位 5 ビット
    struct Drums { int ch; uint8_t keys; const char* what; };
    const Drums drums[] = {
        { 6, 0x10, "FCh places BD" },
        { 7, 0x09, "FDh places HH and SD" },
        { 8, 0x06, "FEh places TOM and CYM" },
    };
    for (const Drums& d : drums) {
        Regs pans;
        for (int ch = 6; ch < 9; ++ch) pans.push_back(opl2exPan(ch, (uint8_t)(ch == d.ch ? 1 : 255))[0]);
        Out all  = play("OPL2EX", concat({ pans, oplNotes(true, false, 0x1F) }));
        Out only = play("OPL2EX", concat({ opl2exPan(6, 1), opl2exPan(7, 1), opl2exPan(8, 1),
                                           oplNotes(true, false, d.keys) }));
        // 左端の倍率 1 は定位を持たない音の倍率と同じなので、R が 0 であることも見る
        check(std::string("OPL2EX: ") + d.what,
              rms(only.l) > kSilent && allZero(only.r) && all.l == only.l);
    }

    // ADPCM は FFh に従い、FM の定位とは別。
    // 後から書いたほうが勝つので、動かないはずの側を後に書く
    Out y8950 = play("Y8950", withAdpcm(adpcmPlay(0x00)));
    Out adpcmCenter = play("OPL2EX", withAdpcm(adpcmPlay(0x00)));
    check("OPL2EX: the ADPCM sounds on both sides alike until FFh is written",
          audible(adpcmCenter) && adpcmCenter.l == adpcmCenter.r);
    check("OPL2EX: FFh places the ADPCM, and F6h-FEh do not",
          sameOnLeft(play("OPL2EX", withAdpcm(concat({ opl2exPan(9, 1), opl2exFmPan(255),
                                                       adpcmPlay(0x00) }))), y8950));
    Out adpcmRight = play("OPL2EX", withAdpcm(concat({ opl2exPan(9, 255), adpcmPlay(0x00) })));
    check("OPL2EX: FFh 255 puts the ADPCM hard right",
          adpcmRight.r == y8950.l && allZero(adpcmRight.l));
    check("OPL2EX: the ADPCM centre is equal power too",
          std::fabs(rms(adpcmCenter.l) / rms(y8950.l) - kCentreGain) < 0.01);
    // メロディの 9 チャンネルとリズムの 5 音の両方で見る
    Regs nine;
    for (int ch = 0; ch < 9; ++ch) nine = concat({ nine, oplNote(ch) });
    auto fmStays = [](const Regs& notes) {
        return sameOnLeft(play("OPL2EX", concat({ opl2exFmPan(1), opl2exPan(9, 255), notes })),
                          play("OPL2", notes));
    };
    check("OPL2EX: FFh does not move the FM channels", fmStays(nine) && fmStays(oplNotes(true)));
}

// ADPCM は再生が止まると 0 を出す。最後に復号した値を出し続けると直流が乗る。
// 止まり方は 3 つで、停止アドレスに届く場合だけは kFrames のうちに届く短いデータで見る
void testAdpcmStop() {
    struct Stop { const char* what; uint32_t bytes; Regs regs; };
    const Stop stops[] = {
        { "reaching the stop address", 256,         {} },                 // 約 990 フレーム
        { "07h b0 (RESET)",            kAdpcmBytes, { { 0x07, 0x01 } } },
        { "clearing 07h b7 (START)",   kAdpcmBytes, { { 0x07, 0x00 } } },
    };
    for (const Stop& s : stops) {
        std::vector<Out> tails;
        for (const char* chip : { "Y8950", "OPL2EX" }) {
            Engine e;
            uint32_t id = e.add(chip);
            if (!std::strcmp(chip, "OPL2EX")) e.write(id, opl2exHardLeft());
            withAdpcm(adpcmPlay(0x00, 0, s.bytes))(e, id);
            Out head = e.render(200);
            e.write(id, s.regs);
            tails.push_back(e.render());
            check(std::string(chip) + ": ADPCM puts out zero after " + s.what,
                  rms(head.l) > kSilent && zeroInSecondHalf(tails.back()));
        }
        check(std::string("  (and the OPL2EX matches the Y8950 through ") + s.what + ")",
              sameOnLeft(tails[1], tails[0]));
    }
}

// YM2149 1 系統ぶん。base は SSGS では 0x00 (SSG-1) か 0x10 (SSG-2)
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

// unit は 0 が SSG-1、1 が SSG-2
Regs ssgVolume(int unit, int ch, uint8_t volume) {
    return { { (uint8_t)(0x20 + unit * 4 + ch), volume } };
}

Regs ssgPan(int unit, int ch, uint8_t pan) {
    return { { (uint8_t)(0x28 + unit * 4 + ch), pan } };
}

// 左端の倍率はちょうど 1 なので、L が素の SSG の出力そのものになる
Regs ssgHardLeft(int unit) {
    return concat({ ssgPan(unit, 0, 1), ssgPan(unit, 1, 1), ssgPan(unit, 2, 1) });
}

void testSsgs() {
    Out ssg  = play("SSG", kSsgClock, ssgNotes(0x00));
    Out ssg1 = play("SSGS", kSsgClock, concat({ ssgHardLeft(0), ssgNotes(0x00) }));
    check("SSGS: SSG-1 hard left sounds exactly as the plain SSG",
          audible(ssg) && ssg1.l == ssg.l && allZero(ssg1.r));
    check("SSGS: SSG-2 (10h-1Fh, positions 2Ch-2Eh) sounds exactly the same",
          same(play("SSGS", kSsgClock, concat({ ssgHardLeft(1), ssgNotes(0x10) })), ssg1));
    // 6144000 は、YMZ705 のように分周する (5.12MHz 以上で 1/3) と食い違う値
    check("SSGS: clock is the SSG's own clock, not divided",
          play("SSGS", 6144000, concat({ ssgHardLeft(0), ssgNotes(0x00) })).l ==
          play("SSG", 6144000, ssgNotes(0x00)).l);
    check("SSGS: a silent chip puts out exactly zero", [] {
        Out o = play("SSGS", Regs{});
        return allZero(o.l) && allZero(o.r);
    }());

    // 定位。center は書かなかったときの出力
    Out center = play("SSGS", ssgTone(0x00, 0));
    auto panned = [](uint8_t pan) {
        return play("SSGS", concat({ ssgPan(0, 0, pan), ssgTone(0x00, 0) }));
    };
    Out left = panned(1), right = panned(255);
    check("SSGS: a channel sounds on both sides alike until a position is written",
          audible(center) && center.l == center.r);
    check("SSGS: pan 128 is the centre, and so is the undefined 0",
          same(panned(128), center) && same(panned(0), center));
    check("SSGS: pan 1 is hard left", rms(left.l) > kSilent && allZero(left.r));
    check("SSGS: pan 255 is hard right", right.r == left.l && allZero(right.l));
    check("SSGS: the centre is equal power (cos 45 degrees each side)",
          std::fabs(rms(center.l) / rms(left.l) - kCentreGain) < 0.01);
    Out p64 = panned(64), p192 = panned(192);
    check("SSGS: pan 64 leans left by the equal power law",
          std::fabs(rms(p64.r) / rms(p64.l) - panTan(64)) < 0.01 &&
          std::fabs((rms(p64.l) * rms(p64.l) + rms(p64.r) * rms(p64.r)) /
                    (rms(left.l) * rms(left.l)) - 1.0) < 0.02);
    check("SSGS: pan 192 leans right by the equal power law",
          std::fabs(rms(p192.r) / rms(p192.l) - panTan(192)) < 0.01);

    // 音量
    auto volume = [](uint8_t v) {
        return play("SSGS", concat({ ssgVolume(0, 0, v), ssgTone(0x00, 0) }));
    };
    check("SSGS: volume 128 is the default", same(volume(128), center));
    check("SSGS: volume 0 silences the channel", [&] {
        Out o = volume(0);
        return allZero(o.l) && allZero(o.r);
    }());
    check("SSGS: volume 64 halves the level",
          std::fabs(rms(volume(64).l) / rms(center.l) - 0.5) < 0.01);
    check("SSGS: volumes above 128 are held at 128",
          same(volume(129), center) && same(volume(255), center));

    // チャンネルごと、系統ごとに独立している
    Out aLeft  = play("SSGS", concat({ ssgPan(0, 0, 1), ssgTone(0x00, 0) }));
    Out bRight = play("SSGS", concat({ ssgPan(0, 1, 255), ssgTone(0x00, 1) }));
    Out both   = play("SSGS", concat({ ssgPan(0, 0, 1), ssgPan(0, 1, 255), ssgTone(0x00, 0),
                                       ssgTone(0x00, 1), { { 0x07, 0x3C } } }));
    check("SSGS: each channel of a unit has its own position",
          both.l == aLeft.l && both.r == bRight.r);
    Out bOnly = play("SSGS", ssgTone(0x00, 1));
    check("SSGS: each channel of a unit has its own volume",
          same(play("SSGS", concat({ ssgVolume(0, 0, 0), ssgTone(0x00, 0),
                                     ssgTone(0x00, 1), { { 0x07, 0x3C } } })), bOnly));
    Out unit2 = play("SSGS", ssgTone(0x10, 0));
    check("SSGS: the volumes of SSG-1 (20h-22h) and SSG-2 (24h-26h) are separate",
          audible(unit2) &&
          same(play("SSGS", concat({ ssgVolume(0, 0, 0), ssgTone(0x10, 0) })), unit2) &&
          silent(play("SSGS", concat({ ssgVolume(1, 0, 0), ssgTone(0x10, 0) }))));
    // 2 系統は足し合わされるだけなので、同時に鳴らした出力は別々に鳴らした和になる
    Out unit1Only = play("SSGS", ssgNotes(0x00));
    Out unit2Only = play("SSGS", ssgTone(0x10, 1));
    Out together  = play("SSGS", concat({ ssgNotes(0x00), ssgTone(0x10, 1) }));
    double d = 0.0;
    for (size_t i = 0; i < together.l.size(); ++i)
        d = std::fmax(d, std::fabs((double)together.l[i] - ((double)unit1Only.l[i] + unit2Only.l[i])));
    check("SSGS: SSG-1 and SSG-2 are separate units that sound together",
          audible(unit2Only) && d < 1e-6);
    Out unit2Right = play("SSGS", concat({ ssgPan(1, 0, 255), ssgTone(0x10, 0) }));
    Out units = play("SSGS", concat({ ssgPan(0, 0, 1), ssgTone(0x00, 0),
                                      ssgPan(1, 0, 255), ssgTone(0x10, 0) }));
    check("SSGS: the two units sit on opposite sides at once",
          units.l == aLeft.l && units.r == unit2Right.r);

    // 未接続: GPIO (0Eh/0Fh、1Eh/1Fh。1Fh の下位 4bit は基板の LED)、
    // 20h-2Fh の 4 個ずつの組の 4 個目、30h 以降
    Regs junk;
    for (int r : { 0x0E, 0x0F, 0x1E, 0x1F, 0x23, 0x27, 0x2B, 0x2F }) junk.push_back({ (uint8_t)r, 0xFF });
    for (int r = 0x30; r < 0x100; ++r) junk.push_back({ (uint8_t)r, 0xFF });
    check("SSGS: registers it does not have change nothing",
          same(play("SSGS", concat({ junk, ssgNotes(0x00) })), unit1Only));
}

// 標準の SCC の窓 (port 0)。5 チャンネルに別々の波形・周波数・音量を入れ、
// sounding のビットが立ったチャンネルだけを鳴らす。CH3 と CH4 は波形を共有する。
// 鳴らさないチャンネルは 8Fh ではなく音量 0 で黙らせる。emu2212 はリセット直後に
// 全チャンネルを有効にしていて、8Fh の変更は各チャンネルの波形が 1 周したときに効くため
Regs sccNotes(uint8_t sounding = 0x1F) {
    Regs r;
    for (int i = 0; i < 32; ++i) {
        r.push_back({ (uint8_t)(0x00 + i), (uint8_t)(i < 16 ? 0x70 : 0x90) });
        r.push_back({ (uint8_t)(0x20 + i), (uint8_t)(i * 8 - 128) });
        r.push_back({ (uint8_t)(0x40 + i), (uint8_t)((i < 16 ? i : 31 - i) * 16 - 120) });
        r.push_back({ (uint8_t)(0x60 + i), (uint8_t)(i % 4 < 2 ? 0x50 : 0xC0) });
    }
    static const uint16_t freq[5] = { 0x0FE, 0x150, 0x1C0, 0x0A0, 0x230 };
    static const uint8_t volume[5] = { 0x0F, 0x0C, 0x0A, 0x0E, 0x08 };
    for (int ch = 0; ch < 5; ++ch) {
        r.push_back({ (uint8_t)(0x80 + ch * 2), (uint8_t)(freq[ch] & 0xFF) });
        r.push_back({ (uint8_t)(0x81 + ch * 2), (uint8_t)(freq[ch] >> 8) });
        r.push_back({ (uint8_t)(0x8A + ch), (uint8_t)((sounding >> ch) & 1 ? volume[ch] : 0) });
    }
    r.push_back({ 0x8F, 0x1F });
    return r;
}

// 9900h-9904h は port 1 の 00h-04h
Regs sccexPan(int ch, uint8_t pan) {
    return { { (uint8_t)ch, pan, 1 } };
}

Regs sccexPans(uint8_t pan) {
    Regs r;
    for (int ch = 0; ch < 5; ++ch) r.push_back({ (uint8_t)ch, pan, 1 });
    return r;
}

void testSccex() {
    Out scc = play("SCC", sccNotes());
    Out left = play("SCCEX", concat({ sccexPans(1), sccNotes() }));
    check("SCCEX: hard left sounds exactly as the plain SCC", audible(scc) && sameOnLeft(left, scc));
    Out right = play("SCCEX", concat({ sccexPans(255), sccNotes() }));
    check("SCCEX: pan 255 is hard right", right.r == scc.l && allZero(right.l));
    Out slowScc = play("SCC", 1789772, sccNotes());
    check("SCCEX: clock means the same as for the SCC chip",
          sameOnLeft(play("SCCEX", 1789772, concat({ sccexPans(1), sccNotes() })), slowScc) &&
          slowScc.l != scc.l);

    Out center = play("SCCEX", sccNotes());
    check("SCCEX: the channels sound on both sides alike until 9900h-9904h are written",
          audible(center) && center.l == center.r);
    check("SCCEX: pan 128 is the centre, and so is the undefined 0",
          same(play("SCCEX", concat({ sccexPans(128), sccNotes() })), center) &&
          same(play("SCCEX", concat({ sccexPans(0), sccNotes() })), center));
    check("SCCEX: the centre is equal power (cos 45 degrees each side)",
          std::fabs(rms(center.l) / rms(left.l) - kCentreGain) < 0.01);
    Out p64 = play("SCCEX", concat({ sccexPan(0, 64), sccNotes(0x01) }));
    check("SCCEX: pan 64 leans left by the equal power law",
          std::fabs(rms(p64.r) / rms(p64.l) - panTan(64)) < 0.01);

    // k だけを左端、ほかを右端に置き、素の SCC で k だけを鳴らした音と、k 以外を鳴らした音に
    // 分かれるかを見る。番地のずれとチャンネルの取り違えを捕まえる
    bool mapped = true;
    for (int k = 0; k < 5; ++k) {
        Out split = play("SCCEX", concat({ sccexPans(255), sccexPan(k, 1), sccNotes() }));
        Out only = play("SCC", sccNotes((uint8_t)(1 << k)));
        Out rest = play("SCC", sccNotes((uint8_t)(0x1F & ~(1 << k))));
        mapped = mapped && audible(only) && split.l == only.l && split.r == rest.l;
    }
    check("SCCEX: 9900h-9904h place channels A-E in that order", mapped);

    // 窓や定位に紛れ込めば音が変わる値を、鳴らしたあとに書く。
    // 0x98 / 0x99 は、番地の上位バイトを port に取る読み方でなら届く番地
    Regs junk;
    for (int r = 0x05; r < 0x100; ++r) junk.push_back({ (uint8_t)r, 0x01, 1 });
    for (uint32_t port : { 2u, 0x98u, 0x99u, 0xFFu })
        for (int r = 0; r < 5; ++r) junk.push_back({ (uint8_t)r, 0x01, port });
    check("SCCEX: port 1 past 04h and ports 2 and above change nothing",
          same(play("SCCEX", concat({ sccNotes(), junk })), center));
}

// 07h の REC と MEMORY DATA を立て、0Fh からサンプルを書き込む
Regs adpcmWrite(const std::vector<uint8_t>& data) {
    Regs r = { { 0x07, 0x60 } };
    for (uint8_t b : data) r.push_back({ 0x0F, b });
    r.push_back({ 0x07, 0x00 });
    return r;
}

void testMemoryApi() {
    constexpr uint32_t kSpace = 256 * 1024;
    const auto data = adpcmData(kAdpcmBytes);
    std::vector<uint8_t> block(kSpace, 0);
    Engine e;
    bool lists = true;
    for (const char* name : kChips) {
        uint32_t id = e.add(name);
        const Names expected = !std::strcmp(name, "Y8950")  ? Names{ "ADPCM_B", "ADPCM_B_ROMMODE" }
                             : !std::strcmp(name, "OPL2EX") ? Names{ "ADPCM_B" } : Names{};
        const Names memories = listNames(A.GetMemoryCount, A.GetMemoryName, e, id);
        if (memories != expected || listNames(A.GetMemoryCount, A.GetMemoryName, e, id) != memories) {
            std::printf("  %s has %u memories\n", name, A.GetMemoryCount(e.h, id));
            lists = false;
        }
    }
    check("memories: Y8950 has ADPCM_B and ADPCM_B_ROMMODE, OPL2EX has ADPCM_B", lists);
    check("GetMemoryCount is 0 and GetMemoryName is null for an unknown chip_id",
          A.GetMemoryCount(e.h, 1000) == 0 && A.GetMemoryName(e.h, 1000, 0) == nullptr);
    check("GetMemoryName is null past the last memory",
          A.GetMemoryName(e.h, kY8950, 2) == nullptr && A.GetMemoryName(e.h, kOpl2ex, 1) == nullptr &&
          A.GetMemoryName(e.h, kOpl2, 0) == nullptr);

    auto rejected = [&](uint32_t id, const char* memory) {
        return A.SetMemory(e.h, id, memory, data.data(), 16) == FM_ERR_INVALID_ARG &&
               A.SetMemoryEx(e.h, id, memory, 0, block.data(), 16, FM_ACCESS_ROM) == FM_ERR_INVALID_ARG;
    };
    check("SetMemory / SetMemoryEx reject a memory the chip does not have",
          rejected(kOpl2ex, "ADPCM_B_ROMMODE") && rejected(kOpl2, "ADPCM_B") &&
          rejected(kY8950, "PCM") && rejected(kY8950, "ADPCM_A"));
    check("SetMemory / SetMemoryEx match the whole name, case sensitively",
          rejected(kY8950, "adpcm_b") && rejected(kY8950, "ADPCM") && rejected(kY8950, "ADPCM_B_ROM") &&
          rejected(kY8950, "ADPCM_B_ROMMODEX") && rejected(kY8950, ""));
    check("SetMemory / SetMemoryEx reject a null name and an unknown chip_id",
          rejected(kY8950, nullptr) && rejected(1000, "ADPCM_B"));

    bool accepted = true;
    for (uint32_t id = 0; id < kChipCount; ++id) {
        const uint32_t n = A.GetMemoryCount(e.h, id);
        for (uint32_t i = 0; i < n; ++i) {
            const char* name = A.GetMemoryName(e.h, id, i);
            accepted = accepted &&
                A.SetMemory(e.h, id, name, data.data(), (uint32_t)data.size()) == FM_OK &&
                A.SetMemoryEx(e.h, id, name, 0, block.data(), kSpace, FM_ACCESS_RAM) == FM_OK;
        }
    }
    check("every listed memory is accepted by SetMemory and SetMemoryEx", accepted);

    // ROM モードは 32 バイト単位だが、0 番地から読み終える前に止めるので、RAM モードで
    // 同じデータを鳴らしたものと一致する
    const Out ref   = play("Y8950", withAdpcm(adpcmPlay(0x00)));
    const Out empty = play("Y8950", withAdpcm(adpcmPlay(0x00), false));
    auto romModeFilled = [&](uint8_t reg08) {
        return play("Y8950", [&](Engine& e, uint32_t id) {
            A.SetMemory(e.h, id, "ADPCM_B_ROMMODE", data.data(), (uint32_t)data.size());
            e.write(id, adpcmPlay(reg08));
        });
    };
    check("Y8950: SetMemory to ADPCM_B_ROMMODE plays in ROM mode", same(romModeFilled(0x01), ref));
    check("  (and leaves the RAM-mode memory empty)", same(romModeFilled(0x00), empty));
}

void testSetMemoryEx() {
    constexpr uint32_t kSpace = 256 * 1024;
    const auto data = adpcmData(kAdpcmBytes);
    const Out ref        = play("OPL2EX", withAdpcm(adpcmPlay(0x00)));
    const Out empty      = play("OPL2EX", withAdpcm(adpcmPlay(0x00), false));
    const Out refY8950   = play("Y8950",  withAdpcm(adpcmPlay(0x00)));
    const Out emptyY8950 = play("Y8950",  withAdpcm(adpcmPlay(0x00), false));

    // --- OPL2EX ---
    {
        std::vector<uint8_t> block(kSpace, 0);
        Engine e;
        uint32_t a = e.add("OPL2EX"), b = e.add("OPL2EX");
        bool ok = A.SetMemoryEx(e.h, a, "ADPCM_B", 0, block.data(), kSpace, FM_ACCESS_RAM) == FM_OK &&
                  A.SetMemoryEx(e.h, b, "ADPCM_B", 0, block.data(), kSpace, FM_ACCESS_RAM) == FM_OK;
        e.write(a, adpcmWrite(data));
        check("SetMemoryEx: a chip writes 0Fh straight into a RAM block",
              ok && std::equal(data.begin(), data.end(), block.begin()));
        e.write(b, adpcmPlay(0x00));
        check("SetMemoryEx: two OPL2EX share one RAM block", same(e.render(), ref));
    }
    {
        std::vector<uint8_t> block(kSpace, 0);
        Out o = play("OPL2EX", [&](Engine& e, uint32_t id) {
            A.SetMemoryEx(e.h, id, "ADPCM_B", 0, block.data(), kSpace, FM_ACCESS_RAM);
            std::copy(data.begin(), data.end(), block.begin());
            e.write(id, adpcmPlay(0x00));
        });
        check("SetMemoryEx: the chip sees what the caller wrote into a RAM block", same(o, ref));
    }
    {
        // 分割: 前半を 1 個目、後半を 2 個目の 0 番地に
        std::vector<uint8_t> block(kSpace, 0);
        std::copy(data.begin(), data.end(), block.begin() + kSpace / 2);
        auto split = [&](bool second) {
            Engine e;
            uint32_t a = e.add("OPL2EX"), b = e.add("OPL2EX");
            A.SetMemoryEx(e.h, a, "ADPCM_B", 0, block.data(), kSpace / 2, FM_ACCESS_RAM);
            A.SetMemoryEx(e.h, b, "ADPCM_B", 0, block.data() + kSpace / 2, kSpace / 2, FM_ACCESS_RAM);
            e.write(second ? b : a, adpcmPlay(0x00));
            return e.render();
        };
        check("SetMemoryEx: two OPL2EX split one block in halves",
              same(split(true), ref) && same(split(false), empty));
    }
    {
        // base: 64KB から先に置いたデータは 64KB から再生すると鳴り、0 番地は空いている
        std::vector<uint8_t> block(data);
        auto at = [&](uint32_t start) {
            return play("OPL2EX", [&](Engine& e, uint32_t id) {
                A.SetMemoryEx(e.h, id, "ADPCM_B", 0x10000, block.data(), (uint32_t)block.size(), FM_ACCESS_RAM);
                e.write(id, adpcmPlay(0x00, start));
            });
        };
        check("SetMemoryEx: base places a block, and addresses no block covers read 0",
              same(at(0x10000), ref) && same(at(0), empty));
    }
    {
        std::vector<uint8_t> rom(data);
        Out o = play("OPL2EX", [&](Engine& e, uint32_t id) {
            A.SetMemoryEx(e.h, id, "ADPCM_B", 0, rom.data(), (uint32_t)rom.size(), FM_ACCESS_ROM);
            e.write(id, adpcmWrite(std::vector<uint8_t>(kAdpcmBytes, 0x88)));
            e.write(id, adpcmPlay(0x00));
        });
        check("SetMemoryEx: writes to a ROM block are dropped", rom == data && same(o, ref));
    }
    {
        // SetMemory で入れた内容は、割り当てがある間は見えず、外すと戻る
        std::vector<uint8_t> block(kSpace, 0);
        auto legacy = [&](bool unmap) {
            return play("OPL2EX", [&](Engine& e, uint32_t id) {
                A.SetMemory(e.h, id, "ADPCM_B", data.data(), (uint32_t)data.size());
                A.SetMemoryEx(e.h, id, "ADPCM_B", 0, block.data(), kSpace, FM_ACCESS_RAM);
                if (unmap) A.SetMemoryEx(e.h, id, "ADPCM_B", 0, nullptr, kSpace, FM_ACCESS_ROM);
                e.write(id, adpcmPlay(0x00));
            });
        };
        check("SetMemoryEx: SetMemory contents hide while mapped and return when unmapped",
              same(legacy(false), empty) && same(legacy(true), ref));
    }
    {
        std::vector<uint8_t> block(kSpace, 0);
        Engine e;
        uint32_t x = e.add("OPL2EX"), opl2 = e.add("OPL2");
        bool ok = A.SetMemoryEx(e.h, x, "ADPCM_B", 0, block.data(), 4096, FM_ACCESS_RAM) == FM_OK;
        check("SetMemoryEx: overlapping a mapping is rejected",
              ok && A.SetMemoryEx(e.h, x, "ADPCM_B", 2048, block.data(), 4096, FM_ACCESS_RAM) == FM_ERR_INVALID_ARG);
        check("SetMemoryEx: a mapping next to another is accepted",
              A.SetMemoryEx(e.h, x, "ADPCM_B", 4096, block.data() + 4096, 4096, FM_ACCESS_RAM) == FM_OK);
        check("SetMemoryEx: size 0, an unknown access or chip_id is rejected",
              A.SetMemoryEx(e.h, x, "ADPCM_B", 0x20000, block.data(), 0, FM_ACCESS_RAM) == FM_ERR_INVALID_ARG &&
              A.SetMemoryEx(e.h, x, "ADPCM_B", 0x20000, block.data(), 16, (FmMemoryAccess)2) == FM_ERR_INVALID_ARG &&
              A.SetMemoryEx(e.h, 99, "ADPCM_B", 0x20000, block.data(), 16, FM_ACCESS_RAM) == FM_ERR_INVALID_ARG);
        check("SetMemoryEx: memory the chip does not have is rejected",
              A.SetMemoryEx(e.h, x, "ADPCM_B_ROMMODE", 0, block.data(), 16, FM_ACCESS_RAM) == FM_ERR_INVALID_ARG &&
              A.SetMemoryEx(e.h, opl2, "ADPCM_B", 0, block.data(), 16, FM_ACCESS_RAM) == FM_ERR_INVALID_ARG);
        check("SetMemoryEx: unmapping a range with nothing in it succeeds",
              A.SetMemoryEx(e.h, x, "ADPCM_B", 0x30000, nullptr, 16, FM_ACCESS_ROM) == FM_OK);
    }

    // --- Y8950 (素の emu8950。RAM はメモリ空間 1 つ分を丸ごと渡すときだけ) ---
    {
        std::vector<uint8_t> block(kSpace, 0);
        Engine e;
        uint32_t id = e.add("Y8950");
        bool ok = A.SetMemoryEx(e.h, id, "ADPCM_B", 0, block.data(), kSpace, FM_ACCESS_RAM) == FM_OK;
        e.write(id, adpcmWrite(data));
        check("SetMemoryEx Y8950: a whole-space RAM block is read and written in place",
              ok && std::equal(data.begin(), data.end(), block.begin()));
        e.write(id, adpcmPlay(0x00));
        check("SetMemoryEx Y8950: it plays what was written", same(e.render(), refY8950));
    }
    {
        std::vector<uint8_t> block(kSpace, 0);
        Engine e;
        uint32_t a = e.add("Y8950"), b = e.add("Y8950");
        A.SetMemoryEx(e.h, a, "ADPCM_B", 0, block.data(), kSpace, FM_ACCESS_RAM);
        A.SetMemoryEx(e.h, b, "ADPCM_B", 0, block.data(), kSpace, FM_ACCESS_RAM);
        e.write(a, adpcmWrite(data));
        e.write(b, adpcmPlay(0x00));
        check("SetMemoryEx Y8950: two Y8950 share one whole-space RAM block", same(e.render(), refY8950));
    }
    {
        std::vector<uint8_t> block(kSpace, 0);
        Engine e;
        uint32_t id = e.add("Y8950");
        check("SetMemoryEx Y8950: a RAM block that is not the whole space is unavailable",
              A.SetMemoryEx(e.h, id, "ADPCM_B", 0, block.data(), kSpace / 2, FM_ACCESS_RAM) == FM_ERR_UNAVAILABLE &&
              A.SetMemoryEx(e.h, id, "ADPCM_B", 4, block.data(), kSpace, FM_ACCESS_RAM) == FM_ERR_UNAVAILABLE);
    }
    {
        // ROM モードのメモリ。ROM モードは 32 バイト単位だが、0 番地から読み終える前に
        // 止めるので、RAM モードで同じデータを鳴らしたものと一致する
        std::vector<uint8_t> rom(data);
        std::vector<uint8_t> block(kSpace, 0);
        std::copy(data.begin(), data.end(), block.begin());
        auto romMode = [&](FmMemoryAccess access) {
            return play("Y8950", [&](Engine& e, uint32_t id) {
                uint8_t* p = access == FM_ACCESS_ROM ? rom.data() : block.data();
                uint32_t n = access == FM_ACCESS_ROM ? (uint32_t)rom.size() : kSpace;
                A.SetMemoryEx(e.h, id, "ADPCM_B_ROMMODE", 0, p, n, access);
                e.write(id, adpcmPlay(0x01));
            });
        };
        check("SetMemoryEx Y8950: ROM-mode memory takes a ROM block", same(romMode(FM_ACCESS_ROM), refY8950));
        check("SetMemoryEx Y8950: ROM-mode memory takes a whole-space RAM block", same(romMode(FM_ACCESS_RAM), refY8950));
        check("  (without a mapping, ROM mode does not play the data)",
              maxDiff(play("Y8950", withAdpcm(adpcmPlay(0x01))).l, refY8950.l) > kSilent);
    }
    {
        std::vector<uint8_t> zeros(kAdpcmBytes, 0);
        auto legacy = [&](bool unmap) {
            return play("Y8950", [&](Engine& e, uint32_t id) {
                A.SetMemory(e.h, id, "ADPCM_B", data.data(), (uint32_t)data.size());
                A.SetMemoryEx(e.h, id, "ADPCM_B", 0, zeros.data(), (uint32_t)zeros.size(), FM_ACCESS_ROM);
                if (unmap) A.SetMemoryEx(e.h, id, "ADPCM_B", 0, nullptr, kSpace, FM_ACCESS_ROM);
                e.write(id, adpcmPlay(0x00));
            });
        };
        check("SetMemoryEx Y8950: SetMemory contents hide while mapped and return when unmapped",
              same(legacy(false), emptyY8950) && same(legacy(true), refY8950));
    }
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
    testOpllexPan();
    testConverterPhase();
    testOpl2exFm();
    testOpl2exAdpcm();
    testOpl2exPan();
    testAdpcmStop();
    testSsgs();
    testSccex();
    testMemoryApi();
    testSetMemoryEx();

    std::printf("%s (%d failed)\n", g_fails ? "FAILED" : "PASSED", g_fails);
    return g_fails ? 1 : 0;
}
