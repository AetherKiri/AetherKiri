*syokai_start_zh_tw|吉里吉里與 KAG 介紹－選單
@startanchor

; 載入背景，並在訊息圖層上繪製選單
@backlay
@loadbg storage="_24_5" page=back
@current page=back
@cm
@layopt layer=message0 page=back visible=true
@nowait
@history output=false
@style align=center
[font size=40 color=0x00ffff]吉里吉里與 KAG 介紹[resetfont][r]
[r]
[link target="*about_kirikiri_zh_tw"]什麼是吉里吉里？[endlink][r]
[r]
[link target="*about_kag_zh_tw"]什麼是 KAG？[endlink][r]
[r]
[link target="*about_aetherkiri_zh_tw"]什麼是 AetherKiri？[endlink][r]
[r]
[link storage="first.ks" target="*syokai_start"]返回語言選擇[endlink][r]
@endnowait
@history output=true
@current page=fore

; 訊息圖層轉場
@trans method=crossfade time=800
@wt

; 記錄歷程
@record

; 等待使用者選擇
@s

*to_syokai_start_zh_tw
; 返回繁體中文選單
@backlay
@layopt layer=message0 page=back visible=false
@trans method=crossfade time=300
@wt
@jump target=*syokai_start_zh_tw

*about_kirikiri_zh_tw|什麼是吉里吉里？
@changebg_and_clear storage="_24_4"
　吉里吉里是一套使用名為 TJS 的腳本語言來完成各種工作的軟體。[l][r]
　TJS 是一種彷彿把 Java 與 JavaScript 相加後再除以三的語言；與 C 或 C++ 相比，我想它比較容易學習。[l][r]
　在吉里吉里中，可以透過 TJS 控制引擎本體，製作各式各樣的應用程式。[l][r]
　它尤其擅長多媒體功能，適合採用較靜態表現方式的 2D 遊戲。[p]
*about_kirikiri2_zh_tw|
@cm
　吉里吉里會把多個稱為「圖層」的畫面重疊起來構成整個畫面。[l]圖層可以使用 Alpha 混合疊加，也能形成階層結構。[l][r]
　圖層預設可以讀取 PNG／JPEG／ERI／BMP，還能使用 Susie-plugin 擴充可讀取的格式。[l][r]
　繪圖功能雖然無法處理太複雜的內容，但可以繪製半透明矩形、顯示具抗鋸齒效果的文字，以及縮放或變形圖片。[l][r]
　也可以把 AVI／MPEG 或 SWF（Macromedia Flash）當作影片播放。[p]
*about_kirikiri3_zh_tw|
@cm
　吉里吉里可以播放 CD-DA、MIDI 序列資料與 PCM，並可分別調整音量。[l]除了未壓縮的 .WAV 檔案外，PCM 還能透過外掛擴充可播放的格式，也可以播放 OggVorbis。[l][r]
　可以同時播放多個 PCM 音效。[l]如果勉強去做，CD-DA 與 MIDI 序列資料也能同時播放多個。[p]
*about_kirikiri4_zh_tw
@cm
　此外，周邊工具還包括：[l]
能將多個檔案合併成一個，或建立可獨立執行檔案的 [font color=0xffff00]Releaser[resetfont]；[l]
用來設定吉里吉里本體的 [font color=0xffff00]吉里吉里設定[resetfont]；[l]
由製作者預先準備字型，讓玩家即使沒有安裝該字型也能使用的 [font color=0xffff00]預先渲染字型製作工具[resetfont]；[l]
以及在具有透明度的圖片格式之間互相轉換的 [font color=0xffff00]透明圖片格式轉換器[resetfont]。[l]
[r]
[r]
@start_select
[link target=*to_syokai_start_zh_tw]返回選單[endlink]
@end_select
[s]

