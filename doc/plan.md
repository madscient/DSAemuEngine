# DSAemuEngine 作業計画・仕様・経緯

AI がセッションをまたいで作業を引き継ぐための文書。利用者向けの現在の仕様は
`README.md`、このリポジトリでの作業規則は `CLAUDE.md` にある。

## 確度の読み方

| 印 | 意味 |
|---|---|
| **確認済み** | このリポジトリで走らせて確かめた。何でどう確かめたかを添える |
| **確認済み(読解)** | ソースや文書を読んで確かめた。走らせてはいない |
| **未検証** | 作ったが動かして確かめていない |
| **推測** | 出典を示せない。根拠を一行添える |

## 0. 現在地と次の一手

2026-10-02 時点。

**入っているもの**

- Y8960 の拡張 OPL2 部 `OPL2EX` と拡張 OPLL 部 `OPLLEX`（§3.1、§3.2）
- FmEngineApi の部位ゲイン（任意エクスポート 3 関数）。部位を持つのは
  `OPLL` / `OPLLP` / `OPLLX` / `VRC7`（§3.3）
- emu2413 / emu8950 のステレオ出力のレート変換の位相ずれを、エンジン側で揃えた（§3.4）。
  **既存チップの出力がネイティブレート以外で変わる**
- `tests/api_test.cpp`（40 項目）と CTest への登録

**ユーザーに判断を仰いでいること**（§6）

- `OPLLEX` に部位を持たせるか（今は持たせていない）
- OPL2EX の波形選択の細部を emu8950 と openMSX 系フォークのどちらに揃えるか（今は emu8950）

**保留**: `OPL2EX` 2 個でサンプル RAM を共有させるか（§6.2）。ユーザーが FmEngineApi を
改定するので、それを待つ。

**上流へ報告する不具合**（§9）: ユーザーが後で issue / PR を出す。出したら番号を §9 に書く。

**次の一手**: 上の判断待ち。FMEngineTest の `docs/CHANGELOG.md` は DSAemuEngine に
部位ゲインが無いと記録している（`0f5c786` 時点）。あちらを直すかは FMEngineTest 側の作業。

## 1. 目的とスコープ

- Y8960 カートリッジの拡張 OPL2 部・拡張 OPLL 部を、emu8950 / emu2413 を土台に
  FmEngineApi のチップとして提供する（2026-10-02 のユーザー依頼）
- FMEngineTest の `docs/FmEngineApi.md` の改訂（`e39b206`、部位ゲイン）に追随する

**スコープ外**: Y8960 の SSGS / DCSG / SCC / MSX-TIMER / ミキサー、I/O イネーブラと
メモリマップド I/O の窓。チップの外の配線はアプリケーションの領分である。

## 2. 情報源と役割

| 情報源 | 版 | 使ったもの | 役割 |
|---|---|---|---|
| madscient/blueMSX-plus_Y8960 | `d824b4d1` | `blueMSX/Src/SoundChips/Y8960OpllCore.{c,h}`（最終変更 `49df6d61`）、`doc/fork/y8960/implementation-plan.md` §3.3・§5.1・§5.2 | OPLLEX のコードの出所。OPL2EX は仕様判断だけを参照 |
| madscient/openMSX_Y8960 | `main` `b5850635c` | `doc/fork/y8960/implementation-plan.md` §3.4・§3.4.1・§3.4.3、`src/sound/Y8960OPL2.cc`・`Y8960Adpcm.cc`（最終変更 `7aa2a9684`） | OPL2EX の仕様判断の情報リソース。コードは取り込んでいない |
| madscient/MsxSoundSuiteExtension | `463f7c6` | `docs/y8960/hardware.md` | Y8960 を駆動するファームウェアが前提にしている仕様（クロック、OPLL-EX / OPL2-EX のレジスタ） |
| madscient/Y8960emu | `b695a31` | `README.md`、`doc/Y8960emu_Architecture.md`、`src/FmChip.h` | 同じ Y8960 を FmEngineApi で出す ymfm ベースのエンジン。チップ名と「1 回路 = 1 チップ」のモデルを揃えた |
| madscient/FMEngineTest | `e39b206` | `docs/FmEngineApi.md`、`docs/CHANGELOG.md` | FmEngineApi の仕様の正 |
| madscient/YMEngine | `8f81213` | `src/FmEngineApi.h` | 参照ヘッダ。`src/FmEngineApi.h` はこの写し（一字も変えていない） |

