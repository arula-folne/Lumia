# Lumia

**Live × Music × View**

Windows（x64）向け OBS Studio ソースプラグイン **LumiaMusicView**。  
ローカルの音楽フォルダを、ジャケット・アルバム・曲名・アーティスト付きオーバーレイとして配信に載せられます（別途 Node サーバ不要）。

## ダウンロード

[Releases](https://github.com/arula-folne/Lumia/releases) から Windows 用インストーラー  
（`LumiaMusicView-*-windows-x64.exe`）を入手してください。

デザイン / Custom CSS エディタ: [folne.net/apps/lumia](https://folne.net/apps/lumia)

## インストール（エンドユーザー）

1. OBS Studio を終了する
2. インストーラー（`.exe`）を実行し、OBS フォルダを確認する
3. OBS を起動 → **ソース追加 → LumiaMusicView**
4. プレイリストに曲ファイルやアルバムフォルダを追加し、必要なら Web エディタの Custom CSS を貼り付ける

詳細は同梱の `INSTALL.txt` を参照。

## ソースからビルド（Windows）

必要環境:

- Visual Studio 2022 Build Tools（MSVC）
- OBS Studio が `C:\Program Files\obs-studio` にインストール済み（import lib 生成用）
- 配布用 `.exe` 作成時は [Inno Setup 6](https://jrsoftware.org/isinfo.php)

```bat
cd obs-plugin
build.bat
```

ローカルへ直接コピー（管理者）:

```bat
install-to-obs.bat
```

配布パッケージ（インストーラー）作成:

```powershell
.\scripts\package-release.ps1 -Version 0.1.1
```

## ライセンス

[GNU General Public License v3.0](LICENSE.txt)（日本語の説明を先頭に掲載）

## メモ

- シャッフルはアルバム単位ではなく、全曲を混ぜます
- ループはプレイリスト終端で最初から再開します
- アーティストは可能なら Windows のファイルプロパティ、アルバム名はフォルダ名、曲名はファイル名（拡張子なし）です

---

## English

OBS Studio source plugin **LumiaMusicView** for Windows (x64).  
Local music folders become an on-stream overlay — jacket, album, title, and artist — without a separate Node server.

Download the installer from [Releases](https://github.com/arula-folne/Lumia/releases) (`LumiaMusicView-*-windows-x64.exe`).  
Design / Custom CSS: [folne.net/apps/lumia](https://folne.net/apps/lumia)

License: [GPL-3.0](LICENSE.txt)
