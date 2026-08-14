// DSAemuEngine.cpp
// FmEngineApi 準拠エミュレーションエンジン
// 統合コア:
//   emu2149  (YM2149/AY-3-8910 PSG)  → SSG チップ
//   emu2413  (YM2413 OPLL)            → OPLL / OPLLP / OPLLX / VRC7 チップ
//   emu8950  (Y8950/YM3526/YM3812)   → Y8950 / OPL / OPL2 チップ
//   emu2212  (Konami SCC)             → SCC / SCCP チップ
//   emu76489 (SN76489 DCSG)          → DCSG チップ

#include "FmEngineApi.h"
#include "../extern/emu2149/emu2149.h"
#include "../extern/emu2413/emu2413.h"
#include "../extern/emu8950/emu8950.h"
#include "../extern/emu2212/emu2212.h"
#include "../extern/emu76489/emu76489.h"

#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <algorithm>

// =========================================================
//  定数
// =========================================================
static constexpr float kOutputScale = 1.0f / 32768.0f;

// =========================================================
//  チップ種別列挙
// =========================================================
enum class ChipKind {
    SSG,    // emu2149  YM2149
    OPLL,   // emu2413  YM2413 (2413 ROM patch)
    OPLLP,  // emu2413  YM2413 (281B patch)
    OPLLX,  // emu2413  YM2413 (2413 ROM patch, eXtended = identical to OPLL here)
    VRC7,   // emu2413  VRC7
    Y8950,  // emu8950  Y8950  (type=0)
    OPL,    // emu8950  YM3526 (type=1)
    OPL2,   // emu8950  YM3812 (type=2)
    SCC,    // emu2212  Konami SCC
    SCCP,   // emu2212  Konami SCC-I (SCC+)
    DCSG,   // emu76489 SN76489
};

// =========================================================
//  チップエントリ (ポリモーフィズムを使わず union で管理)
// =========================================================
struct ChipEntry {
    ChipKind  kind;
    std::string name;
    uint32_t  sample_rate;  // エンジンのサンプルレート
    uint32_t  clock;        // マスタークロック

    // 各コアへのポインタ (使用するのは kind に応じた1つのみ)
    PSG*   psg   = nullptr;
    OPLL*  opll  = nullptr;
    OPL*   opl   = nullptr;
    SCC*   scc   = nullptr;
    SNG*   sng   = nullptr;

    float gain_l = 1.0f;
    float gain_r = 1.0f;

    ~ChipEntry() {
        if (psg)  { PSG_delete(psg);  psg  = nullptr; }
        if (opll) { OPLL_delete(opll); opll = nullptr; }
        if (opl)  { OPL_delete(opl);  opl  = nullptr; }
        if (scc)  { SCC_delete(scc);  scc  = nullptr; }
        if (sng)  { SNG_delete(sng);  sng  = nullptr; }
    }
};

// =========================================================
//  エンジン本体
// =========================================================
struct FmEngineOpaque {
    uint32_t sample_rate;
    std::vector<std::unique_ptr<ChipEntry>> chips;
    std::mutex write_mutex;
};

// =========================================================
//  対応チップテーブル
// =========================================================
struct ChipDesc {
    const char* name;
    ChipKind    kind;
    uint32_t    default_clock;
};

static const ChipDesc kChipTable[] = {
    { "SSG",   ChipKind::SSG,   2000000  },  // AY-3-8910 / YM2149
    { "OPLL",  ChipKind::OPLL,  3579545  },  // YM2413
    { "OPLLP", ChipKind::OPLLP, 3579545  },  // YM2413 (281B patches)
    { "OPLLX", ChipKind::OPLLX, 3579545  },  // YM2413 (extended alias)
    { "VRC7",  ChipKind::VRC7,  3579545  },  // VRC7
    { "Y8950", ChipKind::Y8950, 3579545  },  // Y8950
    { "OPL",   ChipKind::OPL,   3579545  },  // YM3526
    { "OPL2",  ChipKind::OPL2,  3579545  },  // YM3812
    { "SCC",   ChipKind::SCC,   3579545  },  // Konami SCC
    { "SCCP",  ChipKind::SCCP,  3579545  },  // Konami SCC-I (SCC+)
    { "DCSG",  ChipKind::DCSG,  3579545  },  // SN76489
};
static constexpr uint32_t kChipCount = (uint32_t)(sizeof(kChipTable) / sizeof(kChipTable[0]));

