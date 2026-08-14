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
| `extern/emu2212`  | [emu2212](https://github.com/digital-sound-antiques/emu2212)   | Konami SCC / SCC-I | `SCC` / `SCCP` |
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
| `SCC`   | Konami SCC               | 3.580 MHz | 1,789,772 Hz |
| `SCCP`  | Konami SCC-I (SCC+)      | 3.580 MHz | 1,789,772 Hz |
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
# 成果物: build/bin/libDSAemuEngine.so  (Linux)
#         build/bin/libDSAemuEngine.dylib (macOS)
```

### Windows (Visual Studio 2022)

```cmd
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
:: 成果物: build\bin\DSAemuEngine.dll
```

## FMEngineTest との接続

ビルドした共有ライブラリを FMEngineTest の実行ディレクトリに置き、
`-e` オプションで指定します。

```bash
# Linux の例
cp build/bin/libDSAemuEngine.so <FMEngineTest_dir>/
cd <FMEngineTest_dir>
./FMEngineTest -e ./libDSAemuEngine.so patches/ssg.json
./FMEngineTest -e ./libDSAemuEngine.so patches/opll.json
./FMEngineTest -e ./libDSAemuEngine.so patches/all.json
```

## チップ固有の注意事項

### DCSG (SN76489)
レジスタアクセスはシリアルバイト形式です。`reg` は無視され、`val` が
そのまま `SNG_writeIO()` へ渡されます。JSON パッチの `val` に
シリアルバイトを直接記述してください。

### Y8950 (ADPCM)
ADPCM データは `FmEngine_SetMemory(chip_id, FM_MEM_ADPCM_B, data, size)` で
書き込みます。

### SCC / SCCP
`reg` は Z80 メモリ空間上のレジスタ窓オフセットです。窓の先頭アドレス
(`SCC` は 0x9800、`SCCP` は 0xB800) はエンジンが加算します。

`SCC`

| オフセット | 内容 |
|---|---|
| `0x00–0x1F` | 波形 CH0 (32 bytes, 符号付き 8bit) |
| `0x20–0x3F` | 波形 CH1 |
| `0x40–0x5F` | 波形 CH2 |
| `0x60–0x7F` | 波形 CH3 (CH4 と共有) |
| `0x80–0x89` | 周波数 CH0–4 (各 lo, hi の 2 bytes / 12bit 値 N) |
| `0x8A–0x8E` | ボリューム CH0–4 (下位 4bit) |
| `0x8F`      | チャンネルイネーブル (bit0–4) |
| `0xE0–0xFF` | deformation register |

`SCCP`

| オフセット | 内容 |
|---|---|
| `0x00–0x1F` | 波形 CH0 |
| `0x20–0x3F` | 波形 CH1 |
| `0x40–0x5F` | 波形 CH2 |
| `0x60–0x7F` | 波形 CH3 |
| `0x80–0x9F` | 波形 CH4 (独立) |
| `0xA0–0xA9` | 周波数 CH0–4 |
| `0xAA–0xAE` | ボリューム CH0–4 |
| `0xAF`      | チャンネルイネーブル (bit0–4) |
| `0xC0–0xDF` | deformation register |

発音周波数は `f = clock / (32 * (N + 1))` です。窓外のオフセットへの書き込みは
実機と同様に無視されます。

互換 (`SCC`) と拡張 (`SCCP`) の選択はチップ名で行います。マッパーレジスタ
(0xBFFE) とデバイス有効化 (0x9000 / 0xB000) はチップ生成時に設定されるため、
アプリケーション側で書き込む必要はありません。

## ライセンス

各 submodule はそれぞれのライセンスに従います。  
`emu2149`, `emu2413`, `emu8950`, `emu2212`, `emu76489` はすべて **MIT License** です。
