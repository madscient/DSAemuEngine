// y8960_scc_test.cpp
// Y8960 の SCC のコア (src/y8960/Y8960SccCore.*) を直接叩く試験。
//   y8960_scc_test
// 失敗した項目があれば終了コードが 0 以外になる。
//
// エンジンでは SCCEX チップになり、api_test が定位のレジスタまでを確かめる。
// ここでは、エンジンが使わない SCC+ の型と quality 0 の経路を含めて、素の emu2212 と
// 同じ書き込みをして出力を突き合わせる。

#include "Y8960SccCore.h"
#include "emu2212.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace {

int g_fails = 0;

void check(const std::string& what, bool ok) {
    std::printf("%-72s %s\n", what.c_str(), ok ? "ok" : "FAIL");
    if (!ok) ++g_fails;
}

constexpr uint32_t kClock = 3579545;
constexpr uint32_t kRate = 48000;
constexpr int kFrames = 4800;  // 0.1 秒

// 中央の倍率 cos(pi/4)
constexpr double kCentreGain = 0.70710678118654752;

// 等パワーの則で振ったときの、右と左の比
double panTan(uint8_t pan) {
    return std::tan((pan - 1) / 254.0 * 1.57079632679489661923);
}

// レジスタ窓のオフセット (README の SCC / SCCP の表と同じ) と値
struct Reg { uint16_t off; uint8_t val; };
using Regs = std::vector<Reg>;

// 5 チャンネルをそれぞれ別の波形・周波数・音量で鳴らす。SCC (標準) では
// CH3 と CH4 が波形を共有する
Regs notes(bool plus) {
    const uint16_t freq = plus ? 0xA0 : 0x80;
    const uint16_t vol  = plus ? 0xAA : 0x8A;
    const uint16_t en   = plus ? 0xAF : 0x8F;
    Regs regs;
    for (int ch = 0; ch < (plus ? 5 : 4); ++ch)
        for (int i = 0; i < 32; ++i) {
            int v = 0;
            switch (ch) {
            case 0: v = i < 16 ? 100 : -100; break;           // 矩形
            case 1: v = i * 8 - 128; break;                   // のこぎり
            case 2: v = (int)(120 * std::sin(i * 0.19634954)); break;
            case 3: v = i < 8 ? 90 : -30; break;
            case 4: v = (i & 4) ? 70 : -70; break;
            }
            regs.push_back({ (uint16_t)(ch * 32 + i), (uint8_t)(int8_t)v });
        }
    const uint16_t periods[5] = { 0x1AB, 0x0FE, 0x23C, 0x152, 0x0C9 };
    const uint8_t volumes[5] = { 15, 13, 11, 14, 12 };
    for (int ch = 0; ch < 5; ++ch) {
        regs.push_back({ (uint16_t)(freq + ch * 2), (uint8_t)(periods[ch] & 0xFF) });
        regs.push_back({ (uint16_t)(freq + ch * 2 + 1), (uint8_t)(periods[ch] >> 8) });
        regs.push_back({ (uint16_t)(vol + ch), volumes[ch] });
    }
    regs.push_back({ en, 0x1F });
    return regs;
}

// チップ生成時にエンジンがするのと同じ準備 (src/DSAemuEngine.cpp の createChip)
template <class Write>
void activate(bool plus, Write write) {
    if (plus) {
        write(0xBFFE, 0x20);
        write(0xB000, 0x80);
    } else {
        write(0x9000, 0x3F);
    }
}

uint32_t window(bool plus) { return plus ? 0xB800 : 0x9800; }

// mute は素のコアのマスク (SCC_MASK_CH)。チャンネルイネーブルと違い、
// 周期の切れ目を待たずに効くので、最初のサンプルから比べられる
std::vector<int32_t> playPlain(bool plus, const Regs& regs, uint32_t quality = 1,
                               uint32_t mute = 0) {
    SCC* scc = SCC_new(kClock, kRate);
    SCC_set_type(scc, plus ? SCC_ENHANCED : SCC_STANDARD);
    SCC_set_quality(scc, quality);
    SCC_reset(scc);
    SCC_setMask(scc, mute);
    activate(plus, [&](uint32_t a, uint32_t v) { SCC_write(scc, a, v); });
    for (const Reg& r : regs) SCC_write(scc, window(plus) + r.off, r.val);
    std::vector<int32_t> out(kFrames);
    for (int32_t& s : out) s = SCC_calc(scc);
    SCC_delete(scc);
    return out;
}

