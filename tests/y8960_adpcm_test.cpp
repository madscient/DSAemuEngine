// y8960_adpcm_test.cpp
// Y8960 の ADPCM のコア (src/y8960/Y8960Opl2exAdpcm.*) を直接叩く試験。
//   y8960_adpcm_test
// 失敗した項目があれば終了コードが 0 以外になる。
//
// 振る舞いは Y8960 の RTL (hra1129/Y8960_Cartridge v0.9.16 の adpcm_patch) に合わせてある。
// 状態レジスタ、0Fh の読み出し、復号器の中の値は FmEngineApi から見えないので、ここで見る。
// 期待値は、RTL の式に生の値を入れて手で計算したものと、LPF の設計値 (作者の作業履歴の
// 1 kHz 0.0 dB、4 kHz -1.4 dB、8 kHz -28.9 dB) から作る。

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

// ヘッダに extern "C" が無いので、ここで囲む
extern "C" {
#include "Y8960Opl2exAdpcm.h"
}
#include "Y8960Opl2exCore.h"

namespace {

int g_fails = 0;

void check(const std::string& what, bool ok) {
    std::printf("%-72s %s\n", what.c_str(), ok ? "ok" : "FAIL");
    if (!ok) ++g_fails;
}

constexpr uint32_t kClock = 3579545;
constexpr double kNativeRate = kClock / 72.0;

// 07h のビット
constexpr uint8_t kReset = 0x01;
constexpr uint8_t kSpOff = 0x08;
constexpr uint8_t kRepeat = 0x10;
constexpr uint8_t kMemoryData = 0x20;
constexpr uint8_t kRec = 0x40;
constexpr uint8_t kStart = 0x80;

// メモリからの再生。RTL では START だけの 80h は CPU からの直接合成になる
constexpr uint8_t kPlay = kStart | kMemoryData;

// 状態レジスタのビット
constexpr uint8_t kBsy = 0x01;
constexpr uint8_t kBufRdy = 0x08;
constexpr uint8_t kEos = 0x10;

// 一回再生が終わるまでの上限。終わらない変異でも試験が止まらないようにする
constexpr int kRunLimit = 1000;

// 既定は、開始番地 0、停止番地 0003h (16 バイト)、DELTA-N 最大 (ほぼ 1 サンプルに 1 ニブル)
class Adpcm {
public:
    explicit Adpcm(size_t size = 64, uint16_t stop = 0x0003, uint16_t delta = 0xFFFF) : mem_(size) {
        for (size_t i = 0; i < mem_.size(); ++i) mem_[i] = (uint8_t)(0x35 + i * 7);
        region_ = { 0, (uint32_t)mem_.size(), mem_.data(), 1 };
        core_ = Y8960OPL_ADPCM_new(kClock);
        Y8960OPL_ADPCM_setMemoryMap(core_, &region_, 1);
        setRange(0, stop);
        write(0x10, delta & 0xFF);
        write(0x11, delta >> 8);
    }
    ~Adpcm() { Y8960OPL_ADPCM_delete(core_); }
    Adpcm(const Adpcm&) = delete;
    Adpcm& operator=(const Adpcm&) = delete;

    std::vector<uint8_t>& mem() { return mem_; }
    const Y8960OPL_ADPCM& core() const { return *core_; }

    void setRange(uint16_t start, uint16_t stop) {
        write(0x09, start & 0xFF);
        write(0x0A, start >> 8);
        write(0x0B, stop & 0xFF);
        write(0x0C, stop >> 8);
    }
    void write(uint32_t reg, uint32_t val) { Y8960OPL_ADPCM_writeReg(core_, reg, val); }
    uint8_t read() { return Y8960OPL_ADPCM_readData(core_); }
    int16_t calc() { return Y8960OPL_ADPCM_calc(core_); }
    void run(int samples) {
        for (int i = 0; i < samples; ++i) calc();
    }
    uint8_t status() { return Y8960OPL_ADPCM_status(core_); }
    bool bsy() { return (status() & kBsy) != 0; }
    bool eos() { return (status() & kEos) != 0; }
    bool bufRdy() { return (status() & kBufRdy) != 0; }
    void resetStatus() { Y8960OPL_ADPCM_resetStatus(core_); }

    // EOS が立つまで進める。上限まで進んでも立たなければ false
    bool runToEnd() {
        for (int i = 0; i < kRunLimit; ++i) {
            if (eos()) return true;
            calc();
        }
        return eos();
    }

