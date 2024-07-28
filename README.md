# SBC8080-CPM

電脳伝説さん設計のSBC8080と私自身の過去作EMUZ80-57Qを組み合わせて  
CP/M 2.2 を動作させるためのファームウエアです。

SBC8080の技術情報は以下サイトをご参照ください。  
http://www.amy.hi-ho.ne.jp/officetetsu/storage/sbc8080_techdata.pdf

EMUZ80-57QとZ80を組み合わせてCP/Mを走らせるファームウエアは  
Hanyazouさんが公開されています。  
https://github.com/hanyazou/SuperMEZ80

本ソースは、Hanyazouさんのソースを私自身の学習のために  
EMUZ80-57Qに特化させ機能を絞り込んで再構築したものです。  
![stacked board photo](https://github.com/Gazelle8087/SBC8080-CPM/blob/main/photo/IMG_5043.JPG)  
## ビルド環境

PICファームウエア MPLABX IDE v6.20  XC8 v2.46  

8080 IPL, CP/M BIOS, CP/M BDOS&CCPのアセンブルには  
Macro Assembler AS V1.42 を使用しました。  
http://john.ccac.rwth-aachen.de:8000/as/  

生成したバイナリをCソースに取り込み可能なテキストに  
変換するために、pythonを使っています。  
私はWindows11上でPythonを使うために Anacondaを導入しています。  
https://www.anaconda.com/download/

CP/M BDOS, CCPのソース  
The Unofficial CP/M Web site http://www.cpm.z80.de/  
内の以下リンクより8080 インテルニモニックソースを取得しました。  
このソースに CPU種類指示 "CPU 8080"を加えると  
AS V1.42にて正常にアセンブルできました。  
元のソースではプロンプト文字が小文字になるため   
1バイトだけパッチを当てました。  
http://www.cpm.z80.de/download/cpm2-asm.zip  

Anaconda プロンプトにて AS V1.42へのコマンドパスを通した上で  
アセンブル-バイナリ生成-Cソースへのインクルードファイルまでを  
一連で実行するバッチファイルを用意しました。  
z80フォルダ内の  
ipl.bat IPLとCP/MのBIOSを作成  
cpm.bat CP/MのBDOSとCCPを作成  

生成したインクルードファイルは、PICのFWビルドの際にPICの  
ROM領域に書き込まれ、IPLは8080起動前にSRAMに展開され  
BDOS,CCP,BIOSは8080実行時にIN命令をトリガとし  
PICのROMからSRAMにDMAにて8080バイナリが直接展開されます。  

このことにより、8080のコードを変更する度にディスクイメージを  
SDに書き込む必要がなくなります。  
(ディスクのシステムトラックのCP/Mコードは使いません)  

## EMUZ80-57Q

SBC8080にてCP/M 2.2を動作させるためのベースボードです。  
写真はSBC8080と組み合わせるための必要最低限の部品のみ  
旧仕様の基板に実装した状態です。  
57Q-0416.zipは実際に基板発注に使ったガーバーデータです、  
2023年4月時点でJLCPCB発注実績ありです。  
（シルク印刷誤植がありますが未修正のままです）
![base board photo](https://github.com/Gazelle8087/SBC8080-CPM/blob/main/photo/IMG_5039.JPG)  
### 基板上のジャンパ設定  
SBC8080と組み合わせるにはSRAM周辺に5個所のジャンパが必要です  
![base board photo](https://github.com/Gazelle8087/SBC8080-CPM/blob/main/photo/emuz80-57q.jpg)

