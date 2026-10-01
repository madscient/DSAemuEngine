// DSAemuEngine.cpp
// FmEngineApi 準拠エミュレーションエンジン
// 統合コア:
//   emu2149  (YM2149/AY-3-8910 PSG)  → SSG チップ
//   emu2413  (YM2413 OPLL)            → OPLL / OPLLP / OPLLX / VRC7 チップ
//   emu8950  (Y8950/YM3526/YM3812)   → Y8950 / OPL / OPL2 チップ
//   emu2212  (Konami SCC)             → SCC / SCCP チップ
//   emu76489 (SN76489 DCSG)          → DCSG チップ
//   src/y8960 (emu8950 / emu2413 のフォーク) → OPL2EX / OPLLEX チップ (Y8960)

#include "FmEngineApi.h"
#include "../extern/emu2149/emu2149.h"
#include "../extern/emu2413/emu2413.h"
#include "../extern/emu8950/emu8950.h"
#include "../extern/emu2212/emu2212.h"
#include "../extern/emu76489/emu76489.h"
#include "y8960/Y8960Opl2exCore.h"
#include "y8960/Y8960OpllCore.h"

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

// Y8960 では 2 回路の OPL2EX がこの容量の SRAM を分け合うが、FmEngineApi には
// チップ間でメモリを共有させる口が無いので、1 チップごとに丸ごと持たせる
static constexpr uint32_t kY8960AdpcmRamSize = 256 * 1024;

// 部位マスクが uint32_t なので、FmPart の番号は 0〜31 に収まる
static constexpr uint32_t kPartSlots = 32;

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
    OPL2EX, // y8960    Y8960 拡張 OPL2 (YM3812 + ADPCM-B)
    OPLLEX, // y8960    Y8960 拡張 OPLL (チャンネル別プリセット音色バンク)
};

// =========================================================
//  チップエントリ
// =========================================================
struct ChipEntry {
    ChipKind  kind;
    std::string name;
    uint32_t  sample_rate;  // エンジンのサンプルレート
    uint32_t  clock;        // マスタークロック

    // 各コアへのポインタ (使用するのは kind に応じた1つのみ)
    PSG*       psg    = nullptr;
    OPLL*      opll   = nullptr;
    OPL*       opl    = nullptr;
    SCC*       scc    = nullptr;
    SNG*       sng    = nullptr;
    Y8960OPL*  opl2ex = nullptr;
    Y8960OPLL* opllex = nullptr;

    // OPL2EX の ADPCM サンプル RAM。コアは窓として参照するだけで所有しない
    std::vector<uint8_t> adpcm_ram;

    float gain_l = 1.0f;
    float gain_r = 1.0f;
    float part_gain[kPartSlots][2];

    ChipEntry() {
        for (auto& g : part_gain) { g[0] = 1.0f; g[1] = 1.0f; }
    }

