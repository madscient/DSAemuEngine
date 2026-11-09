// y8960_adpcm_test.cpp
// Y8960 の ADPCM のコア (src/y8960/Y8960Opl2exAdpcm.*) を直接叩く試験。
//   y8960_adpcm_test
// 失敗した項目があれば終了コードが 0 以外になる。
//
// 状態レジスタの PCM BSY は FmEngineApi から読めないので、api_test では確かめられない。
// Y8960 の RTL は PCM BSY を START のビットそのものとして扱い、一回再生の終わり、
// START の無い 07h の書き込み、RESET (START と同時でも) で 0 に戻す。実機の Y8950 とは違う。

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

// ヘッダに extern "C" が無いので、ここで囲む
extern "C" {
#include "Y8960Opl2exAdpcm.h"
}

namespace {

int g_fails = 0;

void check(const std::string& what, bool ok) {
    std::printf("%-72s %s\n", what.c_str(), ok ? "ok" : "FAIL");
    if (!ok) ++g_fails;
}

constexpr uint32_t kClock = 3579545;

// 07h のビット
constexpr uint8_t kReset = 0x01;
constexpr uint8_t kRepeat = 0x10;
constexpr uint8_t kMemoryData = 0x20;
constexpr uint8_t kStart = 0x80;

// メモリからの再生。RTL では START だけの 80h は CPU からの直接合成になる
constexpr uint8_t kPlay = kStart | kMemoryData;

// 状態レジスタのビット
constexpr uint8_t kBsy = 0x01;
constexpr uint8_t kEos = 0x10;

// 一回再生が終わるまでの上限。終わらない変異でも試験が止まらないようにする
constexpr int kRunLimit = 1000;

// 開始番地 0、停止番地 0003h (4 バイト単位で 15 バイト目まで)、DELTA-N 最大で、
// 1 サンプルにつき 1 ニブル進む
class Adpcm {
public:
    Adpcm() : mem_(64) {
        for (size_t i = 0; i < mem_.size(); ++i) mem_[i] = (uint8_t)(0x35 + i * 7);
        region_ = { 0, (uint32_t)mem_.size(), mem_.data(), 0 };
        core_ = Y8960OPL_ADPCM_new(kClock);
        Y8960OPL_ADPCM_setMemoryMap(core_, &region_, 1);
        write(0x09, 0x00);
        write(0x0A, 0x00);
        write(0x0B, 0x03);
        write(0x0C, 0x00);
        write(0x10, 0xFF);
        write(0x11, 0xFF);
        write(0x12, 0xFF);
    }
    ~Adpcm() { Y8960OPL_ADPCM_delete(core_); }
    Adpcm(const Adpcm&) = delete;
    Adpcm& operator=(const Adpcm&) = delete;

    void write(uint32_t reg, uint32_t val) { Y8960OPL_ADPCM_writeReg(core_, reg, val); }
    void run(int samples) {
        for (int i = 0; i < samples; ++i) Y8960OPL_ADPCM_calc(core_);
    }
    bool bsy() { return (Y8960OPL_ADPCM_status(core_) & kBsy) != 0; }
    bool eos() { return (Y8960OPL_ADPCM_status(core_) & kEos) != 0; }

    // EOS が立つまで進める。上限まで進んでも立たなければ false
    bool runToEnd() {
        for (int i = 0; i < kRunLimit; ++i) {
            if (eos()) return true;
            Y8960OPL_ADPCM_calc(core_);
        }
        return eos();
    }

private:
    std::vector<uint8_t> mem_;
    Y8960OPL_ADPCM_REGION region_;
    Y8960OPL_ADPCM* core_;
};

void testBsy() {
    {
        Adpcm d;
        check("PCM BSY is 0 after reset", !d.bsy());
    }
    {
        Adpcm d;
        d.write(0x07, kPlay);
        check("START raises PCM BSY", d.bsy());
        d.run(10);
        check("PCM BSY stays up while the sample plays", d.bsy() && !d.eos());
    }
    {
        Adpcm d;
        d.write(0x07, kPlay);
        bool ended = d.runToEnd();
        check("PCM BSY drops when a one-shot sample ends", ended && !d.bsy() && d.eos());
    }
    {
        Adpcm d;
        d.write(0x07, kPlay);
        d.run(5);
        d.write(0x07, 0x00);
        check("07h without START drops PCM BSY while playing", !d.bsy() && !d.eos());
    }
    {
        Adpcm d;
        d.write(0x07, kPlay);
        d.run(5);
        d.write(0x07, kReset);
        check("RESET alone drops PCM BSY while playing", !d.bsy() && !d.eos());
    }
    {
        Adpcm d;
        d.write(0x07, kPlay);
        d.run(5);
        d.write(0x07, kStart | kReset);
        check("RESET wins over START: 07h = 81h drops PCM BSY", !d.bsy() && !d.eos());
    }
    {
        Adpcm d;
        d.write(0x07, kStart | kReset);
        check("07h = 81h does not raise PCM BSY while stopped", !d.bsy());
    }
    {
        Adpcm d;
        d.write(0x07, kPlay | kRepeat);
        d.run(200);
        check("PCM BSY stays up through the end address while repeating", d.bsy() && !d.eos());
    }
    {
        Adpcm d;
        d.write(0x07, kPlay);
        d.runToEnd();
        d.write(0x07, kPlay);
        check("START raises PCM BSY again after the sample ended", d.bsy());
    }
}

}  // namespace

int main() {
    testBsy();

    std::printf("%s (%d failed)\n", g_fails ? "FAILED" : "PASSED", g_fails);
    return g_fails ? 1 : 0;
}