### 2.1 持ち込んではいけないもの

**GPL のコード。** 本リポジトリは MIT である。

- blueMSX-plus_Y8960 の `Y8960Opl2Core.cpp` / `Y8960Opl2Adpcm.cpp` と、openMSX_Y8960 の
  `Y8960OPL2` / `Y8960Adpcm` は openMSX の Y8950 のフォークで、GPL v2 以降
  （**確認済み(読解)**: 各ファイル冒頭のライセンス表記）。依頼文は「blueMSX-plus_Y8960 に
  emu8950/emu2413 ベースの実装がある」としていたが、**emu ベースなのは OPLLEX だけ**だった。
  OPL2EX は emu8950 を自分でフォークし、両フォークの仕様判断だけを参照した
- blueMSX-plus_Y8960 の `Y8960OpllCore.*` にあった blueMSX のセーブステート用の追加
  （`Y8960OPLL_RateConv_getBufferLength`、`Y8960OPLL_relinkAfterRestore`）と、その
  変更表記は外した。本エンジンに用途が無い。残りは emu2413（MIT）と Y8960 向けの変更と
  音色データ（CC BY-SA）である

### 2.2 落としてはいけない帰属表示

OPLLEX の音色データは "Copyright free OPLL(x) ROM patches"（David Viens, Hubert
Lamontagne、CC BY-SA）。帰属表示は `src/y8960/Y8960OpllCore.c` の音色表の直前のコメントと、
`README.md` のライセンス節にある。

## 3. 仕様

### 3.1 OPLLEX

**コア**: `src/y8960/Y8960OpllCore.{c,h}`。emu2413 v1.5.9 のフォークで、公開記号は
`OPLL*` → `Y8960OPLL*` に改名してある（素の emu2413 と同じバイナリに入るため）。

素の emu2413 との差（**確認済み**: 改名を戻して submodule の `emu2413.c/.h` と diff を取った。
submodule の作業ツリーは記録コミット `11676f6` と `CMakeLists.txt` だけが違い、ソースは同一）:

| 差 | 内容 |
|---|---|
| 音色表 4 面 | `patch[19*2]` → `patch[4][19*2]`。バンク順は 0 OPLL / 1 OPLL-X / 2 OPLL-P / 3 VRC7 |
| チャンネル別バンク | `patch_bank[9]`。`set_patch()` がこれを見て向け先を選ぶ |
| バンクレジスタ | `40h`-`48h` の b1:b0。`reg[]` を `0x49` 語に広げた |
| ユーザー音色 | `user_patch[2]`。バンクに属さずチップに 1 組 |
| 関数の引数 | `resetPatch()` は全バンクを読み直す。`setPatch` / `copyPatch` はバンクを取る |

**エンジン側**: `Y8960OPLL_new()` が音色の読み込みとリセットまで済ませる。出力は
OPLL 系と同じくメロディを L、リズムを R に振り分けて取り出し（§3.3）、部位ゲイン 1.0 で
足し合わせる。部位は持たせていない（§6.1）。

### 3.2 OPL2EX

**コア**: `src/y8960/Y8960Opl2exCore.{c,h}`、`Y8960Opl2exAdpcm.{c,h}`。emu8950 v1.1.4 の
フォークで、公開記号は `OPL*` → `Y8960OPL*` に改名してある。

素の emu8950 との差（**確認済み**: 改名を戻して submodule の 4 ファイルと diff を取った。
submodule の作業ツリーは記録コミット `c27078c` と `CMakeLists.txt` だけが違う）:

| 差 | 内容 | 理由 |
|---|---|---|
| チップ種別を削除 | ADPCM を常に持ち、`E0h`-`F5h` の波形選択を常に `01h` の b5 で許す | emu8950 は ADPCM を Y8950 に、波形選択を YM3812 にしか与えない |
| ADPCM の ROM を削除 | `08h` の b0 を無視。アドレス単位は b1 だけで決まる（0: 4 バイト、1: 32 バイト。emu8950 の RAM256K / RAM64K と同じ） | Y8960 の ADPCM の後ろには SRAM しか無い（両フォークの文書。2026-09-12 のユーザー回答が出典） |
| サンプル RAM を外に出す | `Y8960OPL_setADPCMMemory()` で窓を渡す。窓の外の読みは 0、書き込みは捨てる | 実機は 2 回路で 256KB を分け合い、分け方が 4 状態ある。器を先に作った |
| `emuadpcm.c` の外部記号 | `FILE *fp` を削除、`decode_*_address` を `static` に | 素の emu8950 と同じバイナリに入るため |