static const ChipDesc* findChipDesc(const char* name) {
    if (!name) return nullptr;
    for (uint32_t i = 0; i < kChipCount; ++i)
        if (strcmp(kChipTable[i].name, name) == 0)
            return &kChipTable[i];
    return nullptr;
}

// =========================================================
//  ネイティブレート取得ヘルパー
// =========================================================
static uint32_t nativeRate(const ChipEntry& c) {
    switch (c.kind) {
    case ChipKind::SSG:
        // YM2149: clk / 8 (internal div) then /1 for output
        // PSG_new で rate を sample_rate に設定しているので
        // 実際のネイティブはclk/8; ただしクロックが2MHzなら250000
        return c.clock / 8;
    case ChipKind::OPLL:
    case ChipKind::OPLLP:
    case ChipKind::OPLLX:
    case ChipKind::VRC7:
        return c.clock / 72;
    case ChipKind::Y8950:
    case ChipKind::OPL:
    case ChipKind::OPL2:
        return c.clock / 72;
    case ChipKind::SCC:
    case ChipKind::SCCP:
        // emu2212 の内部ステップは clk/2 (sccstep)。
        // 発音周波数 clk/(32*(N+1)) は 1波形サンプルあたり16ステップで導かれる
        return c.clock / 2;
    case ChipKind::DCSG:
        // SN76489: clk / 16 ≈ 223722 at 3.58MHz
        return c.clock / 16;
    }
    return c.sample_rate;
}

// =========================================================
//  チップ生成
// =========================================================
static std::unique_ptr<ChipEntry> createChip(
    const ChipDesc& desc, uint32_t clock, uint32_t sample_rate)
{
    auto e = std::make_unique<ChipEntry>();
    e->kind        = desc.kind;
    e->name        = desc.name;
    e->sample_rate = sample_rate;
    e->clock       = (clock != 0) ? clock : desc.default_clock;

    switch (desc.kind) {
    case ChipKind::SSG:
        e->psg = PSG_new(e->clock, sample_rate);
        if (!e->psg) return nullptr;
        PSG_setQuality(e->psg, 1);
        PSG_reset(e->psg);
        // 全チャンネルミュート状態(reg7=0x3F)にリセット後は
        // ユーザーの init で制御させる
        break;

    case ChipKind::OPLL:
    case ChipKind::OPLLX:
        e->opll = OPLL_new(e->clock, sample_rate);
        if (!e->opll) return nullptr;
        OPLL_setChipType(e->opll, 0);
        OPLL_resetPatch(e->opll, OPLL_2413_TONE);
        OPLL_reset(e->opll);
        break;

    case ChipKind::OPLLP:
        e->opll = OPLL_new(e->clock, sample_rate);
        if (!e->opll) return nullptr;
        OPLL_setChipType(e->opll, 0);
        OPLL_resetPatch(e->opll, OPLL_281B_TONE);
        OPLL_reset(e->opll);
        break;

    case ChipKind::VRC7:
        e->opll = OPLL_new(e->clock, sample_rate);
        if (!e->opll) return nullptr;
        OPLL_setChipType(e->opll, 1);
        OPLL_resetPatch(e->opll, OPLL_VRC7_TONE);
        OPLL_reset(e->opll);
        break;

    case ChipKind::Y8950:
        e->opl = OPL_new(e->clock, sample_rate);
        if (!e->opl) return nullptr;
        OPL_setChipType(e->opl, 0); // Y8950
        OPL_reset(e->opl);
        break;

    case ChipKind::OPL:
        e->opl = OPL_new(e->clock, sample_rate);
        if (!e->opl) return nullptr;
        OPL_setChipType(e->opl, 1); // YM3526
        OPL_reset(e->opl);
        break;

    case ChipKind::OPL2:
        e->opl = OPL_new(e->clock, sample_rate);
        if (!e->opl) return nullptr;
        OPL_setChipType(e->opl, 2); // YM3812
        OPL_reset(e->opl);
        break;

    case ChipKind::SCC:
    case ChipKind::SCCP:
        e->scc = SCC_new(e->clock, sample_rate);
        if (!e->scc) return nullptr;
        SCC_set_type(e->scc, desc.kind == ChipKind::SCCP ? SCC_ENHANCED : SCC_STANDARD);
        SCC_set_quality(e->scc, 1);
        SCC_reset(e->scc);
        // mode ($E0) は実機ではマッパー側 (0xBFFE/0xB000) の設定であり
        // 音源レジスタ窓の外にあるため、チップ種別として生成時に固定する
        if (desc.kind == ChipKind::SCCP)
            SCC_writeReg(e->scc, 0xE0, 1);
        break;

    case ChipKind::DCSG:
        e->sng = SNG_new(e->clock, sample_rate);
        if (!e->sng) return nullptr;
        SNG_set_quality(e->sng, 1);
        SNG_reset(e->sng);
        break;
    }

    return e;
}

