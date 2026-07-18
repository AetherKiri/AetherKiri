*syokai_start_zh_cn|吉里吉里与 KAG 简介 - 菜单
@startanchor

; 加载背景，并在消息层上绘制菜单
@backlay
@loadbg storage="_24_5" page=back
@current page=back
@cm
@layopt layer=message0 page=back visible=true
@nowait
@history output=false
@style align=center
[font size=40 color=0x00ffff]吉里吉里与 KAG 简介[resetfont][r]
[r]
[link target="*about_kirikiri_zh_cn"]什么是吉里吉里？[endlink][r]
[r]
[link target="*about_kag_zh_cn"]什么是 KAG？[endlink][r]
[r]
[link target="*about_aetherkiri_zh_cn"]什么是 AetherKiri？[endlink][r]
[r]
[link storage="first.ks" target="*syokai_start"]返回语言选择[endlink][r]
@endnowait
@history output=true
@current page=fore

; 消息层转场
@trans method=crossfade time=800
@wt

; 记录已读位置
@record

; 等待用户选择
@s

*to_syokai_start_zh_cn
; 返回简体中文菜单
@backlay
@layopt layer=message0 page=back visible=false
@trans method=crossfade time=300
@wt
@jump target=*syokai_start_zh_cn

*about_kirikiri_zh_cn|什么是吉里吉里？
@changebg_and_clear storage="_24_4"
　吉里吉里是一款使用 TJS 脚本语言完成各种工作的软件。[l][r]
　TJS 就像把 Java 和 JavaScript 加在一起再除以三，与 C 或 C++ 相比，我认为它更容易学习。[l][r]
　在吉里吉里中，可以用 TJS 控制程序本体，从而制作各种应用程序。[l][r]
　它尤其擅长多媒体功能，适合使用相对静态表现形式的 2D 游戏。[p]
*about_kirikiri2_zh_cn|
@cm
　吉里吉里把多张称为“图层”的画面叠在一起构成最终画面。[l]图层可以使用 Alpha 混合进行叠加，也可以采用层级结构。[l][r]
　图层默认可以读取 PNG/JPEG/ERI/BMP，还可以通过 Susie 插件扩展可读取的格式。[l][r]
　绘图功能虽然无法完成过于复杂的操作，但可以绘制半透明矩形和抗锯齿文字，也可以缩放或变形图像。[l][r]
　还可以把 AVI/MPEG 或 SWF（Macromedia Flash）作为影片播放。[p]
*about_kirikiri3_zh_cn|
@cm
　吉里吉里可以播放 CD-DA、MIDI 序列数据和 PCM，并分别调节音量。[l]PCM 除了未压缩的 .WAV 文件外，还可以通过插件扩展可播放的格式，也能够播放 Ogg Vorbis。[l][r]
　多个 PCM 可以同时播放。[l]如果一定要这样做，CD-DA 和 MIDI 序列数据也可以同时播放多路。[p]
*about_kirikiri4_zh_cn
@cm
　此外，配套工具还包括：
可以把多个文件合并为一个文件，或制作可独立运行文件的 [font color=0xffff00]Releaser[resetfont]；[l]
用于设置吉里吉里本体的[font color=0xffff00]吉里吉里设置[resetfont]；[l]
由制作者准备字体，使玩家没有安装该字体也能使用的[font color=0xffff00]预渲染字体制作工具[resetfont]；[l]
以及在带透明度的图像格式之间相互转换的[font color=0xffff00]透明图像格式转换器[resetfont]。[l]
[r]
[r]
@start_select
[link target=*to_syokai_start_zh_cn]返回菜单[endlink]
@end_select
[s]

