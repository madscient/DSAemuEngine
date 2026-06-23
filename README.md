# DSAemuEngine

**FmEngineApi** 準拠の音源エミュレーションエンジン。  
[digital-sound-antiques](https://github.com/digital-sound-antiques) の各エミュレーションコアを  
Git submodule として統合した共有ライブラリ (DLL / .so / .dylib) です。

## 統合コア (submodules)

| パス | リポジトリ | チップ | FmEngineApi チップ名 |
|---|---|---|---|
| `extern/emu2149`  | [emu2149](https://github.com/digital-sound-antiques/emu2149)   | YM2149 / AY-3-8910 | `SSG` |
| `extern/emu2413`  | [emu2413](https://github.com/digital-sound-antiques/emu2413)   | YM2413 (OPLL)      | `OPLL` / `OPLLP` / `OPLLX` / `VRC7` |
| `extern/emu8950`  | [emu8950](https://github.com/digital-sound-antiques/emu8950)   | Y8950 / YM3526 / YM3812 | `Y8950` / `OPL` / `OPL2` |
| `extern/emu2212`  | [emu2212](https://github.com/digital-sound-antiques/emu2212)   | Konami SCC         | `SCC` |
| `extern/emu76489` | [emu76489](https://github.com/digital-sound-antiques/emu76489) | SN76489 (DCSG)     | `DCSG` |

## 対応チップ一覧

| チップ名 | 実チップ | デフォルトクロック | ネイティブレート |
|---|---|---|---|
| `SSG`   | YM2149 / AY-3-8910       | 2.000 MHz | 250,000 Hz |
| `OPLL`  | YM2413 (2413 ROM patch)  | 3.580 MHz |  49,715 Hz |
| `OPLLP` | YM2413 (281B patch)      | 3.580 MHz |  49,715 Hz |
| `OPLLX` | YM2413 (2413 ROM patch)  | 3.580 MHz |  49,715 Hz |
| `VRC7`  | VRC7                     | 3.580 MHz |  49,715 Hz |
| `Y8950` | Y8950 (ADPCM 対応)       | 3.580 MHz |  49,715 Hz |
| `OPL`   | YM3526                   | 3.580 MHz |  49,715 Hz |
| `OPL2`  | YM3812                   | 3.580 MHz |  49,715 Hz |
| `SCC`   | Konami SCC               | 3.580 MHz |  55,930 Hz |
| `DCSG`  | SN76489                  | 3.580 MHz | 223,721 Hz |

## ファイル構成

```
DSAemuEngine/
├── .gitmodules               ← submodule 定義
├── CMakeLists.txt
├── README.md
├── extern/
│   ├── emu2149/              ← git submodule
│   ├── emu2413/              ← git submodule
│   ├── emu8950/              ← git submodule
│   ├── emu2212/              ← git submodule
│   └── emu76489/             ← git submodule
└── src/
    ├── FmEngineApi.h         ← API ヘッダ (FMEngineTest と共通)
    └── DSAemuEngine.cpp      ← エンジン実装
```

## セットアップ

```bash
git clone https://github.com/<your-org>/DSAemuEngine
cd DSAemuEngine
git submodule update --init --recursive
```

## ビルド

### Linux / macOS

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
# 成果物: build/bin/libFmEngineApi.so  (Linux)
#         build/bin/libFmEngineApi.dylib (macOS)
```

### Windows (Visual Studio 2022)

```cmd
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
:: 成果物: build\bin\FmEngineApi.dll
```

## FMEngineTest との接続

ビルドした共有ライブラリを FMEngineTest の実行ディレクトリに置き、
`-e` オプションで指定します。

```bash
# Linux の例
cp build/bin/libFmEngineApi.so <FMEngineTest_dir>/
cd <FMEngineTest_dir>
./FMEngineTest -e ./libFmEngineApi.so patches/ssg.json
./FMEngineTest -e ./libFmEngineApi.so patches/opll.json
./FMEngineTest -e ./libFmEngineApi.so patches/all.json
```

## チップ固有の注意事項

### DCSG (SN76489)
レジスタアクセスはシリアルバイト形式です。`reg` は無視され、`val` が
そのまま `SNG_writeIO()` へ渡されます。JSON パッチの `val` に
シリアルバイトを直接記述してください。

### Y8950 (ADPCM)
ADPCM データは `FmEngine_SetMemory(chip_id, FM_MEM_ADPCM_B, data, size)` で
書き込みます。

### SCC
アドレスは `0xC000 + reg` にマッピングされます。

| オフセット | 内容 |
|---|---|
| `0x00–0x9F` | 波形データ (CH0–4 各 32 bytes) |
| `0x40–0x49` | 周波数レジスタ (CH0–4, 各 2 bytes) |
| `0x50–0x54` | ボリュームレジスタ (CH0–4) |
| `0x54`      | チャンネルイネーブルビットマスク |

## ライセンス

各 submodule はそれぞれのライセンスに従います。  
`emu2149`, `emu2413`, `emu8950`, `emu2212`, `emu76489` はすべて **MIT License** です。
