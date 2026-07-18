@call storage="snow.ks"
; Load the snow plug-in
@wait time=200

@eval exp="f.bg=''"
; Stores the currently loaded background

; Define macro changebg_and_clear
@macro name="changebg_and_clear"
@if exp="mp.storage != f.bg"
; If it's the same as the currently loaded background, don't execute the change
@eval exp="f.bg = mp.storage"
@backlay
@layopt layer=message0 page=back visible=false
@trans method=crossfade time=300
@wt
@image * layer=base page=back
@trans method=crossfade time=300
@wt
@cm
@layopt layer=message0 page=back visible=true
@trans method=crossfade time=300
@wt
@endif
@endmacro

; Define macro loadbg
@macro name="loadbg"
@image * layer=base
@eval exp="f.bg = mp.storage"
@endmacro

; Define macro start_select
@macro name="start_select"
@backlay
@nowait
@history output=false
@current page=back
@endmacro

; Define macro end_select
@macro name="end_select"
@trans method=crossfade time=400
@wt
@endnowait
@history output=true
@current page=fore
@endmacro

; Define macro i
@macro name="i"
@font italic=true
@endmacro

; Define macro /i
@macro name="/i"
@font italic=false
@endmacro



*syokai_start|Introduction to Kirikiri and KAG - Menu
@startanchor

; Load the background image, draw the menu on the message layer
@backlay
@loadbg storage="_24_5" page=back
@current page=back
@cm
@layopt layer=message0 page=back visible=true
@nowait
@history output=false
@style align=center
[font size=40 color=0x00ffff]吉里吉里とKAGの紹介[resetfont][r]
[font size=20 color=0x00ffff]Introduction to Kirikiri and KAG[resetfont][r]
[r]
[link target="*about_kirikiri"]吉里吉里とは[endlink][r]
[link target="*about_kirikiri_en"]What is Kirikiri?[endlink][r]
[r]
[link target="*about_kag"]KAGとは[endlink][r]
[link target="*about_kag_en"]What is KAG?[endlink][r]
[link target="*about_kirikirisdl2"]AetherKiriとは？[endlink][r]
[link target="*about_kirikirisdl2_en"]What is AetherKiri?[endlink][r]
[font size=18][link storage="first_zh_cn.ks" target="*syokai_start_zh_cn"]简体中文[endlink] / [link storage="first_zh_tw.ks" target="*syokai_start_zh_tw"]繁體中文[endlink] / [link storage="first_ko.ks" target="*syokai_start_ko"]한국어[endlink][resetfont][r]
[r]
;[link exp="kag.shutdown()" color=0xff0000 hint="吉里吉里/KAGの紹介を終了します / Exit the Introduction to Kirikiri/KAG"]終了 / Exit[endlink]
@endnowait
@history output=true
@current page=fore

; Message layer transition
@trans method=crossfade time=800
@wt

; History recording
@record

; Wait until a selection is made
@s

*to_syokai_start
; return to syokai_start
@backlay
@layopt layer=message0 page=back visible=false
@trans method=crossfade time=300
@wt
@jump target=*syokai_start