*about_kag_zh_tw|什麼是 KAG？
@changebg_and_clear storage="_24_4"
　KAG 是一套用來製作視覺小說、聲音小說等小說類遊戲，或是透過選擇選項推進故事的文字冒險遊戲之工具套件。[l][r]
　KAG 是讓吉里吉里作為遊戲引擎運作的腳本，本身由 TJS 腳本寫成。[l]KAG 使用的腳本稱為「情境腳本」，與 TJS 腳本又是不同的東西。[l]TJS 腳本需要相當程度的程式設計知識，情境腳本則更簡單，也更容易撰寫。[l][r]
　KAG 是建立在吉里吉里之上的系統，因此吉里吉里的絕大多數功能都能在 KAG 中使用。[p]
*about_kag3_zh_tw|
@cm
　KAG 的文字顯示除了眼前所見的抗鋸齒文字外，還可以：[l][r]
顯示[font size=60]大型文字[resetfont]；[l][r]
[ruby text="ㄊㄧˋ"]替[ruby text="ㄏㄢˋ"]漢[ruby text="ㄗˋ"]字[ruby text="ㄐㄧㄚ"]加[ruby text="ㄕㄤˋ"]上[ruby text="ㄓㄨˋ"]注[ruby text="ㄧㄣ"]音；[l][font shadow=false edge=true edgecolor=0xff0000]使用描邊文字[resetfont]；[l][r]
[style align=center]將文字置中；[r]
[style align=right]將文字靠右對齊；[r][resetstyle]
[l]
顯示像 [graph storage="ExQuestion.png" alt="!?"] 這樣的特殊符號；[l][r]
還能做到其他各種效果。[p]
*about_kag4_zh_tw|
@position vertical=true
　也可以使用直書顯示。[l][r]
　直書時同樣可以使用與橫書完全相同的功能。[p]
@layopt layer=message0 visible=false
@layopt layer=message1 visible=true
@current layer=message1
@position frame=messageframe left=20 top=280 marginl=16 margint=16 marginr=0 marginb=16 draggable=true vertical=false
　也可以像這樣在訊息框裡顯示訊息。[l]這是冒險遊戲中常見的類型。[p]
@layopt layer=message1 visible=false
@layopt layer=message0 visible=true
@current layer=message0
@position vertical=false
*about_kag5_zh_tw|
@cm
　角色立繪可以像這樣顯示（不好意思，還是一如往常地用了[ruby text="・"]海[ruby text="・"]膽）
@backlay
@image storage=uni page=back layer=0 visible=true opacity=255
@trans method=crossfade time=1000
@wt
並透過 Alpha 混合疊加。[l][r]
　也能像這樣
@backlay
@layopt page=back layer=0 opacity=128
@trans method=crossfade time=1000
@wt
淡淡地顯示。[l][r]
　預設最多可以重疊顯示三張圖片。[p]
@backlay
@layopt page=back layer=0 visible=false
@trans method=crossfade time=300
@wt
*about_kag6_zh_tw|
@cm
　轉場（畫面切換）預設有三種類型。[l][r]
　第一種是單純的交叉淡化：[l]
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
　第二種是能產生捲動效果的 scroll 轉場：[l]
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
　最後是讓製作者自由建立圖樣的 universal 轉場。[l][r]
　Universal 轉場需要準備一張稱為規則圖片的灰階圖片，畫面會從圖片中較暗的部分開始切換。[l][r]
　例如：[l]
@image layer=base page=fore storage="trans1"
使用像這樣的規則圖片時……[l]
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
　再例如：[l]
@image layer=base page=fore storage="nami"
使用像這樣的規則圖片時……[l]
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
　就是這樣，可以製作出各式各樣的效果。[p]
*about_kag7_zh_tw|什麼是 KAG？
@cm
　背景音樂可以使用 CD-DA、MIDI 或 PCM。[l]音效則可以使用 PCM。[l]這些聲音都能進行淡入淡出等音量控制。[l][r]
　PCM 預設可以播放未壓縮的 .WAV。[l]也能透過外掛擴充可播放的格式，並播放 OggVorbis。[l][r]
　影片可以播放 AVI／MPEG／SWF。[p]
*about_kag8_zh_tw|
@cm
　KAG 變數既能存放字串，也能存放數值；變數數量不限、字串長度不限，數值除了整數外也能處理實數。[l]嚴格來說，這並不是 KAG 變數本身的規格，而是 KAG 所依據的 TJS 之規格。[l][r]
　變數分為遊戲變數與系統變數兩種。遊戲變數會隨書籤一同讀取或儲存；系統變數則與書籤無關，能一直保有相同內容。[l][r]
　以下示範如何使用變數……[p]
@eval exp="f.v1 = intrandom(1, 9)"
@eval exp="f.v2 = intrandom(1, 9)"
@eval exp="f.ans = f.v1 * f.v2"
@eval exp="f.input = ''"
*about_kag_var_0_zh_tw|計算題
@cm
　這是一道計算題。[emb exp="f.v1"] × [emb exp="f.v2"] 是多少？[r]
[font size=20]（在下方輸入欄填入答案後，請按旁邊的「確定」）[resetfont][r]
[r]
@start_select
　[edit name="f.input" length=200 opacity=80 bgcolor=0x000000 color=0xffffff] [link target=*about_kag_var_1_zh_tw]　　確定　　[endlink][r]
[r]
　[link target=*about_kag_9_zh_tw]太麻煩了，直接跳過[endlink]
@end_select
@eval exp="kag.fore.messages[0].links[0].object.focus()"
; 將焦點設定到輸入欄
; 為了能透過「系統－返回上一處」回到這裡，在此記錄通過位置
@record
[s]

