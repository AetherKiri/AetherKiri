*syokai_start_ko|키리키리와 KAG 소개 - 메뉴
@startanchor

; 배경 이미지를 불러오고 메시지 레이어에 메뉴를 그린다
@backlay
@loadbg storage="_24_5" page=back
@current page=back
@cm
@layopt layer=message0 page=back visible=true
@nowait
@history output=false
@style align=center
[font size=40 color=0x00ffff]키리키리와 KAG 소개[resetfont][r]
[r]
[link target="*about_kirikiri_ko"]키리키리란?[endlink][r]
[r]
[link target="*about_kag_ko"]KAG란?[endlink][r]
[r]
[link target="*about_aetherkiri_ko"]AetherKiri란?[endlink][r]
[r]
[link storage="first.ks" target="*syokai_start"]언어 선택으로 돌아가기[endlink][r]
@endnowait
@history output=true
@current page=fore

; 메시지 레이어 전환
@trans method=crossfade time=800
@wt

; 읽은 위치 기록
@record

; 선택할 때까지 기다린다
@s

*to_syokai_start_ko
; 한국어 메뉴로 돌아간다
@backlay
@layopt layer=message0 page=back visible=false
@trans method=crossfade time=300
@wt
@jump target=*syokai_start_ko

*about_kirikiri_ko|키리키리란?
@changebg_and_clear storage="_24_4"
　키리키리는 TJS라는 스크립트 언어를 사용해 여러 가지 일을 하기 위한 소프트웨어입니다.[l][r]
　TJS는 Java와 JavaScript를 더한 뒤 3으로 나눈 듯한 언어로, C나 C++에 비하면 배우기 쉬운 언어라고 생각합니다.[l][r]
　키리키리에서는 이 TJS로 본체를 제어하여 다양한 애플리케이션을 만들 수 있습니다.[l][r]
　특히 멀티미디어 기능이 강하며, 비교적 정적인 표현을 사용하는 2D 게임에 적합합니다.[p]
*about_kirikiri2_ko|
@cm
　키리키리는 레이어라고 부르는 여러 화면을 겹쳐서 최종 화면을 구성합니다.[l]레이어는 알파 블렌딩으로 겹칠 수 있고 계층 구조를 가질 수도 있습니다.[l][r]
　레이어에는 기본적으로 PNG/JPEG/ERI/BMP를 불러올 수 있으며, Susie 플러그인으로 읽을 수 있는 형식을 확장할 수도 있습니다.[l][r]
　그리기 기능으로 아주 복잡한 작업을 할 수는 없지만, 반투명 사각형과 안티앨리어싱 문자를 그리거나 이미지를 확대·축소하고 변형할 수 있습니다.[l][r]
　AVI/MPEG 또는 SWF(Macromedia Flash)를 동영상으로 재생할 수도 있습니다.[p]
*about_kirikiri3_ko|
@cm
　키리키리는 CD-DA, MIDI 시퀀스 데이터, PCM을 재생하고 각각의 음량을 조절할 수 있습니다.[l]PCM은 무압축 .WAV 파일 외에도 플러그인으로 재생 가능한 형식을 확장할 수 있으며 Ogg Vorbis도 재생할 수 있습니다.[l][r]
　여러 PCM을 동시에 재생할 수 있습니다.[l]억지로 하려 한다면 CD-DA나 MIDI 시퀀스 데이터도 여러 개를 동시에 재생할 수 있습니다.[p]
*about_kirikiri4_ko
@cm
　그 밖의 보조 도구로는,
여러 파일을 하나로 합치거나 단독 실행 파일을 만들 수 있는 [font color=0xffff00]Releaser[resetfont],[l]
키리키리 본체를 설정하는 [font color=0xffff00]키리키리 설정[resetfont],[l]
제작자 쪽에서 글꼴을 준비하여 플레이어에게 해당 글꼴이 설치되어 있지 않아도 사용할 수 있게 하는 [font color=0xffff00]렌더링된 글꼴 제작 도구[resetfont],[l]
투명도를 가진 이미지 형식끼리 서로 변환하는 [font color=0xffff00]투명 이미지 형식 변환기[resetfont]가 있습니다.[l]
[r]
[r]
@start_select
[link target=*to_syokai_start_ko]메뉴로 돌아가기[endlink]
@end_select
[s]