*about_kag_zh_cn|什么是 KAG？
@changebg_and_clear storage="_24_4"
　KAG 是一套用于制作视觉小说、有声小说等小说类游戏，以及通过选择选项推进故事的文字冒险游戏的工具包。[l][r]
　KAG 是让吉里吉里作为游戏引擎运行的脚本，其本身由 TJS 脚本编写。[l]KAG 使用的脚本称为“场景脚本”，它与 TJS 脚本又是不同的东西。[l]编写 TJS 脚本需要较多编程知识，而场景脚本更加简单、容易书写。[l][r]
　KAG 是建立在吉里吉里之上的系统，因此吉里吉里的大多数功能都可以在 KAG 中使用。[p]
*about_kag3_zh_cn|
@cm
　KAG 的文字显示除了你现在看到的抗锯齿文字外，还可以：[l][r]
显示[font size=60]大号文字[resetfont]；[l][r]
给[ruby text="hàn"]汉[ruby text="zì"]字[ruby text="zhù"]注[ruby text="pīn"]拼[ruby text="yīn"]音；[l][font shadow=false edge=true edgecolor=0xff0000]显示描边文字[resetfont]；[l][r]
[style align=center]让文字居中；[r]
[style align=right]让文字右对齐；[r][resetstyle]
[l]
显示 [graph storage="ExQuestion.png" alt="!?"] 这样的特殊符号；[l][r]
等等，可以实现各种效果。[p]
*about_kag4_zh_cn|
@position vertical=true
　还可以使用竖排文字。[l][r]
　竖排文字也可以使用与横排文字完全相同的功能。[p]
@layopt layer=message0 visible=false
@layopt layer=message1 visible=true
@current layer=message1
@position frame=messageframe left=20 top=280 marginl=16 margint=16 marginr=0 marginb=16 draggable=true vertical=false
　也可以像这样在消息框中显示文字。[l]这是冒险游戏中常见的类型。[p]
@layopt layer=message1 visible=false
@layopt layer=message0 visible=true
@current layer=message0
@position vertical=false
*about_kag5_zh_cn|
@cm
　立绘可以像这样显示（抱歉，还是[ruby text="海胆"]海胆）
@backlay
@image storage=uni page=back layer=0 visible=true opacity=255
@trans method=crossfade time=1000
@wt
并通过 Alpha 混合叠加。[l][r]
　像这样
@backlay
@layopt page=back layer=0 opacity=128
@trans method=crossfade time=1000
@wt
还可以淡淡地显示。[l][r]
　默认状态下最多可以叠加显示三张图像。[p]
@backlay
@layopt page=back layer=0 visible=false
@trans method=crossfade time=300
@wt
*about_kag6_zh_cn|
@cm
　转场（画面切换）默认有三种。[l][r]
　一种是简单的交叉淡化：[l]
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
　另一种是可以产生滚动效果的滚动转场：[l]
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
　最后一种是让制作者自由创建切换图案的通用转场。[l][r]
　通用转场需要准备一张称为“规则图”的灰度图像，画面会从图像中较暗的位置更早开始切换。[l][r]
　例如：[l]
@image layer=base page=fore storage="trans1"
如果使用这样的规则图……[l]
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
如果使用这样的规则图……[l]
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
　像这样，可以制作出各种各样的效果。[p]
*about_kag7_zh_cn|什么是 KAG？
@cm
　BGM 可以使用 CD-DA、MIDI 或 PCM，音效则可以使用 PCM。[l]它们都支持淡入淡出等音量控制。[l][r]
　PCM 默认可以播放未压缩的 .WAV。[l]还可以通过插件扩展可播放的格式，也能够播放 Ogg Vorbis。[l][r]
　影片可以播放 AVI/MPEG/SWF。[p]
*about_kag8_zh_cn|
@cm
　KAG 变量既可以存放字符串，也可以存放数值；变量数量和字符串长度都不受限制；数值除了整数，也可以使用实数。[l]与其说这是 KAG 变量的规格，不如说是作为 KAG 基础的 TJS 的规格。[l][r]
　变量分为游戏变量和系统变量两种。游戏变量会随书签一起读取和保存，而系统变量与书签无关，可以始终保持相同内容。[l][r]
　下面展示一个使用变量的例子……[p]
@eval exp="f.v1 = intrandom(1, 9)"
@eval exp="f.v2 = intrandom(1, 9)"
@eval exp="f.ans = f.v1 * f.v2"
@eval exp="f.input = ''"
*about_kag_var_0_zh_cn|计算题
@cm
　这是一道计算题。[emb exp="f.v1"] × [emb exp="f.v2"] 等于多少？[r]
[font size=20]（在下方输入框中填写答案后，请单击旁边的“确定”）[resetfont][r]
[r]
@start_select
　[edit name="f.input" length=200 opacity=80 bgcolor=0x000000 color=0xffffff] [link target=*about_kag_var_1_zh_cn]　　确定　　[endlink][r]
[r]
　[link target=*about_kag_9_zh_cn]嫌麻烦，跳过这里[endlink]
@end_select
@eval exp="kag.fore.messages[0].links[0].object.focus()"
; 将焦点设置到输入框
; 在这里记录已读位置，以便通过“系统 - 返回上一处”回到这里
@record
[s]