*about_kirikiri|吉里吉里とは
@changebg_and_clear storage="_24_4"
　吉里吉里は、TJSというスクリプト言語を使っていろいろな事をするためのソフトウェアです。[l][r]
　TJSはJavaとJavaScriptを足して３で割ったような言語で、CやC++に比べれば習得しやすい言語だと思います。[l][r]
　吉里吉里ではこのTJSで本体を制御することにより、さまざまなアプリケーションを作成することができます。[l][r]
　特にマルチメディア系の機能が強く、比較的静的な表現を用いる２Ｄゲームに適しています。[p]
*about_kirikiri2|
@cm
　吉里吉里は、レイヤと呼ばれる画面を何枚も重ね合わせて画面を構成します。[l]レイヤはアルファブレンドによる重ね合わせが可能で、階層構造を採ることもできます。[l][r]
　レイヤには標準でPNG/JPEG/ERI/BMPを読み込み可能で、Susie-pluginで読み込み可能な形式を拡張することもできます。[l][r]
　描画はあまり複雑なことはできませんが、半透明矩形の描画やアンチエイリアス可能な文字表示、画像の拡大縮小や変形を行う事ができます。[l][r]
　AVI/MPEGやSWF(Macromedia Flash)をムービーとして再生させることができます。[p]
*about_kirikiri3|
@cm
　吉里吉里ではCD-DA、MIDIシーケンスデータ、PCMを再生させることができ、それぞれ音量調節が可能です。[l]PCMは無圧縮の.WAVファイルのほか、プラグインで再生可能な形式を拡張でき、OggVorbisも再生することができます。[l][r]
　PCMは複数を同時に再生することができます。[l]CD-DAやMIDIシーケンスデータでも無理矢理やろうと思えば複数同時再生できます。[p]
*about_kirikiri4
@cm
　その他、周辺ツールとして、
複数のファイルを一つにまとめたり、単体で実行可能なファイルを作成することができる[font color=0xffff00]Releaser[resetfont]、[l]
吉里吉里本体の設定を行う[font color=0xffff00]吉里吉里設定[resetfont]、[l]
制作者側でフォントを用意し、プレーヤ側にフォントがインストールされて無くても使えるようにする[font color=0xffff00]レンダリング済みフォント作成ツール[resetfont]、[l]
透明度を持った画像フォーマット間の相互変換を行う[font color=0xffff00]透過画像フォーマットコンバータ[resetfont]があります。[l]
[r]
[r]
@start_select
[link target=*to_syokai_start]メニューに戻る[endlink]
@end_select
[s]