*about_kag_var_1_zh_tw
@commit
@jump cond="str2num(f.input) == f.ans" target=*about_kag_var_correct_zh_tw
@cm
　答錯了！[l][r]
　請再輸入一次。[p]
@jump target=*about_kag_var_0_zh_tw

*about_kag_var_correct_zh_tw
@cm
　答對了！[p]
@jump target=*about_kag_9_zh_tw

*about_kag_9_zh_tw|
@cm
@snowinit forevisible=true backvisible=false
　KAG 的一大特色，就是高度的擴充性與自訂能力。[l]即使某些功能無法單靠 KAG 實現，也能使用 TJS 直接控制吉里吉里，完成各式各樣的工作。[l][r]
　例如，載入能顯示「雪花」的 KAG 外掛後，就能像這樣讓雪花出現在畫面上。[l]此外，也有能增加轉場種類等功能的外掛。[l][r]
　而且 KAG 本身就是以 TJS 腳本寫成，因此只要修改腳本，就能細緻地自訂程式各個角落的運作方式。[p]
@backlay
@snowopt backvisible=false
@trans method=crossfade time=1000
@wt
@snowuninit
*about_kag_fin_zh_tw|KAG 介紹結束
@cm
　KAG 的介紹到此結束。[l][r]
　也請大家務必使用吉里吉里／KAG，製作出精彩的遊戲。[l][r]
[r]
@start_select
[link target=*to_syokai_start_zh_tw]返回選單[endlink]
@end_select
[s]

*about_aetherkiri_zh_tw|什麼是 AetherKiri？
@changebg_and_clear storage="_24_4"
AetherKiri 是讓吉里吉里 2 內容能在現代 Godot 應用程式中執行的跨平台相容引擎。[l][r]
它保留原生 TJS2 與 KAG3 執行環境，因此既有的吉里吉里作品不必改寫成其他腳本語言。[l][r]
如此一來，吉里吉里作品便能透過 AetherKiri 在多種平台上執行。[p]
*about_aetherkiri_2_zh_tw|
@cm
AetherKiri 並不是把吉里吉里重新實作成 JavaScript，而是讓吉里吉里的原生核心與 Godot 宿主協同運作。[l][r]
因此，以 KAG3 製作的專案通常只需要少量相容性調整便能執行；TJS2 程式碼也不必轉換成 JavaScript 或其他腳本語言。[p]
*about_aetherkiri_3_zh_tw|
@cm
AetherKiri 目前以 macOS、iOS／iPadOS、Android 與 Web 為主要目標平台。[l][r]
遊戲內容可以使用目錄或 XP3 封裝；這一份示例正是由 AetherKiri 直接執行原有的 KAG3 情境腳本。[p]
*about_aetherkiri_4_zh_tw
@cm
AetherKiri 仍在開發中，部分功能可能尚未完全運作。[l][r]
如果你有程式碼或文件可以貢獻，歡迎向 AetherKiri 專案儲存庫提交 Pull Request。[l][r]
[r]
[r]
@start_select
[link exp="System.shellExecute('https://github.com/AetherKiri/AetherKiri')" hint="開啟 AetherKiri 專案儲存庫"]AetherKiri 專案儲存庫[endlink][r]
[r]
[link target=*to_syokai_start_zh_tw]返回選單[endlink]
@end_select
[s]