**変えていないもの**（emu8950 のまま）: 波形選択の効き方（後述）、`07h` の b3（SP-OFF）で
ADPCM が無音になること、64K モード、`15h`-`17h`（受けて何もしない）、ステータス。

**エンジン側**: チップ 1 個ごとに 256KB の `std::vector` を確保し、窓として渡す。
`FmEngine_SetMemory(FM_MEM_ADPCM_B)` は先頭から書き、256KB を超える分は捨てる。

**両フォーク（openMSX_Y8960 / blueMSX-plus_Y8960）との違い**（**確認済み(読解)**:
本リポジトリ側はコード。両フォーク側は openMSX_Y8960 の `Y8960OPL2.cc` の
`updateWaveTables` と `0xe0` の分岐、および両フォークの `implementation-plan.md`）:

| 点 | 本リポジトリ（emu8950 のまま） | 両フォーク（openMSX の Y8950 由来） |
|---|---|---|
| `01h` の b5 が 0 の間 | `E0h`-`F5h` の書き込みを捨てる。b5 を 0 にする前に選んであった波形は残る | `E0h`-`F5h` の書き込みは憶えるが、全スロットがサインで鳴る。b5 を 1 にすると憶えた波形になる |
| リズム用スロット | 波形選択が効く | サインのまま |
| サンプル RAM | チップごとに 256KB | 2 回路で 1 つの 256KB を共有（起動時） |

前の 2 つは実機（Y8960 の OPL2 は jtopl2）で決まる。§6.3。

**両フォークの文書と実装の食い違い**（**確認済み(読解)**）: どちらの文書も「`07h` の b3
（SP-OFF）を落とした」と書くが、両方の ADPCM はいまも b3 で無音になる
（openMSX_Y8960 `Y8960Adpcm.cc` の `calcSample`、blueMSX-plus_Y8960 `Y8960Opl2Adpcm.cpp`）。
本リポジトリも b3 で無音になるので、振る舞いは揃っている。

### 3.3 部位ゲイン

- `FmEngine_SetPartGain` / `FmEngine_GetPartGain` / `FmEngine_GetPartMask` を
  エクスポートする（仕様上は任意）
- 部位を持つのは `OPLL` / `OPLLP` / `OPLLX` / `VRC7`（`FM_PART_OPLL_MELODY` /
  `FM_PART_OPLL_RHYTHM`）。他は 0。仕様書の表どおり
- 2 系統の取り出し方: emu2413 のパン（拡張機能）で ch0-8 を L だけ、BD/HH/SD/TOM/CYM を
  R だけに出させ、`calcStereo` の L と R をそれぞれメロディとリズムとして受け取る。
  エンジンが部位ゲインを掛けて L/R に混ぜ直す。`OPLL_reset()` はパンを中央に戻すので、
  リセットの後に掛け直す必要がある
- 引数の扱いは YMEngine に合わせた: 持たない部位・範囲外の部位・未知の `chip_id`・
  `GetPartGain` の出力ポインタが null のときは `FM_ERR_INVALID_ARG`
- `Set/GetPartGain` は `write_mutex` を取る（`Generate` と並行して呼べるという仕様のため）

### 3.4 レート変換の位相合わせ

**事実**（**確認済み**）: emu2413 と emu8950 のレート変換器（`*_RateConv_getData`）は、
補間位置を決める `timer` を全チャンネルで共有し、呼ぶたびに `f_ratio` だけ進める。
`*_calcStereo` は 1 出力サンプルにつき L と R で 2 回呼ぶので、L と R が別々の、どちらも
誤った位置で補間される。

- 計測: emu2413 単体で、48kHz、メロディ 1 音 + リズム 5 音、中央定位。素の
  `OPLL_calcStereo` は |L−R| が最大 10129 LSB（ピーク 17401）、L と mono の `OPLL_calc` の
  差が最大 10298 LSB。timer を 1 出力サンプルにつき 1 回だけ進めると、L = R = mono が
  ビット一致した
- コードの所在: emu2413 `emu2413.c` の `OPLL_RateConv_getData` / `OPLL_calcStereo`、
  emu8950 `emu8950.c` の `OPL_RateConv_getData` / `OPL_calcStereo`
  （**確認済み(読解)**。emu8950 側は同じ形のコード）