*about_kag|KAGとは
@changebg_and_clear storage="_24_4"
　KAGは、ビジ○アルノベルやサウンドノベルのようなノベル系ゲームや、選択肢を選んでストーリーが進むような文字ベースのアドベンチャーゲームを作成するためのキットです。[l][r]
　KAGは吉里吉里をゲームエンジンとして動作させるためのスクリプトで、それ自体はTJSスクリプトで書かれています。[l]KAG用のスクリプトは「シナリオ」と呼ばれ、TJSスクリプトとはまた別のものです。[l]TJSスクリプトはプログラミングの知識がかなり必要になりますが、シナリオはより簡単で記述しやすいものです。[l][r]
　KAGは吉里吉里の上に成り立つシステムなので、吉里吉里の機能のほとんどはKAGで使用できます。[p]
*about_kag3|
@cm
　KAGの文字表示は、ご覧の通りのアンチエイリアス文字表示に加え、[l][r]
[font size=60]大きな文字[resetfont]を表示したり、[l][r]
[ruby text="る"]ル[ruby text="び"]ビ[ruby text="を"]を[ruby text="ふ"]振[ruby text="っ"]っ[ruby text="た"]た[ruby text="り"]り、[l][font shadow=false edge=true edgecolor=0xff0000]縁取り文字にしたり[resetfont]、[l][r]
[style align=center]センタリングしてみたり[r]
[style align=right]右詰めしてみたり[r][resetstyle]
[l]
[graph storage="ExQuestion.png" alt="!?"]のような特殊記号を表示してみたり、[l][r]
と、いろいろできます。[p]
*about_kag4|
@position vertical=true
　また、縦書き表示をすることもできます。[l][r]
　縦書きでも横書きと全く同じ機能を使用することができます。[p]
@layopt layer=message0 visible=false
@layopt layer=message1 visible=true
@current layer=message1
@position frame=messageframe left=20 top=280 marginl=16 margint=16 marginr=0 marginb=16 draggable=true vertical=false
　このようにメッセージ枠の中にメッセージを表示させることもできます。[l]アドベンチャーゲームで良くあるタイプです。[p]
@layopt layer=message1 visible=false
@layopt layer=message0 visible=true
@current layer=message0
@position vertical=false
*about_kag5|
@cm
　立ち絵はこのように(あいかわらず[ruby text="・"]う[ruby text="・"]にですみません)
@backlay
@image storage=uni page=back layer=0 visible=true opacity=255
@trans method=crossfade time=1000
@wt
アルファブレンドによる重ね合わせが可能です。[l][r]
　このように
@backlay
@layopt page=back layer=0 opacity=128
@trans method=crossfade time=1000
@wt
薄く表示することもできます。[l][r]
　標準の状態で３つまで重ね合わせて表示できます。[p]
@backlay
@layopt page=back layer=0 visible=false
@trans method=crossfade time=300
@wt
*about_kag6|
@cm
　トランジション(画面切り替え)には標準で３つの種類があります。[l][r]
　一つは単純なクロスフェード、[l]
@backlay
@layopt page=back layer=message0 visible=false
@trans method=crossfade time=300
@wt
@backlay
@image storage="_24" page=back layer=base
@trans method=crossfade time=3000
@wt
@backlay
@image storage="_24_4" page=back layer=base
@trans method=crossfade time=3000
@wt
@backlay
@layopt page=back layer=message0 visible=true
@trans method=crossfade time=300
@wt
[l][r]
　もう一つはスクロール効果を出すことのできるスクロールトランジション、[l]
@backlay
@layopt page=back layer=message0 visible=false
@trans method=crossfade time=300
@wt
@backlay
@image storage="_24" page=back layer=base
@trans method=scroll from=right stay=stayfore time=3000
@wt
@backlay
@image storage="_24_4" page=back layer=base
@trans method=scroll from=bottom stay=nostay time=3000
@wt
@backlay
@layopt page=back layer=message0 visible=true
@trans method=crossfade time=300
@wt
[l][r]
　そして最後は制作者が自由にパターンを作成できるユニバーサルトランジションです。[l][r]
　ユニバーサルトランジションはルール画像と呼ばれるグレースケールの画像を用意し、その画像の暗いところからより早く切り替えが始まるものです。[l][r]
　たとえば、[l]
@image layer=base page=fore storage="trans1"
このようなルール画像であれば・・・[l]
@backlay
@layopt page=back layer=message0 visible=false
@image storage="_24_4" page=back layer=base
@trans method=crossfade time=300
@wt
@backlay
@image storage="_24" page=back layer=base
@trans method=universal rule="trans1" vague=64 time=3000
@wt
@backlay
@image storage="_24_4" page=back layer=base
@trans method=universal rule="trans1" vague=64 time=3000
@wt
@backlay
@layopt page=back layer=message0 visible=true
@trans method=crossfade time=300
@wt
[r]
　たとえば、[l]
@image layer=base page=fore storage="nami"
このようなルール画像であれば・・・[l]
@backlay
@layopt page=back layer=message0 visible=false
@image storage="_24_4" page=back layer=base
@trans method=crossfade time=300
@wt
@backlay
@image storage="_24" page=back layer=base
@trans method=universal rule="nami" vague=64 time=3000
@wt
@backlay
@image storage="_24_4" page=back layer=base
@trans method=universal rule="nami" vague=64 time=3000
@wt
@backlay
@layopt page=back layer=message0 visible=true
@trans method=crossfade time=300
@wt
[r]
　という感じで、いろいろ作ることができます。[p]
*about_kag7|KAGとは
@cm
　BGMとしてはCD-DA、MIDI、PCMのいずれかを使用できます。[l]効果音にはPCMを使用できます。[l]いずれもフェードなどの音量制御ができます。[l][r]
　PCMは標準で無圧縮の.WAVを再生できます。[l]また、プラグインで再生可能な形式を拡張でき、OggVorbisも再生できます。[l][r]
　ムービーはAVI/MPEG/SWFを再生できます。[p]
*about_kag8|
@cm
　KAGの変数は文字列でも数値でも入れることができ、変数の数は無制限、文字列の長さも無制限、数値は整数だけでなく実数も扱うことができます。[l]これはKAGの変数の仕様というか、KAGのベースとなっているTJSの仕様です。[l][r]
　変数にはゲーム変数とシステム変数の２種類あって、ゲーム変数は栞とともに読み込まれたり保存されたりしますが、システム変数は栞とは関係なく、常に同じ内容を保つことができるものです。[l][r]
　変数を使った例を示します・・・[p]
@eval exp="f.v1 = intrandom(1, 9)"
@eval exp="f.v2 = intrandom(1, 9)"
@eval exp="f.ans = f.v1 * f.v2"
@eval exp="f.input = ''"
*about_kag_var_0|計算問題
@cm
　計算問題です。[emb exp="f.v1"] × [emb exp="f.v2"] は？[r]
[font size=20](下の入力欄に入力したら、よこの「OK」をクリックしてください)[resetfont][r]
[r]
@start_select
　[edit name="f.input" length=200 opacity=80 bgcolor=0x000000 color=0xffffff] [link target=*about_kag_var_1]　　　OK　　　[endlink][r]
[r]
　[link target=*about_kag_9]面倒なのでとばす[endlink]
@end_select
@eval exp="kag.fore.messages[0].links[0].object.focus()"
; ↑入力欄にフォーカスを設定する
; 「システム - 前に戻る」でこの位置に戻れるようにここで通過記録を行う
@record
[s]