*about_kag_ko|KAG란?
@changebg_and_clear storage="_24_4"
　KAG는 비주얼 노벨이나 사운드 노벨 같은 노벨 게임, 또는 선택지를 골라 이야기를 진행하는 문자 중심의 어드벤처 게임을 만들기 위한 키트입니다.[l][r]
　KAG는 키리키리를 게임 엔진으로 동작시키기 위한 스크립트이며, 그 자체도 TJS 스크립트로 작성되어 있습니다.[l]KAG용 스크립트는 “시나리오”라고 부르며 TJS 스크립트와는 또 다른 것입니다.[l]TJS 스크립트는 상당한 프로그래밍 지식이 필요하지만 시나리오는 더 간단하고 쉽게 작성할 수 있습니다.[l][r]
　KAG는 키리키리 위에 만들어진 시스템이므로 키리키리 기능의 대부분을 KAG에서 사용할 수 있습니다.[p]
*about_kag3_ko|
@cm
　KAG의 문자 표시 기능에는 지금 보시는 안티앨리어싱 문자 외에도,[l][r]
[font size=60]큰 문자[resetfont]를 표시하거나,[l][r]
[ruby text="한"]漢[ruby text="자"]字에 [ruby text="발"]발[ruby text="음"]음을 붙이거나, [l][font shadow=false edge=true edgecolor=0xff0000]테두리 문자를 표시하거나[resetfont],[l][r]
[style align=center]가운데 정렬하거나,[r]
[style align=right]오른쪽 정렬하거나,[r][resetstyle]
[l]
[graph storage="ExQuestion.png" alt="!?"] 같은 특수 기호를 표시하는 등,[l][r]
여러 가지 표현을 사용할 수 있습니다.[p]
*about_kag4_ko|
@position vertical=true
　또한 문자를 세로로 표시할 수도 있습니다.[l][r]
　세로쓰기에서도 가로쓰기와 완전히 같은 기능을 사용할 수 있습니다.[p]
@layopt layer=message0 visible=false
@layopt layer=message1 visible=true
@current layer=message1
@position frame=messageframe left=20 top=280 marginl=16 margint=16 marginr=0 marginb=16 draggable=true vertical=false
　이처럼 메시지 프레임 안에 문장을 표시할 수도 있습니다.[l]어드벤처 게임에서 흔히 볼 수 있는 형식입니다.[p]
@layopt layer=message1 visible=false
@layopt layer=message0 visible=true
@current layer=message0
@position vertical=false
*about_kag5_ko|
@cm
　캐릭터 그림은 이렇게 표시할 수 있습니다(늘 그렇듯 [ruby text="·"]성[ruby text="·"]게라 죄송합니다).
@backlay
@image storage=uni page=back layer=0 visible=true opacity=255
@trans method=crossfade time=1000
@wt
알파 블렌딩으로 겹쳐 표시할 수 있습니다.[l][r]
　이렇게
@backlay
@layopt page=back layer=0 opacity=128
@trans method=crossfade time=1000
@wt
희미하게 표시할 수도 있습니다.[l][r]
　기본 상태에서는 세 장까지 겹쳐 표시할 수 있습니다.[p]
@backlay
@layopt page=back layer=0 visible=false
@trans method=crossfade time=300
@wt
*about_kag6_ko|
@cm
　전환(화면 전환)에는 기본적으로 세 종류가 있습니다.[l][r]
　하나는 단순한 크로스페이드입니다.[l]
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
　또 하나는 스크롤 효과를 내는 스크롤 전환입니다.[l]
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
　마지막은 제작자가 자유롭게 패턴을 만들 수 있는 유니버설 전환입니다.[l][r]
　유니버설 전환에서는 규칙 이미지라고 부르는 회색조 이미지를 준비하며, 그 이미지에서 어두운 부분부터 더 일찍 전환이 시작됩니다.[l][r]
　예를 들어,[l]
@image layer=base page=fore storage="trans1"
이런 규칙 이미지라면……[l]
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
　또 예를 들어,[l]
@image layer=base page=fore storage="nami"
이런 규칙 이미지라면……[l]
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
　이런 식으로 여러 가지 효과를 만들 수 있습니다.[p]
*about_kag7_ko|KAG란?
@cm
　BGM에는 CD-DA, MIDI, PCM 중 하나를 사용할 수 있고 효과음에는 PCM을 사용할 수 있습니다.[l]어느 쪽이든 페이드 같은 음량 제어가 가능합니다.[l][r]
　PCM은 기본적으로 무압축 .WAV를 재생할 수 있습니다.[l]또한 플러그인으로 재생 가능한 형식을 확장할 수 있으며 Ogg Vorbis도 재생할 수 있습니다.[l][r]
　동영상은 AVI/MPEG/SWF를 재생할 수 있습니다.[p]
*about_kag8_ko|
@cm
　KAG 변수에는 문자열과 수치를 모두 넣을 수 있고, 변수의 수와 문자열의 길이에 제한이 없으며, 수치는 정수뿐 아니라 실수도 다룰 수 있습니다.[l]이는 KAG 변수의 사양이라기보다 KAG의 기반인 TJS의 사양입니다.[l][r]
　변수에는 게임 변수와 시스템 변수 두 종류가 있습니다. 게임 변수는 책갈피와 함께 불러오고 저장하지만, 시스템 변수는 책갈피와 관계없이 항상 같은 내용을 유지할 수 있습니다.[l][r]
　변수를 사용한 예를 보여 드리겠습니다……[p]
@eval exp="f.v1 = intrandom(1, 9)"
@eval exp="f.v2 = intrandom(1, 9)"
@eval exp="f.ans = f.v1 * f.v2"
@eval exp="f.input = ''"
*about_kag_var_0_ko|계산 문제
@cm
　계산 문제입니다. [emb exp="f.v1"] × [emb exp="f.v2"]는 얼마일까요?[r]
[font size=20](아래 입력란에 답을 입력한 뒤 옆의 “확인”을 클릭해 주세요.)[resetfont][r]
[r]
@start_select
　[edit name="f.input" length=200 opacity=80 bgcolor=0x000000 color=0xffffff] [link target=*about_kag_var_1_ko]　　확인　　[endlink][r]
[r]
　[link target=*about_kag_9_ko]귀찮으니 건너뛰기[endlink]
@end_select
@eval exp="kag.fore.messages[0].links[0].object.focus()"
; 입력란에 포커스를 설정한다
; “시스템 - 이전으로 돌아가기”로 이 위치에 돌아올 수 있도록 여기서 읽은 위치를 기록한다
@record
[s]