**対処**: `src/DSAemuEngine.cpp` の `calcStereoAligned()`。呼ぶ前に timer を 1 サンプル
ぶん進めて置き、呼び出しの間だけ `f_ratio` を 0 にする。submodule とフォークは改造していない。
全 OPL / OPLL 系チップ（`OPLL` `OPLLP` `OPLLX` `VRC7` `Y8950` `OPL` `OPL2` `OPL2EX`
`OPLLEX`）に掛けている。

**影響**: これらのチップの出力が、ネイティブレート（`clock / 72`）以外で変わる（§5.3）。
`SSG` `SCC` `SCCP` `DCSG` は変換器が別物で、影響しない。

**前提**: `*_RateConv` の構造体のフィールド `timer` / `f_ratio` が公開ヘッダにあり、
`getData` 以外で `f_ratio` を使わないこと（変換表は生成時に作られる）。上流での扱いは §9.1。

### 3.5 外に出る値

| 値 | 内容 | 前提 | やり直しの値段 |
|---|---|---|---|
| チップ名 | `OPL2EX` / `OPLLEX` | 依頼文と Y8960emu が同じ名前を使っている | `kChipTable`、README、試験、利用側のパッチ |
| 対応チップの並び | 既存 11 個の後ろに追加 | 既存の index を保存している利用者がいても壊さない | 並べ替えは index の互換を壊す |
| 既定のクロック | 両方 3579545Hz | MSSX `hardware.md`「OPLL-EX / OPL2-EX は 3.579545MHz」 | 定数 1 つ |
| `reg` / `port` | `reg` はレジスタ番号、`port` は無視（他のチップと同じ） | Y8960emu も `port=0` では同じ（**確認済み(読解)**: `src/FmChip.h` の `write`）。`port≠0` の Y8960emu の扱いは**未確認** | 定義を変えると README と利用側 |
| OPLLEX の部位 | 無し | 仕様書の部位の表に OPLLEX が無い | §6.1 |
| OPL2EX のサンプル RAM | チップごとに 256KB。`SetMemory` はそこへコピーする。共有しない | FmEngineApi にチップ間でメモリを共有させる口が無い。Y8960emu は `SetMemory` が参照なので結果として共有できる（§6.2） | §6.2 |

## 4. 決定事項

### 4.1 OPL2EX は emu8950 をフォークする（2026-10-02）

依頼は「emu8950 ベースで実装、blueMSX-plus_Y8960 から切り出してよい」。blueMSX 側の
OPL2EX は GPL なので切り出せない（§2.1）。emu8950 は ADPCM と波形選択の両方をすでに
持っているので、チップ種別の分岐を外すだけで OPL2EX になる。

**前提**: 素の emu8950 を `Y8950` / `OPL` / `OPL2` に使い続けること。使わなくなれば
フォークせず改造で済む。

### 4.2 改名してフォークする（2026-10-02）

blueMSX-plus_Y8960 と同じ方式（公開記号に `Y8960` を前置）。OPLLEX はそのまま写し、
OPL2EX は `sed` で機械的に改名してから差分を当てた。**フォークは上流と行単位で近いまま
保つ**（改名を戻せば上流と diff が取れる）。

### 4.3 部位の 2 系統はパンで取り出す（2026-10-02）

emu2413 の内部の `ch_out[]` を直接読むと、レート変換器より前の値しか取れない。パンなら
公開 API だけで、変換器を通った 2 系統が得られる。§3.4 の位相合わせが前提。

## 5. 確かめたこと

### 5.1 api_test（**確認済み**）

`tests/api_test.cpp`、40 項目すべて通過。Windows（Visual Studio 2026、x64 Release）と
Linux（WSL の Ubuntu 18.04、GCC 7.5、`-Wall -Wextra -fvisibility=hidden -fno-common`。
cmake が無かったので同じフラグで gcc / g++ を直接呼んだ）の両方。Linux では
`src/` と `tests/` に警告なし（`extern/` の警告は見ていない）、公開記号は `FmEngine_*` の
17 個だけ（`nm -D`）。Windows のエクスポートも同じ 17 個（`dumpbin /exports`）。

試験の作り: 新しいチップは、素のコアのチップに同じ書き込みをして出力を突き合わせる。

