// DSAemuEngine.cpp
// FmEngineApi 準拠エミュレーションエンジン
// 統合コア:
//   emu2149  (YM2149/AY-3-8910 PSG)  → SSG チップ
//   emu2413  (YM2413 OPLL)            → OPLL / OPLLP / OPLLX / VRC7 チップ
//   emu8950  (Y8950/YM3526/YM3812)   → Y8950 / OPL / OPL2 チップ
//   emu2212  (Konami SCC)             → SCC / SCCP チップ
//   emu76489 (SN76489 DCSG)          → DCSG チップ
//   src/y8960 (emu8950 / emu2413 / emu2149 のフォーク) → OPL2EX / OPLLEX / SSGS チップ (Y8960)

#include "FmEngineApi.h"
#include "../extern/emu2149/emu2149.h"
#include "../extern/emu2413/emu2413.h"
#include "../extern/emu8950/emu8950.h"
#include "../extern/emu2212/emu2212.h"
#include "../extern/emu76489/emu76489.h"
#include "y8960/Y8960Opl2exCore.h"
#include "y8960/Y8960OpllCore.h"
#include "y8960/Y8960SsgsCore.h"

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

// ADPCM-B が番地を引ける範囲。Y8950 も OPL2EX も 256KB
static constexpr uint32_t kAdpcmSpaceSize = 256 * 1024;

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
    SSGS,   // y8960    Y8960 SSGS (YMZ705 の SSG 部: YM2149 x2 + パンポット)
};

// =========================================================
//  部位と外部メモリの名前
//  表の添字が FmEngine_GetPartName / FmEngine_GetMemoryName の index で、
//  そのまま ChipEntry の part_gain / mappings の添字にもなる
// =========================================================
struct NameList {
    const char* const* names;
    uint32_t           count;
};

// routeOpllParts が L / R に振り分ける順
enum : uint32_t { kPartMelody, kPartRhythm, kPartSlots };
static const char* const kOpllParts[kPartSlots] = { "MELODY", "RHYTHM" };

// emu8950 の memory[] の添字と同じ並び (0 が RAM モード、1 が ROM モードのメモリ)
enum : uint32_t { kMemAdpcmB, kMemAdpcmBRomMode, kMemorySlots };
static const char* const kAdpcmMemories[kMemorySlots] = { "ADPCM_B", "ADPCM_B_ROMMODE" };

static NameList partNames(ChipKind kind) {
    switch (kind) {
    case ChipKind::OPLL:
    case ChipKind::OPLLP:
    case ChipKind::OPLLX:
    case ChipKind::VRC7:
        return { kOpllParts, kPartSlots };
    default:
        // OPLLEX は部位を出さない
        return { nullptr, 0 };
    }
}

static NameList memoryNames(ChipKind kind) {
    switch (kind) {
    case ChipKind::Y8950:
        return { kAdpcmMemories, kMemorySlots };
    case ChipKind::OPL2EX:
        // ROM モードのメモリは無い (08h の ROM ビットを立てても RAM を読む) ので、
        // 先頭の ADPCM_B だけ
        return { kAdpcmMemories, 1 };
    default:
        return { nullptr, 0 };
    }
}

// 一覧の添字。一覧に無い名前と nullptr は -1
static int findName(NameList list, const char* name) {
    if (!name) return -1;
    for (uint32_t i = 0; i < list.count; ++i)
        if (strcmp(list.names[i], name) == 0)
            return (int)i;
    return -1;
}

// =========================================================
//  チップエントリ
// =========================================================
struct ChipEntry {
    ChipKind  kind;
    std::string name;
    uint32_t  clock;        // マスタークロック

    // 各コアへのポインタ (使用するのは kind に応じた1つのみ)
    PSG*       psg    = nullptr;
    OPLL*      opll   = nullptr;
    OPL*       opl    = nullptr;
    SCC*       scc    = nullptr;
    SNG*       sng    = nullptr;
    Y8960OPL*  opl2ex = nullptr;
    Y8960OPLL* opllex = nullptr;
    Y8960SSG*  ssgs[2] = { nullptr, nullptr };

    // FmEngine_SetMemoryEx の割り当て
    struct MemMapping {
        uint32_t base;
        uint32_t size;
        uint8_t* data;
        bool     ram;
    };
    std::vector<MemMapping> mappings[kMemorySlots];

    // OPL2EX: FmEngine_SetMemory の書き込み先。割り当てが 1 つも無い間だけコアから見える
    std::vector<uint8_t> adpcm_ram;
    // OPL2EX: コアに渡す割り当ての表。コアは参照するだけ
    std::vector<Y8960OPL_ADPCM_REGION> adpcm_map;