struct Stereo {
    std::vector<int32_t> l, r;
    bool operator==(const Stereo& o) const { return l == o.l && r == o.r; }
};

struct Fork {
    bool plus = false;
    uint32_t quality = 1;
};

// setup は窓への書き込みの前に定位などを触るためのもの
Stereo playFork(const Fork& f, const Regs& regs,
                const std::function<void(Y8960SCC*)>& setup = nullptr) {
    Y8960SCC* scc = Y8960SCC_new(kClock, kRate);
    Y8960SCC_set_type(scc, f.plus ? Y8960SCC_ENHANCED : Y8960SCC_STANDARD);
    Y8960SCC_set_quality(scc, f.quality);
    Y8960SCC_reset(scc);
    activate(f.plus, [&](uint32_t a, uint32_t v) { Y8960SCC_write(scc, a, v); });
    if (setup) setup(scc);
    for (const Reg& r : regs) Y8960SCC_write(scc, window(f.plus) + r.off, r.val);
    Stereo out;
    out.l.resize(kFrames);
    out.r.resize(kFrames);
    for (int i = 0; i < kFrames; ++i) {
        int32_t lr[2];
        Y8960SCC_calcStereo(scc, lr);
        out.l[i] = lr[0];
        out.r[i] = lr[1];
    }
    Y8960SCC_delete(scc);
    return out;
}

auto panAll(uint8_t pan) {
    return [pan](Y8960SCC* scc) {
        for (uint32_t ch = 0; ch < 5; ++ch) Y8960SCC_setChannelPan(scc, ch, pan);
    };
}

double rms(const std::vector<int32_t>& v) {
    double acc = 0.0;
    for (int32_t s : v) acc += (double)s * s;
    return std::sqrt(acc / (double)v.size());
}

bool allZero(const std::vector<int32_t>& v) {
    for (int32_t s : v) if (s != 0) return false;
    return true;
}

void testMono(bool plus, const char* name) {
    std::vector<int32_t> plain = playPlain(plus, notes(plus));
    check(std::string(name) + ": the tone is audible", rms(plain) > 100.0);

    Y8960SCC* scc = Y8960SCC_new(kClock, kRate);
    Y8960SCC_set_type(scc, plus ? Y8960SCC_ENHANCED : Y8960SCC_STANDARD);
    Y8960SCC_set_quality(scc, 1);
    Y8960SCC_reset(scc);
    activate(plus, [&](uint32_t a, uint32_t v) { Y8960SCC_write(scc, a, v); });
    Y8960SCC_setChannelPan(scc, 0, 1);
    Y8960SCC_setChannelPan(scc, 1, 255);
    for (const Reg& r : notes(plus)) Y8960SCC_write(scc, window(plus) + r.off, r.val);
    std::vector<int32_t> mono(kFrames);
    for (int32_t& s : mono) s = Y8960SCC_calc(scc);
    Y8960SCC_delete(scc);
    check(std::string(name) + ": calc (mono) is the plain SCC and ignores the positions",
          mono == plain);
}

void testPan(bool plus, const char* name) {
    const std::string n = name;
    const Fork f{ plus };
    const Regs regs = notes(plus);
    std::vector<int32_t> plain = playPlain(plus, regs);

    Stereo left = playFork(f, regs, panAll(1));
    check(n + ": pan 1 is hard left and sounds exactly as the plain SCC",
          left.l == plain && allZero(left.r));
    Stereo right = playFork(f, regs, panAll(255));
    check(n + ": pan 255 is hard right and sounds exactly as the plain SCC",
          right.r == plain && allZero(right.l));

    Stereo center = playFork(f, regs);
    check(n + ": every channel is centred until a position is written",
          center.l == center.r && rms(center.l) > 100.0);
    check(n + ": pan 128 is the centre, and so is the undefined 0",
          playFork(f, regs, panAll(128)) == center && playFork(f, regs, panAll(0)) == center);
    check(n + ": the centre is equal power (cos 45 degrees each side)",
          std::fabs(rms(center.l) / rms(plain) - kCentreGain) < 0.01);

    Stereo p64 = playFork(f, regs, panAll(64));
    check(n + ": pan 64 leans left by the equal power law",
          std::fabs(rms(p64.r) / rms(p64.l) - panTan(64)) < 0.01 &&
          std::fabs((rms(p64.l) * rms(p64.l) + rms(p64.r) * rms(p64.r)) /
                    (rms(plain) * rms(plain)) - 1.0) < 0.02);
    Stereo p192 = playFork(f, regs, panAll(192));
    check(n + ": pan 192 leans right by the equal power law",
          std::fabs(rms(p192.r) / rms(p192.l) - panTan(192)) < 0.01 &&
          std::fabs((rms(p192.l) * rms(p192.l) + rms(p192.r) * rms(p192.r)) /
                    (rms(plain) * rms(plain)) - 1.0) < 0.02);

    // ch だけを左端、他を右端に置くと、左は ch だけを鳴らした素の SCC と一致する
    bool own = true;
    for (uint32_t ch = 0; ch < 5; ++ch) {
        std::vector<int32_t> only = playPlain(plus, regs, 1, 0x1F & ~SCC_MASK_CH(ch));
        Stereo s = playFork(f, regs, [ch](Y8960SCC* scc) {
            panAll(255)(scc);
            Y8960SCC_setChannelPan(scc, ch, 1);
        });
        std::vector<int32_t> rest = playPlain(plus, regs, 1, SCC_MASK_CH(ch));
        own = own && rms(only) > 10.0 && s.l == only && s.r == rest;
    }
    check(n + ": each of the five channels has its own position", own);
}