| 見たこと | 突き合わせ |
|---|---|
| OPL2EX の FM（波形選択あり / なし、リズム含む） | `OPL2` とビット一致 |
| OPL2EX の ADPCM | `Y8950` とビット一致 |
| `08h` b0 を立てても RAM から鳴る | 立てない場合とビット一致。`Y8950` では出力が変わる |
| `0Fh` 経由の書き込み | `SetMemory` と同じ出力 |
| チップごとに RAM が別 | 別チップに入れたデータは鳴らない |
| OPLLEX の 4 バンク | 音色 1 の出力が 4 バンクで互いに違う |
| チャンネル別のバンク | 2 チャンネル同時の出力 = 別々に鳴らした和（4 LSB 以内）、かつ「後勝ち」とは違う |
| 鳴っている途中のバンク切替 | 切り替えない場合と出力が違う |
| ユーザー音色 | バンク 0 と 3 でビット一致、`OPLL` ともビット一致 |
| `49h` 以降 | 書いても出力が変わらない |
| 部位ゲイン | メロディだけ = メロディだけを鳴らした `OPLL` とビット一致、リズムも同様。既定ゲインの出力 = 両者の和 |
| 位相合わせ | `Y8950` `OPL` `OPL2` `OPL2EX` が 48kHz で L == R |

### 5.2 試験の判別力（**確認済み**）

ソースの写しに 1 か所ずつ壊す変更を入れ、試験が落ちることを確かめた。

| 壊し方 | 落ちた項目 |
|---|---|
| M1 バンクレジスタを無視（常にバンク 0） | 4 バンクの違い、チャンネル別バンク、途中の切替（3 件） |
| M2 バンクレジスタがチップ全体に効く | チャンネル別バンク（1 件） |
| M3 OPL2EX の波形選択を無効化 | `OPL2` との一致（1 件） |
| M4 OPL2EX が `08h` b0 で ROM（0）を読む | ROM ビットの項目（1 件） |
| M5 メロディ / リズムを振り分けない | 部位の 3 件 |
| M6 OPL2EX の RAM を全チップで共有 | RAM がチップごとの項目（1 件） |
| M7 位相合わせを外す（素の挙動） | L == R の 4 件 |

M5 は最初の版の試験では落ちない作りだった（**確認済み(読解)**: 両部位に全体の和が
入っても「和が全体と一致」「片方を 0 にすると鳴る」は成り立つ。最初の版に M5 を掛けて
走らせてはいない）。「片方だけを鳴らしたチップと一致」を足して、落ちることを確かめた。

**見ていないもの**: OPLL 系の位相合わせは DLL 越しでは直接見られない（L と R に別の
信号を振り分けているため）。同じ関数 `calcStereoAligned` を通る OPL 系で見ており、
OPLL は次の §5.3 で見ている。

### 5.3 既存チップとの比較（**確認済み**）

変更前のソース（`0f5c786` と同じ作業ツリー）でビルドした DLL と、変更後の DLL に同じ書き込みを
して 0.5 秒ぶん比べた（48000 / 49715 / 44100Hz）。

- `SSG` `SCC` `SCCP` `DCSG`: 全レートでビット一致
- OPL / OPLL 系: 49715Hz（変換器を通らない）ではビット一致。48000 / 44100Hz では違う
  （§3.4 の修正による）

変更後の出力が正しい側であることは、コアを直接リンクして mono 経路（`OPLL_calc` /
`OPL_calc`。`getData` を 1 回だけ呼ぶので変換器を正しく使う）の出力と DLL の出力を比べて
確かめた。

| | 変更前の DLL | 変更後の DLL |
|---|---|---|
| `OPL2`、48kHz | 最大 2893 LSB 違う | ビット一致 |
| `OPLL`、48kHz | 最大 10304 LSB 違う | 最大 1 LSB（メロディとリズムを別々に変換する丸め） |
| 44.1kHz | `OPL2` 2591、`OPLL` 11823 LSB | `OPL2` 0、`OPLL` 1 LSB |

これらの比較に使ったプログラムはリポジトリに置いていない（変更前の DLL が要るため）。

### 5.4 確かめていないこと（**未検証**）

- 実機との一致。Y8960 の実機は未完成で、OPL2 は jtopl2、OPLL は IKAOPLL（両フォークの
  `hardware-notes.md` §1。**確認済み(読解)**）
- FMEngineTest を本 DLL で走らせること（パッチに `OPL2EX` / `OPLLEX` が無い）
- macOS でのビルド

## 6. 未決事項