    // 復号器の値が変わるまで進め、変わった値を返す。上限まで変わらなければ 0
    int32_t nextDecoded() {
        const int32_t before = core_->x;
        for (int i = 0; i < kRunLimit; ++i) {
            calc();
            if (core_->x != before) return core_->x;
        }
        return 0;
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
        check("repeating: PCM BSY stays up and EOS rises at the end of each loop", d.bsy() && d.eos());
        d.resetStatus();
        d.run(40);
        check("  (EOS rises again on the next loop)", d.bsy() && d.eos());
    }
    {
        Adpcm d;
        d.write(0x07, kPlay);
        d.runToEnd();
        d.write(0x07, kPlay);
        check("START raises PCM BSY again after the sample ended", d.bsy());
    }
    {
        Adpcm d;
        d.write(0x07, kPlay | kRepeat);
        d.run(10);
        d.resetStatus();
        d.run(200);
        bool before = d.bsy() && d.eos();
        d.resetStatus();
        check("the IRQ reset clears EOS but keeps PCM BSY while playing", before && d.bsy() && !d.eos());
    }
}

// 04h の IRQ リセットはコアの書き込みから ADPCM に届くので、コアを通して確かめる
void testThroughCore() {
    std::vector<uint8_t> mem(64, 0x5A);
    Y8960OPL_ADPCM_REGION region = { 0, (uint32_t)mem.size(), mem.data(), 0 };
    Y8960OPL* opl = Y8960OPL_new(kClock, 49716);
    Y8960OPL_setADPCMMemoryMap(opl, &region, 1);

    check("the status reads 06h after reset (bits 2 and 1 read 1, the flags masked)",
          Y8960OPL_status(opl) == 0x06);

    const uint8_t regs[][2] = {
        { 0x09, 0x00 }, { 0x0A, 0x00 }, { 0x0B, 0x00 }, { 0x0C, 0x00 },
        { 0x10, 0xFF }, { 0x11, 0xFF }, { 0x12, 0xFF }, { 0x07, kPlay | kRepeat },
    };
    for (const auto& r : regs) Y8960OPL_writeReg(opl, r[0], r[1]);
    for (int i = 0; i < 40; ++i) Y8960OPL_calc(opl);
    Y8960OPL_writeReg(opl, 0x04, 0x00);  // EOS と BUF_RDY の覆いを外す
    bool before = (Y8960OPL_status(opl) & (kBsy | kEos)) == (kBsy | kEos);
    Y8960OPL_writeReg(opl, 0x04, 0x80);
    bool after = (Y8960OPL_status(opl) & (kBsy | kEos)) == kBsy;
    check("04h = 80h (IRQ reset) clears EOS and keeps PCM BSY while playing", before && after);

    for (int i = 0; i < 40; ++i) Y8960OPL_calc(opl);
    Y8960OPL_writeReg(opl, 0x04, 0x10);  // EOS を覆う
    Y8960OPL_writeReg(opl, 0x04, 0x00);
    check("writing the EOS mask bit of 04h clears EOS, so unmasking does not bring it back",
          (Y8960OPL_status(opl) & kEos) == 0);

    Y8960OPL_writeIO(opl, 0, 0x08);
    check("reading a register other than 0Fh gives FFh", Y8960OPL_readIO(opl) == 0xFF);
    Y8960OPL_delete(opl);

    // タイマ 1 を 4 サンプルで溢れさせ、覆いのビットを書いてから止め、覆いを外す
    Y8960OPL* t = Y8960OPL_new(kClock, 49716);
    Y8960OPL_writeReg(t, 0x02, 0xFF);
    Y8960OPL_writeReg(t, 0x04, 0x01);
    for (int i = 0; i < 10; ++i) Y8960OPL_calc(t);
    const bool raised = (Y8960OPL_status(t) & 0x40) != 0;
    Y8960OPL_writeReg(t, 0x04, 0x40);
    Y8960OPL_writeReg(t, 0x04, 0x00);
    check("writing the timer 1 mask bit of 04h clears its flag", raised && (Y8960OPL_status(t) & 0x40) == 0);
    Y8960OPL_delete(t);
}

void testDecoder() {
    {
        Adpcm d;
        d.mem()[0] = 0x70;
        d.write(0x07, kPlay);
        const int32_t x1 = d.nextDecoded();
        const int32_t step1 = d.core().step;
        check("nibble 7 from reset adds (15 x 127) >> 3 = 238, step (153 x 127) >> 6 = 303",
              x1 == 238 && step1 == 303);
        const int32_t x2 = d.nextDecoded();
        check("the next nibble 0 adds (1 x 303) >> 3 = 37, step (57 x 303) >> 6 = 269",
              x2 == 275 && d.core().step == 269);
    }
    {
        Adpcm d(64, 0x000F);
        for (auto& b : d.mem()) b = 0x77;
        d.write(0x07, kPlay);
        int32_t last = 0;
        bool rising = true;
        for (int i = 0; i < 60; ++i) {
            d.calc();
            rising = rising && d.core().x >= last;
            last = d.core().x;
        }
        check("the decoder saturates at 32767 instead of wrapping, and the step tops out at 24576",
              rising && last == 32767 && d.core().step == 24576);
    }
    {
        // 開始番地 1 は 4 バイト目。その前のバイトの下位ニブルや、上位ニブルの読み飛ばしなら別の値になる
        Adpcm d;
        d.setRange(1, 3);
        d.mem()[3] = 0x01;
        d.mem()[4] = 0x70;
        d.write(0x07, kPlay);
        check("the first nibble is the upper nibble of byte start x 4", d.nextDecoded() == 238);
    }
}

// 4 バイトの試料 (開始 0、停止 0) は 8 ニブル。7 ニブルを復号し、8 ニブル目で EOS になる。
// 最後のニブルは RTL では復号器に入るが、同じ時に止まるので出力に出ない
void testRange() {
    Adpcm d(64, 0x0000);
    d.write(0x07, kPlay);
    int decodes = 0;
    int32_t last = 0;
    for (int i = 0; i < kRunLimit && !d.eos(); ++i) {
        d.calc();
        if (d.core().x != last && d.core().x != 0) ++decodes;
        last = d.core().x;
    }
    check("a 4 byte sample decodes 7 nibbles and the 8th step raises EOS", d.eos() && decodes == 7);

    Adpcm r(64, 0x0000);
    r.mem()[0] = 0x70;
    r.write(0x07, kPlay | kRepeat);
    const int32_t first = r.nextDecoded();
    r.runToEnd();
    const bool cleared = r.core().x == 0;
    check("REPEAT starts over with a cleared decoder", first == 238 && cleared && r.nextDecoded() == 238);

    // EOS は終わりの印が 0 から 1 になるときだけ立つ。印は番地が進むと 0 に戻るので、一度
    // 終わった後に開始番地を停止番地より後ろにすると、EOS が立たず START (PCM BSY) も残る
    Adpcm e(64, 0x0000);
    e.write(0x07, kPlay);
    const bool ended = e.runToEnd() && !e.bsy();
    e.resetStatus();
    e.setRange(2, 1);
    e.write(0x07, kPlay);
    e.run(50);
    check("after a sample ended, start past stop raises no EOS and leaves PCM BSY up",
          ended && !e.eos() && e.bsy());
}

// --- 出力 ---

// ADPCM の符号器。復号の式は RTL と同じ。試験の信号を作るだけなので、符号化の良し悪しは問わない
struct Encoder {
    int32_t x = 0, step = 127;
    std::vector<int32_t> decoded;
    uint32_t encode(int32_t target) {
        static const int32_t F[] = { 57, 57, 57, 57, 77, 102, 128, 153 };
        const int32_t diff = target - x;
        const uint32_t sign = diff < 0 ? 8 : 0;
        int32_t k = (int32_t)std::lround(((std::abs(diff) * 8.0) / step - 1.0) / 2.0);
        k = k < 0 ? 0 : k > 7 ? 7 : k;
        const int32_t d = ((k * 2 + 1) * step) >> 3;
        x = sign ? x - d : x + d;
        x = x < -32768 ? -32768 : x > 32767 ? 32767 : x;
        step = (step * F[k]) >> 6;
        step = step < 127 ? 127 : step > 24576 ? 24576 : step;
        decoded.push_back(x);
        return sign | (uint32_t)k;
    }
};

// ハン窓を掛けた DFT で、f の成分の振幅を求める
double amplitude(const std::vector<double>& v, double f, double rate) {
    const double pi = 3.14159265358979323846;
    double re = 0.0, im = 0.0, wsum = 0.0;
    for (size_t n = 0; n < v.size(); ++n) {
        const double w = 0.5 - 0.5 * std::cos(2.0 * pi * n / (v.size() - 1));
        re += w * v[n] * std::cos(2.0 * pi * f * n / rate);
        im -= w * v[n] * std::sin(2.0 * pi * f * n / rate);
        wsum += w;
    }
    return 2.0 * std::sqrt(re * re + im * im) / wsum;
}

// f のサイン波を ADPCM にして、ADPCM のレート (DELTA-N で決まる) で鳴らし、
// 出力の f の成分と、復号した値の f の成分の比を dB で返す。出力は 12h = 255 の倍率
// (255 / 256) と、FM に対する水準 (1 / 8) を掛けた値なので、その分を戻して比べる
double response(double f, uint16_t delta) {
    const double adpcmRate = kNativeRate * delta / 65536.0;
    const int nibbles = 8192;
    Encoder enc;
    std::vector<uint8_t> bytes(nibbles / 2);
    for (int i = 0; i < nibbles; ++i) {
        const int32_t s = (int32_t)std::lround(20000.0 * std::sin(2.0 * 3.14159265358979323846 * f * i / adpcmRate));
        const uint32_t nib = enc.encode(s);
        bytes[i / 2] |= (uint8_t)((i & 1) ? nib : nib << 4);
    }
    Adpcm d(bytes.size(), (uint16_t)(bytes.size() / 4 - 1), delta);
    std::copy(bytes.begin(), bytes.end(), d.mem().begin());
    d.write(0x07, kPlay);
    const int samples = (int)(nibbles * 65536.0 / delta) - 64;
    std::vector<double> out;
    for (int i = 0; i < samples; ++i) {
        const int16_t s = d.calc();
        if (i >= samples / 4) out.push_back(s * 8.0 * 256.0 / 255.0);
    }
    std::vector<double> in(enc.decoded.begin() + nibbles / 4, enc.decoded.end());
    return 20.0 * std::log10(amplitude(out, f, kNativeRate) / amplitude(in, f, adpcmRate));
}

void testOutput() {
    {
        Adpcm d;
        Adpcm quiet;
        quiet.write(0x12, 0x00);
        d.write(0x07, kPlay);
        quiet.write(0x07, kPlay);
        double energy = 0.0, quietEnergy = 0.0;
        for (int i = 0; i < 30; ++i) {
            energy += std::fabs((double)d.calc());
            quietEnergy += std::fabs((double)quiet.calc());
        }
        check("12h is 255 after reset: a sample plays without writing 12h", energy > 0.0 && quietEnergy == 0.0);
    }
    {
        // 復号器が 32767 に張り付いた直流: 12h = 255 で (32767 x 255) >> 8 = 32639、LPF の直流の
        // 倍率は 1。RTL は FM (演算子 ±4095) に ADPCM >> 2 を足し、ここの FM はその半分の幅
        // なので、出力は 32639 >> 3 = 4079 (旋律 1 チャンネルの最大 2047 の約 2 倍)
        Adpcm d(64, 0x000F);
        for (auto& b : d.mem()) b = 0x77;
        d.write(0x07, kPlay);
        d.run(80);
        check("full scale ADPCM puts out 32639 >> 3 = 4079, twice a melody channel", d.calc() == 4079);
    }
    {
        Adpcm d(64, 0x000F);
        d.write(0x07, kPlay | kSpOff);
        bool zero = true;
        for (int i = 0; i < 100; ++i) zero = zero && d.calc() == 0;
        check("SP-OFF mutes the output and keeps playing", zero && d.bsy());
    }
    {
        // 止めた直後は LPF に残ったサンプルが尾を引き、25 サンプルで抜けて 0 になる
        Adpcm d(64, 0x000F);
        d.write(0x07, kPlay);
        d.run(60);
        d.write(0x07, 0x00);
        bool tail = false, zero = true;
        for (int i = 1; i <= 60; ++i) {
            const int16_t s = d.calc();
            if (i <= 6) tail = tail || s != 0;
            if (i >= 25) zero = zero && s == 0;
        }
        check("after a stop the filter tails off and the output is 0 from the 25th sample", tail && zero);
    }
    const double r1k = response(1000.0, 0xFFFF);
    const double r8k = response(8000.0, 0xFFFF);
    std::printf("  response at DELTA-N FFFFh: 1 kHz %.2f dB, 8 kHz %.2f dB\n", r1k, r8k);
    check("the low pass filter: 1 kHz passes at 0 dB", std::fabs(r1k) < 0.3);
    check("the low pass filter: 8 kHz is down 28.9 dB", std::fabs(r8k + 28.9) < 1.5);
    // DELTA-N 4000h では 1 ニブルが 4 サンプル。4 倍の線形補間の応答は
    // (sin(4x) / (4 sin x))^2、x = pi f / fs で、4 kHz では -2.88 dB。LPF の -1.4 dB と
    // 合わせて約 -4.3 dB。補間せずに直前の値を保つなら -1.44 dB で、合わせて約 -2.9 dB になる
    const double r4k = response(4000.0, 0x4000);
    std::printf("  response at DELTA-N 4000h: 4 kHz %.2f dB\n", r4k);
    check("DELTA-N 4000h: 4 kHz comes out at -4.3 dB (linear interpolation and the filter)",
          std::fabs(r4k + 4.3) < 0.5);
}

// --- 08h ---

void test08h() {
    {
        // 64K でも開始番地は 4 バイト単位。32 バイト単位なら 32 バイト目を読む
        Adpcm d;
        d.mem()[4] = 0x70;
        d.mem()[32] = 0x10;
        d.write(0x08, 0x02);
        d.setRange(1, 3);
        d.write(0x07, kPlay);
        check("64K: the start address still counts 4 bytes", d.nextDecoded() == 238);
    }
    {
        // 64K では番地の 16 ビットより上を捨てる。開始番地 4000h は 10000h バイト目で、0 バイト目を読む
        Adpcm d(0x10010, 0x4003);
        d.mem()[0x00000] = 0x70;
        d.mem()[0x10000] = 0x10;
        d.write(0x08, 0x02);
        d.setRange(0x4000, 0x4003);
        d.write(0x07, kPlay);
        const int32_t with64k = d.nextDecoded();
        Adpcm e(0x10010, 0x4003);
        e.mem()[0x00000] = 0x70;
        e.mem()[0x10000] = 0x10;
        e.setRange(0x4000, 0x4003);
        e.write(0x07, kPlay);
        check("64K: the memory address wraps at 64KB", with64k == 238 && e.nextDecoded() == 47);
    }
    {
        // CPU からの書き込みも 4 バイト単位。32 バイト単位なら 32 バイト目に入る
        Adpcm d;
        d.write(0x08, 0x02);
        d.setRange(1, 1);
        d.write(0x07, kRec | kMemoryData);
        d.write(0x0F, 0xC3);
        check("64K: 0Fh writes count 4 bytes too", d.mem()[4] == 0xC3 && d.mem()[32] != 0xC3);
    }
    {
        Adpcm d;
        d.mem()[0] = 0x70;
        d.write(0x08, 0x01);
        d.write(0x07, kPlay);
        check("ROM: playback reads 0 (nibble 0 adds 15)", d.nextDecoded() == 15);
    }
    {
        Adpcm d;
        const std::vector<uint8_t> before = d.mem();
        d.write(0x08, 0x01);
        d.write(0x07, kRec | kMemoryData);
        for (int i = 0; i < 8; ++i) d.write(0x0F, 0xAA);
        d.write(0x07, kMemoryData);
        d.read();
        d.read();
        check("ROM: 0Fh writes do not reach the memory, and reads give 0", d.mem() == before && d.read() == 0);
    }
}

// --- CPU からの直接合成 ---

void testCpuSynthesis() {
    Adpcm d;
    d.mem()[0] = 0x10;  // メモリを読めば 47 になる
    d.write(0x07, kStart);
    d.write(0x0F, 0x70);
    check("07h = 80h: BUF_RDY drops when 0Fh is written", !d.bufRdy());
    check("07h = 80h decodes the byte written to 0Fh, not the sample memory", d.nextDecoded() == 238);
    check("  BUF_RDY rises once the upper nibble is decoded", d.bufRdy());
    const int32_t afterLower = d.nextDecoded();
    check("  the lower nibble of the same byte follows (0 adds 37)", afterLower == 275);
    const int32_t again = d.nextDecoded();
    d.run(200);
    check("  without a new byte the last one is used again, and there is no EOS",
          again > afterLower && !d.eos() && d.bsy());
}

// --- メモリの読み書き ---

void testMemoryAccess() {
    {
        // 開始 0、停止 1 は 0-7 バイト目。8 バイト目を書くと EOS で、9 バイト目は 0 バイト目に入る
        Adpcm d;
        d.setRange(0, 1);
        d.write(0x07, kRec | kMemoryData);
        for (int i = 0; i < 8; ++i) d.write(0x0F, (uint8_t)(0xA0 + i));
        const bool eosAt8 = d.eos();
        d.write(0x0F, 0xB0);
        bool filled = d.mem()[0] == 0xB0 && d.mem()[8] == (uint8_t)(0x35 + 8 * 7);
        for (int i = 1; i < 8; ++i) filled = filled && d.mem()[i] == (uint8_t)(0xA0 + i);
        check("60h: 0Fh fills start x 4 .. stop x 4 + 3, then EOS and back to the start", eosAt8 && filled);
    }
    {
        Adpcm d;
        d.setRange(1, 1);
        d.write(0x0F, 0x5C);
        d.write(0x07, kMemoryData);
        const uint8_t dummy1 = d.read(), dummy2 = d.read();
        bool bytes = true;
        for (int i = 4; i < 8; ++i) bytes = bytes && d.read() == d.mem()[i];
        const bool noEos = !d.eos();
        const uint8_t past = d.read();
        check("20h: two dummy reads give the last 0Fh value, then the bytes from start x 4",
              dummy1 == 0x5C && dummy2 == 0x5C && bytes && noEos);
        check("20h: past stop x 4 + 3 a read gives 0 and raises EOS", past == 0 && d.eos());
    }
}

// 再生中に START を書き直すと開始番地からやり直すが、復号器は消さず、戻る刻みで元の位置の
// ニブルをもう 1 つ復号する (RTL のとおり)。RESET を挟めば消える
void testRestart() {
    // 7, 0, 1 と復号したところ (x = 375、刻み 239) で START を書き直す。
    // 戻る刻みで元の位置の 0 を復号して (1 x 239) >> 3 = 29 を足し 404、刻み (57 x 239) >> 6 = 212。
    // 次に開始番地の 7 で (15 x 212) >> 3 = 397 を足して 801
    Adpcm d(64, 0x000F);
    for (auto& b : d.mem()) b = 0x10;
    d.mem()[0] = 0x70;
    d.write(0x07, kPlay);
    d.run(5);
    const int32_t x = d.core().x;
    d.write(0x07, kPlay);
    const int32_t back = d.nextDecoded();
    const int32_t first = d.nextDecoded();
    check("START while playing: one more nibble from the old place, then the start, decoder kept",
          x == 375 && back == 404 && first == 801);

    // RESET と START を同じサンプルのうちに書くと、DELTA-N の位相は次のサンプルまで残る。
    // DELTA-N 4000h、6 サンプル鳴らした後は位相 8000h で桁上がり無し。次の 2 サンプル目で
    // 桁上がりし、3 サンプル目で開始番地に戻り、そこから 4 サンプル目 (書いてから 7 サンプル目)
    // に最初のニブル (7 で 238) を復号する
    {
        Adpcm p(64, 0x000F, 0x4000);
        p.mem()[0] = 0x70;
        p.write(0x07, kPlay);
        p.run(6);
        p.write(0x07, kReset);
        p.write(0x07, kPlay);
        int at = 0;
        for (int i = 1; i <= 20 && !at; ++i) {
            p.calc();
            if (p.core().x != 0) at = i;
        }
        check("RESET and START in one sample: the restart waits for the old DELTA-N phase",
              at == 7 && p.core().x == 238);
    }
    // 間にサンプルを挟まずに書いても、START が落ちた時点で止まる
    for (uint8_t stop : { kReset, (uint8_t)0x00 }) {
        Adpcm r(64, 0x000F);
        for (auto& b : r.mem()) b = 0x77;
        r.write(0x07, kPlay);
        r.run(5);
        r.write(0x07, stop);
        r.write(0x07, kPlay);
        check(std::string(stop == kReset ? "RESET" : "07h = 00h") + " then START starts from a cleared decoder",
              r.nextDecoded() == 238);
    }
}

}  // namespace

int main() {
    testBsy();
    testThroughCore();
    testDecoder();
    testRange();
    testOutput();
    test08h();
    testCpuSynthesis();
    testMemoryAccess();
    testRestart();

    std::printf("%s (%d failed)\n", g_fails ? "FAILED" : "PASSED", g_fails);
    return g_fails ? 1 : 0;
}