    ~ChipEntry() {
        if (psg)    { PSG_delete(psg);  psg  = nullptr; }
        if (opll)   { OPLL_delete(opll); opll = nullptr; }
        if (opl)    { OPL_delete(opl);  opl  = nullptr; }
        if (scc)    { SCC_delete(scc);  scc  = nullptr; }
        if (sng)    { SNG_delete(sng);  sng  = nullptr; }
        if (opl2ex) { Y8960OPL_delete(opl2ex);  opl2ex = nullptr; }
        if (opllex) { Y8960OPLL_delete(opllex); opllex = nullptr; }
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
    { "OPL2EX", ChipKind::OPL2EX, 3579545 }, // Y8960 拡張 OPL2
    { "OPLLEX", ChipKind::OPLLEX, 3579545 }, // Y8960 拡張 OPLL
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
    case ChipKind::OPLLEX:
        return c.clock / 72;
    case ChipKind::Y8950:
    case ChipKind::OPL:
    case ChipKind::OPL2:
    case ChipKind::OPL2EX:
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
//  部位
// =========================================================
static uint32_t partMask(ChipKind kind) {
    switch (kind) {
    case ChipKind::OPLL:
    case ChipKind::OPLLP:
    case ChipKind::OPLLX:
    case ChipKind::VRC7:
        return (1u << FM_PART_OPLL_MELODY) | (1u << FM_PART_OPLL_RHYTHM);
    default:
        // OPLLEX は仕様書の部位の表に載っていないので部位を持たせない。
        // 出力はメロディとリズムに分けて取り出してあり (routeOpllParts)、
        // 部位を持たせるならこのマスクに加えるだけでよい
        return 0;
    }
}

// emu2413 のパンはステレオ定位のための拡張機能だが、ここではメロディを L、
// リズムを R に振り分けて 2 系統を別々に取り出すのに使う。OPLL_reset は
// パンを中央に戻すので、リセットのたびに掛け直すこと
template <typename Chip>
static void routeOpllParts(Chip* chip, void (*setPan)(Chip*, uint32_t, uint8_t)) {
    for (uint32_t ch = 0; ch < 9; ++ch)  setPan(chip, ch, 2);  // 0..8: メロディ
    for (uint32_t ch = 9; ch < 14; ++ch) setPan(chip, ch, 1);  // 9..13: BD HH SD TOM CYM
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
        routeOpllParts(e->opll, OPLL_setPan);
        break;

    case ChipKind::OPLLP:
        e->opll = OPLL_new(e->clock, sample_rate);
        if (!e->opll) return nullptr;
        OPLL_setChipType(e->opll, 0);
        OPLL_resetPatch(e->opll, OPLL_281B_TONE);
        OPLL_reset(e->opll);
        routeOpllParts(e->opll, OPLL_setPan);
        break;

    case ChipKind::VRC7:
        e->opll = OPLL_new(e->clock, sample_rate);
        if (!e->opll) return nullptr;
        OPLL_setChipType(e->opll, 1);
        OPLL_resetPatch(e->opll, OPLL_VRC7_TONE);
        OPLL_reset(e->opll);
        routeOpllParts(e->opll, OPLL_setPan);
        break;

    case ChipKind::OPLLEX:
        // Y8960OPLL_new が 4 バンクの音色の読み込みとリセットまで済ませる
        e->opllex = Y8960OPLL_new(e->clock, sample_rate);
        if (!e->opllex) return nullptr;
        routeOpllParts(e->opllex, Y8960OPLL_setPan);
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

    case ChipKind::OPL2EX:
        e->opl2ex = Y8960OPL_new(e->clock, sample_rate);
        if (!e->opl2ex) return nullptr;
        e->adpcm_ram.assign(kY8960AdpcmRamSize, 0);
        Y8960OPL_setADPCMMemory(e->opl2ex, e->adpcm_ram.data(), kY8960AdpcmRamSize);
        break;

    case ChipKind::SCC:
    case ChipKind::SCCP:
        e->scc = SCC_new(e->clock, sample_rate);
        if (!e->scc) return nullptr;
        SCC_set_type(e->scc, desc.kind == ChipKind::SCCP ? SCC_ENHANCED : SCC_STANDARD);
        SCC_set_quality(e->scc, 1);
        SCC_reset(e->scc);
        // SCC_write() はレジスタ窓の外へのアクセスを無視するため、実機と同じ
        // 起動シーケンスでデバイスを有効化しておく必要がある
        if (desc.kind == ChipKind::SCCP) {
            SCC_write(e->scc, 0xBFFE, 0x20); // SCC+ モード (base_adr = 0xB000)
            SCC_write(e->scc, 0xB000, 0x80);
        } else {
            SCC_write(e->scc, 0x9000, 0x3F);
        }
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
        // reg はレジスタ窓 0x9800-0x98FF 内のオフセット
        SCC_write(c.scc, 0x9800 + reg, val);
        break;
    case ChipKind::SCCP:
        // SCC+ モードでは窓が 0xB800-0xB8FF に移り、配置も標準 SCC と異なる
        SCC_write(c.scc, 0xB800 + reg, val);
        break;
    case ChipKind::DCSG:
        // SN76489はシリアルバイト形式 (reg は無視, val をそのまま writeIO)
        SNG_writeIO(c.sng, val);
        break;
    case ChipKind::OPL2EX:
        Y8960OPL_writeReg(c.opl2ex, reg, val);
        break;
    case ChipKind::OPLLEX:
        Y8960OPLL_writeReg(c.opllex, reg, val);
        break;
    }
}

// =========================================================
//  1サンプル生成 (ステレオ, float)
// =========================================================
// emu2413 / emu8950 (とそのフォーク) のレート変換器は、補間位置を決める timer を
// 全チャンネルで共有し、getData を呼ぶたびに f_ratio だけ進める。calcStereo は
// 1 出力サンプルにつき getData を L と R で 2 回呼ぶので、そのままでは L と R が
// 別々の、どちらも誤った位置で補間される。先に 1 サンプルぶん進めた timer を置き、
// 呼び出しの間だけ増分を 0 にして、両チャンネルを mono の calc と同じ位置に揃える
template <typename Chip>
static void calcStereoAligned(Chip* chip, void (*calcStereo)(Chip*, int32_t*), int32_t buf[2]) {
    auto* conv = chip->conv;
    if (!conv) {
        calcStereo(chip, buf);
        return;
    }
    const double ratio = conv->f_ratio;
    const double t = conv->timer + ratio;
    conv->timer = t - std::floor(t);
    conv->f_ratio = 0.0;
    calcStereo(chip, buf);
    conv->f_ratio = ratio;
}

// buf はメロディとリズムの 2 系統 (routeOpllParts で振り分けたもの)
static void mixOpllParts(const ChipEntry& c, const int32_t buf[2], float& out_l, float& out_r) {
    const float melody = (float)buf[0];
    const float rhythm = (float)buf[1];
    const float (&m)[2] = c.part_gain[FM_PART_OPLL_MELODY];
    const float (&r)[2] = c.part_gain[FM_PART_OPLL_RHYTHM];
    out_l += (melody * m[0] + rhythm * r[0]) * kOutputScale * c.gain_l;
    out_r += (melody * m[1] + rhythm * r[1]) * kOutputScale * c.gain_r;
}

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
        calcStereoAligned(c.opll, OPLL_calcStereo, buf);
        mixOpllParts(c, buf, out_l, out_r);
        return;
    case ChipKind::OPLLEX:
        calcStereoAligned(c.opllex, Y8960OPLL_calcStereo, buf);
        mixOpllParts(c, buf, out_l, out_r);
        return;
    case ChipKind::Y8950:
    case ChipKind::OPL:
    case ChipKind::OPL2:
        calcStereoAligned(c.opl, OPL_calcStereo, buf);
        break;
    case ChipKind::OPL2EX:
        calcStereoAligned(c.opl2ex, Y8960OPL_calcStereo, buf);
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