void testEdges() {
    const Fork f;
    const Regs regs = notes(false);
    Stereo center = playFork(f, regs);

    check("channels past 4 change nothing",
          playFork(f, regs, [](Y8960SCC* scc) {
              Y8960SCC_setChannelPan(scc, 5, 1);
              Y8960SCC_setChannelPan(scc, 0xFFFFFFFFu, 255);
          }) == center);

    // リセットしないと窓が 0 番地にあり有効にもならないので、内部のレジスタへ直接書く
    // (Y8960SCC_writeReg の表)。素のコアも同じにして比べる
    {
        Y8960SCC* fork = Y8960SCC_new(kClock, kRate);
        SCC* plain = SCC_new(kClock, kRate);
        Y8960SCC_set_quality(fork, 1);
        SCC_set_quality(plain, 1);
        for (const Reg& r : notes(true)) {
            uint32_t a = r.off < 0xA0 ? r.off
                       : r.off < 0xAA ? r.off - 0xA0 + 0xC0
                       : r.off < 0xAF ? r.off - 0xAA + 0xD0
                       : 0xE1;
            Y8960SCC_writeReg(fork, a, r.val);
            SCC_writeReg(plain, a, r.val);
        }
        Stereo s;
        std::vector<int32_t> mono(kFrames);
        for (int i = 0; i < kFrames; ++i) {
            int32_t lr[2];
            Y8960SCC_calcStereo(fork, lr);
            s.l.push_back(lr[0]);
            s.r.push_back(lr[1]);
            mono[i] = SCC_calc(plain);
        }
        Y8960SCC_delete(fork);
        SCC_delete(plain);
        check("a chip that is never reset is centred too",
              rms(mono) > 100.0 && s.l == s.r &&
              std::fabs(rms(s.l) / rms(mono) - kCentreGain) < 0.01);
    }

    Y8960SCC* scc = Y8960SCC_new(kClock, kRate);
    Y8960SCC_set_type(scc, Y8960SCC_STANDARD);
    Y8960SCC_set_quality(scc, 1);
    panAll(1)(scc);
    Y8960SCC_reset(scc);
    activate(false, [&](uint32_t a, uint32_t v) { Y8960SCC_write(scc, a, v); });
    for (const Reg& r : regs) Y8960SCC_write(scc, window(false) + r.off, r.val);
    Stereo afterReset;
    afterReset.l.resize(kFrames);
    afterReset.r.resize(kFrames);
    for (int i = 0; i < kFrames; ++i) {
        int32_t lr[2];
        Y8960SCC_calcStereo(scc, lr);
        afterReset.l[i] = lr[0];
        afterReset.r[i] = lr[1];
    }
    Y8960SCC_delete(scc);
    check("reset brings every position back to the centre", afterReset == center);

    std::vector<int32_t> plain0 = playPlain(false, regs, 0);
    Stereo left0 = playFork(Fork{ false, 0 }, regs, panAll(1));
    check("without the rate converter (quality 0) hard left is still the plain SCC",
          rms(plain0) > 100.0 && left0.l == plain0 && allZero(left0.r));
}

}  // namespace

int main() {
    testMono(false, "SCC");
    testMono(true, "SCC+");
    testPan(false, "SCC");
    testPan(true, "SCC+");
    testEdges();

    std::printf("%s (%d failed)\n", g_fails ? "FAILED" : "PASSED", g_fails);
    return g_fails ? 1 : 0;
}