*about_kag_var_1
@commit
@jump cond="str2num(f.input) == f.ans" target=*about_kag_var_correct
@cm
　不正解！[l][r]
　もう一度入力してください。[p]
@jump target=*about_kag_var_0

*about_kag_var_correct
@cm
　正解です！[p]
@jump target=*about_kag_9

*about_kag_9|
@cm
@snowinit forevisible=true backvisible=false
　KAGの大きな特徴として、その高い拡張性とカスタマイズ性が挙げられます。[l]KAGだけでは実現できないような機能も、TJSを使って直接吉里吉里を制御すればいろいろな事ができます。[l][r]
　たとえば、KAG用プラグインとして「雪」を表示させるプラグインを読み込めば、このように雪を表示させることができます。[l]ほかにもトランジションの種類を増やすプラグインなどがあります。[l][r]
　また、KAGそのものがTJSスクリプトで書かれているため、スクリプトを変更すれば隅々にわたって動作をカスタマイズする事ができます。[p]
@backlay
@snowopt backvisible=false
@trans method=crossfade time=1000
@wt
@snowuninit
*about_kag_fin|KAGの紹介おしまい
@cm
　KAGの紹介はこれでおしまいです。[l][r]
　みなさんも是非吉里吉里/KAGを使ってすばらしいゲームを作ってください。[l][r]
[r]
@start_select
[link target=*to_syokai_start]メニューに戻る[endlink]
@end_select
[s]

*about_kirikirisdl2|AetherKiriとは？
@changebg_and_clear storage="_24_4"
AetherKiriは、KiriKiri2コンテンツを現代の端末で動かすためのGodot 4.7ベースのアプリケーションシェルです。[l][r]
macOS、iOS／iPadOS、Android、Webを対象とし、ゲームのディレクトリまたはXP3アーカイブを読み込めます。[l][r]
KiriKiri2を別のスクリプト方式で再実装するのではなく、既存のC++コアをGodotホストへ接続しています。[p]
*about_kirikirisdl2_2|
@cm
AetherKiriの実行経路は、Godotシェル、GDExtension、C ABI、C++17 KiriKiri2コアの順に接続されています。[l][r]
Godot側がアプリケーションUIと各プラットフォームとの統合を担当し、TJS VM、KAG、画面、音声、ストレージはKiriKiri2コアが処理します。[l][r]
そのため、このデモのような既存のKAG3プロジェクトを元の構造のまま読み込めます。[p]
*about_kirikirisdl2_3|
@cm
TJS2コードをJavaScriptなどへ変換する必要はありません。[l][r]
標準のstartup.tjs、KAG3シナリオ、互換プラグインをKiriKiri2コアで直接実行します。[l][r]
このデモも、元のKAG3起動チェーンを変更せずにAetherKiri上で動作しています。[p]
*about_kirikirisdl2_4
@cm
AetherKiriは開発中のオープンソースプロジェクトです。[l][r]
対応状況、ビルド方法、プラットフォームごとの手順はGitHubリポジトリで確認できます。[l][r]
[r]
[r]
@start_select
[link exp="System.shellExecute('https://github.com/AetherKiri/AetherKiri')" hint="AetherKiri GitHubを開く"]AetherKiri GitHub[endlink][r]
[r]
[link target=*to_syokai_start]メニューに戻る[endlink]
@end_select
[s]