*about_kag_var_1_ko
@commit
@jump cond="str2num(f.input) == f.ans" target=*about_kag_var_correct_ko
@cm
　틀렸습니다![l][r]
　다시 입력해 주세요.[p]
@jump target=*about_kag_var_0_ko

*about_kag_var_correct_ko
@cm
　정답입니다![p]
@jump target=*about_kag_9_ko

*about_kag_9_ko|
@cm
@snowinit forevisible=true backvisible=false
　KAG의 큰 특징으로 높은 확장성과 사용자 정의 가능성을 꼽을 수 있습니다.[l]KAG만으로 구현할 수 없는 기능도 TJS를 사용해 키리키리를 직접 제어하면 여러 가지 일을 할 수 있습니다.[l][r]
　예를 들어 KAG용 플러그인으로 “눈”을 표시하는 플러그인을 불러오면 이렇게 눈을 표시할 수 있습니다.[l]그 밖에도 전환의 종류를 늘리는 플러그인 등이 있습니다.[l][r]
　또한 KAG 자체가 TJS 스크립트로 작성되어 있으므로 스크립트를 변경하면 구석구석의 동작까지 원하는 대로 바꿀 수 있습니다.[p]
@backlay
@snowopt backvisible=false
@trans method=crossfade time=1000
@wt
@snowuninit
*about_kag_fin_ko|KAG 소개 끝
@cm
　KAG 소개는 이것으로 끝입니다.[l][r]
　여러분도 키리키리/KAG로 훌륭한 게임을 만들어 보세요.[l][r]
[r]
@start_select
[link target=*to_syokai_start_ko]메뉴로 돌아가기[endlink]
@end_select
[s]

*about_aetherkiri_ko|AetherKiri란?
@changebg_and_clear storage="_24_4"
AetherKiri는 Godot 애플리케이션 셸 안에서 C++17 KiriKiri2 코어로 게임 콘텐츠를 실행하는 현대적인 크로스 플랫폼 런타임입니다.[l][r]
키리키리와 KAG3 기반 프로젝트를 Wine에 의존하지 않고 현대적인 플랫폼에서 실행할 수 있게 해 줍니다.[l][r]
현재 AetherKiri는 macOS, iOS, iPadOS, Android 및 Web 플랫폼을 대상으로 합니다.
[p]
*about_aetherkiri_2_ko|
@cm
AetherKiri는 키리키리를 다른 스크립트 엔진으로 다시 구현한 것이 아니라, KiriKiri2 코어가 기존 게임 콘텐츠를 직접 실행하는 런타임입니다.[l][r]
따라서 KAG3 프로젝트는 대개 적은 수정만으로 AetherKiri에서 실행할 수 있습니다.[l][r]
프로젝트에서 사용하는 네이티브 플러그인에 소스 코드가 있다면 AetherKiri가 지원하는 플랫폼용으로 이식하고 컴파일할 수도 있습니다.
[p]
*about_aetherkiri_3_ko|
@cm
TJS2 코드는 JavaScript나 다른 스크립트 언어로 변환하지 않아도 AetherKiri에서 직접 실행할 수 있습니다.[l][r]
따라서 기존 KAG3 스크립트와 TJS로 작성된 플러그인을 가능한 한 적은 수정으로 계속 사용할 수 있습니다.
[p]
*about_aetherkiri_4_ko
@cm
AetherKiri는 아직 개발 중이므로 일부 기능이 완전히 동작하지 않을 수 있습니다.[l][r]
코드나 문서로 기여하고 싶다면 AetherKiri 프로젝트 저장소로 Pull Request를 보내 주세요.[l][r]
[r]
[r]
@start_select
[link exp="System.shellExecute('https://github.com/AetherKiri/AetherKiri')" hint="AetherKiri 프로젝트 홈페이지 열기"]AetherKiri 프로젝트 홈페이지[endlink][r]
[r]
[link target=*to_syokai_start_ko]메뉴로 돌아가기[endlink]
@end_select
[s]