// =========================================================
//  レジスタ書き込みヘルパー
//  DCSG は port/reg を無視し val をシリアルバイトとして送る
// =========================================================
static void chipWrite(ChipEntry& c, uint8_t reg, uint8_t val, uint32_t port) {
    (void)port;
    switch (c.kind) {
    case ChipKind::SSG:
        PSG_writeReg(c.psg, reg, val);
        break;
    case ChipKind::OPLL:
    case ChipKind::OPLLP:
    case ChipKind::OPLLX:
    case ChipKind::VRC7:
        OPLL_writeReg(c.opll, reg, val);
        break;
    case ChipKind::Y8950:
    case ChipKind::OPL:
    case ChipKind::OPL2:
        OPL_writeReg(c.opl, reg, val);
        break;
    case ChipKind::SCC:
    case ChipKind::SCCP:
        // SCC_write() は Z80 メモリ空間の絶対アドレスを取り base_adr/active に依存するため、
        // レジスタ指向 API には内部レジスタマップ直叩きの writeReg を使う
        // $00-9F:wave $C0-C9:freq $D0-D4:volume $E1:ch enable $E2:deformation
        // 実機のレジスタ窓に存在しないものは受け付けない:
        //   $E0     … emu2212 の合成レジスタ (mode はチップ種別で固定)
        //   $80-$9F … CH4 独立波形は SCC-I のみ。SCC では CH3 と共有
        if (reg == 0xE0) break;
        if (c.kind == ChipKind::SCC && 0x80 <= reg && reg <= 0x9F) break;
        SCC_writeReg(c.scc, reg, val);
        break;
    case ChipKind::DCSG:
        // SN76489はシリアルバイト形式 (reg は無視, val をそのまま writeIO)
        SNG_writeIO(c.sng, val);
        break;
    }
}

// =========================================================
//  1サンプル生成 (ステレオ, float)
// =========================================================
static void chipCalcStereo(ChipEntry& c, float& out_l, float& out_r) {
    int32_t buf[2] = {0, 0};
    switch (c.kind) {
    case ChipKind::SSG: {
        int16_t mono = PSG_calc(c.psg);
        buf[0] = buf[1] = (int32_t)mono;
        break;
    }
    case ChipKind::OPLL:
    case ChipKind::OPLLP:
    case ChipKind::OPLLX:
    case ChipKind::VRC7:
        OPLL_calcStereo(c.opll, buf);
        break;
    case ChipKind::Y8950:
    case ChipKind::OPL:
    case ChipKind::OPL2:
        OPL_calcStereo(c.opl, buf);
        break;
    case ChipKind::SCC:
    case ChipKind::SCCP: {
        int16_t mono = SCC_calc(c.scc);
        buf[0] = buf[1] = (int32_t)mono;
        break;
    }
    case ChipKind::DCSG: {
        int32_t sng_buf[2] = {0, 0};
        SNG_calc_stereo(c.sng, sng_buf);
        buf[0] = sng_buf[0];
        buf[1] = sng_buf[1];
        break;
    }
    }
    out_l += (float)buf[0] * kOutputScale * c.gain_l;
    out_r += (float)buf[1] * kOutputScale * c.gain_r;
}