*about_kirikiri_en|What is Kirikiri?
@changebg_and_clear storage="_24_4"
Kirikiri is software created in the TJS scripting language to perform various tasks.[l][r]
The TJS language is like a cross between Java and Javascript, and relatively easy to learn compared to C or C++.[l][r]
Using Kirikiri, the main program is controlled by TJS scripts in order to create various applications.[l][r]
It has particularly strong multimedia capabilities and is suitable for 2D games with relatively static displays.[p]
*about_kirikiri2_en|
@cm
Kirikiri composes its picture using numerous images called layers overlapping each other.[l]  Layers can be overlayed using an alpha blend and can take on an heirarchical structure.[l][r]
By default, layers can be loaded from PNG/JPEG/ERI/BMP files; the Susie plugin can extend this to include even more file formats.[l][r]
The drawing system can't draw anything too complex, but it can render partially transparent rectangles and and antialiased text, as well as resize or transform images.[l][r]
Using AVI/MPEG or SWF (Macromedia Flash) files, it can play movies.[p]
*about_kirikiri3_en|
@cm
Kirikiri can play CD-DA, MIDI sequences, or PCM audio, adjusting the various volume levels.[l]  For PCM audio, it can play uncompressed .wav files; this can be extended to other formats as well using plugins, such as OggVorbis playback.[l][r]
Several PCM sounds can be played at the same time.[l]  If you force it to, it can even play multiple CD-DA or MIDI sequences at the same time.[p]
*about_kirikiri4_en
@cm
Other supplimentary tools are included:  
[font color=0xffff00]Releaser[resetfont] can combine several files into one, or create a stand-alone self-executing file,[l] 
[font color=0xffff00]Kirikiri Settings[resetfont] can adjust the various options for the Kirikiri main program,[l] 
the [font color=0xffff00]Pre-rendered Font Creation Tool[resetfont] can be used to prepare fonts on the developer side, so they're usable even if the font isn't installed on the player's side,[l] 
and the [font color=0xffff00]Transparent Image Format Converter[resetfont] can be used to convert images with transparency data between various image formats.[l] 
[r]
[r]
@start_select
[link target=*to_syokai_start]Return to the menu[endlink]
@end_select
[s]

