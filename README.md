# DSAemuEngine

**FmEngineApi** 準拠の音源エミュレーションエンジン。  
[digital-sound-antiques](https://github.com/digital-sound-antiques) の各エミュレーションコアを  
Git submodule として統合した共有ライブラリ (DLL / .so / .dylib) です。  
[Y8960](https://github.com/hra1129/Y8960_Cartridge) カートリッジの拡張 OPL2 部・拡張 OPLL 部・SSG 部・SCC も、
emu8950 / emu2413 / emu2149 / emu2212 を改造したコアで提供します。

## 統合コア (submodules)

| パス | リポジトリ | チップ | FmEngineApi チップ名 |
|---|---|---|---|
| `extern/emu2149`  | [emu2149](https://github.com/digital-sound-antiques/emu2149)   | YM2149 / AY-3-8910 | `SSG` |
| `extern/emu2413`  | [emu2413](https://github.com/digital-sound-antiques/emu2413)   | YM2413 (OPLL)      | `OPLL` / `OPLLP` / `OPLLX` / `VRC7` |
| `extern/emu8950`  | [emu8950](https://github.com/digital-sound-antiques/emu8950)   | Y8950 / YM3526 / YM3812 | `Y8950` / `OPL` / `OPL2` |
| `extern/emu2212`  | [emu2212](https://github.com/digital-sound-antiques/emu2212)   | Konami SCC / SCC-I | `SCC` / `SCCP` |
| `extern/emu76489` | [emu76489](https://github.com/digital-sound-antiques/emu76489) | SN76489 (DCSG)     | `DCSG` |

## Y8960 用のコア (submodule の改造版)

| パス | 元にしたコア | Y8960 のブロック | FmEngineApi チップ名 |
|---|---|---|---|
| `src/y8960/Y8960Opl2exCore.*`, `Y8960Opl2exAdpcm.*` | emu8950 v1.1.4 (レート変換と出力のタイミングは v1.2.3 と同じ。ADPCM は Y8960 の振る舞いに合わせて書き直し) | 拡張 OPL2 部 (YM3812 + ADPCM-B) | `OPL2EX` |
| `src/y8960/Y8960OpllCore.*` | emu2413 v1.5.9 (レート変換と出力のタイミングは v1.6.4 と同じ) | 拡張 OPLL 部 (チャンネル別プリセット音色バンク) | `OPLLEX` |
| `src/y8960/Y8960SsgsCore.*` | emu2149 v1.42 | SSG 部 (YM2149 × 2 + チャンネルごとの音量と定位) | `SSGEX` |
| `src/y8960/Y8960SccCore.*` | emu2212 v1.16 (レート変換の比を正確にし、`0x90–0x9F` をミラーにした) | SCC (標準の SCC + チャンネルごとの定位) | `SCCEX` |

`extern/` の submodule は改造せず、そのまま `SSG` / `OPL2` / `Y8950` / `OPLL` などに使います。

## 対応チップ一覧

| チップ名 | 実チップ | ネイティブレート |
|---|---|---|
| `SSG`   | YM2149 / AY-3-8910       | clock / 8 (2.000 MHz で 250,000 Hz) |
| `OPLL`  | YM2413 (2413 ROM patch)  | clock / 72 (3.579545 MHz で 49,715 Hz) |
| `OPLLP` | YM2413 (281B patch)      | clock / 72 |
| `OPLLX` | YM2413 (2413 ROM patch)  | clock / 72 |
| `VRC7`  | VRC7                     | clock / 72 |
| `Y8950` | Y8950 (ADPCM 対応)       | clock / 72 |
| `OPL`   | YM3526                   | clock / 72 |
| `OPL2`  | YM3812                   | clock / 72 |
| `SCC`   | Konami SCC               | clock / 2 (3.579545 MHz で 1,789,772 Hz) |
| `SCCP`  | Konami SCC-I (SCC+)      | clock / 2 |
| `DCSG`  | SN76489                  | clock / 16 (3.579545 MHz で 223,721 Hz) |
| `OPL2EX` | Y8960 拡張 OPL2 部 (YM3812 + ADPCM-B) | clock / 72 |
| `OPLLEX` | Y8960 拡張 OPLL 部 (YM2413 + 音色バンク) | clock / 72 |
| `SSGEX`  | Y8960 SSG 部 (YM2149 × 2) | clock / 8 (1.789772 MHz で 223,721 Hz) |
| `SCCEX`  | Y8960 SCC (Konami SCC + 定位) | clock / 2 |

`FmEngine_GetSupportedChip` はこの表の順にチップ名を返します。
ネイティブレートは、チップのコアが内部で音を生成するレートです。

クロックは `FmEngine_AddChip` の `clock` 引数で必ず指定します。エンジンは既定の
クロックを持たず、0 を渡すと `FM_ERR_INVALID_ARG` を返します。

チップごとの挙動 (部位ごとのゲイン、外部メモリ、各チップのレジスタと注意事項) は
[doc/chips.md](doc/chips.md) にあります。

## ファイル構成

```
DSAemuEngine/
├── .gitmodules               ← submodule 定義
├── CMakeLists.txt
├── README.md
├── doc/
│   └── chips.md              ← チップごとの挙動
├── extern/
│   ├── emu2149/              ← git submodule
│   ├── emu2413/              ← git submodule
│   ├── emu8950/              ← git submodule
│   ├── emu2212/              ← git submodule
│   └── emu76489/             ← git submodule
├── src/
│   ├── FmEngineApi.h         ← API ヘッダ (FMEngineTest と共通)
│   ├── DSAemuEngine.cpp      ← エンジン実装
│   └── y8960/                ← OPL2EX / OPLLEX / SSGEX / SCCEX 用のコア (emu8950 / emu2413 / emu2149 / emu2212 の改造版)
└── tests/
    ├── api_test.cpp          ← DLL を実行時にロードして叩く試験
    ├── y8960_scc_test.cpp    ← SCCEX 用のコアを直接叩く試験
    └── y8960_adpcm_test.cpp  ← OPL2EX の ADPCM のコアを直接叩く試験
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

ビルドディレクトリのパスが深いと、MSBuild がパス長の上限に当たって失敗します。

### 試験

ビルドすると `api_test`、`y8960_scc_test`、`y8960_adpcm_test` も作られます。`api_test` は
ビルドした共有ライブラリを実行時にロードし、エクスポートされた関数だけを通して振る舞いを
確かめます。`y8960_scc_test` は `SCCEX` 用のコアを、元の emu2212 と並べて直接確かめます
(音程とミラーを含む)。`y8960_adpcm_test` は `OPL2EX` の ADPCM のコアを直接叩き、API から
見えない状態レジスタ、`0x0F` の読み出し、復号器の値と、出力の周波数特性を確かめます。

```bash
ctest --test-dir build -C Release --output-on-failure
```

試験を作らない場合は `-DDSAEMU_BUILD_TESTS=OFF` を指定します。

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

## ライセンス

各 submodule はそれぞれのライセンスに従います。  
`emu2149`, `emu2413`, `emu8950`, `emu2212`, `emu76489` はすべて **MIT License** です。

`src/y8960/` のコアは emu8950 / emu2413 / emu2149 / emu2212 を改造したもので、元と同じ **MIT License** です。
ただし `src/y8960/Y8960OpllCore.c` のプリセット音色データは
"Copyright free OPLL(x) ROM patches" (David Viens, Hubert Lamontagne) によるもので、
**CC BY-SA** に従います。
