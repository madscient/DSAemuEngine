// test_engine.cpp
// FmEngineApi の動作検証テスト (RtAudio 不要、無音バッファ生成のみ)
#include "FmEngineApi.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>

#ifdef _WIN32
#  include <windows.h>
#  define LOAD_LIB(p)    LoadLibraryA(p)
#  define GET_SYM(h,n)   GetProcAddress((HMODULE)(h),(n))
#  define FREE_LIB(h)    FreeLibrary((HMODULE)(h))
#else
#  include <dlfcn.h>
#  define LOAD_LIB(p)    dlopen((p), RTLD_LAZY)
#  define GET_SYM(h,n)   dlsym((h),(n))
#  define FREE_LIB(h)    dlclose(h)
#endif

// 関数ポインタ
typedef FmEngineHandle (*pfCreate)(uint32_t);
typedef void           (*pfDestroy)(FmEngineHandle);
typedef uint32_t       (*pfInquiry)(FmEngineHandle);
typedef const char*    (*pfGetSupportedChip)(FmEngineHandle,uint32_t);
typedef FmResult       (*pfAddChip)(FmEngineHandle,const char*,uint32_t,uint32_t*);
typedef const char*    (*pfGetChipName)(FmEngineHandle,uint32_t);
typedef uint32_t       (*pfGetNativeRate)(FmEngineHandle,uint32_t);
typedef uint32_t       (*pfGetSampleRate)(FmEngineHandle);
typedef FmResult       (*pfWrite)(FmEngineHandle,uint32_t,uint8_t,uint8_t,uint32_t);
typedef FmResult       (*pfSetGain)(FmEngineHandle,uint32_t,float,float);
typedef FmResult       (*pfGenerate)(FmEngineHandle,float*,float*,uint32_t);