*about_kag_en|What is KAG?
@changebg_and_clear storage="_24_4"
KAG is a kit designed to create visual novel or sound novel-style games, or story-driven, choice-based text adventure games.[l][r]
KAG is a script that is executed by the Kirikiri game engine, itself written using TJS script.[l]  The scripts used by KAG are called "scenarios," and are again different from TJS scripts.[l]  TJS scripts require a rather heavy understanding of programming, but scenarios can be written more easily.[l][r]
KAG is a system created on top of Kirikiri, so most of the functionality in Kirikiri can be used in KAG.[p]
*about_kag3_en|
@cm
Text display in KAG, as you can see, includes antialiased text,[l][r]
displaying [font size=60]large text[resetfont],[l][r]
p[ruby text="also"]ronu[ruby text="known"]ncia[ruby text="as"]tion[ruby text="''ruby''"] gui[ruby text="text"]des, [l][font shadow=false edge=true edgecolor=0xff0000]outlined text[resetfont],[l][r]
[style align=center]center-aligned text,[r]
[style align=right]right-aligned text,[r][resetstyle]
[l]
in-line special characters like this [graph storage="ExQuestion.png" alt="!?"],[l][r]
and various other capabilities.[p]
*about_kag4_en|
@position vertical=true
Also, it can display text vertically.[l][r]
You can use exactly the same features when using vertical text.[p]
@layopt layer=message0 visible=false
@layopt layer=message1 visible=true
@current layer=message1
@position frame=messageframe left=20 top=280 marginl=16 margint=16 marginr=0 marginb=16 draggable=true vertical=false
Messages can be displayed inside a message frame like this.[l]  This type is often used in adventure games.[p]
@layopt layer=message1 visible=false
@layopt layer=message0 visible=true
@current layer=message0
@position vertical=false
*about_kag5_en|
@cm
Character portraits can be used like this (sorry it's a [i]sea urchin[/i] as always) 
@backlay
@image storage=uni page=back layer=0 visible=true opacity=255
@trans method=crossfade time=1000
@wt
overlayed using an alpha blend.[l][r]
Like this, 
@backlay
@layopt page=back layer=0 opacity=128
@trans method=crossfade time=1000
@wt
it can be displayed very faintly.[l][r]
By default up to three images can be overlayed.[p]
@backlay
@layopt page=back layer=0 visible=false
@trans method=crossfade time=300
@wt
*about_kag6_en|
@cm
By defualt there are three kinds of transitions (screen changes).[l][r]
One of these is the simple cross fade, [l]
@backlay
@layopt page=back layer=message0 visible=false
@trans method=crossfade time=300
@wt
@backlay
@image storage="_24" page=back layer=base
@trans method=crossfade time=3000
@wt
@backlay
@image storage="_24_4" page=back layer=base
@trans method=crossfade time=3000
@wt
@backlay
@layopt page=back layer=message0 visible=true
@trans method=crossfade time=300
@wt
[l][r]
Another is the scroll transition, which produces a sliding effect, [l]
@backlay
@layopt page=back layer=message0 visible=false
@trans method=crossfade time=300
@wt
@backlay
@image storage="_24" page=back layer=base
@trans method=scroll from=right stay=stayfore time=3000
@wt
@backlay
@image storage="_24_4" page=back layer=base
@trans method=scroll from=bottom stay=nostay time=3000
@wt
@backlay
@layopt page=back layer=message0 visible=true
@trans method=crossfade time=300
@wt
[l][r]
and the final is the universal translation, which allows creators to freely make patterns.[l][r]
Universal transitions use grayscale images called "transition rules" that transition sooner on the darker parts of the image.[l][r]
For instance, [l]
@image layer=base page=fore storage="trans1"
If you use a rule image like this...[l]
@backlay
@layopt page=back layer=message0 visible=false
@image storage="_24_4" page=back layer=base
@trans method=crossfade time=300
@wt
@backlay
@image storage="_24" page=back layer=base
@trans method=universal rule="trans1" vague=64 time=3000
@wt
@backlay
@image storage="_24_4" page=back layer=base
@trans method=universal rule="trans1" vague=64 time=3000
@wt
@backlay
@layopt page=back layer=message0 visible=true
@trans method=crossfade time=300
@wt
[r]
For instance, [l]
@image layer=base page=fore storage="nami"
If you use a rule image like this...[l]
@backlay
@layopt page=back layer=message0 visible=false
@image storage="_24_4" page=back layer=base
@trans method=crossfade time=300
@wt
@backlay
@image storage="_24" page=back layer=base
@trans method=universal rule="nami" vague=64 time=3000
@wt
@backlay
@image storage="_24_4" page=back layer=base
@trans method=universal rule="nami" vague=64 time=3000
@wt
@backlay
@layopt page=back layer=message0 visible=true
@trans method=crossfade time=300
@wt
[r]
And like that, various kinds can be created.[p]
*about_kag7_en|What is KAG?
@cm
For background music, either CD-DA, MIDI, or PCM can be used.[l]  Sound effects can use PCM.[l]  All of these can use features such as fading and volume control.[l][r]
By default, KAG can play uncompressed .wav files for PCM audio.[l]  Plugins can also be used to extend playable formats so it can play OggVorbis files.[l][r]
KAG can play AVI/MPEG/SWF movie files.[p]
*about_kag8_en|
@cm
KAG variables can store strings as well as numbers.  It can handle an unlimited number of variables, handle strings of unlimited length; and it deal with not only integers, but floating-point values as well.[l]  Rather than properties of KAG variables, these are properties of TJS variables, which KAG is based on.[l][r]
Within variables there are two types: game variables and system variables.  Game variables are saved and loaded along with bookmark data, but system variables can continuously store the same contents regardless of bookmark data.[l][r]
Here's an example of using variables...[p]
@eval exp="f.v1 = intrandom(1, 9)"
@eval exp="f.v2 = intrandom(1, 9)"
@eval exp="f.ans = f.v1 * f.v2"
@eval exp="f.input = ''"
*about_kag_var_0_en|Math problem
@cm
Here's a math problem.  What is [emb exp="f.v1"] × [emb exp="f.v2"]?[r]
[font size=20](After inputting your answer, click "OK".)[resetfont][r]
[r]
@start_select
　[edit name="f.input" length=200 opacity=80 bgcolor=0x000000 color=0xffffff] [link target=*about_kag_var_1_en]      OK      [endlink][r]
[r]
　[link target=*about_kag_9_en]It's a pain, so I'll skip it[endlink]
@end_select
@eval exp="kag.fore.messages[0].links[0].object.focus()"
; set focus to the input field
; Set history logging in case they choose "System -> go back" to return to this point
@record
[s]

*about_kag_var_1_en
@commit
@jump cond="str2num(f.input) == f.ans" target=*about_kag_var_correct_en
@cm
Incorrect![l][r]
Please input it again.[p]
@jump target=*about_kag_var_0_en

*about_kag_var_correct_en
@cm
Correct![p]
@jump target=*about_kag_9_en

*about_kag_9_en|
@cm
@snowinit forevisible=true backvisible=false
An important characteristic of KAG is its extensibility and customizability.[l]  Even if you can't do something with just KAG, by using TJS to directly control Kirikiri, many things are possible.[l][r]
For instance, by loading a plugin for KAG to display "snow," you can make it snow like this.[l]  There are also plugins that increase the types of transitions available.[l][r]
Also, because KAG itself is written as a TJS script, by editing the script files, you can customize the way every last nook and cranny of the the program runs.[p]
@backlay
@snowopt backvisible=false
@trans method=crossfade time=1000
@wt
@snowuninit
*about_kag_fin_en|End of the KAG introduction
@cm
This concludes the introduction to KAG.[l][r]
By all means, everybody please use Kirikiri/KAG to make more wonderful games.[l][r]
[r]
@start_select
[link target=*to_syokai_start]Return to the menu[endlink]
@end_select
[s]

*about_kirikirisdl2_en|What is AetherKiri?
@changebg_and_clear storage="_24_4"
AetherKiri is a Godot 4.7-based application shell for running KiriKiri2 content on modern devices.[l][r]
It targets macOS, iOS/iPadOS, Android, and Web, and can open either a game directory or an XP3 archive.[l][r]
Rather than reimplementing KiriKiri2 with another scripting system, it connects the existing C++ runtime core to a Godot host.[p]
*about_kirikirisdl2_2_en|
@cm
AetherKiri connects a Godot shell to the C++17 KiriKiri2 core through GDExtension and a stable C ABI.[l][r]
Godot provides the application UI and platform integration, while the KiriKiri2 core handles the TJS VM, KAG, graphics, audio, and storage.[l][r]
This lets existing KAG3 projects such as this demo retain their original structure.[p]
*about_kirikirisdl2_3_en|
@cm
TJS2 code does not need to be converted to JavaScript or another scripting language.[l][r]
The KiriKiri2 core directly runs the standard startup.tjs, KAG3 scenarios, and compatible plugins.[l][r]
This demo therefore uses the original KAG3 startup chain unchanged on AetherKiri.[p]
*about_kirikirisdl2_4_en
@cm
AetherKiri is an open-source work in progress.[l][r]
Its GitHub repository contains current compatibility information, build instructions, and platform-specific guidance.[l][r]
[r]
[r]
@start_select
[link exp="System.shellExecute('https://github.com/AetherKiri/AetherKiri')" hint="Open AetherKiri on GitHub"]AetherKiri on GitHub[endlink][r]
[r]
[link target=*to_syokai_start]Return to the menu[endlink]
@end_select
[s]
