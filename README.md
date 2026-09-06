# ifmgs.sph/axmgs.sph - PostScript/PDF/AI Susie Plug-ins

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Platform: Windows](https://img.shields.io/badge/Platform-Windows-blue.svg)](https://www.microsoft.com/windows)

- PostScript/EPS/AI/PDFファイル用の 64bit Susieプラグインです
- **ifmgs.sph** (IN): PostScript/EPS画像ビューアプラグイン
- **axmgs.sph** (AX): PDF/AI複数ページ対応アーカイブプラグイン
- あふｗで動作確認しています

## 特徴

- Ghostscriptを使用して割かし高速表示がんばっている
- 設定ファイルで表示DPIを指定可能である

## インストール

1. `gsdll64.dll` を使用しますので https://www.ghostscript.com/download/gsdnld.html からインストーラをダウンロードしてGhostscriptをインストールしてください

2. ifmgs.sph なり axmgs.sph をプラグインフォルダに置いてください

3. あふｗなら
   - **ifmgs.sph** 置くだけでOKです
   - **axmgs.sph** 置いてから、拡張子判別実行で `pdf ai` に対して `&S_ARC axmgs.sph` を指定してください

## 設定ファイル（任意）

各プラグインは同じフォルダ内に INI ファイルがあれば読み込みます（無ければデフォルト値で動きます）

- `ifmgs.sph` ↔ `ifmgs.ini`
- `axmgs.sph` ↔ `axmgs.ini`

例:

```ini
[render]
; Ghostscript レンダリング解像度 (DPI)。
; デフォルト 150。30〜1200 の範囲外は無効として無視されます。
dpi=200
```

## 📚 参考資料

### 開発に使用したソフトウェア・参考にした情報
- **[Susie 32bit / 64bit Plug-in の仕様(2025-8-10版) - TORO's Library](http://toro.d.dooo.jp/dlsphapi.html)**: susie.h を拝借
- **[runspx](https://github.com/toroidj/runspx)**: APIの動作確認用
- **[Ghostscript](https://www.ghostscript.com/)**: PostScript/EPSのレンダリングエンジン