### 6.1 OPLLEX に部位を持たせるか

YM2413 の部位はメロディとリズムが別の端子から出ることに由来する。Y8960 の OPLLEX は
FPGA 内のブロックで、デジタルミキサーへの渡し方は分からない（ミキサーのレジスタ表は
未記入。MSSX `hardware.md`。**確認済み(読解)**）。

**値段**: 出力はすでに 2 系統で取り出しているので、`partMask()` に 1 行、README の表、
試験の期待値。加えて FMEngineTest の仕様書の部位の表に `OPLLEX` を足す必要がある
（仕様書の側の作業）。

### 6.2 OPL2EX 2 個でサンプル RAM を共有させるか — **保留。FmEngineApi の改定待ち（2026-10-02、ユーザー判断）**

ユーザーが API を改定する。改定されたら FMEngineTest の `docs/FmEngineApi.md` を読み、
下の材料と合わせて決め直す。

実機は 2 回路で 256KB を分け合い、起動時は共有（両フォークと MSSX の文書。
**確認済み(読解)**）。本エンジンはチップごとに別の RAM を持つ。`0Fh` 経由で片方の回路から
書いたサンプルを、もう片方で鳴らすソフトは、本エンジンでは鳴らない。

**値段**: コアは窓を外から受け取る形にしてあるので、エンジン側で RAM の持ち主と窓の
割り当てを決めるだけで済む。決めるべきは外から見える部分（どのチップどうしを組にするか、
`SetMemory` がどこに書くか）で、FmEngineApi に口が無い。

**Y8960emu（`b695a31`）のやり方**（**確認済み(読解)**: `src/FmChip.h` の
`MemoryYmfmInterface`、`src/opl2ex.cpp` の `write_data`。動かしてはいない）:

- 共有のための仕組みは無い。1 回路 = 1 チップ
- `FmEngine_SetMemory` はコピーせず、呼び出し元のポインタを覚える。読み出しはそのバッファを
  直接見る。**2 チップに同じバッファを渡せば、再生は結果として共有になる**（README と設計書に
  共有の手段としての記載は無い）
- そのバッファは書き込み不可の扱いで、書ける領域を作る `allocMemory` はどこからも呼ばれて
  いない。`07h` の REC / MEMORY DATA と `0Fh` による書き込みは捨てられる。`SetMemory` の前は
  読み出しが 0
- 本エンジンとの差: 本エンジンの `SetMemory` はチップ自身の 256KB へのコピーで、`0Fh` 経由で
  書ける（自チップの RAM だけ）。FmEngineApi の仕様（「data の寿命は呼び出し元が管理する
  こと」）はどちらの作りも許す

**案**（未決）: `SetMemory` を Y8960emu と同じ参照にすれば、同じバッファを渡すだけで共有に
なる。ただし受け取るデータは `const` なので、そのままでは `0Fh` 経由の書き込みと両立しない。
書き込みも共有したいなら、API の外に共有の手段が要る。

**見送った案**: 同じエンジン内の `OPL2EX` を追加順に 2 個ずつ組にして共有させる。
理由: 組み方が暗黙になり、1 個だけ使う利用者の `SetMemory` の意味が変わる。
Y8960emu とも振る舞いが分かれる。

### 6.3 波形選択の細部をどちらに揃えるか

§3.2 の表の前 2 行。実機（jtopl2）の振る舞いを読めば決まる。本リポジトリは
emu8950 の YM3812 のまま。**どちらが実機に近いかは未確認**。
**値段**: フォークの `writeReg` の `01h` と `E0h` の分岐と、リズムの計算の数行。
試験の「`OPL2` とビット一致」は成り立たなくなるので、試験の作りも変わる。

### 6.4 §3.4 を上流へ報告するか — **決着。報告する（2026-10-02、ユーザー判断）**

ユーザーが後で issue / PR を出す。記録は §9。

## 7. 見送った提案

- **blueMSX-plus_Y8960 の OPL2EX（openMSX 由来）を写す**。理由: GPL（§2.1）
- **emu2413 の内部の `ch_out[]` を読んで部位を分ける**。理由: レート変換器より前の値で、
  変換を自前で持つことになる（§4.3）
- **位相ずれを submodule で直す**。理由: submodule を改造しない規則。エンジン側で
  公開フィールドだけを使って揃えられた
- **OPLL 系の位相合わせを DLL 越しに試験する**。理由: 同じ信号を L と R の両方に出す
  手段が API に無い。コア単体の計測（§5.3）で代えた