*about_kag_var_1_zh_cn
@commit
@jump cond="str2num(f.input) == f.ans" target=*about_kag_var_correct_zh_cn
@cm
　回答错误！[l][r]
　请再输入一次。[p]
@jump target=*about_kag_var_0_zh_cn

*about_kag_var_correct_zh_cn
@cm
　回答正确！[p]
@jump target=*about_kag_9_zh_cn

*about_kag_9_zh_cn|
@cm
@snowinit forevisible=true backvisible=false
　KAG 的一大特色是高度的可扩展性和可定制性。[l]即使是单靠 KAG 无法实现的功能，也可以用 TJS 直接控制吉里吉里，完成各种各样的事情。[l][r]
　例如，加载一个用于 KAG 的“雪花”插件，就可以像这样显示雪花。[l]此外还有增加转场种类等功能的插件。[l][r]
　而且 KAG 本身就是用 TJS 脚本编写的，因此修改脚本便可以细致地定制各处行为。[p]
@backlay
@snowopt backvisible=false
@trans method=crossfade time=1000
@wt
@snowuninit
*about_kag_fin_zh_cn|KAG 简介结束
@cm
　KAG 的介绍到这里就结束了。[l][r]
　也请大家使用吉里吉里/KAG 制作出优秀的游戏。[l][r]
[r]
@start_select
[link target=*to_syokai_start_zh_cn]返回菜单[endlink]
@end_select
[s]

*about_aetherkiri_zh_cn|什么是 AetherKiri？
@changebg_and_clear storage="_24_4"
AetherKiri 是一个由 Godot 应用外壳承载、使用 C++17 KiriKiri2 核心执行游戏内容的现代跨平台运行时。[l][r]
它让基于吉里吉里和 KAG3 的项目无需依赖 Wine，也能在现代平台上运行。[l][r]
目前，AetherKiri 面向 macOS、iOS、iPadOS、Android 和 Web 平台。
[p]
*about_aetherkiri_2_zh_cn|
@cm
AetherKiri 不是把吉里吉里改写成另一套脚本引擎，而是由 KiriKiri2 核心直接执行原有游戏内容。[l][r]
因此，KAG3 项目通常只需少量调整就可以在 AetherKiri 上运行。[l][r]
项目使用的原生插件如果有源代码，也可以针对 AetherKiri 支持的平台进行移植和编译。
[p]
*about_aetherkiri_3_zh_cn|
@cm
TJS2 代码无需转换为 JavaScript 或其他脚本语言，就可以由 AetherKiri 直接执行。[l][r]
因此，现有的 KAG3 脚本和使用 TJS 编写的插件能够以尽可能少的改动继续使用。
[p]
*about_aetherkiri_4_zh_cn
@cm
AetherKiri 仍在开发中，因此部分功能可能尚未完全正常工作。[l][r]
如果你愿意贡献代码或文档，请向 AetherKiri 项目仓库提交 Pull Request。[l][r]
[r]
[r]
@start_select
[link exp="System.shellExecute('https://github.com/AetherKiri/AetherKiri')" hint="打开 AetherKiri 项目主页"]AetherKiri 项目主页[endlink][r]
[r]
[link target=*to_syokai_start_zh_cn]返回菜单[endlink]
@end_select
[s]