    // Y8950: emu8950 が自分で確保した RAM / ROM と、ROM の割り当てを複製する先
    uint8_t* y8950_own[kMemorySlots] = { nullptr, nullptr };
    std::vector<uint8_t> y8950_image[kMemorySlots];

    float gain_l = 1.0f;
    float gain_r = 1.0f;
    float part_gain[kPartSlots][2];

    ChipEntry() {
        for (auto& g : part_gain) { g[0] = 1.0f; g[1] = 1.0f; }
    }

    ~ChipEntry() {
        if (psg)    { PSG_delete(psg);  psg  = nullptr; }
        if (opll)   { OPLL_delete(opll); opll = nullptr; }
        if (opl) {
            // OPL_delete は memory[] を解放するので、差し替えていたら emu8950 自身のものに戻す
            if (opl->adpcm && y8950_own[0]) {
                opl->adpcm->memory[0] = y8950_own[0];
                opl->adpcm->memory[1] = y8950_own[1];
            }
            OPL_delete(opl);
            opl = nullptr;
        }
        if (scc)    { SCC_delete(scc);  scc  = nullptr; }
        if (sng)    { SNG_delete(sng);  sng  = nullptr; }
        if (opl2ex) { Y8960OPL_delete(opl2ex);  opl2ex = nullptr; }
        if (opllex) { Y8960OPLL_delete(opllex); opllex = nullptr; }
        for (auto& unit : ssgs)
            if (unit) { Y8960SSG_delete(unit); unit = nullptr; }
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
};

static const ChipDesc kChipTable[] = {
    { "SSG",    ChipKind::SSG    },  // AY-3-8910 / YM2149
    { "OPLL",   ChipKind::OPLL   },  // YM2413
    { "OPLLP",  ChipKind::OPLLP  },  // YM2413 (281B patches)
    { "OPLLX",  ChipKind::OPLLX  },  // YM2413 (extended alias)
    { "VRC7",   ChipKind::VRC7   },  // VRC7
    { "Y8950",  ChipKind::Y8950  },  // Y8950
    { "OPL",    ChipKind::OPL    },  // YM3526
    { "OPL2",   ChipKind::OPL2   },  // YM3812
    { "SCC",    ChipKind::SCC    },  // Konami SCC
    { "SCCP",   ChipKind::SCCP   },  // Konami SCC-I (SCC+)
    { "DCSG",   ChipKind::DCSG   },  // SN76489
    { "OPL2EX", ChipKind::OPL2EX },  // Y8960 拡張 OPL2
    { "OPLLEX", ChipKind::OPLLEX },  // Y8960 拡張 OPLL
    { "SSGS",   ChipKind::SSGS   },  // Y8960 SSGS (clock はマスタークロック)
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
//  クロック
// =========================================================
// SSGS の clock はマスタークロック (YMZ705 の XI ピン) で、SSG はそれを分周した
// クロックで動く。分周比を選ぶ S6M ピンに当たる指定が無いので、EPSGemuEngine の
// SSGS と同じく 5.12MHz 未満なら 1/2、以上なら 1/3 とする
static uint32_t ssgsUnitClock(uint32_t clock) {
    return clock < 5120000 ? clock / 2 : clock / 3;
}

// =========================================================
//  部位
// =========================================================
// emu2413 のパンはステレオ定位のための拡張機能だが、ここではメロディを L、
// リズムを R に振り分けて 2 系統を別々に取り出すのに使う。OPLL_reset は
// パンを中央に戻すので、リセットのたびに掛け直すこと
template <typename Chip>
static void routeOpllParts(Chip* chip, void (*setPan)(Chip*, uint32_t, uint8_t)) {
    for (uint32_t ch = 0; ch < 9; ++ch)  setPan(chip, ch, 2);  // 0..8: メロディ
    for (uint32_t ch = 9; ch < 14; ++ch) setPan(chip, ch, 1);  // 9..13: BD HH SD TOM CYM
}

// =========================================================
//  外部メモリ
// =========================================================
static void applyOpl2exMemory(ChipEntry& c) {
    c.adpcm_map.clear();
    if (c.mappings[kMemAdpcmB].empty()) {
        c.adpcm_map.push_back({ 0, kAdpcmSpaceSize, c.adpcm_ram.data(), 1 });
    } else {
        for (const auto& m : c.mappings[kMemAdpcmB])
            c.adpcm_map.push_back({ m.base, m.size, m.data, (uint8_t)(m.ram ? 1 : 0) });
    }
    Y8960OPL_setADPCMMemoryMap(c.opl2ex, c.adpcm_map.data(), (uint32_t)c.adpcm_map.size());
}

// emu8950 はメモリを 256KB の連続した配列として添字で読み書きするので、割り当ての
// 表を挟めない。memory[] のポインタを差し替えて表す:
//   割り当て無し          … emu8950 自身のもの
//   RAM の割り当てがある  … そのブロック (base 0 で 256KB 以上のものしか受け付けない)
//   ROM の割り当てだけ    … 0 で埋めたバッファに複製したもの
// 再生中の wave も同じ配列を指しているので、一緒に差し替える
static void applyY8950Memory(ChipEntry& c, int space) {
    OPL_ADPCM* adpcm = c.opl->adpcm;
    uint8_t* const before = adpcm->memory[space];
    uint8_t* after = c.y8950_own[space];
    auto& image = c.y8950_image[space];
    const auto& maps = c.mappings[space];

    const auto ram = std::find_if(maps.begin(), maps.end(),
                                  [](const ChipEntry::MemMapping& m) { return m.ram; });
    if (ram != maps.end()) {
        after = ram->data;
    } else if (!maps.empty()) {
        image.assign(kAdpcmSpaceSize, 0);
        for (const auto& m : maps) {
            if (m.base >= kAdpcmSpaceSize) continue;
            const uint32_t n = std::min(m.size, kAdpcmSpaceSize - m.base);
            std::memcpy(image.data() + m.base, m.data, n);
        }
        after = image.data();
    }
    adpcm->memory[space] = after;
    if (adpcm->wave == before) adpcm->wave = after;
    if (after != image.data()) std::vector<uint8_t>().swap(image);
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
    e->clock       = clock;

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
        // 部位は出さないが、OPLL 系と同じくメロディとリズムを分けて取り出し、等倍で
        // 混ぜ直す。分けずに取り出すと、レート変換が働くときに両者が重なるところで
        // 出力が 1 LSB 変わる
        routeOpllParts(e->opllex, Y8960OPLL_setPan);
        break;

    case ChipKind::Y8950:
        e->opl = OPL_new(e->clock, sample_rate);
        if (!e->opl) return nullptr;
        OPL_setChipType(e->opl, 0); // Y8950
        OPL_reset(e->opl);
        if (!e->opl->adpcm) return nullptr;
        for (uint32_t space = 0; space < kMemorySlots; ++space)
            e->y8950_own[space] = e->opl->adpcm->memory[space];
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
        e->adpcm_ram.assign(kAdpcmSpaceSize, 0);
        applyOpl2exMemory(*e);
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

    case ChipKind::SSGS:
        for (auto& unit : e->ssgs) {
            unit = Y8960SSG_new(ssgsUnitClock(e->clock), sample_rate);
            if (!unit) return nullptr;
            Y8960SSG_setQuality(unit, 1);
            Y8960SSG_reset(unit);
        }
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
    case ChipKind::SSGS:
        // 00h-1Fh が SSG-1、20h-3Fh が SSG-2。40h 以降は YMZ705 では ADPCM 部だが、
        // Y8960 の SSGS は持たない
        if (reg < 0x40)
            Y8960SSG_writeReg(c.ssgs[reg >> 5], reg & 0x1F, val);
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
    const float (&m)[2] = c.part_gain[kPartMelody];
    const float (&r)[2] = c.part_gain[kPartRhythm];
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
    case ChipKind::SSGS:
        for (auto* unit : c.ssgs) {
            int32_t unit_buf[2];
            Y8960SSG_calcStereo(unit, unit_buf);
            buf[0] += unit_buf[0];
            buf[1] += unit_buf[1];
        }
        break;
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
    if (!engine || !name || clock == 0) return FM_ERR_INVALID_ARG;
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

FMENGINE_API uint32_t FMENGINE_CALL FmEngine_GetPartCount(
    FmEngineHandle engine, uint32_t chip_id)
{
    if (!engine || chip_id >= engine->chips.size()) return 0;
    return partNames(engine->chips[chip_id]->kind).count;
}

FMENGINE_API const char* FMENGINE_CALL FmEngine_GetPartName(
    FmEngineHandle engine, uint32_t chip_id, uint32_t index)
{
    if (!engine || chip_id >= engine->chips.size()) return nullptr;
    const NameList parts = partNames(engine->chips[chip_id]->kind);
    return index < parts.count ? parts.names[index] : nullptr;
}

FMENGINE_API FmResult FMENGINE_CALL FmEngine_SetPartGain(
    FmEngineHandle engine, uint32_t chip_id, const char* part,
    float gain_l, float gain_r)
{
    if (!engine || chip_id >= engine->chips.size()) return FM_ERR_INVALID_ARG;
    auto& c = *engine->chips[chip_id];
    const int slot = findName(partNames(c.kind), part);
    if (slot < 0) return FM_ERR_INVALID_ARG;
    std::lock_guard<std::mutex> lock(engine->write_mutex);
    c.part_gain[slot][0] = gain_l;
    c.part_gain[slot][1] = gain_r;
    return FM_OK;
}

FMENGINE_API FmResult FMENGINE_CALL FmEngine_GetPartGain(
    FmEngineHandle engine, uint32_t chip_id, const char* part,
    float* out_gain_l, float* out_gain_r)
{
    if (!engine || chip_id >= engine->chips.size()) return FM_ERR_INVALID_ARG;
    if (!out_gain_l || !out_gain_r) return FM_ERR_INVALID_ARG;
    auto& c = *engine->chips[chip_id];
    const int slot = findName(partNames(c.kind), part);
    if (slot < 0) return FM_ERR_INVALID_ARG;
    std::lock_guard<std::mutex> lock(engine->write_mutex);
    *out_gain_l = c.part_gain[slot][0];
    *out_gain_r = c.part_gain[slot][1];
    return FM_OK;
}

FMENGINE_API uint32_t FMENGINE_CALL FmEngine_GetMemoryCount(
    FmEngineHandle engine, uint32_t chip_id)
{
    if (!engine || chip_id >= engine->chips.size()) return 0;
    return memoryNames(engine->chips[chip_id]->kind).count;
}

FMENGINE_API const char* FMENGINE_CALL FmEngine_GetMemoryName(
    FmEngineHandle engine, uint32_t chip_id, uint32_t index)
{
    if (!engine || chip_id >= engine->chips.size()) return nullptr;
    const NameList memories = memoryNames(engine->chips[chip_id]->kind);
    return index < memories.count ? memories.names[index] : nullptr;
}

FMENGINE_API FmResult FMENGINE_CALL FmEngine_SetMemory(
    FmEngineHandle engine, uint32_t chip_id,
    const char* memory, const uint8_t* data, uint32_t size)
{
    if (!engine || chip_id >= engine->chips.size()) return FM_ERR_INVALID_ARG;
    if (!data && size != 0) return FM_ERR_INVALID_ARG;
    auto& c = *engine->chips[chip_id];
    const int space = findName(memoryNames(c.kind), memory);
    if (space < 0) return FM_ERR_INVALID_ARG;

    // チップ自身が持つ 256KB の先頭へ複製する。SetMemoryEx の割り当てがある間は
    // コアから見えないが、割り当てが全部外れると見えるようになる
    uint8_t* dest = nullptr;
    if (c.kind == ChipKind::Y8950)  dest = c.y8950_own[space];
    if (c.kind == ChipKind::OPL2EX) dest = c.adpcm_ram.data();
    // memoryNames に足したチップの書き込み先をここに足し忘れても、黙って FM_OK にしない
    if (!dest) return FM_ERR_UNAVAILABLE;
    if (size) std::memcpy(dest, data, std::min(size, kAdpcmSpaceSize));
    return FM_OK;
}

FMENGINE_API FmResult FMENGINE_CALL FmEngine_SetMemoryEx(
    FmEngineHandle engine, uint32_t chip_id,
    const char* memory, uint32_t base,
    uint8_t* data, uint32_t size, FmMemoryAccess access)
{
    if (!engine || chip_id >= engine->chips.size()) return FM_ERR_INVALID_ARG;
    if (size == 0) return FM_ERR_INVALID_ARG;
    auto& c = *engine->chips[chip_id];
    const int space = findName(memoryNames(c.kind), memory);
    if (space < 0) return FM_ERR_INVALID_ARG;

    const uint64_t lo = base;
    const uint64_t hi = (uint64_t)base + size;
    auto overlaps = [lo, hi](const ChipEntry::MemMapping& m) {
        return lo < (uint64_t)m.base + m.size && (uint64_t)m.base < hi;
    };
    auto& maps = c.mappings[space];

    std::lock_guard<std::mutex> lock(engine->write_mutex);
    if (data) {
        if (access != FM_ACCESS_ROM && access != FM_ACCESS_RAM) return FM_ERR_INVALID_ARG;
        if (std::any_of(maps.begin(), maps.end(), overlaps)) return FM_ERR_INVALID_ARG;
        // emu8950 の配列を丸ごと差し替えることでしか、その場での読み書きを表せない
        if (c.kind == ChipKind::Y8950 && access == FM_ACCESS_RAM &&
            (base != 0 || size < kAdpcmSpaceSize))
            return FM_ERR_UNAVAILABLE;
        maps.push_back({ base, size, data, access == FM_ACCESS_RAM });
    } else {
        maps.erase(std::remove_if(maps.begin(), maps.end(), overlaps), maps.end());
    }

    if (c.kind == ChipKind::OPL2EX) applyOpl2exMemory(c);
    if (c.kind == ChipKind::Y8950)  applyY8950Memory(c, space);
    return FM_OK;
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