    try {
        auto chip = createChip(*desc, clock, engine->sample_rate);
        if (!chip) return FM_ERR_ALLOC;

        if (out_id) *out_id = (uint32_t)engine->chips.size();
        engine->chips.push_back(std::move(chip));
    } catch (const std::bad_alloc&) {
        return FM_ERR_ALLOC;
    }
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

static bool hasPart(const ChipEntry& c, FmPart part) {
    const uint32_t n = (uint32_t)part;
    return n < kPartSlots && (partMask(c.kind) & (1u << n)) != 0;
}

FMENGINE_API FmResult FMENGINE_CALL FmEngine_SetPartGain(
    FmEngineHandle engine, uint32_t chip_id, FmPart part,
    float gain_l, float gain_r)
{
    if (!engine || chip_id >= engine->chips.size()) return FM_ERR_INVALID_ARG;
    auto& c = *engine->chips[chip_id];
    if (!hasPart(c, part)) return FM_ERR_INVALID_ARG;
    std::lock_guard<std::mutex> lock(engine->write_mutex);
    c.part_gain[part][0] = gain_l;
    c.part_gain[part][1] = gain_r;
    return FM_OK;
}

FMENGINE_API FmResult FMENGINE_CALL FmEngine_GetPartGain(
    FmEngineHandle engine, uint32_t chip_id, FmPart part,
    float* out_gain_l, float* out_gain_r)
{
    if (!engine || chip_id >= engine->chips.size()) return FM_ERR_INVALID_ARG;
    if (!out_gain_l || !out_gain_r) return FM_ERR_INVALID_ARG;
    auto& c = *engine->chips[chip_id];
    if (!hasPart(c, part)) return FM_ERR_INVALID_ARG;
    std::lock_guard<std::mutex> lock(engine->write_mutex);
    *out_gain_l = c.part_gain[part][0];
    *out_gain_r = c.part_gain[part][1];
    return FM_OK;
}

FMENGINE_API FmResult FMENGINE_CALL FmEngine_GetPartMask(
    FmEngineHandle engine, uint32_t chip_id, uint32_t* out_mask)
{
    if (!engine || chip_id >= engine->chips.size()) return FM_ERR_INVALID_ARG;
    if (!out_mask) return FM_ERR_INVALID_ARG;
    *out_mask = partMask(engine->chips[chip_id]->kind);
    return FM_OK;
}

FMENGINE_API FmResult FMENGINE_CALL FmEngine_SetMemory(
    FmEngineHandle engine, uint32_t chip_id,
    FmMemoryType mem_type, const uint8_t* data, uint32_t size)
{
    if (!engine || chip_id >= engine->chips.size()) return FM_ERR_INVALID_ARG;
    if (!data && size != 0) return FM_ERR_INVALID_ARG;
    auto& c = *engine->chips[chip_id];

    // Y8950 は ADPCM-B RAM に対応
    if ((c.kind == ChipKind::Y8950) && c.opl) {
        if (mem_type == FM_MEM_ADPCM_B) {
            OPL_writeADPCMData(c.opl, 0, 0, size, data);
            return FM_OK;
        }
    }
    if ((c.kind == ChipKind::OPL2EX) && c.opl2ex) {
        if (mem_type == FM_MEM_ADPCM_B) {
            Y8960OPL_writeADPCMData(c.opl2ex, 0, size, data);
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