## 8. 実行経緯

### 2026-10-02 — OPL2EX / OPLLEX と部位ゲイン

1. 依頼: OPL2EX / OPLLEX を emu8950 / emu2413 ベースで実装、AI 向け文書 `doc/plan.md` と
   運用規則だけの `CLAUDE.md` を作る、FmEngineApi の改訂に追随する。途中で
   openMSX_Y8960 も参照するよう指示があった
2. blueMSX-plus_Y8960 を読み、OPL2EX が openMSX 由来（GPL）と分かった。OPLLEX は
   emu2413 のフォークなので写し、OPL2EX は emu8950 をフォークした（§4.1）
3. 部位ゲインをパンで実装した（§4.3）。既存チップを変更前と比べたところ、OPLL 系が
   48kHz で大きく違った。調べると emu2413 / emu8950 の `calcStereo` の位相ずれ（§3.4）で、
   変更前から既存の全 OPL / OPLL 系チップにあった。エンジン側で揃えた
4. 試験を書き、壊した版で落ちることを確かめた（§5.2）。M5 で試験の弱さが分かり直した
5. 上流の不具合はユーザーが後で issue / PR を出すことになった。再現と修正案を確かめ、§9 に記録した

## 9. 上流へ報告する不具合

**2026-10-02 のユーザー判断で、ユーザーが後で issue / PR を出す。** 出したら番号と状態を
各項の「報告」に書く。

対象の版はどちらも上流の最新で、submodule の記録コミットと同じ（**確認済み**:
2026-10-02 に GitHub API で各リポジトリの最新 3 コミットを見た）。issue のタイトルを
stereo / rate / conv / resampl / leak / memory / timer / adpcm で検索した範囲では、同じ報告は
無かった（**確認済み**。本文までは見ていない）。

計測はすべて Windows（Visual Studio 2026、x64 Release）で、submodule のソースを試験用の
プログラムに直接リンクして行った。試験用のプログラムはリポジトリに置いていない。

### 9.1 emu2413 / emu8950: `calcStereo` で L と R の補間位置がずれる

- **報告**: 未（issue / PR の番号をここに書く）
- **対象**: digital-sound-antiques/emu2413 `11676f6`（v1.5.9）の `emu2413.c`
  `OPLL_RateConv_getData` / `OPLL_calcStereo`。digital-sound-antiques/emu8950 `c27078c`
  （v1.1.4）の `emu8950.c` `OPL_RateConv_getData` / `OPL_calcStereo`
- **起きること**: 出力レートが `clock / 72` と違ってレート変換器が働くとき、`calcStereo` の
  L と R が別々の位置で補間される。パンが中央でも L ≠ R になり、どちらも mono の `calc` の
  出力と一致しない
- **原因**: 変換器の `timer` は全チャンネルで 1 つで、`getData` を呼ぶたびに `f_ratio` だけ
  進む。`calc` は 1 出力サンプルにつき 1 回呼ぶので正しいが、`calcStereo` は L と R で 2 回
  呼ぶので、1 出力サンプルで 2 回進む。`getData` の直前のコメント「this function must be
  called f_out / f_inp times per one putData call」も、チャンネルごとに 1 回ずつの意味で
  書かれていない
- **計測**（**確認済み**）: 48kHz、0.5 秒。パンは既定（中央）

  | コア | 鳴らしたもの | ピーク | max \|L−R\| | max \|L − mono\| |
  |---|---|---|---|---|
  | emu2413 | メロディ 1 音 + リズム 5 音 | 17401 | 10129 | 10298 |
  | emu8950 (YM3812) | 6 音 + リズム 5 音、波形選択なし | 6320 | 4658 | 4656 |

  単位は 16bit の LSB。mono は同じ書き込みをした別インスタンスの `OPLL_calc` / `OPL_calc`
- **再現**（emu2413。emu8950 は `OPL_*` に置き換えて同じ）:

  ```c
  OPLL *mono = OPLL_new(3579545, 48000), *st = OPLL_new(3579545, 48000);
  /* 両方に同じ書き込み: 30h=10h 10h=40h 20h=15h 16h=20h 26h=05h 17h=50h 27h=05h
     18h=C0h 28h=01h 0Eh=20h 0Eh=3Fh */
  for (int i = 0; i < 24000; i++) {
    int32_t s[2];
    int m = OPLL_calc(mono);
    OPLL_calcStereo(st, s);   /* s[0] != s[1]、s[0] != m になるサンプルがある */
  }
  ```

