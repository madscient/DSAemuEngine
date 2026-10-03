# DSAemuEngine

**FmEngineApi** 準拠の音源エミュレーションエンジン。  
[digital-sound-antiques](https://github.com/digital-sound-antiques) の各エミュレーションコアを  
Git submodule として統合した共有ライブラリ (DLL / .so / .dylib) です。  
[Y8960](https://github.com/hra1129/Y8960_Cartridge) カートリッジの拡張 OPL2 部・拡張 OPLL 部・SSGS も、
emu8950 / emu2413 / emu2149 を改造したコアで提供します。

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
| `src/y8960/Y8960Opl2exCore.*`, `Y8960Opl2exAdpcm.*` | emu8950 v1.1.4 | 拡張 OPL2 部 (YM3812 + ADPCM-B) | `OPL2EX` |
| `src/y8960/Y8960OpllCore.*` | emu2413 v1.5.9 | 拡張 OPLL 部 (チャンネル別プリセット音色バンク) | `OPLLEX` |
| `src/y8960/Y8960SsgsCore.*` | emu2149 v1.42 | SSGS (YMZ705 の SSG 部相当。YM2149 × 2 + パンポット) | `SSGS` |

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
| `SSGS`   | Y8960 SSGS (YMZ705 の SSG 部相当) | SSG の動作クロック / 8 (3.579545 MHz で 223,721 Hz) |

`FmEngine_GetSupportedChip` はこの表の順にチップ名を返します。
ネイティブレートは、チップのコアが内部で音を生成するレートです。

クロックは `FmEngine_AddChip` の `clock` 引数で必ず指定します。エンジンは既定の
クロックを持たず、0 を渡すと `FM_ERR_INVALID_ARG` を返します。

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
├── src/
│   ├── FmEngineApi.h         ← API ヘッダ (FMEngineTest と共通)
│   ├── DSAemuEngine.cpp      ← エンジン実装
│   └── y8960/                ← OPL2EX / OPLLEX / SSGS 用のコア (emu8950 / emu2413 / emu2149 の改造版)
└── tests/
    └── api_test.cpp          ← DLL を実行時にロードして叩く試験
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

ビルドすると `api_test` も作られます。ビルドした共有ライブラリを実行時にロードし、
エクスポートされた関数だけを通して振る舞いを確かめます。

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

## 部位ごとのゲイン

FmEngineApi の任意エクスポート `FmEngine_GetPartCount` / `FmEngine_GetPartName` /
`FmEngine_SetPartGain` / `FmEngine_GetPartGain` に対応しています。部位は名前で指定します。

| チップ | 部位の名前 |
|---|---|
| `OPLL` / `OPLLP` / `OPLLX` / `VRC7` | `MELODY` (メロディ)、`RHYTHM` (リズム) |
| 上記以外 (`OPLLEX` を含む) | なし (`FmEngine_GetPartCount` は 0) |

部位のゲインの既定値は 1.0 です。実際に掛かるゲインは `FmEngine_SetGain` のゲインと
部位のゲインの積です。

## 外部メモリ

FmEngineApi の任意エクスポート `FmEngine_GetMemoryCount` / `FmEngine_GetMemoryName` /
`FmEngine_SetMemory` に対応しています。外部メモリは名前で指定します。

| チップ | 外部メモリの名前 | 内容 |
|---|---|---|
| `Y8950` | `ADPCM_B` | ADPCM の RAM モードのメモリ (256KB) |
| `Y8950` | `ADPCM_B_ROMMODE` | ADPCM の ROM モードのメモリ (256KB) |
| `OPL2EX` | `ADPCM_B` | ADPCM のサンプル RAM (256KB) |
| 上記以外 | なし (`FmEngine_GetMemoryCount` は 0) | |

`FmEngine_SetMemory` は、チップが自分で持つそのメモリの先頭へデータを複製します。
256KB を超える部分は捨てられます。チップが持たない名前を渡すと `FM_ERR_INVALID_ARG` です。

## 外部メモリの割り当て

FmEngineApi の任意エクスポート `FmEngine_SetMemoryEx` に対応しています。
呼び出し側のメモリブロックをチップのメモリの `[base, base + size)` に
割り当てます。`FM_ACCESS_RAM` で割り当てたブロックはエンジンが複製せずにその場で
読み書きするので、複数のチップや他のデバイスと共有できます。

| チップ | 外部メモリの名前 | 割り当てられるもの |
|---|---|---|
| `OPL2EX` | `ADPCM_B` | ROM / RAM とも、任意の `base` と `size` |
| `Y8950` | `ADPCM_B` | ROM は任意の `base` と `size`。RAM は `base` が 0 で `size` が 256KB 以上のものだけ |
| `Y8950` | `ADPCM_B_ROMMODE` | 同上 |

- 上の表以外の組み合わせは `FM_ERR_INVALID_ARG` です。`Y8950` に表の条件を満たさない
  RAM を割り当てると `FM_ERR_UNAVAILABLE` です
- 同じ RAM ブロックを複数のチップに割り当てると、チップ間でメモリを共有します。
  `Y8950` どうしでも、256KB 以上のブロックを `base` 0 に割り当てれば共有できます
- 割り当ての無い番地は読むと 0 です。割り当てが 1 つでもある間、そのメモリは割り当て
  だけで決まり、`FmEngine_SetMemory` で書き込んだ内容は見えません。割り当てをすべて
  外すと、`FmEngine_SetMemory` で書き込んだ内容が見えるようになります
- `FM_ACCESS_ROM` で割り当てたブロックへのチップからの書き込みは、`OPL2EX` では
  捨てられます。`Y8950` では捨てられず、エンジン内の複製に書き込まれます
  (ブロック自体は書き換わりません)。割り当ての無い番地への書き込みも同様です
- `Y8950` は ROM モードでも `0x0F` からの書き込みを ROM モードのメモリに書き込みます

## チップ固有の注意事項

### DCSG (SN76489)
レジスタアクセスはシリアルバイト形式です。`reg` は無視され、`val` が
そのまま `SNG_writeIO()` へ渡されます。JSON パッチの `val` に
シリアルバイトを直接記述してください。

### Y8950 (ADPCM)
ADPCM データは `FmEngine_SetMemory` で書き込みます。メモリの名前に `ADPCM_B` を
指定すると RAM モードのメモリ、`ADPCM_B_ROMMODE` を指定すると ROM モードのメモリの
先頭から入ります ([外部メモリ](#外部メモリ))。共有する RAM は `FmEngine_SetMemoryEx` で
割り当てます ([外部メモリの割り当て](#外部メモリの割り当て))。

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

### OPL2EX (Y8960 拡張 OPL2 部)

YM3812 に Y8950 の ADPCM-B を足したもので、レジスタ配置は Y8950 と同じです。
`reg` はレジスタ番号、`port` は使いません。

| レジスタ | 内容 |
|---|---|
| `0x07`, `0x09–0x12` | ADPCM-B |
| `0x08` | bit7-6 は CSM / NOTE-SEL、bit1 は ADPCM の 64K モード。bit0 (ROM) は無視 |
| `0xE0–0xF5` | 波形選択 (YM3812 と同じ)。`0x01` の bit5 が 1 のときだけ書き込みを受け付ける |
| 上記以外 | YM3812 と同じ |

- ADPCM のサンプルメモリは RAM 256KB だけで、ROM はありません。`0x08` の bit0 を
  立てても RAM から再生します
- サンプルデータは `FmEngine_SetMemory` にメモリの名前 `ADPCM_B` を指定して、
  RAM の先頭から書き込みます。256KB を超える部分は捨てられます。
  `0x07` の REC と MEMORY DATA を立てて `0x0F` に書き込む方法でも書けます
- `0x07` の bit3 (SP-OFF) を立てると ADPCM は無音になります
- 実機は 2 回路を持ち、2 回路で 256KB の SRAM を分け合います。2 回路を鳴らすときは
  `OPL2EX` を 2 個追加します。`FmEngine_SetMemory` で書き込むと、各チップが別々に
  持つ 256KB の RAM に入ります。2 個で SRAM を分け合うには、呼び出し側で 256KB の
  ブロックを用意し、`FmEngine_SetMemoryEx` で `FM_ACCESS_RAM` として割り当てます
  ([外部メモリの割り当て](#外部メモリの割り当て))

  | 分け方 | 1 個目 | 2 個目 |
  |---|---|---|
  | 共有 | ブロック全体を `base` 0 に | 同じブロック全体を `base` 0 に |
  | 分割 | 前半 128KB を `base` 0 に | 後半 128KB を `base` 0 に |
  | 片寄せ | ブロック全体を `base` 0 に | 割り当てない |

### OPLLEX (Y8960 拡張 OPLL 部)

YM2413 に、チャンネルごとにプリセット音色のバンクを選ぶレジスタを足したものです。
`reg` はレジスタ番号、`port` は使いません。

| レジスタ | 内容 |
|---|---|
| `0x00–0x3F` | YM2413 と同じ |
| `0x40–0x48` | ch0–ch8 の音色バンク (bit1-0)。上位ビットは無視 |

| バンク | プリセット音色 |
|---|---|
| 0 | OPLL (YM2413) |
| 1 | OPLL-X (YM2423 相当) |
| 2 | OPLL-P (YMF281 相当) |
| 3 | VRC7 (DS1001 相当) |

- バンクレジスタを書くと、そのチャンネルが鳴っている途中でも音色が切り替わります
- ユーザー音色 (音色 0) はバンクに属さず、全バンクで共通です
- リズム音色は全バンクで共通です
- プリセット音色は 4 バンクとも
  [Copyright free OPLL(x) ROM patches](https://github.com/plgDavid/misc/wiki/Copyright-free-OPLL(x)-ROM-patches)
  から取っています。そのためバンク 0 の音色は `OPLL` チップの音色と完全には一致しません
- 実機は 2 回路を持ちます。2 回路を鳴らすときは `OPLLEX` を 2 個追加してください

### SSGS (Y8960 SSGS)

YM2149 相当の SSG を 2 系統持ち、6 チャンネルそれぞれにパンポットを持ちます。
レジスタ配置は YMZ705 の SSG 部と同じです。`reg` はレジスタ番号、`port` は使いません。

| レジスタ | 内容 |
|---|---|
| `0x00–0x0D` | SSG-1 (YM2149 と同じ配置) |
| `0x10–0x12` | SSG-1 CH A–C のパンポット (bit3-0) |
| `0x20–0x2D` | SSG-2 (YM2149 と同じ配置) |
| `0x30–0x32` | SSG-2 CH A–C のパンポット (bit3-0) |
| 上記以外 | 無視 (`0x40` 以降を含む) |

- パンポットが中央のとき、SSG-1 / SSG-2 はそれぞれ、SSG の動作クロックを同じにした
  `SSG` チップと同じ音を出します
- `clock` はマスタークロックです。SSG はその 1/2 (5.12MHz 以上なら 1/3) で動きます。
  3.579545MHz を渡すと SSG は 1.789772MHz で動き、ネイティブレートはその 1/8 です
- パンポットは 0 が左端、8 が中央、15 が右端です。片側は全開のまま、反対側のレベル
  だけが線形に絞られます。0 と 1 はどちらも左端です

  | 値 | 0 | 1 | 4 | 8 | 11 | 14 | 15 |
  |---|---|---|---|---|---|---|---|
  | L | 1.000 | 1.000 | 1.000 | 1.000 | 0.571 | 0.143 | 0.000 |
  | R | 0.000 | 0.000 | 0.429 | 1.000 | 1.000 | 1.000 | 1.000 |

- リセット直後のパンポットは全チャンネル 8 (中央) です

## ライセンス

各 submodule はそれぞれのライセンスに従います。  
`emu2149`, `emu2413`, `emu8950`, `emu2212`, `emu76489` はすべて **MIT License** です。

`src/y8960/` のコアは emu8950 / emu2413 / emu2149 を改造したもので、元と同じ **MIT License** です。
ただし `src/y8960/Y8960OpllCore.c` のプリセット音色データは
"Copyright free OPLL(x) ROM patches" (David Viens, Hubert Lamontagne) によるもので、
**CC BY-SA** に従います。
