# schedule-viewer-m5epd

M5Paper(M5EPD)のE-Inkディスプレイに、PC上のOutlook予定表を6時間分のタイムライン表示するビューアです。
PC側のPythonスクリプトがOutlookの予定をUSBシリアル経由でM5Paperに送信し、デバイス側がそれを受信してE-Inkに描画します。

> **インターネット通信は一切行いません。** 通信はPCとM5Paper間のUSBシリアルのみで完結します(Wi-Fi/Bluetoothは使用しない)。

## 構成

```
.
├── src/                    # M5Paper (ESP32) ファームウェア
│   ├── main.cpp            # setup/loop、シリアル受信、RTC同期
│   ├── protocol.cpp/.h      # PC↔デバイス間のテキストプロトコル
│   ├── schedule.cpp/.h      # 予定データの保持
│   ├── display.cpp/.h       # E-Ink描画(タイムラインUI)
│   └── time_util.h          # UTC/JST変換ユーティリティ
├── pc_python/
│   └── scheduler_sender.py  # Outlook予定取得 → シリアル送信
└── platformio.ini           # PlatformIOビルド設定
```

## 動作の流れ

1. M5Paperが起動すると `REQ:ALL` をシリアルへ送信
2. PC側 (`scheduler_sender.py`) が受信し、当日のOutlook予定を取得して送信
3. M5Paperは受信データを画面に描画(GC16フルリフレッシュ)
4. PC側は10分おきに自動で再送信し、画面を更新

## 通信プロトコル(PC → デバイス)

```
NOW:{utc_epoch}
EVENT:CLEAR
EVENT:ADD\t{start_utc}\t{end_utc}\t{title}\t{location}
...
EVENT:FINISH
```

- `NOW:` … 現在時刻(UTC epoch秒)。デバイスのRTC同期に使用
- `EVENT:CLEAR` … 受信時点で保持している予定データを即座にクリア
- `EVENT:ADD` … 予定を1件追加(タブ区切り)
- `EVENT:FINISH` … 受信完了の合図。これを受けてのみ画面を再描画する

デバイス → PC:

```
REQ:ALL
```

起動時に送信し、全予定の再送を要求する。

## デバイス側(M5Paper) ファームウェア

### 必要環境

- [Visual Studio Code](https://code.visualstudio.com/)
- VSCode拡張機能 [PlatformIO IDE](https://marketplace.visualstudio.com/items?itemName=platformio.platformio-ide)
- M5Paper本体(M5EPDライブラリ)

### ビルド/書き込み(VSCode + PlatformIO)

1. VSCodeでこのプロジェクトのフォルダを開く(PlatformIO拡張が自動的にプロジェクトを認識する)
2. M5PaperをUSBで接続する
3. 画面下部のステータスバー、または左側のPlatformIOアイコン(蟻のマーク)から以下を実行
   - **Build**(✓アイコン): ビルドのみ
   - **Upload**(→アイコン): ビルドして書き込み
   - **Monitor**(プラグアイコン): シリアルモニタを開く(115200bps)

`platformio.ini` で `m5stack/M5EPD` ライブラリと `huge_app.csv` パーティションを使用。

### フォント

`src/display.cpp` の `FONT_PATH` で指定したTTFフォントをSDカードのルートに配置する。
現在は `/MPLUS1-ExtraBold.ttf` ([M PLUS 1](https://mplusfonts.github.io/)) を使用。

### 画面レイアウト

- 540×960、`SetRotation(90)` の縦長表示
- ヘッダー: 日付・現在時刻
- 左側ラベル列: 1時間ごとの時刻
- メインエリア: 現在時刻から6時間分のタイムラインに予定ボックスを表示
- 現在時刻位置に横線+丸印のインジケーターを表示

### 時刻管理

- `time_util.h` の `JST_OFFSET`(+9時間)でUTC⇔JSTを変換
- `NOW:` 受信時にシステムクロックとRTC(JST wall-clock)の両方を同期
- 起動時はRTCから時刻を復元するため、シリアル未接続でもおおよその時刻表示が可能

## PC側(Python) スクリプト

`pc_python/scheduler_sender.py`

### 必要環境

- Python 3
- `pywin32`(Outlook COM操作)
- `pyserial`(シリアル通信)

```sh
pip install pywin32 pyserial
```

### 設定ファイル

実行ディレクトリに以下を配置する。

`config.json`:
```json
{
  "com_port": "COM3"
}
```

- `com_port` に `"FILE"` を指定すると、シリアルの代わりに `output.txt` へ送信内容を書き出す(デバッグ用)。

`filter_words.txt`(任意):
```
正規表現パターン=置換後文字列
```

予定タイトル・場所のテキストに対して、行ごとに `正規表現=置換文字列` の形式で置換ルールを適用できる。

### 実行

```sh
python pc_python/scheduler_sender.py
```

- 起動時にシリアルポートを監視するスレッドを開始し、デバイスから `REQ:ALL` を受信すると即座に予定を送信
- それとは別に10分おきの定時送信も行う
- 当日 0:00〜24:00 の予定のうち、`BusyStatus` が「仮の予定」「空き」以外のものを送信対象とする

### 注意: タイムゾーンの扱い

OutlookのCOMが返す `item.Start` / `item.End` はJSTの時刻をそのままUTCとしてタグ付けした値になっているため、
`.timestamp()` の結果は真のUTCより9時間進んでいる。送信前に9時間(`9 * 3600`秒)を引いて真のUTC epochに補正している。