- **修正案**（**確認済み**: 下の差分を submodule のソースの写しに当て、上の計測をやり直した。
  emu2413 / emu8950 とも max |L−R| = 0、max |L − mono| = 0 になった）。L を取る前の `timer` を
  覚えておき、R を取る前に戻す。公開 API は変わらない

  ```diff
  --- emu2413.c  OPLL_calcStereo
     if (opll->conv) {
  +    double timer = opll->conv->timer;
       out[0] = OPLL_RateConv_getData(opll->conv, 0);
  +    opll->conv->timer = timer;
       out[1] = OPLL_RateConv_getData(opll->conv, 1);
  --- emu8950.c  OPL_calcStereo
     if (opl->conv) {
  +    double timer = opl->conv->timer;
       out[0] = OPL_RateConv_getData(opl->conv, 0);
  +    opl->conv->timer = timer;
       out[1] = OPL_RateConv_getData(opl->conv, 1);
  ```

  別案: `timer` を進める処理を `getData` から出し、出力 1 サンプルにつき 1 回呼ぶ関数にする。
  公開関数 `*_RateConv_getData` の意味が変わるので、上の案より影響が大きい
- **本リポジトリ側**: `src/DSAemuEngine.cpp` の `calcStereoAligned()` で回避している（§3.4）。
  上の修正案が上流に入っても、回避策はそのままで正しい（呼ぶ前に置いた `timer` を、
  上流の修正が L と R で同じに使うだけになる。**確認済み(読解)**）。
  **上流の修正が別の形（チャンネルごとの `timer` など）だった場合は、回避策が誤りになりうる。**
  submodule を上げるときは、回避策を外して §5.3 の比較（`OPL_calc` / `OPLL_calc` との一致）を
  やり直す。api_test の「L == R」は、両チャンネルが同じ誤った位置に揃った場合を見分けられない

### 9.2 emu8950: Y8950 以外の種別にすると ADPCM のメモリが解放されない

- **報告**: 未（issue / PR の番号をここに書く）
- **対象**: emu8950 `c27078c` の `emu8950.c` `refresh_adpcm_object`
- **起きること**: `OPL_new` は種別 0（Y8950）で ADPCM を作る。その後 `OPL_setChipType` で
  1（YM3526）か 2（YM3812）にすると、ADPCM の RAM と ROM（各 256KB）が解放されずに残る。
  YM3526 / YM3812 として使うときは、1 個作るたびに 512KB 漏れる
- **原因**: `free(opl->adpcm)` で構造体だけを解放し、`OPL_ADPCM_delete` を呼んでいない
- **計測**（**確認済み**）: `OPL_new` → `OPL_setChipType(opl, 2)` → `OPL_delete` を 200 回で、
  プロセスの PagefileUsage が 1.2MB → 103.1MB。`OPL_setChipType` を呼ばない（Y8950 のまま）
  200 回では 103.1MB → 103.6MB
- **修正案**（**確認済み**: 写しに当てて同じ計測をやり直し、YM3812 の 200 回で 1.3MB → 1.0MB）

  ```diff
  --- emu8950.c  refresh_adpcm_object
       if (opl->adpcm != NULL) {
  -      free(opl->adpcm);
  +      OPL_ADPCM_delete(opl->adpcm);
         opl->adpcm = NULL;
  ```

- **本リポジトリ側**: 回避していない。`OPL` / `OPL2` チップを 1 個足すごとに 512KB が
  プロセスの終わりまで残る。OPL2EX のフォークにはこの関数が無い

### 9.3 ついでに見つけたもの（報告は任意）

- emu8950 `emuadpcm.c` に、使われていない外部変数 `FILE *fp;` と、外部リンケージの
  `decode_start_address` / `decode_stop_address` がある。利用側に同じ名前があると
  リンクで衝突しうる（GCC 10 以降の既定の `-fno-common` では、同名の `fp` は多重定義になる）。
  **確認済み(読解)**。本リポジトリでは素の emu8950 を 1 つしかリンクしないので起きていない
- emu76489 / emu2149 / emu2212 には同じ形のレート変換器が無く、9.1 は当たらない
  （**確認済み(読解)**: `RateConv` / `f_ratio` / `->timer` を検索して当たらず、
  `SNG_calc_stereo` は補間をしない）