// =========================================================
//  C API 実装
// =========================================================
extern "C" {

FMENGINE_API FmEngineHandle FMENGINE_CALL FmEngine_Create(uint32_t sample_rate) {
    if (sample_rate == 0) sample_rate = 48000;
    auto* eng = new(std::nothrow) FmEngineOpaque();
    if (!eng) return nullptr;
    eng->sample_rate = sample_rate;
    return eng;
}

FMENGINE_API void FMENGINE_CALL FmEngine_Destroy(FmEngineHandle engine) {
    delete engine;
}

FMENGINE_API uint32_t FMENGINE_CALL FmEngine_Inquiry(FmEngineHandle /*engine*/) {
    return kChipCount;
}

FMENGINE_API const char* FMENGINE_CALL FmEngine_GetSupportedChip(
    FmEngineHandle /*engine*/, uint32_t index)
{
    if (index >= kChipCount) return nullptr;
    return kChipTable[index].name;
}

FMENGINE_API FmResult FMENGINE_CALL FmEngine_AddChip(
    FmEngineHandle engine, const char* name, uint32_t clock, uint32_t* out_id)
{
    if (!engine || !name) return FM_ERR_INVALID_ARG;
    const ChipDesc* desc = findChipDesc(name);
    if (!desc) return FM_ERR_UNKNOWN_CHIP;

    auto chip = createChip(*desc, clock, engine->sample_rate);
    if (!chip) return FM_ERR_ALLOC;

    if (out_id) *out_id = (uint32_t)engine->chips.size();
    engine->chips.push_back(std::move(chip));
    return FM_OK;
}

FMENGINE_API const char* FMENGINE_CALL FmEngine_GetChipName(
    FmEngineHandle engine, uint32_t chip_id)
{
    if (!engine || chip_id >= engine->chips.size()) return nullptr;
    return engine->chips[chip_id]->name.c_str();
}

FMENGINE_API uint32_t FMENGINE_CALL FmEngine_GetNativeRate(
    FmEngineHandle engine, uint32_t chip_id)
{
    if (!engine || chip_id >= engine->chips.size()) return 0;
    return nativeRate(*engine->chips[chip_id]);
}

FMENGINE_API uint32_t FMENGINE_CALL FmEngine_GetSampleRate(FmEngineHandle engine) {
    if (!engine) return 0;
    return engine->sample_rate;
}

FMENGINE_API FmResult FMENGINE_CALL FmEngine_Write(
    FmEngineHandle engine, uint32_t chip_id,
    uint8_t reg, uint8_t value, uint32_t port)
{
    if (!engine || chip_id >= engine->chips.size()) return FM_ERR_INVALID_ARG;
    std::lock_guard<std::mutex> lock(engine->write_mutex);
    chipWrite(*engine->chips[chip_id], reg, value, port);
    return FM_OK;
}

FMENGINE_API FmResult FMENGINE_CALL FmEngine_SetGain(
    FmEngineHandle engine, uint32_t chip_id, float gain_l, float gain_r)
{
    if (!engine || chip_id >= engine->chips.size()) return FM_ERR_INVALID_ARG;
    engine->chips[chip_id]->gain_l = gain_l;
    engine->chips[chip_id]->gain_r = gain_r;
    return FM_OK;
}

FMENGINE_API FmResult FMENGINE_CALL FmEngine_GetGain(
    FmEngineHandle engine, uint32_t chip_id,
    float* out_gain_l, float* out_gain_r)
{
    if (!engine || chip_id >= engine->chips.size()) return FM_ERR_INVALID_ARG;
    if (out_gain_l) *out_gain_l = engine->chips[chip_id]->gain_l;
    if (out_gain_r) *out_gain_r = engine->chips[chip_id]->gain_r;
    return FM_OK;
}

FMENGINE_API FmResult FMENGINE_CALL FmEngine_SetMemory(
    FmEngineHandle engine, uint32_t chip_id,
    FmMemoryType mem_type, const uint8_t* data, uint32_t size)
{
    if (!engine || chip_id >= engine->chips.size()) return FM_ERR_INVALID_ARG;
    auto& c = *engine->chips[chip_id];

    // Y8950 は ADPCM-B RAM に対応
    if ((c.kind == ChipKind::Y8950) && c.opl) {
        if (mem_type == FM_MEM_ADPCM_B) {
            OPL_writeADPCMData(c.opl, 0, 0, size, data);
            return FM_OK;
        }
    }
    // 他のチップは外部メモリ不要
    return FM_ERR_UNAVAILABLE;
}

FMENGINE_API uint32_t FMENGINE_CALL FmEngine_GetMemorySize(
    FmEngineHandle engine, uint32_t chip_id, FmMemoryType /*mem_type*/)
{
    if (!engine || chip_id >= engine->chips.size()) return 0;
    return 0; // 本実装では動的追跡なし
}

FMENGINE_API FmResult FMENGINE_CALL FmEngine_Generate(
    FmEngineHandle engine, float* out_l, float* out_r, uint32_t samples)
{
    if (!engine || !out_l || !out_r) return FM_ERR_INVALID_ARG;

    for (uint32_t i = 0; i < samples; ++i) {
        float l = 0.0f, r = 0.0f;
        {
            std::lock_guard<std::mutex> lock(engine->write_mutex);
            for (auto& chip : engine->chips)
                chipCalcStereo(*chip, l, r);
        }
        // クリッピング
        out_l[i] = std::max(-1.0f, std::min(1.0f, l));
        out_r[i] = std::max(-1.0f, std::min(1.0f, r));
    }
    return FM_OK;
}

} // extern "C"