#define LOAD(api,h,name) api.name = (pf##name)GET_SYM(h,"FmEngine_"#name); \
    if(!api.name){fprintf(stderr,"Symbol FmEngine_"#name" not found\n");return 1;}

struct Api {
    pfCreate          Create;
    pfDestroy         Destroy;
    pfInquiry         Inquiry;
    pfGetSupportedChip GetSupportedChip;
    pfAddChip         AddChip;
    pfGetChipName     GetChipName;
    pfGetNativeRate   GetNativeRate;
    pfGetSampleRate   GetSampleRate;
    pfWrite           Write;
    pfSetGain         SetGain;
    pfGenerate        Generate;
};

static int loadApi(const char* path, Api& api) {
    void* h = LOAD_LIB(path);
    if (!h) { fprintf(stderr,"Cannot load: %s\n",path); return 1; }
    LOAD(api,h,Create)
    LOAD(api,h,Destroy)
    LOAD(api,h,Inquiry)
    LOAD(api,h,GetSupportedChip)
    LOAD(api,h,AddChip)
    LOAD(api,h,GetChipName)
    LOAD(api,h,GetNativeRate)
    LOAD(api,h,GetSampleRate)
    LOAD(api,h,Write)
    LOAD(api,h,SetGain)
    LOAD(api,h,Generate)
    return 0;
}

// SSG (YM2149) テスト: CH-A 440Hz 方形波
static void testSSG(const Api& a, FmEngineHandle eng, uint32_t id) {
    // CH-A freq = clock/(16*N) = 2000000/(16*284) ≈ 440 Hz → N=284=0x011C
    a.Write(eng, id, 0x00, 0x1C, 0); // freq_lo
    a.Write(eng, id, 0x01, 0x01, 0); // freq_hi
    a.Write(eng, id, 0x08, 0x0F, 0); // vol=15
    a.Write(eng, id, 0x07, 0x3E, 0); // tone-A enable
}

// OPLL (YM2413) テスト: CH0 inst=1 261Hz
static void testOPLL(const Api& a, FmEngineHandle eng, uint32_t id) {
    a.Write(eng, id, 0x30, 0x10, 0); // CH0 inst=1, vol=0
    a.Write(eng, id, 0x10, 0x58, 0); // F-lo
    a.Write(eng, id, 0x20, 0x1C, 0); // key-off (sustain=0,block=3,f-hi=2)
    a.Write(eng, id, 0x20, 0x1C | 0x10, 0); // key-on
}

// OPL (Y8950/YM3526/YM3812) テスト: CH0 261Hz
static void testOPL(const Api& a, FmEngineHandle eng, uint32_t id) {
    a.Write(eng, id, 0x20, 0x01, 0);
    a.Write(eng, id, 0x40, 0x28, 0);
    a.Write(eng, id, 0x60, 0xF0, 0);
    a.Write(eng, id, 0x80, 0x77, 0);
    a.Write(eng, id, 0x23, 0x01, 0);
    a.Write(eng, id, 0x43, 0x00, 0);
    a.Write(eng, id, 0x63, 0xF0, 0);
    a.Write(eng, id, 0x83, 0x07, 0);
    a.Write(eng, id, 0xA0, 0xD5, 0); // F-lo
    a.Write(eng, id, 0xB0, 0x2C, 0); // key-on block=2 F-hi=4
}

// SCC テスト: CH0 波形設定
static void testSCC(const Api& a, FmEngineHandle eng, uint32_t id) {
    // CH0 waveform: 鋸歯波 (0x00-0x1F)
    for (int i = 0; i < 32; i++) {
        a.Write(eng, id, (uint8_t)(0x00 + i), (uint8_t)(i * 4 - 64), 0);
    }
    a.Write(eng, id, 0x40, 0x8D, 0); // CH0 freq_lo (A4≈440Hz @3.58MHz)
    a.Write(eng, id, 0x41, 0x00, 0);
    a.Write(eng, id, 0x50, 0x0F, 0); // CH0 vol
    a.Write(eng, id, 0x54, 0x01, 0); // CH0 enable
}

// DCSG (SN76489) テスト: CH0 tone 440Hz
static void testDCSG(const Api& a, FmEngineHandle eng, uint32_t id) {
    // N = clock/(32*freq) = 3579545/(32*440) = 254
    // byte1: 1 00 0 | 0111 (ch0 tone latch, N low 4bits = 254&0xF=0xE → 0x8E)
    // byte2: 0 | 0001111  (N high 6bits = 254>>4=15 = 0x0F)
    a.Write(eng, id, 0, 0x8E, 0); // tone CH0 latch+data
    a.Write(eng, id, 0, 0x0F, 0); // tone CH0 data
    a.Write(eng, id, 0, 0x90, 0); // volume CH0 = 0 (最大)
}

int main(int argc, char* argv[]) {
    const char* libPath = (argc > 1) ? argv[1] : "./bin/libFmEngineApi.so";

    printf("=== FmEngineEmu API Test ===\n");
    printf("Loading: %s\n\n", libPath);

    Api a;
    if (loadApi(libPath, a) != 0) return 1;

    FmEngineHandle eng = a.Create(48000);
    if (!eng) { fprintf(stderr, "Create failed\n"); return 1; }

    printf("Sample rate: %u Hz\n", a.GetSampleRate(eng));

    // 対応チップ一覧
    uint32_t n = a.Inquiry(eng);
    printf("Supported chips (%u):", n);
    for (uint32_t i = 0; i < n; ++i)
        printf(" %s", a.GetSupportedChip(eng, i));
    printf("\n\n");

    // 各チップを追加してテスト
    struct TestCase { const char* name; void(*init)(const Api&,FmEngineHandle,uint32_t); };
    static const TestCase cases[] = {
        { "SSG",   testSSG   },
        { "OPLL",  testOPLL  },
        { "OPLLP", testOPLL  },
        { "OPLLX", testOPLL  },
        { "VRC7",  testOPLL  },
        { "Y8950", testOPL   },
        { "OPL",   testOPL   },
        { "OPL2",  testOPL   },
        { "SCC",   testSCC   },
        { "DCSG",  testDCSG  },
    };

    bool all_ok = true;
    for (auto& tc : cases) {
        uint32_t id = 0xFFFFFFFF;
        FmResult res = a.AddChip(eng, tc.name, 0, &id);
        if (res != FM_OK) {
            printf("[FAIL] AddChip(%s): code=%d\n", tc.name, (int)res);
            all_ok = false;
            continue;
        }
        printf("[OK]   AddChip(%s) -> id=%u  native_rate=%u Hz\n",
               tc.name, id, a.GetNativeRate(eng, id));

        // レジスタ書き込みテスト
        if (tc.init) tc.init(a, eng, id);
        a.SetGain(eng, id, 0.5f, 0.5f);
    }

    // 波形生成テスト (1秒分)
    printf("\nGenerating 48000 samples...\n");
    static float buf_l[1024], buf_r[1024];
    float max_l = 0.0f, max_r = 0.0f;
    uint32_t remaining = 48000;
    while (remaining > 0) {
        uint32_t chunk = (remaining > 1024) ? 1024 : remaining;
        FmResult gr = a.Generate(eng, buf_l, buf_r, chunk);
        if (gr != FM_OK) {
            printf("[FAIL] Generate: code=%d\n", (int)gr);
            all_ok = false;
            break;
        }
        for (uint32_t i = 0; i < chunk; ++i) {
            if (buf_l[i] > max_l) max_l = buf_l[i];
            if (buf_r[i] > max_r) max_r = buf_r[i];
        }
        remaining -= chunk;
    }
    printf("Peak: L=%.4f  R=%.4f\n", max_l, max_r);

    if (max_l > 0.0f || max_r > 0.0f)
        printf("[OK]   Audio output is non-zero (chips generating sound)\n");
    else
        printf("[WARN] Audio output is all zero (chips may need key-on)\n");

    a.Destroy(eng);

    printf("\n%s\n", all_ok ? "=== All tests PASSED ===" : "=== Some tests FAILED ===");
    return all_ok ? 0 : 1;
}
