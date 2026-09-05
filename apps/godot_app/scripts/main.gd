extends Control

const APP_DISPLAY_NAME := "Aether"
const BACKENDS := ["Godot Native", "GPU Bridge", "Debug CPU"]
const SETTINGS_KEY := "aether_kiri/render_backend"
const GAME_PATH_KEY := "aether_kiri/game_path"
const GAME_AUTO_COVER_SCANNED_FIELD := "_autoCoverScanned"
const GAME_LIST_FILE := "user://aetherkiri_games.json"
const VIDEO_LIST_FILE := "user://aetherkiri_videos.json"
const VIDEO_PROGRESS_FILE := "user://aetherkiri_video_progress.json"
const VIDEO_HIDDEN_FILE := "user://aetherkiri_hidden_videos.json"
const IMPORT_STATE_FILE := "user://aetherkiri_import_state.cfg"
const VIDEO_EXTENSIONS := ["mp4", "mkv", "mov", "m4v", "avi", "webm", "flv", "ts", "m2ts", "mpeg", "mpg", "wmv"]
const SUBTITLE_EXTENSIONS := ["srt", "vtt", "ass", "ssa"]
const COVER_IMAGE_EXTENSIONS := ["png", "webp", "jpg", "jpeg"]
# These candidates are deliberately independent of the selected UI language.
# Prefer cover names across every supported language, then background names.
const DEFAULT_COVER_BASENAMES := [
    "cover", "cover_image", "封面", "封面图", "封面圖片",
    "表紙", "カバー", "표지", "커버",
    "background", "background_image", "背景", "背景图", "背景圖片",
    "壁紙", "배경", "배경화면",
]
const GAME_COVER_PATH_PREFIX := "game://"
const SETTINGS_FILE := "user://aetherkiri_settings.cfg"
const IAP_LIST_LIMIT_PRODUCT_ID := "com.aether.list.limit"
const IAP_COFFEE_PRODUCT_ID := "com.aether.coffee"
const ANDROID_COFFEE_URL := "https://qr.alipay.com/fkx108053gol728ayzhec90"
const APP_SERVICE_FILING_NUMBER := "沪ICP备2026042398号-2A"
const APP_SERVICE_FILING_URL := "https://beian.miit.gov.cn/#/Integrated/index"
const IAP_POLL_INTERVAL_SEC := 0.12
const IAP_DETAIL_AUTHORIZATION_TTL_MS := 30000
const SECRET_UNLOCK_TAP_TARGET := 20
const SECRET_UNLOCK_TAP_WINDOW_MSEC := 6000
const SECRET_UNLOCK_COFFEE_SEC := 30 * 24 * 60 * 60
const LEGAL_AGREEMENT_VERSION := "2026-07-27.4"
const IOS_STATEMENT_VERSION := "2026-08-04"
const LEGAL_AGREEMENT_ZH_HANS := "res://legal/privacy_disclaimer_zh_hans.txt"
const LEGAL_AGREEMENT_ZH_HANT := "res://legal/privacy_disclaimer_zh_hant.txt"
const LEGAL_AGREEMENT_EN := "res://legal/privacy_disclaimer_en.txt"
const LEGAL_AGREEMENT_JA := "res://legal/privacy_disclaimer_ja.txt"
const LEGAL_AGREEMENT_KO := "res://legal/privacy_disclaimer_ko.txt"
const IOS_STATEMENT_ZH_HANS := "res://legal/ios_app_store_statement_zh_hans.txt"
const IOS_STATEMENT_ZH_HANT := "res://legal/ios_app_store_statement_zh_hant.txt"
const IOS_STATEMENT_EN := "res://legal/ios_app_store_statement_en.txt"
const IOS_STATEMENT_JA := "res://legal/ios_app_store_statement_ja.txt"
const IOS_STATEMENT_KO := "res://legal/ios_app_store_statement_ko.txt"
const MOBILE_ORIENTATION_SCHEMA_VERSION := 1
const UI_FONT := preload("res://assets/fonts/aetherkiri-runtime-cjk.otf")
const BODY_FONT := preload("res://assets/fonts/Inter-Variable.ttf")
const DISPLAY_FONT := preload("res://assets/fonts/CormorantGaramond-Variable.ttf")
const DISPLAY_CJK_FONT := preload("res://assets/fonts/NotoSerifCJKsc-Regular.otf")
const UI_SYMBOL_FONT := preload("res://assets/fonts/aetherkiri-runtime-symbols.ttf")
const RUNTIME_FONT_DIR := "user://runtime_fonts"
const RUNTIME_DEFAULT_FONT_FILE := "default.otf"
const RUNTIME_SYMBOL_FONT_FILE := "symbols.ttf"
const ProbeConfig = preload("res://scripts/probe_config.gd")
const GameMetadata = preload("res://scripts/game_metadata.gd")
const CoverIndex = preload("res://scripts/cover_index.gd")
const VNDBCoverResolver = preload("res://scripts/vndb_cover_resolver.gd")
const GameInputMapping = preload("res://scripts/game_input_mapping.gd")
const GameVirtualControls = preload("res://scripts/game_virtual_controls.gd")
const DiagnosticSession = preload("res://scripts/diagnostic_session.gd")
const DiagnosticLocalization = preload("res://scripts/diagnostic_localization.gd")
const DebugConsole = preload("res://scripts/debug_console.gd")
const BuiltinDemo = preload("res://scripts/builtin_demo.gd")
const GameLaunchEntry = preload("res://scripts/game_launch_entry.gd")
const VideoSubtitles = preload("res://scripts/video_subtitles.gd")
const AetherDesignTokens = preload("res://scripts/ui/aether_design_tokens.gd")
const AetherMotion = preload("res://scripts/ui/aether_motion.gd")
const AetherWidgets = preload("res://scripts/ui/aether_widgets.gd")
const AetherSegmentedControl = preload("res://scripts/ui/aether_segmented_control.gd")
const AetherSwitch = preload("res://scripts/ui/aether_switch.gd")
const AetherSlider = preload("res://scripts/ui/aether_slider.gd")
const AetherDisclosure = preload("res://scripts/ui/aether_disclosure.gd")
const AetherSelect = preload("res://scripts/ui/aether_select.gd")
const AetherDisplayScale = preload("res://scripts/ui/aether_display_scale.gd")
const UI_ICON_DIR := "res://assets/ui/icons/"
const ICON_SETTINGS := UI_ICON_DIR + "gear-fill.svg"
const ICON_SAVE := UI_ICON_DIR + "save-fill.svg"
const ICON_REFRESH := UI_ICON_DIR + "arrows-counter-clockwise-fill.svg"
const LOADING_SPINNER_ROTATION := TAU
const LOADING_SPINNER_FLIP_H := true
const ICON_ADD := UI_ICON_DIR + "plus-circle.svg"
const ICON_HELP := UI_ICON_DIR + "help.svg"
const ICON_LIBRARY := UI_ICON_DIR + "library.svg"
const ICON_GAMEPAD := UI_ICON_DIR + "gamepad-bold.svg"
const ICON_PLAY := UI_ICON_DIR + "game-controller.svg"
const ICON_VIDEO := UI_ICON_DIR + "video.svg"
const ICON_PERFORMANCE := UI_ICON_DIR + "performance-fill.svg"
const ICON_HOME := UI_ICON_DIR + "round-home.svg"
const ICON_DELETE := UI_ICON_DIR + "round-delete-forever.svg"
const ICON_PAGE := UI_ICON_DIR + "page-template.svg"
const ICON_RENAME := UI_ICON_DIR + "tab-new-24-filled.svg"
const ICON_PLUGIN := UI_ICON_DIR + "plugin-solid.svg"
const ICON_BACK := UI_ICON_DIR + "chevron-left.svg"
const ICON_CHEVRON_RIGHT := UI_ICON_DIR + "chevron-right.svg"
const ICON_CHEVRON_DOWN := UI_ICON_DIR + "chevron-down.svg"
const ICON_CHECK := UI_ICON_DIR + "check.svg"
const ICON_SEARCH := UI_ICON_DIR + "search.svg"
const LANG_SYSTEM := "system"
const LANG_ZH_HANS := "zh_hans"
const LANG_ZH_HANT := "zh_hant"
const LANG_EN := "en"
const LANG_JA := "ja"
const LANG_KO := "ko"
const LANGUAGE_MODES := [LANG_SYSTEM, LANG_ZH_HANS, LANG_ZH_HANT, LANG_EN, LANG_JA, LANG_KO]
const STYLE_DARK := "dark"
const STYLE_CLASSIC := "classic"
const DEFAULT_STYLE_MODE := STYLE_CLASSIC
const STYLE_MODES := [STYLE_DARK, STYLE_CLASSIC]
const IOS_UI_SCALE_MODES := ["compact", "comfortable", "standard"]
const UI_TEXT := {
    LANG_ZH_HANS: {
        "home.subtitle": "多功能媒体播放器",
        "video.status": "视频库",
        "video.empty_title": "Video 文件夹中还没有视频",
        "video.empty_help_ios": "使用「文件」App 将视频复制到：\n我的 iPhone / iPad > Aether > Video\n支持同名 SRT、VTT、ASS 字幕",
        "video.empty_help_desktop": "点击「导入视频」添加本地视频；同目录同名字幕会自动载入",
        "video.import": "导入视频",
        "video.refresh": "刷新",
        "video.guide": "使用说明",
        "video.guide_title": "导入视频",
        "video.guide_body_ios": "请使用「文件」App 将视频复制到本应用的目录：\n\n1. 打开 iPhone / iPad 上的「文件」App\n2. 前往：我的 iPhone / iPad > Aether > Video\n3. 将视频和同名字幕复制到 Video 目录\n4. 返回本应用，点击「刷新」检测新视频\n\n视频目录：Video/\n字幕支持：SRT、VTT、ASS、SSA",
        "video.guide_body_desktop": "请将本地视频导入视频库：\n\n1. 点击「导入视频」\n2. 选择需要播放的视频文件\n3. 如需字幕，请将同名字幕放在视频所在目录\n4. 返回视频库即可播放，进度会自动记录\n\n字幕支持：SRT、VTT、ASS、SSA",
        "video.remove": "移除视频",
        "video.remove_body": "从视频库移除「%s」？不会删除磁盘上的视频文件，并会清除该视频的播放进度。",
        "video.back": "返回",
        "video.pause": "暂停",
        "video.play": "播放",
        "video.subtitle_off": "字幕关闭",
        "video.subtitle_embedded": "%s（内嵌）",
        "video.resume": "继续上次播放",
        "video.progress": "已播放至 %s / %s",
        "video.open_failed": "无法播放该视频：%s",
        "home.status": "视觉小说库",
        "nav.library": "视觉小说",
        "nav.videos": "视频库",
        "nav.collapse_sidebar": "收起侧边栏",
        "nav.expand_sidebar": "展开侧边栏",
        "home.empty_title": "尚未添加任何游戏",
        "home.game_count": "%d 个游戏",
        "video.video_count": "%d 个视频",
        "search.games_placeholder": "搜索视觉小说",
        "search.videos_placeholder": "搜索视频",
        "search.filtered_count": "显示 %d / %d",
        "search.no_results_title": "未找到匹配内容",
        "search.no_results_help": "请尝试其他关键词或清空搜索框",
        "home.refresh": "刷新",
        "home.import": "导入",
        "home.import_guide": "导入指南",
        "home.empty_help_ios": "使用「文件」App 将游戏文件夹复制到：\n我的 iPhone / iPad > Aether > Games\n然后点击「刷新」",
        "home.empty_help_web": "点击「导入」选择本地视觉小说目录",
        "home.empty_help_desktop": "点击「导入」选择视觉小说目录",
        "settings.title": "设置",
        "settings.save": "保存",
        "settings.unsaved_title": "保存设置？",
        "settings.unsaved_body": "设置已修改。离开前是否保存这些更改？",
        "settings.unsaved_discard": "不保存",
        "settings.unsaved_close": "关闭",
        "settings.section.interface": "界面",
        "settings.section.render": "渲染",
        "settings.section.developer": "开发者",
        "settings.section.about": "关于",
        "settings.section.purchases": "内购项目",
        "settings.language": "语言",
        "settings.language_desc": "默认跟随系统；也可以固定为简体中文、繁体中文、英语、日语或韩语",
        "settings.translation_model": "本地翻译模型",
        "settings.translation_model_desc": "选择外部 GGUF 模型。游戏启动时加载；检测到文本不是当前界面语言时自动翻译",
        "settings.translation_model_select": "选择 GGUF 模型",
        "settings.translation_model_selected": "已选模型",
        "settings.translation_model_clear": "停用本地翻译",
        "settings.translation_model_clear_desc": "清除模型路径；不会删除磁盘上的模型文件",
        "settings.style": "风格",
        "settings.style_desc": "可在当前深色风格和旧版原始浅色风格之间切换",
        "settings.ui_scale": "界面比例",
        "settings.ui_scale_desc": "调整 iPhone 和 iPad 上的界面大小，保存后立即生效",
        "settings.virtual_control_menu": "游戏内控制菜单",
        "settings.virtual_control_menu_desc": "在游戏画面右上角显示虚拟控制菜单按钮",
        "settings.keyboard_control_opacity": "Keyboard 模式按键透明度",
        "settings.keyboard_control_opacity_desc": "调整屏幕虚拟按键的透明度；鼠标指针保持清晰",
        "ui_scale.compact": "较小",
        "ui_scale.comfortable": "合适",
        "ui_scale.standard": "标准",
        "style.dark": "深色",
        "style.classic": "原始浅色",
        "language.system": "跟随系统",
        "language.system_with_value": "跟随系统（%s）",
        "language.zh_hans": "简体中文",
        "language.zh_hant": "繁體中文",
        "language.en": "English",
        "language.ja": "日本語",
        "language.ko": "한국어",
        "settings.render_backend": "渲染管线",
        "settings.render_backend_desc": "保存后生效；运行中切换需重启当前游戏",
        "settings.surface_mode": "画布尺寸",
        "settings.surface_mode_desc": "Game Native 按游戏基准画布运行；Display Fit 按设备显示尺寸运行",
        "settings.upscale": "缩放算法",
        "settings.upscale_desc": "外层拉伸画面时使用；Bicubic/Lanczos 提供更高质量采样",
        "settings.output_resolution": "输出分辨率",
        "settings.output_resolution_desc": "设置外层缩放与画面增强的目标分辨率；较高档位会增加 GPU 和显存占用",
        "settings.output_resolution.original": "原始",
        "settings.frame_enhancement": "画面增强",
        "settings.frame_enhancement_desc": "使用 GPU 修复线条，并按输出分辨率高质量放大；保存后立即生效",
        "settings.frame_enhancement_unavailable_desc": "当前构建或图形设备不支持画面增强；开启后仍会安全使用原始画面",
        "settings.frame_enhancement_mode": "增强效果",
        "settings.frame_enhancement_mode_desc": "选择适合画面的处理风格；推荐模式会优先修复线条与细节",
        "settings.frame_enhancement_kind.off": "关闭",
        "settings.frame_enhancement_kind.preset": "预设",
        "settings.frame_enhancement_kind.custom": "自定义",
        "settings.frame_enhancement_custom_desc": "算法按列表从上到下执行；2× 重建、目标尺寸缩放和同尺寸修复会保留各自语义",
        "settings.frame_enhancement_custom_empty": "当前没有算法；添加后会按照列表顺序处理画面。",
        "settings.frame_enhancement_custom_add": "＋ 添加算法",
        "settings.frame_enhancement_custom_step": "步骤 %d",
        "settings.frame_enhancement_custom_remove": "移除这个算法",
        "settings.frame_enhancement_algorithm.anime4k_upscale_s.desc": "轻量 2× 动漫线条与边缘重建",
        "settings.frame_enhancement_algorithm.anime4k_upscale_l.desc": "高质量 2× 动漫细节重建",
        "settings.frame_enhancement_algorithm.anime4k_upscale_vl.desc": "最高质量 2× 动漫细节重建，负载很高",
        "settings.frame_enhancement_algorithm.anime4k_restore_s.desc": "同尺寸修复模糊线条、噪声和压缩痕迹",
        "settings.frame_enhancement_algorithm.anime4k_restore_soft_s.desc": "轻量柔和修复，降低文字和细线过锐风险",
        "settings.frame_enhancement_algorithm.anime4k_restore_soft_m.desc": "更深入的柔和修复，兼顾纹理和小字",
        "settings.frame_enhancement_algorithm.anime4k_restore_l.desc": "高质量同尺寸线条与细节修复",
        "settings.frame_enhancement_algorithm.anime4k_restore_vl.desc": "最高质量同尺寸修复，负载很高",
        "settings.frame_enhancement_algorithm.fsr1_easu.desc": "边缘感知缩放到所选输出分辨率",
        "settings.frame_enhancement_algorithm.fsr1_rcas.desc": "同尺寸自适应锐化，强化放大后的清晰度",
        "settings.frame_enhancement_algorithm.bicubic.desc": "自然柔和地缩放到所选输出分辨率",
        "settings.frame_enhancement_algorithm.lanczos.desc": "锐利地缩放到所选输出分辨率",
        "settings.frame_enhancement_algorithm.fxaa.desc": "同尺寸平滑明显锯齿",
        "settings.frame_enhancement_algorithm.ravu_lite_r2.desc": "基于方向纹理的轻量 2× 重建",
        "settings.frame_enhancement_algorithm.cunny_2x4c.desc": "轻量神经网络 2× 纹理重建",
        "settings.frame_enhancement_algorithm.nnedi3_nns16.desc": "针对线条和斜边的 2× 插值重建",
        "settings.frame_enhancement_mode.anime4k": "智能修复（推荐）",
        "settings.frame_enhancement_mode.fsr1": "均衡清晰",
        "settings.frame_enhancement_mode.bicubic": "自然柔和",
        "settings.frame_enhancement_mode.lanczos": "锐利细节",
        "settings.frame_enhancement_mode.ravu": "精细放大",
        "settings.frame_enhancement_mode.cunny": "纹理增强",
        "settings.frame_enhancement_mode.nnedi3": "线条平滑",
        "settings.frame_enhancement_mode.chain_4k_max": "4K 极致（极高负载）",
        "settings.frame_enhancement_mode.chain_lossless": "无损画质（极高负载）",
        "settings.frame_enhancement_mode.chain_ultra": "超清平衡（高负载）",
        "settings.frame_enhancement_mode.chain_detail": "高精细（中高负载）",
        "settings.frame_enhancement_mode.chain_balanced": "均衡增强（中等负载）",
        "settings.frame_enhancement_mode.chain_soft": "柔和清晰（推荐，中低负载）",
        "settings.frame_enhancement_mode.chain_light": "轻量增强（低负载）",
        "settings.frame_enhancement_mode.chain_basic": "基础增强（最低负载）",
        "settings.perf": "性能监控",
        "settings.perf_desc": "显示帧率、进程内存、GPU 内存估算和图形 API 信息",
        "settings.fps_limit": "帧率限制",
        "settings.fps_limit_desc": "开启后使用下方目标帧率；关闭时交给显示刷新率",
        "settings.landscape": "锁定横屏",
        "settings.landscape_desc": "游戏运行时强制横屏显示（手机推荐开启）",
        "settings.target_fps": "目标帧率",
        "settings.target_fps_desc": "限制 C++ 引擎 tick/render 频率；可选 60–144 FPS",
        "settings.plugin_load_mode": "插件加载模式",
        "settings.plugin_load_mode_desc": "核心模式只加载常用兼容插件；完整模式保留旧版全量注册",
        "settings.plugin_trace": "插件调用追踪",
        "settings.plugin_trace_desc": "将所有插件原生调用记录到 plugin_trace.log 用于调试",
        "settings.mock": "Mock 绕过",
        "settings.mock_desc": "为缺失插件返回 mock 对象以抑制错误。关闭可暴露真实错误用于调试。",
        "settings.console_log": "控制台日志文件",
        "settings.console_log_desc": "将引擎控制台输出额外写入本地日志文件",
        "settings.trace_log": "追踪日志",
        "settings.trace_log_desc": "启用 spdlog trace 级别详细日志，输出最大调试信息",
        "settings.export_tjs": "导出 TJS 脚本",
        "settings.export_tjs_desc": "游戏加载时自动从 XP3 中导出反汇编的 TJS 字节码脚本",
        "settings.error_dialog_logs": "错误弹窗附带日志",
        "settings.error_dialog_logs_desc": "真正异常弹窗中追加最近 20 行引擎日志；默认关闭",
        "settings.version": "版本",
        "settings.author": "作者",
        "settings.email": "邮箱",
        "settings.app_service_filing": "App服务备案号",
        "settings.app_service_filing_open_failed": "无法打开备案信息网页，请稍后重试。",
        "iap.list_limit.title": "目录限制解锁",
        "iap.list_limit.desc": "永久解锁视觉小说库和视频库中的全部目录项目",
        "iap.coffee.title": "请作者喝一杯咖啡",
        "iap.coffee.desc": "可以获得30天的内测功能使用",
        "iap.coffee.active_until": "内测功能有效期至：%s",
        "iap.coffee.inactive": "内测功能当前未启用",
        "iap.coffee.purchase_success": "感谢支持！内测功能有效期至：%s",
        "support.coffee.title": "请作者喝一杯咖啡",
        "support.coffee.desc": "打开支付宝支持作者，不影响游戏导入或启动",
        "support.coffee.open": "打开支付宝",
        "support.coffee.thanks_title": "感谢支持",
        "support.coffee.thanks": "谢谢你的支持！",
        "support.coffee.open_failed": "无法打开浏览器，请稍后重试。",
        "secret.unlock.title": "内部解锁",
        "secret.unlock.body": "请输入解锁密码",
        "secret.unlock.placeholder": "密码",
        "secret.unlock.confirm": "确认",
        "secret.unlock.failed": "密码不正确，请重试。",
        "secret.unlock.success": "内购已解锁，内测功能有效期至：%s",
        "iap.beta_runtime_unavailable": "此视觉小说兼容正在测试中，请等待后续支持",
        "iap.status.purchased": "已购买",
        "iap.status.not_purchased": "未购买",
        "iap.status.loading": "正在读取商品信息…",
        "iap.status.unavailable": "当前无法连接 App Store",
        "iap.buy": "购买",
        "iap.restore": "恢复购买",
        "iap.restore_desc": "使用当前 App Store 账户恢复已购买的非消耗型项目",
        "iap.restore_action": "恢复",
        "iap.checking_title": "正在验证购买状态",
        "iap.checking_body": "正在校验当前 App Store 账户是否已购买目录限制解锁…",
        "iap.limit_title": "使用上限",
        "iap.limit_body": "未购买时只能运行列表中的第一项。请点击下方购买按钮解锁目录限制。",
        "iap.purchase_success": "目录限制已解锁。",
        "iap.purchase_pending": "购买正在等待批准，批准后可在设置中恢复购买。",
        "iap.purchase_cancelled": "购买已取消。",
        "iap.purchase_failed": "购买失败：%s",
        "iap.restore_success": "购买已恢复，目录限制已解锁。",
        "iap.restore_none": "当前 App Store 账户没有可恢复的目录限制解锁。",
        "iap.verify_failed": "无法验证当前 App Store 账户的购买状态：%s",
        "settings.legal": "隐私与免责协议",
        "settings.legal_desc": "查看当前版本的隐私政策、使用规则、风险提示与免责声明",
        "settings.legal_open": "阅读协议",
        "settings.ios_statement": "Apple App Store 额外声明",
        "settings.ios_statement_desc": "查看 GPLv3、App Store 分发附加许可、源码义务及适用范围",
        "settings.ios_statement_open": "阅读声明",
        "ios_statement.title": "Apple App Store 额外声明",
        "ios_statement.first_summary": "Apple 平台首次使用确认（第 2/2 份）。您需要同时同意本声明和隐私与免责协议，才能使用视觉小说与视频功能。",
        "legal.title": "隐私政策与使用免责协议",
        "legal.first_summary": "首次使用前，请阅读并选择是否同意。协议可在「设置 > 关于」中随时查看。",
        "legal.first_summary_ios": "Apple 平台首次使用确认（第 1/2 份）。同意本协议后，还需要确认 Apple App Store 额外声明。",
        "legal.accept": "同意并继续",
        "legal.decline": "拒绝",
        "legal.close": "关闭",
        "legal.declined_title": "尚未同意协议",
        "legal.declined_body": "您尚未同意全部必需声明，Aether 不会开放视觉小说或视频功能。iOS 不允许应用主动结束自身进程，请从系统应用切换界面关闭本应用；也可以返回重新阅读并逐项同意。",
        "legal.review_again": "重新阅读",
        "detail.eyebrow": "游戏详情",
        "detail.runtime_profile": "运行配置 / %s",
        "detail.last_played": "上次游玩：%s",
        "detail.played": "已玩 %s",
        "detail.launch": "启动视觉小说",
        "detail.launch_entry": "启动入口：%s",
        "detail.default_launch_entry": "游戏目录（自动检测）",
        "detail.set_launch_file": "切换启动文件",
        "detail.reset_launch_file": "恢复目录自动检测",
        "detail.set_cover": "设置封面",
        "detail.delete_cover": "删除封面",
        "detail.clear_cover": "清除封面",
        "detail.rename": "重命名",
        "detail.remove": "移除视觉小说",
        "detail.delete_builtin": "删除内置 Demo",
        "game.today": "今天",
        "game.days_ago": "%d 天前",
        "game.played_duration": "已玩 %s",
        "game.never_played": "尚未游玩",
        "game.builtin_demo": "内置 Demo",
        "game.local": "本地游戏",
        "game.type_directory": "目录",
        "game.type_archive": "归档",
        "dialog.import_title": "导入视觉小说",
        "dialog.import_guide_body": "请使用「文件」App 将视觉小说文件夹复制到本应用的目录：\n\n1. 打开 iPhone / iPad 上的「文件」App\n2. 前往：我的 iPhone / iPad > Aether > Games\n3. 将视觉小说文件夹复制到 Games 目录\n4. 返回本应用，点击「刷新」检测新视觉小说\n\n视觉小说目录：Games/",
        "dialog.ok": "知道了",
        "dialog.scrape_title": "完善游戏信息",
        "dialog.scrape_body": "已添加「%s」。要现在设置封面和显示名称吗？",
        "dialog.later": "稍后",
        "dialog.open_detail": "现在设置",
        "dialog.choose_cover": "选择封面图片",
        "dialog.choose_launch_file": "选择启动文件",
        "dialog.rename": "重命名",
        "dialog.remove_body": "从列表移除「%s」？不会删除磁盘上的视觉小说文件。",
        "dialog.delete_builtin_body": "删除内置示例「%s」及其本地存档？删除后不会自动恢复。",
        "dialog.remove": "移除",
        "dialog.delete": "删除",
        "dialog.select_game_dir": "选择游戏目录",
        "dialog.select_local_game_dir": "选择本地游戏目录",
        "dialog.cancel": "取消",
        "dialog.dev_mount": "开发挂载  %s",
        "message.web_manifest_failed": "无法读取 Web 游戏挂载清单",
        "message.web_mount_failed": "Web 本地挂载失败：%s",
        "message.unknown_error": "未知错误",
        "message.browser_picker_unsupported": "当前浏览器不支持本地文件选择",
        "message.browser_no_ticket": "浏览器没有返回导入任务",
        "message.web_import_failed": "本地游戏导入失败：%s",
        "message.web_game_invalid": "浏览器返回的游戏信息无效",
        "message.web_import_timeout": "本地游戏导入超时",
        "message.web_picker_unsupported_long": "当前浏览器不支持直接选择本地游戏文件。请使用支持 File System Access 或目录上传的浏览器。",
        "message.android_storage_permission_required": "需要允许 Aether 访问文件系统后才能导入或启动外部游戏。请在系统弹窗或权限设置中授予文件访问权限，然后再试。",
        "message.android_video_storage_permission_required": "需要允许 Aether 访问文件系统后才能导入视频。请在系统弹窗或权限设置中授予文件访问权限，然后再试。",
        "message.path_missing": "游戏路径不存在",
        "message.launch_file_unsupported": "启动文件只支持 EXE 或 XP3",
        "message.launch_file_outside_game": "启动文件必须位于当前游戏目录内",
        "message.launch_file_missing": "启动文件不存在：%s",
        "message.cover_file_missing": "无法读取所选封面图片：%s",
        "message.game_exists": "游戏已存在：%s",
        "message.builtin_delete_failed": "删除内置 Demo 时发生错误：%s",
        "alert.error_title": "Aether 错误",
        "alert.warning_title": "Aether 警告",
        "alert.runtime_class_missing": "运行时扩展加载失败：AetherRuntimePlayer 不可用",
        "alert.runtime_create_failed": "运行时扩展加载失败：无法创建 AetherRuntimePlayer",
        "loading.title": "正在启动视觉小说...",
        "loading.translation_model": "正在加载本地翻译模型...",
        "loading.translation_model_detail": "首次加载可能出现短暂卡顿，请耐心等待。"
    },
    LANG_ZH_HANT: {
        "home.subtitle": "多功能媒體播放器",
        "video.status": "影片庫",
        "video.empty_title": "Video 資料夾中還沒有影片",
        "video.empty_help_ios": "使用「檔案」App 將影片複製到：\n我的 iPhone / iPad > Aether > Video\n支援同名 SRT、VTT、ASS 字幕",
        "video.empty_help_desktop": "點擊「匯入影片」加入本機影片；同目錄同名字幕會自動載入",
        "video.import": "匯入影片",
        "video.refresh": "重新整理",
        "video.guide": "使用說明",
        "video.guide_title": "匯入影片",
        "video.guide_body_ios": "請使用「檔案」App 將影片複製到本 App 的目錄：\n\n1. 開啟 iPhone / iPad 上的「檔案」App\n2. 前往：我的 iPhone / iPad > Aether > Video\n3. 將影片和同名字幕複製到 Video 目錄\n4. 返回本 App，點選「重新整理」偵測新影片\n\n影片目錄：Video/\n字幕支援：SRT、VTT、ASS、SSA",
        "video.guide_body_desktop": "請將本機影片匯入影片庫：\n\n1. 點選「匯入影片」\n2. 選擇需要播放的影片檔案\n3. 如需字幕，請將同名字幕放在影片所在目錄\n4. 返回影片庫即可播放，進度會自動記錄\n\n字幕支援：SRT、VTT、ASS、SSA",
        "video.remove": "移除影片",
        "video.remove_body": "要從影片庫移除「%s」嗎？不會刪除磁碟上的影片檔案，並會清除該影片的播放進度。",
        "video.back": "返回",
        "video.pause": "暫停",
        "video.play": "播放",
        "video.subtitle_off": "字幕關閉",
        "video.subtitle_embedded": "%s（內嵌）",
        "video.resume": "繼續上次播放",
        "video.progress": "已播放至 %s / %s",
        "video.open_failed": "無法播放該影片：%s",
        "home.status": "視覺小說庫",
        "nav.library": "視覺小說",
        "nav.videos": "影片庫",
        "nav.collapse_sidebar": "收合側邊欄",
        "nav.expand_sidebar": "展開側邊欄",
        "home.empty_title": "尚未加入任何遊戲",
        "home.game_count": "%d 個遊戲",
        "video.video_count": "%d 個影片",
        "search.games_placeholder": "搜尋視覺小說",
        "search.videos_placeholder": "搜尋影片",
        "search.filtered_count": "顯示 %d / %d",
        "search.no_results_title": "找不到相符內容",
        "search.no_results_help": "請嘗試其他關鍵字或清除搜尋欄",
        "home.refresh": "重新整理",
        "home.import": "匯入",
        "home.import_guide": "匯入指南",
        "home.empty_help_ios": "使用「檔案」App 將遊戲資料夾複製到：\n我的 iPhone / iPad > Aether > Games\n然後點選「重新整理」",
        "home.empty_help_web": "點選「匯入」選擇本機視覺小說目錄",
        "home.empty_help_desktop": "點選「匯入」選擇視覺小說目錄",
        "settings.title": "設定",
        "settings.save": "儲存",
        "settings.unsaved_title": "儲存設定？",
        "settings.unsaved_body": "設定已修改。離開前是否儲存這些變更？",
        "settings.unsaved_discard": "不儲存",
        "settings.unsaved_close": "關閉",
        "settings.section.interface": "介面",
        "settings.section.render": "渲染",
        "settings.section.developer": "開發者",
        "settings.section.about": "關於",
        "settings.section.purchases": "App 內購買",
        "settings.language": "語言",
        "settings.language_desc": "預設跟隨系統；也可以固定為簡體中文、繁體中文、英語、日語或韓語",
        "settings.translation_model": "本機翻譯模型",
        "settings.translation_model_desc": "選擇外部 GGUF 模型。遊戲啟動時載入；偵測到文字不是目前介面語言時自動翻譯",
        "settings.translation_model_select": "選擇 GGUF 模型",
        "settings.translation_model_selected": "已選模型",
        "settings.translation_model_clear": "停用本機翻譯",
        "settings.translation_model_clear_desc": "清除模型路徑；不會刪除磁碟上的模型檔案",
        "settings.style": "風格",
        "settings.style_desc": "可在目前深色風格和舊版原始淺色風格之間切換",
        "settings.ui_scale": "介面比例",
        "settings.ui_scale_desc": "調整 iPhone 和 iPad 上的介面大小，儲存後立即生效",
        "settings.virtual_control_menu": "遊戲內控制選單",
        "settings.virtual_control_menu_desc": "在遊戲畫面右上角顯示虛擬控制選單按鈕",
        "settings.keyboard_control_opacity": "Keyboard 模式按鍵透明度",
        "settings.keyboard_control_opacity_desc": "調整螢幕虛擬按鍵的透明度；滑鼠指標保持清晰",
        "ui_scale.compact": "較小",
        "ui_scale.comfortable": "合適",
        "ui_scale.standard": "標準",
        "style.dark": "深色",
        "style.classic": "原始淺色",
        "language.system": "跟隨系統",
        "language.system_with_value": "跟隨系統（%s）",
        "language.zh_hans": "简体中文",
        "language.zh_hant": "繁體中文",
        "language.en": "English",
        "language.ja": "日本語",
        "language.ko": "한국어",
        "settings.render_backend": "渲染管線",
        "settings.render_backend_desc": "儲存後生效；執行中切換需重新啟動目前遊戲",
        "settings.surface_mode": "畫布尺寸",
        "settings.surface_mode_desc": "Game Native 依遊戲基準畫布執行；Display Fit 依裝置顯示尺寸執行",
        "settings.upscale": "縮放演算法",
        "settings.upscale_desc": "外層拉伸畫面時使用；Bicubic/Lanczos 提供更高品質取樣",
        "settings.output_resolution": "輸出解析度",
        "settings.output_resolution_desc": "設定外層縮放與畫面增強的目標解析度；較高檔位會增加 GPU 與顯示記憶體用量",
        "settings.output_resolution.original": "原始",
        "settings.frame_enhancement": "畫面增強",
        "settings.frame_enhancement_desc": "使用 GPU 修復線條，並依輸出解析度高品質放大；儲存後立即生效",
        "settings.frame_enhancement_unavailable_desc": "目前建置或圖形裝置不支援畫面增強；開啟後仍會安全使用原始畫面",
        "settings.frame_enhancement_mode": "增強效果",
        "settings.frame_enhancement_mode_desc": "選擇適合畫面的處理風格；建議模式會優先修復線條與細節",
        "settings.frame_enhancement_kind.off": "關閉",
        "settings.frame_enhancement_kind.preset": "預設",
        "settings.frame_enhancement_kind.custom": "自訂",
        "settings.frame_enhancement_custom_desc": "演算法依清單由上而下執行；2× 重建、目標尺寸縮放與同尺寸修復會保留各自語意",
        "settings.frame_enhancement_custom_empty": "目前沒有演算法；加入後會依清單順序處理畫面。",
        "settings.frame_enhancement_custom_add": "＋ 加入演算法",
        "settings.frame_enhancement_custom_step": "步驟 %d",
        "settings.frame_enhancement_custom_remove": "移除此演算法",
        "settings.frame_enhancement_algorithm.anime4k_upscale_s.desc": "輕量 2× 動漫線條與邊緣重建",
        "settings.frame_enhancement_algorithm.anime4k_upscale_l.desc": "高品質 2× 動漫細節重建",
        "settings.frame_enhancement_algorithm.anime4k_upscale_vl.desc": "最高品質 2× 動漫細節重建，負載很高",
        "settings.frame_enhancement_algorithm.anime4k_restore_s.desc": "同尺寸修復模糊線條、雜訊與壓縮痕跡",
        "settings.frame_enhancement_algorithm.anime4k_restore_soft_s.desc": "輕量柔和修復，降低文字與細線過銳風險",
        "settings.frame_enhancement_algorithm.anime4k_restore_soft_m.desc": "更深入的柔和修復，兼顧紋理與小字",
        "settings.frame_enhancement_algorithm.anime4k_restore_l.desc": "高品質同尺寸線條與細節修復",
        "settings.frame_enhancement_algorithm.anime4k_restore_vl.desc": "最高品質同尺寸修復，負載很高",
        "settings.frame_enhancement_algorithm.fsr1_easu.desc": "邊緣感知縮放至所選輸出解析度",
        "settings.frame_enhancement_algorithm.fsr1_rcas.desc": "同尺寸自適應銳化，強化放大後的清晰度",
        "settings.frame_enhancement_algorithm.bicubic.desc": "自然柔和地縮放至所選輸出解析度",
        "settings.frame_enhancement_algorithm.lanczos.desc": "銳利地縮放至所選輸出解析度",
        "settings.frame_enhancement_algorithm.fxaa.desc": "同尺寸平滑明顯鋸齒",
        "settings.frame_enhancement_algorithm.ravu_lite_r2.desc": "以方向紋理進行輕量 2× 重建",
        "settings.frame_enhancement_algorithm.cunny_2x4c.desc": "輕量神經網路 2× 紋理重建",
        "settings.frame_enhancement_algorithm.nnedi3_nns16.desc": "針對線條與斜邊的 2× 插值重建",
        "settings.frame_enhancement_mode.anime4k": "智慧修復（建議）",
        "settings.frame_enhancement_mode.fsr1": "均衡清晰",
        "settings.frame_enhancement_mode.bicubic": "自然柔和",
        "settings.frame_enhancement_mode.lanczos": "銳利細節",
        "settings.frame_enhancement_mode.ravu": "精細放大",
        "settings.frame_enhancement_mode.cunny": "紋理增強",
        "settings.frame_enhancement_mode.nnedi3": "線條平滑",
        "settings.frame_enhancement_mode.chain_4k_max": "4K 極致（極高負載）",
        "settings.frame_enhancement_mode.chain_lossless": "無損畫質（極高負載）",
        "settings.frame_enhancement_mode.chain_ultra": "超清平衡（高負載）",
        "settings.frame_enhancement_mode.chain_detail": "高精細（中高負載）",
        "settings.frame_enhancement_mode.chain_balanced": "均衡增強（中等負載）",
        "settings.frame_enhancement_mode.chain_soft": "柔和清晰（建議，中低負載）",
        "settings.frame_enhancement_mode.chain_light": "輕量增強（低負載）",
        "settings.frame_enhancement_mode.chain_basic": "基礎增強（最低負載）",
        "settings.perf": "效能監控",
        "settings.perf_desc": "顯示幀率、程序記憶體、GPU 記憶體估算和圖形 API 資訊",
        "settings.fps_limit": "幀率限制",
        "settings.fps_limit_desc": "開啟後使用下方目標幀率；關閉時交給顯示刷新率",
        "settings.landscape": "鎖定橫向",
        "settings.landscape_desc": "遊戲執行時強制橫向顯示（手機建議開啟）",
        "settings.target_fps": "目標幀率",
        "settings.target_fps_desc": "限制 C++ 引擎 tick/render 頻率；可選 60–144 FPS",
        "settings.plugin_load_mode": "外掛載入模式",
        "settings.plugin_load_mode_desc": "核心模式只載入常用相容外掛；完整模式保留舊版全量註冊",
        "settings.plugin_trace": "外掛呼叫追蹤",
        "settings.plugin_trace_desc": "將所有外掛原生呼叫記錄到 plugin_trace.log 以便除錯",
        "settings.mock": "Mock 繞過",
        "settings.mock_desc": "為缺失外掛返回 mock 物件以抑制錯誤。關閉可暴露真實錯誤用於除錯。",
        "settings.console_log": "主控台日誌檔",
        "settings.console_log_desc": "將引擎主控台輸出額外寫入本機日誌檔",
        "settings.trace_log": "追蹤日誌",
        "settings.trace_log_desc": "啟用 spdlog trace 級別詳細日誌，輸出最大除錯資訊",
        "settings.export_tjs": "匯出 TJS 腳本",
        "settings.export_tjs_desc": "遊戲載入時自動從 XP3 中匯出反組譯的 TJS 位元組碼腳本",
        "settings.error_dialog_logs": "錯誤彈窗附帶日誌",
        "settings.error_dialog_logs_desc": "真正異常彈窗中追加最近 20 行引擎日誌；預設關閉",
        "settings.version": "版本",
        "settings.author": "作者",
        "settings.email": "信箱",
        "settings.app_service_filing": "App 服務備案號",
        "settings.app_service_filing_open_failed": "無法開啟備案資訊網頁，請稍後再試。",
        "iap.list_limit.title": "解除目錄限制",
        "iap.list_limit.desc": "永久解鎖視覺小說庫與影片庫中的所有目錄項目",
        "iap.coffee.title": "請作者喝一杯咖啡",
        "iap.coffee.desc": "可獲得 30 天的測試功能使用權",
        "iap.coffee.active_until": "測試功能有效期限至：%s",
        "iap.coffee.inactive": "測試功能目前尚未啟用",
        "iap.coffee.purchase_success": "感謝支持！測試功能有效期限至：%s",
        "support.coffee.title": "請作者喝一杯咖啡",
        "support.coffee.desc": "開啟支付寶支持作者，不影響遊戲匯入或啟動",
        "support.coffee.open": "開啟支付寶",
        "support.coffee.thanks_title": "感謝支持",
        "support.coffee.thanks": "謝謝你的支持！",
        "support.coffee.open_failed": "無法開啟瀏覽器，請稍後再試。",
        "secret.unlock.title": "內部解鎖",
        "secret.unlock.body": "請輸入解鎖密碼",
        "secret.unlock.placeholder": "密碼",
        "secret.unlock.confirm": "確認",
        "secret.unlock.failed": "密碼不正確，請再試一次。",
        "secret.unlock.success": "內購已解鎖，測試功能有效期限至：%s",
        "iap.beta_runtime_unavailable": "此視覺小說的相容支援仍在測試中，請等待後續支援",
        "iap.status.purchased": "已購買",
        "iap.status.not_purchased": "尚未購買",
        "iap.status.loading": "正在載入商品資訊…",
        "iap.status.unavailable": "目前無法連接 App Store",
        "iap.buy": "購買",
        "iap.restore": "恢復購買",
        "iap.restore_desc": "使用目前的 App Store 帳號恢復已購買的非消耗型項目",
        "iap.restore_action": "恢復",
        "iap.checking_title": "正在驗證購買狀態",
        "iap.checking_body": "正在確認目前的 App Store 帳號是否已購買解除目錄限制…",
        "iap.limit_title": "使用上限",
        "iap.limit_body": "尚未購買時只能執行清單中的第一項。請點選下方購買按鈕解除目錄限制。",
        "iap.purchase_success": "目錄限制已解除。",
        "iap.purchase_pending": "購買正在等待核准，核准後可在設定中恢復購買。",
        "iap.purchase_cancelled": "購買已取消。",
        "iap.purchase_failed": "購買失敗：%s",
        "iap.restore_success": "購買已恢復，目錄限制已解除。",
        "iap.restore_none": "目前的 App Store 帳號沒有可恢復的解除目錄限制。",
        "iap.verify_failed": "無法驗證目前 App Store 帳號的購買狀態：%s",
        "settings.legal": "隱私與免責協議",
        "settings.legal_desc": "查看目前版本的隱私政策、使用規則、風險提示與免責聲明",
        "settings.legal_open": "閱讀協議",
        "settings.ios_statement": "Apple App Store 額外聲明",
        "settings.ios_statement_desc": "查看 GPLv3、App Store 發布附加許可、原始碼義務及適用範圍",
        "settings.ios_statement_open": "閱讀聲明",
        "ios_statement.title": "Apple App Store 額外聲明",
        "ios_statement.first_summary": "Apple 平台首次使用確認（第 2/2 份）。您需要同時同意本聲明和隱私與免責協議，才能使用視覺小說與影片功能。",
        "legal.title": "隱私政策與使用免責協議",
        "legal.first_summary": "首次使用前，請閱讀並選擇是否同意。協議可在「設定 > 關於」中隨時查看。",
        "legal.first_summary_ios": "Apple 平台首次使用確認（第 1/2 份）。同意本協議後，還需要確認 Apple App Store 額外聲明。",
        "legal.accept": "同意並繼續",
        "legal.decline": "拒絕",
        "legal.close": "關閉",
        "legal.declined_title": "尚未同意協議",
        "legal.declined_body": "您尚未同意全部必需聲明，Aether 不會開放視覺小說或影片功能。iOS 不允許 App 主動結束自身程序，請從系統 App 切換畫面關閉本 App；也可以返回重新閱讀並逐項同意。",
        "legal.review_again": "重新閱讀",
        "detail.eyebrow": "遊戲詳情",
        "detail.runtime_profile": "執行設定 / %s",
        "detail.last_played": "上次遊玩：%s",
        "detail.played": "已玩 %s",
        "detail.launch": "啟動視覺小說",
        "detail.launch_entry": "啟動入口：%s",
        "detail.default_launch_entry": "遊戲目錄（自動偵測）",
        "detail.set_launch_file": "切換啟動檔案",
        "detail.reset_launch_file": "恢復目錄自動偵測",
        "detail.set_cover": "設定封面",
        "detail.rename": "重新命名",
        "detail.remove": "移除視覺小說",
        "detail.delete_builtin": "刪除內建 Demo",
        "game.today": "今天",
        "game.days_ago": "%d 天前",
        "game.played_duration": "已玩 %s",
        "game.never_played": "尚未遊玩",
        "game.builtin_demo": "內建 Demo",
        "game.local": "本機遊戲",
        "game.type_directory": "目錄",
        "game.type_archive": "封存",
        "dialog.import_title": "匯入視覺小說",
        "dialog.import_guide_body": "請使用「檔案」App 將視覺小說資料夾複製到本 App 的目錄：\n\n1. 開啟 iPhone / iPad 上的「檔案」App\n2. 前往：我的 iPhone / iPad > Aether > Games\n3. 將視覺小說資料夾複製到 Games 目錄\n4. 返回本 App，點選「重新整理」偵測新視覺小說\n\n視覺小說目錄：Games/",
        "dialog.ok": "知道了",
        "dialog.scrape_title": "完善遊戲資訊",
        "dialog.scrape_body": "已加入「%s」。要現在設定封面和顯示名稱嗎？",
        "dialog.later": "稍後",
        "dialog.open_detail": "現在設定",
        "dialog.choose_cover": "選擇封面圖片",
        "dialog.choose_launch_file": "選擇啟動檔案",
        "dialog.rename": "重新命名",
        "dialog.remove_body": "要從列表移除「%s」嗎？不會刪除磁碟上的視覺小說檔案。",
        "dialog.delete_builtin_body": "要刪除內建示例「%s」及其本機存檔嗎？刪除後不會自動還原。",
        "dialog.remove": "移除",
        "dialog.delete": "刪除",
        "dialog.select_game_dir": "選擇遊戲目錄",
        "dialog.select_local_game_dir": "選擇本機遊戲目錄",
        "dialog.cancel": "取消",
        "dialog.dev_mount": "開發掛載  %s",
        "message.web_manifest_failed": "無法讀取 Web 遊戲掛載清單",
        "message.web_mount_failed": "Web 本機掛載失敗：%s",
        "message.unknown_error": "未知錯誤",
        "message.browser_picker_unsupported": "目前瀏覽器不支援本機檔案選擇",
        "message.browser_no_ticket": "瀏覽器沒有返回匯入任務",
        "message.web_import_failed": "本機遊戲匯入失敗：%s",
        "message.web_game_invalid": "瀏覽器返回的遊戲資訊無效",
        "message.web_import_timeout": "本機遊戲匯入逾時",
        "message.web_picker_unsupported_long": "目前瀏覽器不支援直接選擇本機遊戲檔案。請使用支援 File System Access 或目錄上傳的瀏覽器。",
        "message.android_storage_permission_required": "需要允許 Aether 存取檔案系統後才能匯入或啟動外部遊戲。請在系統彈窗或權限設定中授予檔案存取權限，然後再試。",
        "message.android_video_storage_permission_required": "需要允許 Aether 存取檔案系統後才能匯入影片。請在系統彈窗或權限設定中授予檔案存取權限，然後再試。",
        "message.path_missing": "遊戲路徑不存在",
        "message.launch_file_unsupported": "啟動檔案僅支援 EXE 或 XP3",
        "message.launch_file_outside_game": "啟動檔案必須位於目前遊戲目錄內",
        "message.launch_file_missing": "啟動檔案不存在：%s",
        "message.cover_file_missing": "無法讀取所選封面圖片：%s",
        "message.game_exists": "遊戲已存在：%s",
        "message.builtin_delete_failed": "刪除內建 Demo 時發生錯誤：%s",
        "alert.error_title": "Aether 錯誤",
        "alert.warning_title": "Aether 警告",
        "alert.runtime_class_missing": "執行時擴充載入失敗：AetherRuntimePlayer 不可用",
        "alert.runtime_create_failed": "執行時擴充載入失敗：無法建立 AetherRuntimePlayer",
        "loading.title": "正在啟動視覺小說...",
        "loading.translation_model": "正在載入本機翻譯模型...",
        "loading.translation_model_detail": "首次載入可能會短暫停頓，請耐心等候。"
    },
    LANG_EN: {
        "home.subtitle": "Multifunction Media Player",
        "video.status": "Video Library",
        "video.empty_title": "No videos in the Video folder",
        "video.empty_help_ios": "Copy videos with the Files app to:\nOn My iPhone / iPad > Aether > Video\nMatching SRT, VTT and ASS subtitles are supported",
        "video.empty_help_desktop": "Import a local video; matching subtitles in the same folder load automatically",
        "video.import": "Import video",
        "video.refresh": "Refresh",
        "video.guide": "How to use",
        "video.guide_title": "Import Video",
        "video.guide_body_ios": "Use the Files app to copy videos into this app's directory:\n\n1. Open the Files app on your iPhone / iPad\n2. Go to: On My iPhone / iPad > Aether > Video\n3. Copy videos and matching subtitle files into Video\n4. Return to this app and tap Refresh to detect new videos\n\nVideo directory: Video/\nSubtitles: SRT, VTT, ASS, SSA",
        "video.guide_body_desktop": "Import a local video into the video library:\n\n1. Click Import Video\n2. Select the video file you want to play\n3. For subtitles, place a matching subtitle file beside the video\n4. Return to the library to play; progress is saved automatically\n\nSubtitles: SRT, VTT, ASS, SSA",
        "video.remove": "Remove Video",
        "video.remove_body": "Remove \"%s\" from the video library? The video file will remain on disk, and its playback progress will be cleared.",
        "video.back": "Back",
        "video.pause": "Pause",
        "video.play": "Play",
        "video.subtitle_off": "Subtitles off",
        "video.subtitle_embedded": "%s (embedded)",
        "video.resume": "Resume playback",
        "video.progress": "Played to %s / %s",
        "video.open_failed": "Could not play this video: %s",
        "home.status": "Visual Novel Library",
        "nav.library": "Visual Novels",
        "nav.videos": "Videos",
        "nav.collapse_sidebar": "Collapse sidebar",
        "nav.expand_sidebar": "Expand sidebar",
        "home.empty_title": "No games added yet",
        "home.game_count": "%d games",
        "video.video_count": "%d videos",
        "search.games_placeholder": "Search visual novels",
        "search.videos_placeholder": "Search videos",
        "search.filtered_count": "Showing %d of %d",
        "search.no_results_title": "No matches found",
        "search.no_results_help": "Try another keyword or clear the search field",
        "home.refresh": "Refresh",
        "home.import": "Import",
        "home.import_guide": "Import Guide",
        "home.empty_help_ios": "Use the Files app to copy your game folder to:\nOn My iPhone / iPad > Aether > Games\nThen tap Refresh",
        "home.empty_help_web": "Tap Import to choose a local visual novel folder",
        "home.empty_help_desktop": "Tap Import to choose a visual novel folder",
        "settings.title": "Settings",
        "settings.save": "Save",
        "settings.unsaved_title": "Save settings?",
        "settings.unsaved_body": "Your settings have changed. Save them before leaving?",
        "settings.unsaved_discard": "Don't Save",
        "settings.unsaved_close": "Close",
        "settings.section.interface": "Interface",
        "settings.section.render": "Rendering",
        "settings.section.developer": "Developer",
        "settings.section.about": "About",
        "settings.section.purchases": "In-App Purchases",
        "settings.language": "Language",
        "settings.language_desc": "Defaults to the system language; you can pin Simplified Chinese, Traditional Chinese, English, Japanese, or Korean",
        "settings.translation_model": "Local Translation Model",
        "settings.translation_model_desc": "Choose an external GGUF model. It loads when a game starts and translates text that does not match the interface language",
        "settings.translation_model_select": "Choose GGUF Model",
        "settings.translation_model_selected": "Selected Model",
        "settings.translation_model_clear": "Disable Local Translation",
        "settings.translation_model_clear_desc": "Clear the model path without deleting the model file from disk",
        "settings.style": "Style",
        "settings.style_desc": "Switch between the current dark style and the original classic light style",
        "settings.ui_scale": "Interface Scale",
        "settings.ui_scale_desc": "Adjust the interface size on iPhone and iPad; applies immediately after saving",
        "settings.virtual_control_menu": "In-Game Controls Menu",
        "settings.virtual_control_menu_desc": "Show the virtual-controls menu button at the top-right of the game view",
        "settings.keyboard_control_opacity": "Keyboard Button Opacity",
        "settings.keyboard_control_opacity_desc": "Adjust on-screen virtual-key opacity while keeping the mouse pointer clear",
        "ui_scale.compact": "Smaller",
        "ui_scale.comfortable": "Comfortable",
        "ui_scale.standard": "Standard",
        "style.dark": "Dark",
        "style.classic": "Classic Light",
        "language.system": "Follow System",
        "language.system_with_value": "Follow System (%s)",
        "language.zh_hans": "简体中文",
        "language.zh_hant": "繁體中文",
        "language.en": "English",
        "language.ja": "日本語",
        "language.ko": "한국어",
        "settings.render_backend": "Render Pipeline",
        "settings.render_backend_desc": "Applies after saving; switching during play requires restarting the current game",
        "settings.surface_mode": "Canvas Size",
        "settings.surface_mode_desc": "Game Native uses the game's base canvas; Display Fit uses the device display size",
        "settings.upscale": "Scaling",
        "settings.upscale_desc": "Used when stretching the outer frame; Bicubic/Lanczos provide higher-quality filtering",
        "settings.output_resolution": "Output Resolution",
        "settings.output_resolution_desc": "Sets the target for outer scaling and image enhancement; higher tiers use more GPU time and memory",
        "settings.output_resolution.original": "Original",
        "settings.frame_enhancement": "Image Enhancement",
        "settings.frame_enhancement_desc": "Use the GPU to restore lines and upscale to the selected output resolution; applies immediately after saving",
        "settings.frame_enhancement_unavailable_desc": "Image enhancement is unavailable in this build or on this graphics device; the original frame remains safe",
        "settings.frame_enhancement_mode": "Enhancement Effect",
        "settings.frame_enhancement_mode_desc": "Choose a visual style; the recommended effect prioritizes restoring lines and fine detail",
        "settings.frame_enhancement_kind.off": "Off",
        "settings.frame_enhancement_kind.preset": "Preset",
        "settings.frame_enhancement_kind.custom": "Custom",
        "settings.frame_enhancement_custom_desc": "Algorithms run from top to bottom; 2× reconstruction, target fitting, and same-size restoration keep their defined behavior",
        "settings.frame_enhancement_custom_empty": "No algorithms yet. Add one to build an ordered processing chain.",
        "settings.frame_enhancement_custom_add": "+ Add Algorithm",
        "settings.frame_enhancement_custom_step": "Step %d",
        "settings.frame_enhancement_custom_remove": "Remove this algorithm",
        "settings.frame_enhancement_algorithm.anime4k_upscale_s.desc": "Lightweight 2× reconstruction for anime lines and edges",
        "settings.frame_enhancement_algorithm.anime4k_upscale_l.desc": "High-quality 2× reconstruction for anime detail",
        "settings.frame_enhancement_algorithm.anime4k_upscale_vl.desc": "Maximum-quality 2× anime reconstruction with very high load",
        "settings.frame_enhancement_algorithm.anime4k_restore_s.desc": "Same-size repair for blurred lines, noise, and compression artifacts",
        "settings.frame_enhancement_algorithm.anime4k_restore_soft_s.desc": "Light soft repair that protects small text and thin lines",
        "settings.frame_enhancement_algorithm.anime4k_restore_soft_m.desc": "Deeper soft repair balancing texture detail and small text",
        "settings.frame_enhancement_algorithm.anime4k_restore_l.desc": "High-quality same-size line and detail restoration",
        "settings.frame_enhancement_algorithm.anime4k_restore_vl.desc": "Maximum-quality same-size restoration with very high load",
        "settings.frame_enhancement_algorithm.fsr1_easu.desc": "Edge-adaptive scaling to the selected output resolution",
        "settings.frame_enhancement_algorithm.fsr1_rcas.desc": "Same-size adaptive sharpening after reconstruction or scaling",
        "settings.frame_enhancement_algorithm.bicubic.desc": "Natural, soft scaling to the selected output resolution",
        "settings.frame_enhancement_algorithm.lanczos.desc": "Crisp scaling to the selected output resolution",
        "settings.frame_enhancement_algorithm.fxaa.desc": "Same-size smoothing of visible jagged edges",
        "settings.frame_enhancement_algorithm.ravu_lite_r2.desc": "Lightweight direction-aware 2× reconstruction",
        "settings.frame_enhancement_algorithm.cunny_2x4c.desc": "Light neural 2× texture reconstruction",
        "settings.frame_enhancement_algorithm.nnedi3_nns16.desc": "2× interpolation focused on lines and diagonal edges",
        "settings.frame_enhancement_mode.anime4k": "Smart Restore (Recommended)",
        "settings.frame_enhancement_mode.fsr1": "Balanced Clarity",
        "settings.frame_enhancement_mode.bicubic": "Natural Softness",
        "settings.frame_enhancement_mode.lanczos": "Crisp Detail",
        "settings.frame_enhancement_mode.ravu": "Fine Reconstruction",
        "settings.frame_enhancement_mode.cunny": "Texture Enhancement",
        "settings.frame_enhancement_mode.nnedi3": "Smooth Lines",
        "settings.frame_enhancement_mode.chain_4k_max": "4K Ultimate (Extreme Load)",
        "settings.frame_enhancement_mode.chain_lossless": "Lossless Detail (Extreme Load)",
        "settings.frame_enhancement_mode.chain_ultra": "Ultra-Clear Balance (High Load)",
        "settings.frame_enhancement_mode.chain_detail": "High Definition (Medium-High Load)",
        "settings.frame_enhancement_mode.chain_balanced": "Balanced Enhancement (Medium Load)",
        "settings.frame_enhancement_mode.chain_soft": "Soft Clarity (Recommended, Medium-Low Load)",
        "settings.frame_enhancement_mode.chain_light": "Light Enhancement (Low Load)",
        "settings.frame_enhancement_mode.chain_basic": "Basic Enhancement (Lowest Load)",
        "settings.perf": "Performance Monitor",
        "settings.perf_desc": "Show FPS, process memory, estimated GPU memory, and graphics API information",
        "settings.fps_limit": "FPS Limit",
        "settings.fps_limit_desc": "When enabled, use the target FPS below; otherwise follow the display refresh rate",
        "settings.landscape": "Lock Landscape",
        "settings.landscape_desc": "Force landscape while a game is running (recommended on phones)",
        "settings.target_fps": "Target FPS",
        "settings.target_fps_desc": "Limit the C++ engine tick/render rate; choose 60–144 FPS",
        "settings.plugin_load_mode": "Plugin Load Mode",
        "settings.plugin_load_mode_desc": "Core mode loads common compatibility plugins only; Full mode keeps the legacy registration path",
        "settings.plugin_trace": "Plugin Call Trace",
        "settings.plugin_trace_desc": "Write all native plugin calls to plugin_trace.log for debugging",
        "settings.mock": "Mock Bypass",
        "settings.mock_desc": "Return mock objects for missing plugins to suppress errors. Disable to expose real errors for debugging.",
        "settings.console_log": "Console Log File",
        "settings.console_log_desc": "Also write engine console output to a local log file",
        "settings.trace_log": "Trace Log",
        "settings.trace_log_desc": "Enable spdlog trace-level logs for maximum diagnostic output",
        "settings.export_tjs": "Export TJS Scripts",
        "settings.export_tjs_desc": "Automatically export disassembled TJS bytecode scripts from XP3 files while loading games",
        "settings.error_dialog_logs": "Attach Logs to Errors",
        "settings.error_dialog_logs_desc": "Append the latest 20 engine log lines to real error dialogs; disabled by default",
        "settings.version": "Version",
        "settings.author": "Author",
        "settings.email": "Email",
        "settings.app_service_filing": "App Service Filing Number",
        "settings.app_service_filing_open_failed": "Unable to open the filing page. Please try again later.",
        "iap.list_limit.title": "Unlock Library Limit",
        "iap.list_limit.desc": "Permanently unlock every item in the visual novel and video libraries",
        "iap.coffee.title": "Buy the Author a Coffee",
        "iap.coffee.desc": "Includes 30 days of access to beta features",
        "iap.coffee.active_until": "Beta feature access expires: %s",
        "iap.coffee.inactive": "Beta feature access is not active",
        "iap.coffee.purchase_success": "Thank you! Beta feature access expires: %s",
        "support.coffee.title": "Buy the Author a Coffee",
        "support.coffee.desc": "Open Alipay to support the author; game import and launch are unaffected",
        "support.coffee.open": "Open Alipay",
        "support.coffee.thanks_title": "Thank You",
        "support.coffee.thanks": "Thank you for your support!",
        "support.coffee.open_failed": "Unable to open the browser. Please try again later.",
        "secret.unlock.title": "Secret Unlock",
        "secret.unlock.body": "Enter the unlock passphrase",
        "secret.unlock.placeholder": "Passphrase",
        "secret.unlock.confirm": "Confirm",
        "secret.unlock.failed": "Incorrect passphrase. Please try again.",
        "secret.unlock.success": "Purchases unlocked; beta feature access expires: %s",
        "iap.beta_runtime_unavailable": "Compatibility for this visual novel is still being tested. Please wait for a future update.",
        "iap.status.purchased": "Purchased",
        "iap.status.not_purchased": "Not purchased",
        "iap.status.loading": "Loading product information…",
        "iap.status.unavailable": "The App Store is currently unavailable",
        "iap.buy": "Purchase",
        "iap.restore": "Restore Purchases",
        "iap.restore_desc": "Restore non-consumable purchases for the current App Store account",
        "iap.restore_action": "Restore",
        "iap.checking_title": "Verifying Purchase",
        "iap.checking_body": "Checking whether the current App Store account owns Unlock Library Limit…",
        "iap.limit_title": "Usage Limit",
        "iap.limit_body": "Without this purchase, only the first item in each list can run. Purchase below to unlock the library limit.",
        "iap.purchase_success": "The library limit is unlocked.",
        "iap.purchase_pending": "The purchase is awaiting approval. You can restore it from Settings after approval.",
        "iap.purchase_cancelled": "The purchase was cancelled.",
        "iap.purchase_failed": "Purchase failed: %s",
        "iap.restore_success": "The purchase was restored and the library limit is unlocked.",
        "iap.restore_none": "The current App Store account has no Unlock Library Limit purchase to restore.",
        "iap.verify_failed": "Unable to verify purchases for the current App Store account: %s",
        "settings.legal": "Privacy & Disclaimer",
        "settings.legal_desc": "Read the current privacy policy, terms of use, risk notice, and disclaimer",
        "settings.legal_open": "Read",
        "settings.ios_statement": "Apple App Store Notice",
        "settings.ios_statement_desc": "Review the GPLv3 App Store distribution permission, source obligations, and scope",
        "settings.ios_statement_open": "Read Notice",
        "ios_statement.title": "Apple App Store Additional Permission & Notice",
        "ios_statement.first_summary": "Apple-platform first-use confirmation (document 2 of 2). You must accept both this notice and the Privacy Policy, Terms & Disclaimer before using visual novel or video features.",
        "legal.title": "Privacy Policy, Terms & Disclaimer",
        "legal.first_summary": "Please read and choose whether to agree before first use. You can review this document later under Settings > About.",
        "legal.first_summary_ios": "Apple-platform first-use confirmation (document 1 of 2). After accepting this document, you must also accept the Apple App Store notice.",
        "legal.accept": "Agree and Continue",
        "legal.decline": "Decline",
        "legal.close": "Close",
        "legal.declined_title": "Agreement Not Accepted",
        "legal.declined_body": "You have not accepted every required document, so Aether will not enable visual novel or video features. iOS does not allow an app to terminate itself; close it from the system app switcher, or return to review and accept each document.",
        "legal.review_again": "Review Again",
        "detail.eyebrow": "Library Detail",
        "detail.runtime_profile": "Runtime profile / %s",
        "detail.last_played": "Last played: %s",
        "detail.played": "Played %s",
        "detail.launch": "Launch Visual Novel",
        "detail.launch_entry": "Launch entry: %s",
        "detail.default_launch_entry": "Game folder (auto-detect)",
        "detail.set_launch_file": "Change Launch File",
        "detail.reset_launch_file": "Restore Folder Auto-detect",
        "detail.set_cover": "Set Cover",
        "detail.delete_cover": "Delete Cover",
        "detail.clear_cover": "Clear Cover",
        "detail.rename": "Rename",
        "detail.remove": "Remove Visual Novel",
        "detail.delete_builtin": "Delete Built-in Demo",
        "game.today": "Today",
        "game.days_ago": "%d days ago",
        "game.played_duration": "Played %s",
        "game.never_played": "Not played yet",
        "game.builtin_demo": "Built-in Demo",
        "game.local": "Local Game",
        "game.type_directory": "Directory",
        "game.type_archive": "Archive",
        "dialog.import_title": "Import Visual Novel",
        "dialog.import_guide_body": "Use the Files app to copy your visual novel folder into this app's directory:\n\n1. Open the Files app on your iPhone / iPad\n2. Go to: On My iPhone / iPad > Aether > Games\n3. Copy the visual novel folder into Games\n4. Return to this app and tap Refresh to detect new visual novels\n\nVisual novel directory: Games/",
        "dialog.ok": "Got it",
        "dialog.scrape_title": "Finish Game Info",
        "dialog.scrape_body": "Added \"%s\". Set the cover art and display name now?",
        "dialog.later": "Later",
        "dialog.open_detail": "Set Up Now",
        "dialog.choose_cover": "Choose Cover Image",
        "dialog.choose_launch_file": "Choose Launch File",
        "dialog.rename": "Rename",
        "dialog.remove_body": "Remove \"%s\" from the list? This will not delete visual novel files from disk.",
        "dialog.delete_builtin_body": "Delete the built-in demo \"%s\" and its local saves? It will not be restored automatically.",
        "dialog.remove": "Remove",
        "dialog.delete": "Delete",
        "dialog.select_game_dir": "Choose Game Folder",
        "dialog.select_local_game_dir": "Choose Local Game Folder",
        "dialog.cancel": "Cancel",
        "dialog.dev_mount": "Dev Mount  %s",
        "message.web_manifest_failed": "Could not read the Web game mount manifest",
        "message.web_mount_failed": "Web local mount failed: %s",
        "message.unknown_error": "Unknown error",
        "message.browser_picker_unsupported": "This browser does not support local file picking",
        "message.browser_no_ticket": "The browser did not return an import task",
        "message.web_import_failed": "Local game import failed: %s",
        "message.web_game_invalid": "The browser returned invalid game information",
        "message.web_import_timeout": "Local game import timed out",
        "message.web_picker_unsupported_long": "This browser cannot directly choose local game files. Use a browser that supports File System Access or directory upload.",
        "message.android_storage_permission_required": "Allow Aether to access the file system before importing or launching external games. Grant file access in the system prompt or permission settings, then try again.",
        "message.android_video_storage_permission_required": "Allow Aether to access the file system before importing videos. Grant file access in the system prompt or permission settings, then try again.",
        "message.path_missing": "Game path does not exist",
        "message.launch_file_unsupported": "The launch file must be an EXE or XP3 file",
        "message.launch_file_outside_game": "The launch file must be inside this game folder",
        "message.launch_file_missing": "Launch file does not exist: %s",
        "message.cover_file_missing": "Could not read the selected cover image: %s",
        "message.game_exists": "Game already exists: %s",
        "message.builtin_delete_failed": "Could not completely delete the built-in demo: %s",
        "alert.error_title": "Aether Error",
        "alert.warning_title": "Aether Warning",
        "alert.runtime_class_missing": "Runtime extension failed to load: AetherRuntimePlayer is unavailable",
        "alert.runtime_create_failed": "Runtime extension failed to load: could not create AetherRuntimePlayer",
        "loading.title": "Launching visual novel...",
        "loading.translation_model": "Loading local translation model...",
        "loading.translation_model_detail": "The first load may pause briefly. Please wait."
    },
    LANG_JA: {
        "home.subtitle": "多機能メディアプレーヤー",
        "video.status": "ビデオライブラリ",
        "video.empty_title": "Video フォルダーに動画がありません",
        "video.empty_help_ios": "「ファイル」App で動画を次へコピー：\nこのiPhone / iPad内 > Aether > Video\n同名の SRT、VTT、ASS 字幕に対応",
        "video.empty_help_desktop": "ローカル動画を読み込むと、同じフォルダーの同名字幕も自動で読み込みます",
        "video.import": "動画を読み込む",
        "video.refresh": "更新",
        "video.guide": "使い方",
        "video.guide_title": "動画を読み込む",
        "video.guide_body_ios": "「ファイル」App で動画をこのアプリのディレクトリにコピーしてください：\n\n1. iPhone / iPad で「ファイル」App を開く\n2. 移動先：この iPhone / iPad 内 > Aether > Video\n3. 動画と同名の字幕を Video にコピー\n4. アプリに戻り、「更新」をタップして新しい動画を検出\n\n動画ディレクトリ：Video/\n字幕：SRT、VTT、ASS、SSA",
        "video.guide_body_desktop": "ローカル動画をビデオライブラリに読み込みます：\n\n1. 「動画を読み込む」をクリック\n2. 再生する動画ファイルを選択\n3. 字幕を使う場合は、同名の字幕を動画と同じ場所に配置\n4. ライブラリに戻って再生すると、進捗は自動保存されます\n\n字幕：SRT、VTT、ASS、SSA",
        "video.remove": "動画を削除",
        "video.remove_body": "「%s」をビデオライブラリから削除しますか？ディスク上の動画ファイルは削除されず、再生進捗は消去されます。",
        "video.back": "戻る",
        "video.pause": "一時停止",
        "video.play": "再生",
        "video.subtitle_off": "字幕オフ",
        "video.subtitle_embedded": "%s（埋め込み）",
        "video.resume": "続きから再生",
        "video.progress": "再生位置 %s / %s",
        "video.open_failed": "動画を再生できません：%s",
        "home.status": "ビジュアルノベルライブラリ",
        "nav.library": "ビジュアルノベル",
        "nav.videos": "ビデオ",
        "nav.collapse_sidebar": "サイドバーを折りたたむ",
        "nav.expand_sidebar": "サイドバーを展開",
        "home.empty_title": "ゲームはまだ追加されていません",
        "home.game_count": "%d 本のゲーム",
        "video.video_count": "%d 本のビデオ",
        "search.games_placeholder": "ビジュアルノベルを検索",
        "search.videos_placeholder": "ビデオを検索",
        "search.filtered_count": "%d / %d 件を表示",
        "search.no_results_title": "一致する項目がありません",
        "search.no_results_help": "別のキーワードを試すか、検索欄をクリアしてください",
        "home.refresh": "更新",
        "home.import": "インポート",
        "home.import_guide": "インポートガイド",
        "home.empty_help_ios": "「ファイル」App でゲームフォルダーをコピーしてください：\nこの iPhone / iPad 内 > Aether > Games\nその後「更新」をタップします",
        "home.empty_help_web": "「インポート」をタップしてローカルのビジュアルノベルフォルダーを選択",
        "home.empty_help_desktop": "「インポート」をタップしてビジュアルノベルフォルダーを選択",
        "settings.title": "設定",
        "settings.save": "保存",
        "settings.unsaved_title": "設定を保存しますか？",
        "settings.unsaved_body": "設定が変更されています。移動する前に保存しますか？",
        "settings.unsaved_discard": "保存しない",
        "settings.unsaved_close": "閉じる",
        "settings.section.interface": "インターフェイス",
        "settings.section.render": "レンダリング",
        "settings.section.developer": "開発者",
        "settings.section.about": "情報",
        "settings.section.purchases": "アプリ内課金",
        "settings.language": "言語",
        "settings.language_desc": "既定ではシステムに従います。簡体字中国語、繁体字中国語、英語、日本語、韓国語に固定できます",
        "settings.translation_model": "ローカル翻訳モデル",
        "settings.translation_model_desc": "外部 GGUF モデルを選択します。ゲーム開始時に読み込み、UI 言語と異なるテキストを自動翻訳します",
        "settings.translation_model_select": "GGUF モデルを選択",
        "settings.translation_model_selected": "選択中のモデル",
        "settings.translation_model_clear": "ローカル翻訳を無効化",
        "settings.translation_model_clear_desc": "モデルファイルを削除せず、設定済みのパスだけを消去します",
        "settings.style": "スタイル",
        "settings.style_desc": "現在のダークスタイルと旧来のクラシックライトスタイルを切り替えます",
        "settings.ui_scale": "UI スケール",
        "settings.ui_scale_desc": "iPhone と iPad の UI サイズを調整します。保存後すぐに反映されます",
        "settings.virtual_control_menu": "ゲーム内コントロールメニュー",
        "settings.virtual_control_menu_desc": "ゲーム画面の右上に仮想コントロールメニューボタンを表示します",
        "settings.keyboard_control_opacity": "Keyboard モードのキー透明度",
        "settings.keyboard_control_opacity_desc": "画面上の仮想キーの透明度を調整します。マウスポインターは鮮明なままです",
        "ui_scale.compact": "小さめ",
        "ui_scale.comfortable": "快適",
        "ui_scale.standard": "標準",
        "style.dark": "ダーク",
        "style.classic": "クラシックライト",
        "language.system": "システムに従う",
        "language.system_with_value": "システムに従う（%s）",
        "language.zh_hans": "简体中文",
        "language.zh_hant": "繁體中文",
        "language.en": "English",
        "language.ja": "日本語",
        "language.ko": "한국어",
        "settings.render_backend": "レンダリングパイプライン",
        "settings.render_backend_desc": "保存後に反映されます。実行中の切り替えは現在のゲームの再起動が必要です",
        "settings.surface_mode": "キャンバスサイズ",
        "settings.surface_mode_desc": "Game Native はゲーム基準のキャンバス、Display Fit はデバイス表示サイズで実行します",
        "settings.upscale": "スケーリング",
        "settings.upscale_desc": "外側の画面を引き伸ばすときに使用します。Bicubic/Lanczos は高品質な補間を行います",
        "settings.output_resolution": "出力解像度",
        "settings.output_resolution_desc": "外側のスケーリングと画質強化の目標解像度を設定します。高い設定ほど GPU とメモリを多く使用します",
        "settings.output_resolution.original": "オリジナル",
        "settings.frame_enhancement": "画質強化",
        "settings.frame_enhancement_desc": "GPU で線を修復し、選択した出力解像度へ高品質に拡大します。保存後すぐに反映されます",
        "settings.frame_enhancement_unavailable_desc": "このビルドまたはグラフィックスデバイスでは画質強化を利用できません。元の映像を安全に表示します",
        "settings.frame_enhancement_mode": "強化効果",
        "settings.frame_enhancement_mode_desc": "画面に合う仕上がりを選択します。推奨効果は線と細部の修復を優先します",
        "settings.frame_enhancement_kind.off": "オフ",
        "settings.frame_enhancement_kind.preset": "プリセット",
        "settings.frame_enhancement_kind.custom": "カスタム",
        "settings.frame_enhancement_custom_desc": "アルゴリズムは上から順に実行され、2× 再構成・出力サイズ調整・同サイズ修復の意味を維持します",
        "settings.frame_enhancement_custom_empty": "アルゴリズムがありません。追加して処理チェーンを作成できます。",
        "settings.frame_enhancement_custom_add": "＋ アルゴリズムを追加",
        "settings.frame_enhancement_custom_step": "ステップ %d",
        "settings.frame_enhancement_custom_remove": "このアルゴリズムを削除",
        "settings.frame_enhancement_algorithm.anime4k_upscale_s.desc": "アニメの線と輪郭を軽量に 2× 再構成",
        "settings.frame_enhancement_algorithm.anime4k_upscale_l.desc": "アニメの細部を高品質に 2× 再構成",
        "settings.frame_enhancement_algorithm.anime4k_upscale_vl.desc": "最高品質の 2× 再構成。負荷は非常に高め",
        "settings.frame_enhancement_algorithm.anime4k_restore_s.desc": "ぼやけた線・ノイズ・圧縮跡を同サイズで修復",
        "settings.frame_enhancement_algorithm.anime4k_restore_soft_s.desc": "小さい文字と細線を守る軽量で穏やかな修復",
        "settings.frame_enhancement_algorithm.anime4k_restore_soft_m.desc": "質感と小さい文字を両立する、より深い穏やかな修復",
        "settings.frame_enhancement_algorithm.anime4k_restore_l.desc": "線と細部を高品質に同サイズ修復",
        "settings.frame_enhancement_algorithm.anime4k_restore_vl.desc": "最高品質の同サイズ修復。負荷は非常に高め",
        "settings.frame_enhancement_algorithm.fsr1_easu.desc": "輪郭を考慮して選択した出力解像度へ拡大縮小",
        "settings.frame_enhancement_algorithm.fsr1_rcas.desc": "再構成・拡大後を同サイズで適応的に鮮鋭化",
        "settings.frame_enhancement_algorithm.bicubic.desc": "自然で柔らかく選択した出力解像度へ拡大縮小",
        "settings.frame_enhancement_algorithm.lanczos.desc": "くっきり選択した出力解像度へ拡大縮小",
        "settings.frame_enhancement_algorithm.fxaa.desc": "目立つジャギーを同サイズで滑らかに補正",
        "settings.frame_enhancement_algorithm.ravu_lite_r2.desc": "方向を考慮した軽量 2× 再構成",
        "settings.frame_enhancement_algorithm.cunny_2x4c.desc": "軽量ニューラル 2× テクスチャ再構成",
        "settings.frame_enhancement_algorithm.nnedi3_nns16.desc": "線と斜め輪郭を重視した 2× 補間再構成",
        "settings.frame_enhancement_mode.anime4k": "スマート修復（推奨）",
        "settings.frame_enhancement_mode.fsr1": "バランス鮮明",
        "settings.frame_enhancement_mode.bicubic": "自然でソフト",
        "settings.frame_enhancement_mode.lanczos": "くっきり細部",
        "settings.frame_enhancement_mode.ravu": "高精細拡大",
        "settings.frame_enhancement_mode.cunny": "質感強化",
        "settings.frame_enhancement_mode.nnedi3": "線をなめらかに",
        "settings.frame_enhancement_mode.chain_4k_max": "4K 極致（極高負荷）",
        "settings.frame_enhancement_mode.chain_lossless": "ロスレス画質（極高負荷）",
        "settings.frame_enhancement_mode.chain_ultra": "超鮮明バランス（高負荷）",
        "settings.frame_enhancement_mode.chain_detail": "高精細（中高負荷）",
        "settings.frame_enhancement_mode.chain_balanced": "バランス強化（中負荷）",
        "settings.frame_enhancement_mode.chain_soft": "ソフト鮮明（推奨・中低負荷）",
        "settings.frame_enhancement_mode.chain_light": "軽量強化（低負荷）",
        "settings.frame_enhancement_mode.chain_basic": "基本強化（最低負荷）",
        "settings.perf": "パフォーマンス監視",
        "settings.perf_desc": "FPS、プロセスメモリ、GPU メモリ推定値、グラフィックス API 情報を表示します",
        "settings.fps_limit": "FPS 制限",
        "settings.fps_limit_desc": "有効時は下の目標 FPS を使用します。無効時はディスプレイのリフレッシュレートに従います",
        "settings.landscape": "横向き固定",
        "settings.landscape_desc": "ゲーム実行中に横向き表示を強制します（スマートフォン推奨）",
        "settings.target_fps": "目標 FPS",
        "settings.target_fps_desc": "C++ エンジンの tick/render 頻度を 60～144 FPS から選択します",
        "settings.plugin_load_mode": "プラグイン読み込みモード",
        "settings.plugin_load_mode_desc": "コアモードは一般的な互換プラグインのみ、完全モードは従来の全登録経路を使用します",
        "settings.plugin_trace": "プラグイン呼び出し追跡",
        "settings.plugin_trace_desc": "すべてのネイティブプラグイン呼び出しを plugin_trace.log に記録します",
        "settings.mock": "Mock バイパス",
        "settings.mock_desc": "不足プラグインに mock オブジェクトを返してエラーを抑制します。無効にすると実エラーを確認できます。",
        "settings.console_log": "コンソールログファイル",
        "settings.console_log_desc": "エンジンのコンソール出力をローカルログファイルにも書き込みます",
        "settings.trace_log": "トレースログ",
        "settings.trace_log_desc": "spdlog の trace レベル詳細ログを有効にします",
        "settings.export_tjs": "TJS スクリプトを書き出す",
        "settings.export_tjs_desc": "ゲーム読み込み時に XP3 から逆アセンブル済み TJS バイトコードを自動で書き出します",
        "settings.error_dialog_logs": "エラーにログを添付",
        "settings.error_dialog_logs_desc": "実エラーダイアログに直近 20 行のエンジンログを追加します。既定はオフ",
        "settings.version": "バージョン",
        "settings.author": "作者",
        "settings.email": "メール",
        "settings.app_service_filing": "アプリサービス届出番号",
        "settings.app_service_filing_open_failed": "届出情報ページを開けませんでした。後でもう一度お試しください。",
        "iap.list_limit.title": "ライブラリ制限解除",
        "iap.list_limit.desc": "ビジュアルノベルと動画ライブラリのすべての項目を永久に解除します",
        "iap.coffee.title": "作者にコーヒーを一杯贈る",
        "iap.coffee.desc": "ベータ機能を30日間利用できます",
        "iap.coffee.active_until": "ベータ機能の有効期限：%s",
        "iap.coffee.inactive": "ベータ機能は現在有効ではありません",
        "iap.coffee.purchase_success": "ご支援ありがとうございます！ベータ機能の有効期限：%s",
        "support.coffee.title": "作者にコーヒーを一杯贈る",
        "support.coffee.desc": "Alipay を開いて作者を支援します。ゲームの読み込みや起動には影響しません",
        "support.coffee.open": "Alipay を開く",
        "support.coffee.thanks_title": "ご支援ありがとうございます",
        "support.coffee.thanks": "ご支援ありがとうございます！",
        "support.coffee.open_failed": "ブラウザを開けませんでした。後でもう一度お試しください。",
        "secret.unlock.title": "シークレット解錠",
        "secret.unlock.body": "解錠パスフレーズを入力してください",
        "secret.unlock.placeholder": "パスフレーズ",
        "secret.unlock.confirm": "確認",
        "secret.unlock.failed": "パスフレーズが正しくありません。もう一度お試しください。",
        "secret.unlock.success": "課金が解錠されました。ベータ機能の有効期限：%s",
        "iap.beta_runtime_unavailable": "このビジュアルノベルの互換対応はテスト中です。今後の対応をお待ちください。",
        "iap.status.purchased": "購入済み",
        "iap.status.not_purchased": "未購入",
        "iap.status.loading": "商品情報を読み込み中…",
        "iap.status.unavailable": "現在 App Store に接続できません",
        "iap.buy": "購入",
        "iap.restore": "購入を復元",
        "iap.restore_desc": "現在の App Store アカウントで購入済みの非消耗型アイテムを復元します",
        "iap.restore_action": "復元",
        "iap.checking_title": "購入状況を確認中",
        "iap.checking_body": "現在の App Store アカウントがライブラリ制限解除を購入済みか確認しています…",
        "iap.limit_title": "利用上限",
        "iap.limit_body": "未購入の場合、各リストの最初の項目のみ実行できます。下の購入ボタンから制限を解除してください。",
        "iap.purchase_success": "ライブラリ制限を解除しました。",
        "iap.purchase_pending": "購入は承認待ちです。承認後、設定から購入を復元できます。",
        "iap.purchase_cancelled": "購入をキャンセルしました。",
        "iap.purchase_failed": "購入に失敗しました：%s",
        "iap.restore_success": "購入を復元し、ライブラリ制限を解除しました。",
        "iap.restore_none": "現在の App Store アカウントには復元できるライブラリ制限解除がありません。",
        "iap.verify_failed": "現在の App Store アカウントの購入状況を確認できません：%s",
        "settings.legal": "プライバシーと免責事項",
        "settings.legal_desc": "現在のプライバシーポリシー、利用条件、リスクおよび免責事項を確認します",
        "settings.legal_open": "読む",
        "settings.ios_statement": "Apple App Store 追加声明",
        "settings.ios_statement_desc": "GPLv3、App Store 配布の追加許諾、ソース提供義務および適用範囲を確認します",
        "settings.ios_statement_open": "声明を読む",
        "ios_statement.title": "Apple App Store 追加許諾および声明",
        "ios_statement.first_summary": "Apple プラットフォーム初回確認（2/2）。ビジュアルノベルおよび動画機能を使用するには、本声明とプライバシー・利用条件・免責事項の両方への同意が必要です。",
        "legal.title": "プライバシーポリシー・利用条件・免責事項",
        "legal.first_summary": "初回利用前に内容を読み、同意するか選択してください。設定 > 情報からいつでも確認できます。",
        "legal.first_summary_ios": "Apple プラットフォーム初回確認（1/2）。本書への同意後、Apple App Store 追加声明への同意も必要です。",
        "legal.accept": "同意して続ける",
        "legal.decline": "拒否",
        "legal.close": "閉じる",
        "legal.declined_title": "同意されていません",
        "legal.declined_body": "必要な文書のすべてに同意されていないため、ビジュアルノベルと動画機能は利用できません。iOS では App 自身を終了できません。App スイッチャーから閉じるか、各文書を読み直して同意してください。",
        "legal.review_again": "もう一度読む",
        "detail.eyebrow": "ゲーム詳細",
        "detail.runtime_profile": "ランタイムプロファイル / %s",
        "detail.last_played": "前回プレイ：%s",
        "detail.played": "プレイ時間 %s",
        "detail.launch": "ビジュアルノベルを起動",
        "detail.launch_entry": "起動エントリ：%s",
        "detail.default_launch_entry": "ゲームフォルダー（自動検出）",
        "detail.set_launch_file": "起動ファイルを変更",
        "detail.reset_launch_file": "フォルダーの自動検出に戻す",
        "detail.set_cover": "カバーを設定",
        "detail.rename": "名前を変更",
        "detail.remove": "ビジュアルノベルを削除",
        "detail.delete_builtin": "内蔵デモを削除",
        "game.today": "今日",
        "game.days_ago": "%d 日前",
        "game.played_duration": "プレイ時間 %s",
        "game.never_played": "未プレイ",
        "game.builtin_demo": "内蔵デモ",
        "game.local": "ローカルゲーム",
        "game.type_directory": "フォルダー",
        "game.type_archive": "アーカイブ",
        "dialog.import_title": "ビジュアルノベルをインポート",
        "dialog.import_guide_body": "「ファイル」App でビジュアルノベルのフォルダーをこのアプリのディレクトリにコピーしてください：\n\n1. iPhone / iPad で「ファイル」App を開く\n2. 移動先：この iPhone / iPad 内 > Aether > Games\n3. ビジュアルノベルのフォルダーを Games にコピー\n4. アプリに戻り、「更新」をタップして新しいビジュアルノベルを検出\n\nビジュアルノベルのディレクトリ：Games/",
        "dialog.ok": "了解",
        "dialog.scrape_title": "ゲーム情報を設定",
        "dialog.scrape_body": "「%s」を追加しました。今すぐカバーと表示名を設定しますか？",
        "dialog.later": "あとで",
        "dialog.open_detail": "今すぐ設定",
        "dialog.choose_cover": "カバー画像を選択",
        "dialog.choose_launch_file": "起動ファイルを選択",
        "dialog.rename": "名前を変更",
        "dialog.remove_body": "「%s」をリストから削除しますか？ディスク上のビジュアルノベルファイルは削除されません。",
        "dialog.delete_builtin_body": "内蔵デモ「%s」とローカルセーブデータを削除しますか？削除後は自動的に復元されません。",
        "dialog.remove": "削除",
        "dialog.delete": "削除",
        "dialog.select_game_dir": "ゲームフォルダーを選択",
        "dialog.select_local_game_dir": "ローカルゲームフォルダーを選択",
        "dialog.cancel": "キャンセル",
        "dialog.dev_mount": "開発マウント  %s",
        "message.web_manifest_failed": "Web ゲームのマウントマニフェストを読み取れません",
        "message.web_mount_failed": "Web ローカルマウントに失敗しました：%s",
        "message.unknown_error": "不明なエラー",
        "message.browser_picker_unsupported": "このブラウザーはローカルファイル選択をサポートしていません",
        "message.browser_no_ticket": "ブラウザーからインポートタスクが返されませんでした",
        "message.web_import_failed": "ローカルゲームのインポートに失敗しました：%s",
        "message.web_game_invalid": "ブラウザーから無効なゲーム情報が返されました",
        "message.web_import_timeout": "ローカルゲームのインポートがタイムアウトしました",
        "message.web_picker_unsupported_long": "このブラウザーはローカルゲームファイルの直接選択に対応していません。File System Access またはディレクトリアップロード対応ブラウザーを使用してください。",
        "message.android_storage_permission_required": "外部ゲームのインポートまたは起動には、Aether にファイルシステムへのアクセスを許可する必要があります。システムの権限ダイアログまたは設定でファイルアクセスを許可してから、もう一度お試しください。",
        "message.android_video_storage_permission_required": "動画をインポートするには、Aether にファイルシステムへのアクセスを許可する必要があります。システムの権限ダイアログまたは設定でファイルアクセスを許可してから、もう一度お試しください。",
        "message.path_missing": "ゲームパスが存在しません",
        "message.launch_file_unsupported": "起動ファイルは EXE または XP3 のみ対応しています",
        "message.launch_file_outside_game": "起動ファイルは現在のゲームフォルダー内にある必要があります",
        "message.launch_file_missing": "起動ファイルが存在しません：%s",
        "message.cover_file_missing": "選択したカバー画像を読み込めません：%s",
        "message.game_exists": "ゲームは既に存在します：%s",
        "message.builtin_delete_failed": "内蔵デモを完全に削除できませんでした：%s",
        "alert.error_title": "Aether エラー",
        "alert.warning_title": "Aether 警告",
        "alert.runtime_class_missing": "ランタイム拡張の読み込みに失敗しました：AetherRuntimePlayer は利用できません",
        "alert.runtime_create_failed": "ランタイム拡張の読み込みに失敗しました：AetherRuntimePlayer を作成できません",
        "loading.title": "ビジュアルノベルを起動中...",
        "loading.translation_model": "ローカル翻訳モデルを読み込み中...",
        "loading.translation_model_detail": "初回の読み込みは一時的に停止することがあります。しばらくお待ちください。"
    },
    LANG_KO: {
        "home.subtitle": "다기능 미디어 플레이어",
        "video.status": "비디오 라이브러리",
        "video.empty_title": "Video 폴더에 비디오가 없습니다",
        "video.empty_help_ios": "파일 앱에서 비디오를 다음 위치로 복사하세요:\n나의 iPhone / iPad > Aether > Video\n같은 이름의 SRT, VTT, ASS 자막 지원",
        "video.empty_help_desktop": "로컬 비디오를 가져오면 같은 폴더의 동일한 이름 자막을 자동으로 불러옵니다",
        "video.import": "비디오 가져오기",
        "video.refresh": "새로 고침",
        "video.guide": "사용 방법",
        "video.guide_title": "비디오 가져오기",
        "video.guide_body_ios": "파일 앱으로 비디오를 이 앱의 디렉터리에 복사하세요:\n\n1. iPhone / iPad에서 파일 앱을 엽니다\n2. 이동: 나의 iPhone / iPad > Aether > Video\n3. 비디오와 같은 이름의 자막을 Video에 복사합니다\n4. 앱으로 돌아와 새로 고침을 눌러 새 비디오를 감지합니다\n\n비디오 디렉터리: Video/\n자막: SRT, VTT, ASS, SSA",
        "video.guide_body_desktop": "로컬 비디오를 비디오 라이브러리로 가져옵니다:\n\n1. 비디오 가져오기를 클릭합니다\n2. 재생할 비디오 파일을 선택합니다\n3. 자막이 필요하면 같은 이름의 자막을 비디오 옆에 둡니다\n4. 라이브러리에서 재생하면 진행 위치가 자동 저장됩니다\n\n자막: SRT, VTT, ASS, SSA",
        "video.remove": "비디오 제거",
        "video.remove_body": "\"%s\"을(를) 비디오 라이브러리에서 제거할까요? 디스크의 비디오 파일은 삭제되지 않으며 재생 진행 위치는 지워집니다.",
        "video.back": "뒤로",
        "video.pause": "일시정지",
        "video.play": "재생",
        "video.subtitle_off": "자막 끄기",
        "video.subtitle_embedded": "%s(내장)",
        "video.resume": "이어서 재생",
        "video.progress": "재생 위치 %s / %s",
        "video.open_failed": "비디오를 재생할 수 없습니다: %s",
        "home.status": "비주얼 노벨 라이브러리",
        "nav.library": "비주얼 노벨",
        "nav.videos": "비디오",
        "nav.collapse_sidebar": "사이드바 접기",
        "nav.expand_sidebar": "사이드바 펼치기",
        "home.empty_title": "아직 추가된 게임이 없습니다",
        "home.game_count": "게임 %d개",
        "video.video_count": "비디오 %d개",
        "search.games_placeholder": "비주얼 노벨 검색",
        "search.videos_placeholder": "비디오 검색",
        "search.filtered_count": "%d / %d 표시",
        "search.no_results_title": "일치하는 항목이 없습니다",
        "search.no_results_help": "다른 검색어를 입력하거나 검색창을 비워 보세요",
        "home.refresh": "새로고침",
        "home.import": "가져오기",
        "home.import_guide": "가져오기 가이드",
        "home.empty_help_ios": "파일 앱으로 게임 폴더를 다음 위치에 복사하세요:\n나의 iPhone / iPad > Aether > Games\n그런 다음 새로고침을 누르세요",
        "home.empty_help_web": "가져오기를 눌러 로컬 비주얼 노벨 폴더를 선택하세요",
        "home.empty_help_desktop": "가져오기를 눌러 비주얼 노벨 폴더를 선택하세요",
        "settings.title": "설정",
        "settings.save": "저장",
        "settings.unsaved_title": "설정을 저장할까요?",
        "settings.unsaved_body": "설정이 변경되었습니다. 이동하기 전에 저장할까요?",
        "settings.unsaved_discard": "저장 안 함",
        "settings.unsaved_close": "닫기",
        "settings.section.interface": "인터페이스",
        "settings.section.render": "렌더링",
        "settings.section.developer": "개발자",
        "settings.section.about": "정보",
        "settings.section.purchases": "앱 내 구입",
        "settings.language": "언어",
        "settings.language_desc": "기본값은 시스템 언어입니다. 중국어 간체, 중국어 번체, 영어, 일본어, 한국어로 고정할 수 있습니다",
        "settings.translation_model": "로컬 번역 모델",
        "settings.translation_model_desc": "외부 GGUF 모델을 선택합니다. 게임 시작 시 불러오며 UI 언어와 다른 텍스트를 자동 번역합니다",
        "settings.translation_model_select": "GGUF 모델 선택",
        "settings.translation_model_selected": "선택한 모델",
        "settings.translation_model_clear": "로컬 번역 사용 안 함",
        "settings.translation_model_clear_desc": "디스크의 모델 파일은 삭제하지 않고 설정된 경로만 지웁니다",
        "settings.style": "스타일",
        "settings.style_desc": "현재 다크 스타일과 기존 클래식 라이트 스타일을 전환합니다",
        "settings.ui_scale": "인터페이스 크기",
        "settings.ui_scale_desc": "iPhone 및 iPad의 인터페이스 크기를 조절하며 저장 후 즉시 적용됩니다",
        "settings.virtual_control_menu": "게임 내 컨트롤 메뉴",
        "settings.virtual_control_menu_desc": "게임 화면 오른쪽 위에 가상 컨트롤 메뉴 버튼을 표시합니다",
        "settings.keyboard_control_opacity": "Keyboard 모드 버튼 투명도",
        "settings.keyboard_control_opacity_desc": "화면 가상 키의 투명도를 조절하며 마우스 포인터는 선명하게 유지합니다",
        "ui_scale.compact": "작게",
        "ui_scale.comfortable": "적당히",
        "ui_scale.standard": "표준",
        "style.dark": "다크",
        "style.classic": "클래식 라이트",
        "language.system": "시스템 따르기",
        "language.system_with_value": "시스템 따르기(%s)",
        "language.zh_hans": "简体中文",
        "language.zh_hant": "繁體中文",
        "language.en": "English",
        "language.ja": "日本語",
        "language.ko": "한국어",
        "settings.render_backend": "렌더링 파이프라인",
        "settings.render_backend_desc": "저장 후 적용됩니다. 실행 중 변경하려면 현재 게임을 다시 시작해야 합니다",
        "settings.surface_mode": "캔버스 크기",
        "settings.surface_mode_desc": "Game Native는 게임 기준 캔버스를 사용하고 Display Fit은 장치 표시 크기를 사용합니다",
        "settings.upscale": "스케일링",
        "settings.upscale_desc": "외부 화면을 늘릴 때 사용합니다. Bicubic/Lanczos는 고품질 보간을 적용합니다",
        "settings.output_resolution": "출력 해상도",
        "settings.output_resolution_desc": "외부 스케일링과 화질 향상의 목표 해상도를 설정합니다. 높은 단계일수록 GPU와 메모리를 더 사용합니다",
        "settings.output_resolution.original": "원본",
        "settings.frame_enhancement": "화질 향상",
        "settings.frame_enhancement_desc": "GPU로 선을 복원하고 선택한 출력 해상도로 고품질 확대합니다. 저장 후 즉시 적용됩니다",
        "settings.frame_enhancement_unavailable_desc": "현재 빌드 또는 그래픽 장치에서는 화질 향상을 사용할 수 없습니다. 원본 화면은 안전하게 유지됩니다",
        "settings.frame_enhancement_mode": "향상 효과",
        "settings.frame_enhancement_mode_desc": "화면에 맞는 처리 스타일을 선택합니다. 권장 효과는 선과 세부 복원을 우선합니다",
        "settings.frame_enhancement_kind.off": "끄기",
        "settings.frame_enhancement_kind.preset": "프리셋",
        "settings.frame_enhancement_kind.custom": "사용자 정의",
        "settings.frame_enhancement_custom_desc": "알고리즘은 위에서 아래 순서로 실행되며 2× 재구성, 출력 크기 조정, 동일 크기 복원의 의미를 유지합니다",
        "settings.frame_enhancement_custom_empty": "알고리즘이 없습니다. 추가하여 처리 체인을 만들 수 있습니다.",
        "settings.frame_enhancement_custom_add": "＋ 알고리즘 추가",
        "settings.frame_enhancement_custom_step": "단계 %d",
        "settings.frame_enhancement_custom_remove": "이 알고리즘 제거",
        "settings.frame_enhancement_algorithm.anime4k_upscale_s.desc": "애니메이션 선과 가장자리를 가볍게 2× 재구성",
        "settings.frame_enhancement_algorithm.anime4k_upscale_l.desc": "애니메이션 디테일을 고품질 2× 재구성",
        "settings.frame_enhancement_algorithm.anime4k_upscale_vl.desc": "최고 품질 2× 재구성, 매우 높은 부하",
        "settings.frame_enhancement_algorithm.anime4k_restore_s.desc": "흐린 선, 노이즈, 압축 흔적을 같은 크기로 복원",
        "settings.frame_enhancement_algorithm.anime4k_restore_soft_s.desc": "작은 글자와 가는 선을 보호하는 가벼운 부드러운 복원",
        "settings.frame_enhancement_algorithm.anime4k_restore_soft_m.desc": "질감과 작은 글자를 균형 있게 살리는 부드러운 복원",
        "settings.frame_enhancement_algorithm.anime4k_restore_l.desc": "선과 디테일을 고품질로 같은 크기 복원",
        "settings.frame_enhancement_algorithm.anime4k_restore_vl.desc": "최고 품질 같은 크기 복원, 매우 높은 부하",
        "settings.frame_enhancement_algorithm.fsr1_easu.desc": "가장자리를 고려해 선택한 출력 해상도로 크기 조정",
        "settings.frame_enhancement_algorithm.fsr1_rcas.desc": "재구성 또는 확대 후 같은 크기로 적응형 선명화",
        "settings.frame_enhancement_algorithm.bicubic.desc": "자연스럽고 부드럽게 선택한 출력 해상도로 크기 조정",
        "settings.frame_enhancement_algorithm.lanczos.desc": "선명하게 선택한 출력 해상도로 크기 조정",
        "settings.frame_enhancement_algorithm.fxaa.desc": "눈에 띄는 계단 현상을 같은 크기로 부드럽게 처리",
        "settings.frame_enhancement_algorithm.ravu_lite_r2.desc": "방향성을 고려한 가벼운 2× 재구성",
        "settings.frame_enhancement_algorithm.cunny_2x4c.desc": "가벼운 신경망 2× 텍스처 재구성",
        "settings.frame_enhancement_algorithm.nnedi3_nns16.desc": "선과 대각선 가장자리에 초점을 둔 2× 보간 재구성",
        "settings.frame_enhancement_mode.anime4k": "스마트 복원(권장)",
        "settings.frame_enhancement_mode.fsr1": "균형 잡힌 선명도",
        "settings.frame_enhancement_mode.bicubic": "자연스러운 부드러움",
        "settings.frame_enhancement_mode.lanczos": "또렷한 디테일",
        "settings.frame_enhancement_mode.ravu": "정밀 확대",
        "settings.frame_enhancement_mode.cunny": "텍스처 향상",
        "settings.frame_enhancement_mode.nnedi3": "선 윤곽 보정",
        "settings.frame_enhancement_mode.chain_4k_max": "4K 최고 화질(극고부하)",
        "settings.frame_enhancement_mode.chain_lossless": "무손실 화질(극고부하)",
        "settings.frame_enhancement_mode.chain_ultra": "초고화질 균형(고부하)",
        "settings.frame_enhancement_mode.chain_detail": "고정밀 화질(중고부하)",
        "settings.frame_enhancement_mode.chain_balanced": "균형 향상(중간 부하)",
        "settings.frame_enhancement_mode.chain_soft": "부드러운 선명도(권장, 중저부하)",
        "settings.frame_enhancement_mode.chain_light": "경량 향상(저부하)",
        "settings.frame_enhancement_mode.chain_basic": "기본 향상(최저 부하)",
        "settings.perf": "성능 모니터",
        "settings.perf_desc": "FPS, 프로세스 메모리, GPU 메모리 추정치와 그래픽 API 정보를 표시합니다",
        "settings.fps_limit": "FPS 제한",
        "settings.fps_limit_desc": "켜면 아래 목표 FPS를 사용하고, 끄면 디스플레이 주사율을 따릅니다",
        "settings.landscape": "가로 방향 고정",
        "settings.landscape_desc": "게임 실행 중 가로 표시를 강제합니다(휴대폰 권장)",
        "settings.target_fps": "목표 FPS",
        "settings.target_fps_desc": "C++ 엔진 tick/render 빈도를 60–144 FPS에서 선택합니다",
        "settings.plugin_load_mode": "플러그인 로드 모드",
        "settings.plugin_load_mode_desc": "핵심 모드는 일반 호환 플러그인만 로드하고 전체 모드는 기존 전체 등록 방식을 유지합니다",
        "settings.plugin_trace": "플러그인 호출 추적",
        "settings.plugin_trace_desc": "모든 네이티브 플러그인 호출을 plugin_trace.log에 기록합니다",
        "settings.mock": "Mock 우회",
        "settings.mock_desc": "누락된 플러그인에 mock 객체를 반환해 오류를 억제합니다. 끄면 실제 오류를 확인할 수 있습니다.",
        "settings.console_log": "콘솔 로그 파일",
        "settings.console_log_desc": "엔진 콘솔 출력을 로컬 로그 파일에도 기록합니다",
        "settings.trace_log": "추적 로그",
        "settings.trace_log_desc": "spdlog trace 레벨 상세 로그를 켜서 최대 디버그 정보를 출력합니다",
        "settings.export_tjs": "TJS 스크립트 내보내기",
        "settings.export_tjs_desc": "게임 로드 시 XP3에서 디스어셈블된 TJS 바이트코드 스크립트를 자동으로 내보냅니다",
        "settings.error_dialog_logs": "오류에 로그 첨부",
        "settings.error_dialog_logs_desc": "실제 오류 대화상자에 최근 엔진 로그 20줄을 추가합니다. 기본값은 꺼짐입니다",
        "settings.version": "버전",
        "settings.author": "작성자",
        "settings.email": "이메일",
        "settings.app_service_filing": "앱 서비스 등록 번호",
        "settings.app_service_filing_open_failed": "등록 정보 페이지를 열 수 없습니다. 나중에 다시 시도해 주세요.",
        "iap.list_limit.title": "라이브러리 제한 해제",
        "iap.list_limit.desc": "비주얼 노벨 및 동영상 라이브러리의 모든 항목을 영구적으로 해제합니다",
        "iap.coffee.title": "작가에게 커피 한 잔 사주기",
        "iap.coffee.desc": "베타 기능을 30일 동안 사용할 수 있습니다",
        "iap.coffee.active_until": "베타 기능 만료일: %s",
        "iap.coffee.inactive": "베타 기능이 현재 활성화되어 있지 않습니다",
        "iap.coffee.purchase_success": "후원해 주셔서 감사합니다! 베타 기능 만료일: %s",
        "support.coffee.title": "작가에게 커피 한 잔 사주기",
        "support.coffee.desc": "Alipay를 열어 작가를 후원합니다. 게임 가져오기나 실행에는 영향을 주지 않습니다",
        "support.coffee.open": "Alipay 열기",
        "support.coffee.thanks_title": "후원해 주셔서 감사합니다",
        "support.coffee.thanks": "후원해 주셔서 감사합니다!",
        "support.coffee.open_failed": "브라우저를 열 수 없습니다. 나중에 다시 시도해 주세요.",
        "secret.unlock.title": "시크릿 잠금 해제",
        "secret.unlock.body": "잠금 해제 암호를 입력하세요",
        "secret.unlock.placeholder": "암호",
        "secret.unlock.confirm": "확인",
        "secret.unlock.failed": "암호가 올바르지 않습니다. 다시 시도해 주세요.",
        "secret.unlock.success": "인앱 구매가 잠금 해제되었습니다. 베타 기능 만료일: %s",
        "iap.beta_runtime_unavailable": "이 비주얼 노벨의 호환성은 아직 테스트 중입니다. 추후 지원을 기다려 주세요.",
        "iap.status.purchased": "구입 완료",
        "iap.status.not_purchased": "구입하지 않음",
        "iap.status.loading": "상품 정보 불러오는 중…",
        "iap.status.unavailable": "현재 App Store에 연결할 수 없습니다",
        "iap.buy": "구입",
        "iap.restore": "구입 복원",
        "iap.restore_desc": "현재 App Store 계정의 비소모성 구입 항목을 복원합니다",
        "iap.restore_action": "복원",
        "iap.checking_title": "구입 상태 확인 중",
        "iap.checking_body": "현재 App Store 계정이 라이브러리 제한 해제를 구입했는지 확인하는 중입니다…",
        "iap.limit_title": "사용 한도",
        "iap.limit_body": "구입하지 않은 경우 각 목록의 첫 번째 항목만 실행할 수 있습니다. 아래 구입 버튼으로 제한을 해제하세요.",
        "iap.purchase_success": "라이브러리 제한이 해제되었습니다.",
        "iap.purchase_pending": "구입 승인을 기다리고 있습니다. 승인 후 설정에서 구입을 복원할 수 있습니다.",
        "iap.purchase_cancelled": "구입이 취소되었습니다.",
        "iap.purchase_failed": "구입 실패: %s",
        "iap.restore_success": "구입이 복원되어 라이브러리 제한이 해제되었습니다.",
        "iap.restore_none": "현재 App Store 계정에는 복원할 라이브러리 제한 해제 구입이 없습니다.",
        "iap.verify_failed": "현재 App Store 계정의 구입 상태를 확인할 수 없습니다: %s",
        "settings.legal": "개인정보 및 면책 조항",
        "settings.legal_desc": "현재 개인정보 처리방침, 이용 조건, 위험 고지 및 면책 조항을 확인합니다",
        "settings.legal_open": "읽기",
        "settings.ios_statement": "Apple App Store 추가 고지",
        "settings.ios_statement_desc": "GPLv3, App Store 배포 추가 허가, 소스 제공 의무 및 적용 범위를 확인합니다",
        "settings.ios_statement_open": "고지 읽기",
        "ios_statement.title": "Apple App Store 추가 허가 및 고지",
        "ios_statement.first_summary": "Apple 플랫폼 최초 확인(2/2). 비주얼 노벨 및 비디오 기능을 사용하려면 이 고지와 개인정보·이용 조건·면책 조항에 모두 동의해야 합니다.",
        "legal.title": "개인정보 처리방침·이용 조건·면책 조항",
        "legal.first_summary": "처음 사용하기 전에 내용을 읽고 동의 여부를 선택해 주세요. 설정 > 정보에서 언제든 다시 볼 수 있습니다.",
        "legal.first_summary_ios": "Apple 플랫폼 최초 확인(1/2). 이 문서에 동의한 후 Apple App Store 추가 고지에도 동의해야 합니다.",
        "legal.accept": "동의하고 계속",
        "legal.decline": "거부",
        "legal.close": "닫기",
        "legal.declined_title": "약관에 동의하지 않음",
        "legal.declined_body": "필수 문서에 모두 동의하지 않았으므로 비주얼 노벨 및 비디오 기능을 사용할 수 없습니다. iOS에서는 앱이 스스로 종료될 수 없습니다. 앱 전환 화면에서 닫거나 각 문서를 다시 읽고 동의해 주세요.",
        "legal.review_again": "다시 읽기",
        "detail.eyebrow": "게임 상세",
        "detail.runtime_profile": "런타임 프로필 / %s",
        "detail.last_played": "마지막 플레이: %s",
        "detail.played": "플레이 %s",
        "detail.launch": "비주얼 노벨 실행",
        "detail.launch_entry": "실행 진입점: %s",
        "detail.default_launch_entry": "게임 폴더(자동 감지)",
        "detail.set_launch_file": "실행 파일 변경",
        "detail.reset_launch_file": "폴더 자동 감지 복원",
        "detail.set_cover": "표지 설정",
        "detail.rename": "이름 변경",
        "detail.remove": "비주얼 노벨 제거",
        "detail.delete_builtin": "내장 데모 삭제",
        "game.today": "오늘",
        "game.days_ago": "%d일 전",
        "game.played_duration": "플레이 %s",
        "game.never_played": "아직 플레이하지 않음",
        "game.builtin_demo": "내장 데모",
        "game.local": "로컬 게임",
        "game.type_directory": "폴더",
        "game.type_archive": "아카이브",
        "dialog.import_title": "비주얼 노벨 가져오기",
        "dialog.import_guide_body": "파일 앱으로 비주얼 노벨 폴더를 이 앱의 디렉터리에 복사하세요:\n\n1. iPhone / iPad에서 파일 앱을 엽니다\n2. 이동: 나의 iPhone / iPad > Aether > Games\n3. 비주얼 노벨 폴더를 Games에 복사합니다\n4. 앱으로 돌아와 새로고침을 눌러 새 비주얼 노벨을 감지합니다\n\n비주얼 노벨 디렉터리: Games/",
        "dialog.ok": "확인",
        "dialog.scrape_title": "게임 정보 설정",
        "dialog.scrape_body": "\"%s\"을(를) 추가했습니다. 지금 표지와 표시 이름을 설정할까요?",
        "dialog.later": "나중에",
        "dialog.open_detail": "지금 설정",
        "dialog.choose_cover": "표지 이미지 선택",
        "dialog.choose_launch_file": "실행 파일 선택",
        "dialog.rename": "이름 변경",
        "dialog.remove_body": "\"%s\"을(를) 목록에서 제거할까요? 디스크의 비주얼 노벨 파일은 삭제되지 않습니다.",
        "dialog.delete_builtin_body": "내장 데모 \"%s\"와 로컬 저장 데이터를 삭제할까요? 삭제 후에는 자동으로 복원되지 않습니다.",
        "dialog.remove": "제거",
        "dialog.delete": "삭제",
        "dialog.select_game_dir": "게임 폴더 선택",
        "dialog.select_local_game_dir": "로컬 게임 폴더 선택",
        "dialog.cancel": "취소",
        "dialog.dev_mount": "개발 마운트  %s",
        "message.web_manifest_failed": "Web 게임 마운트 매니페스트를 읽을 수 없습니다",
        "message.web_mount_failed": "Web 로컬 마운트 실패: %s",
        "message.unknown_error": "알 수 없는 오류",
        "message.browser_picker_unsupported": "이 브라우저는 로컬 파일 선택을 지원하지 않습니다",
        "message.browser_no_ticket": "브라우저가 가져오기 작업을 반환하지 않았습니다",
        "message.web_import_failed": "로컬 게임 가져오기 실패: %s",
        "message.web_game_invalid": "브라우저가 잘못된 게임 정보를 반환했습니다",
        "message.web_import_timeout": "로컬 게임 가져오기 시간 초과",
        "message.web_picker_unsupported_long": "이 브라우저는 로컬 게임 파일을 직접 선택할 수 없습니다. File System Access 또는 디렉터리 업로드를 지원하는 브라우저를 사용하세요.",
        "message.android_storage_permission_required": "외부 게임을 가져오거나 실행하려면 Aether의 파일 시스템 접근을 허용해야 합니다. 시스템 권한 창 또는 권한 설정에서 파일 접근 권한을 허용한 뒤 다시 시도하세요.",
        "message.android_video_storage_permission_required": "비디오를 가져오려면 Aether의 파일 시스템 접근을 허용해야 합니다. 시스템 권한 창 또는 권한 설정에서 파일 접근 권한을 허용한 뒤 다시 시도하세요.",
        "message.path_missing": "게임 경로가 존재하지 않습니다",
        "message.launch_file_unsupported": "실행 파일은 EXE 또는 XP3만 지원합니다",
        "message.launch_file_outside_game": "실행 파일은 현재 게임 폴더 안에 있어야 합니다",
        "message.launch_file_missing": "실행 파일이 존재하지 않습니다: %s",
        "message.cover_file_missing": "선택한 표지 이미지를 읽을 수 없습니다: %s",
        "message.game_exists": "게임이 이미 있습니다: %s",
        "message.builtin_delete_failed": "내장 데모를 완전히 삭제하지 못했습니다: %s",
        "alert.error_title": "Aether 오류",
        "alert.warning_title": "Aether 경고",
        "alert.runtime_class_missing": "런타임 확장 로드 실패: AetherRuntimePlayer를 사용할 수 없습니다",
        "alert.runtime_create_failed": "런타임 확장 로드 실패: AetherRuntimePlayer를 만들 수 없습니다",
        "loading.title": "비주얼 노벨 실행 중...",
        "loading.translation_model": "로컬 번역 모델을 불러오는 중...",
        "loading.translation_model_detail": "처음 불러올 때 잠시 멈출 수 있습니다. 기다려 주세요."
    }
}

const ENGINE_RESULT_OK := 0
const MEDIA_STATUS_PLAYING := 1
const MEDIA_STATUS_PAUSED := 2
const MEDIA_STATUS_ENDED := 3
const VIDEO_CONTROLS_AUTO_HIDE_SEC := 3.0
const VIDEO_CONTROLS_FADE_SEC := 0.18
const VIDEO_SEEK_DRAG_THRESHOLD := 14.0
const VIDEO_SEEK_MIN_SPAN_SEC := 30.0
const VIDEO_SEEK_MAX_SPAN_SEC := 180.0
const STARTUP_IDLE := 0
const STARTUP_RUNNING := 1
const STARTUP_SUCCEEDED := 2
const STARTUP_FAILED := 3
const TEXT_TRANSLATION_DISABLED := 0
const TEXT_TRANSLATION_LOADING := 1
const TEXT_TRANSLATION_READY := 2
const TEXT_TRANSLATION_FAILED := 3

const POINTER_DOWN := 1
const POINTER_MOVE := 2
const POINTER_UP := 3
const POINTER_SCROLL := 4
const BUTTON_POSITION_MEMORY_PATH := "user://aetherkiri-button-positions.json"
const POINTER_MOD_LEFT := 0x08
const POINTER_MOD_RIGHT := 0x10
const POINTER_MOD_MIDDLE := 0x20
const POINTER_MOD_CANCEL := 1 << 30
const KEY_MOD_CONTROL := 0x04
const RUNTIME_KIRIKIRI := "kirikiri"
const RUNTIME_ONSCRIPTER := "onscripter"
const RUNTIME_MINORI := "minori"
const RUNTIME_PLAYER_CLASS := "AetherRuntimePlayer"
const ONSCRIPTER_SCRIPT_MARKERS := [
    "0.txt",
    "00.txt",
    "nscr_sec.dat",
    "nscript.___",
    "nscript.dat",
    "onscript.nt2",
    "onscript.nt3",
]
const SHELL_SCROLL_DRAG_THRESHOLD := 4.0
const SHELL_SCROLL_BUTTON_DRAG_THRESHOLD := 28.0
const SHELL_SCROLL_SLIDER_AXIS_THRESHOLD := 10.0
const SHELL_SCROLL_SLIDER_VERTICAL_DOMINANCE := 1.25
const SHELL_SCROLL_AXIS_NONE := ""
const SHELL_SCROLL_AXIS_PENDING := "pending"
const SHELL_SCROLL_AXIS_HORIZONTAL := "horizontal"
const SHELL_SCROLL_AXIS_VERTICAL := "vertical"
const SHELL_SCROLL_DRAG_SPEED := 1.0
const SHELL_SCROLL_TOUCHPAD_SPEED := 12.0
const SHELL_SCROLL_WHEEL_SPEED := 4.0
const SHELL_SCROLL_WHEEL_STEP := 320.0
const SHELL_SCROLL_TWEEN_DURATION := 0.18
const SHELL_SCROLL_MOMENTUM_MIN_SPEED := 110.0
const SHELL_SCROLL_MOMENTUM_MAX_SPEED := 5000.0
const SHELL_SCROLL_MOMENTUM_DISTANCE_FACTOR := 0.28
const SHELL_SCROLL_MOMENTUM_MAX_DURATION := 0.85
const SHELL_SCROLL_MOMENTUM_STALE_MSEC := 140
const SHELL_SCROLL_MOUSE_KEY := -1
const SETTINGS_DRAFT_KEYS := [
    "language",
    "style",
    "ios_ui_scale_mode",
    "game_virtual_menu_enabled",
    "game_virtual_keyboard_opacity",
    "backend",
    "upscale_algorithm",
    "output_resolution",
    "surface_mode",
    "frame_enhancement_enabled",
    "frame_enhancement_kind",
    "frame_enhancement_mode",
    "frame_enhancement_custom_chain",
    "diagnostic_profile",
    "debug_overlay_mode",
    "fps_limit_enabled",
    "target_fps",
    "force_landscape",
    "plugin_load_mode",
    "mock_enabled",
    "error_dialog_logs",
    "text_translation_model_path",
]
const DIAGNOSTIC_PROFILES := ["off", "baseline", "input", "render", "storage", "script", "audio", "video", "plugin", "system", "full"]
const DEBUG_OVERLAY_MODES := ["off", "summary", "detail"]
const ADVANCED_TRACE_TIMEOUT_MS := 30000

var backend: OptionButton
var game_path: LineEdit
var restart_notice: Label
var viewport: TextureRect
var perf: Label
var perf_layer: CanvasLayer
var perf_panel: PanelContainer
var log_view = null
var diagnostic_session = null
var debug_console = null
var shell_root: Control
var shell_safe_top_fill: ColorRect
var shell_sidebar_backdrop: PanelContainer
var shell_content: Control
var shell_sidebar: PanelContainer
var shell_compact_header: PanelContainer
var shell_route_label: Label
var launch_transition_tween: Tween
var shell_library_button: Button
var shell_video_button: Button
var shell_settings_button: Button
var shell_compact_library_button: Button
var shell_compact_video_button: Button
var shell_compact_settings_button: Button
var shell_sidebar_brand: HBoxContainer
var shell_sidebar_brand_labels: VBoxContainer
var shell_sidebar_version: Label
var shell_sidebar_toggle: Button
var shell_sidebar_collapsed := false
var shell_sidebar_animating_expand := false
var shell_sidebar_layout_width := 0.0
var shell_sidebar_tween: Tween
var shell_route := "library"
var home_view: Control
var settings_view: ScrollContainer
var detail_view: Control
var detail_scroll: ScrollContainer
var game_view: Control
var game_virtual_controls
var game_virtual_input_mode := GameVirtualControls.INPUT_MODE_MOUSE
var game_virtual_menu_enabled := true
var game_virtual_keyboard_opacity := 1.0
var modal_layer: Control
var active_modal_scrim: ColorRect
var active_modal_dialog: Control
var loading_panel: PanelContainer
var loading_center: CenterContainer
var loading_card: PanelContainer
var loading_spinner: TextureRect
var loading_hiding := false
var game_scroll: ScrollContainer
var game_list: GridContainer
var video_scroll: ScrollContainer
var video_list: GridContainer
var video_empty_state: Control
var home_game_tab: Button
var home_video_tab: Button
var home_actions: HBoxContainer
var home_page_margin: MarginContainer
var home_header_box: BoxContainer
var home_title_label: Label
var home_search_host: PanelContainer
var home_search_input: LineEdit
var empty_state: Control
var save_button: Button
var bg_rect: ColorRect
var home_subtitle_label: Label
var empty_title_label: Label
var empty_help_label: Label
var video_empty_title_label: Label
var video_empty_help_label: Label
var empty_primary_button: Button
var home_primary_button: Button
var home_guide_button: Button
var home_cards_animated_once := false
var home_compact_layout := false
var home_layout_initialized := false
var home_header_compact := false
var home_header_layout_initialized := false
var loading_title_label: Label
var loading_detail_label: Label
var translation_loading_notice_active := false
var selected_game := {}
var detail_hero_cover: Control
var hero_source_rect := Rect2()
var hero_source_path := ""
var hero_source_texture: Texture2D
var hero_overlay: PanelContainer
var hero_hidden_target: CanvasItem
var hero_transition_id := 0
var known_games: Array[Dictionary] = []
var vndb_cover_queue: Array[Dictionary] = []
var vndb_cover_busy := false
var known_videos: Array[Dictionary] = []
var home_library_mode := "game"
var home_search_queries := {"game": "", "video": ""}
var home_search_syncing := false
var home_filtered_game_count := 0
var home_filtered_video_count := 0
# Keep the runtime viewport unobstructed by default.  Diagnostics continue to
# collect according to the debug profile, while the floating performance
# panel remains an explicit opt-in from Settings (or a legacy config value).
var show_perf_monitor := false
var diagnostic_profile := "baseline" if OS.is_debug_build() else "off"
var debug_overlay_mode := "off"
var lock_landscape := false
var game_runtime_shell_orientation := DisplayServer.SCREEN_SENSOR
var game_runtime_shell_screen_size := Vector2i.ZERO
var game_runtime_shell_orientation_captured := false
var frame_limit_enabled := false
var target_fps := 80
var plugin_trace := false
var plugin_load_mode := "krkrsdl3"
var mock_enabled := true
var console_log_file := false
var trace_log := false
var export_scripts := false
var error_dialog_logs := OS.is_debug_build()
var text_translation_model_path := ""
var advanced_tool_expanded := false
var advanced_expiry_msec := {}
var diagnostic_env_originals := {}
var language_mode := LANG_SYSTEM
var active_language := LANG_ZH_HANS
var style_mode := DEFAULT_STYLE_MODE
var display_title_font: FontVariation
var ios_ui_scale_mode := "comfortable"
var legal_accepted_version := ""
var legal_accepted_at := 0
var ios_statement_accepted_version := ""
var ios_statement_accepted_at := 0
var legal_gate_completed := false
var secret_iap_unlocked := false
var secret_coffee_until_unix := 0
var secret_version_tap_count := 0
var secret_version_last_tap_msec := 0
var iap_state := {}
var iap_coffee_state := {}
var iap_last_revision := -1
var iap_coffee_last_revision := -1
var iap_poll_accum := 0.0
var iap_pending_launch := {}
var iap_pending_check_id := 0
var iap_detail_authorization_key := ""
var iap_detail_authorization_until_msec := 0
var iap_pending_operation_id := 0
var iap_pending_operation_kind := ""
var iap_pending_operation_product_id := ""
var iap_pending_beta_check_id := 0
var iap_pending_beta_game := {}
var iap_settings_refresh_pending := false
var android_video_import_notice_shown := false
var android_storage_permission_request_active := false
var android_storage_permission_request_deadline_msec := 0
var android_storage_permission_request_last_probe_msec := 0
var dirty_settings := false
var settings_animate_next := true
var settings_draft := {}
var settings_relayout_pending := false
var settings_relayout_scroll_vertical := 0
var detail_relayout_pending := false
var detail_relayout_scroll_vertical := 0
var native_launch_file_picker_pending := false
var native_launch_file_picker_library_path := ""
var native_cover_file_picker_pending := false
var native_cover_file_picker_library_path := ""
var native_translation_model_file_picker_pending := false
var active_game_path := ""
var active_game_started_msec := 0
var active_runtime_kind := RUNTIME_KIRIKIRI
var shell_scroll_drag_states := {}
var shell_scroll_remainders := {}
var shell_scroll_tweens := {}
var shell_scroll_targets := {}
var mobile_edge_back_touch_index := -1
var mobile_edge_back_start := Vector2.ZERO
var mobile_edge_back_last := Vector2.ZERO
var mobile_edge_back_cancelled := false
var opaque_frame_shader: Shader
var bicubic_frame_shader: Shader
var lanczos_frame_shader: Shader
var bicubic_frame_material: ShaderMaterial
var lanczos_frame_material: ShaderMaterial
var shown_system_alerts := {}
var ui_icon_cache := {}
var cover_texture_cache := {}
var ui_tokens = AetherDesignTokens.new()
var ui_motion = AetherMotion.new()
var ui_widgets = AetherWidgets.new(ui_tokens, ui_motion)

var player = null
var current_player_runtime_kind := RUNTIME_KIRIKIRI
var builtin_demo = BuiltinDemo.new()
var runtime_default_font_path := ""
var runtime_font_dir_path := ""
var selected_backend := "Godot Native"
var upscale_algorithm := "bicubic"
var output_resolution := OUTPUT_RESOLUTION_DEFAULT
var render_surface_mode := "game"
var frame_enhancement_enabled := false
var frame_enhancement_kind := "off"
var frame_enhancement_mode := FRAME_ENHANCEMENT_MODE_DEFAULT
var frame_enhancement_custom_chain := PackedStringArray([
    "anime4k_upscale_s", "bicubic", "anime4k_restore_soft_s",
])
var game_running := false
var runtime_dialog_input: LineEdit = null
var video_playing := false
var video_view: Control
var video_texture: TextureRect
var video_title_label: Label
var video_subtitle_label: Label
var video_top_bar: Control
var video_top_margin: MarginContainer
var video_back_button: Button
var video_controls: Control
var video_controls_margin: MarginContainer
var video_controls_box: VBoxContainer
var video_timeline: HBoxContainer
var video_action_groups: BoxContainer
var video_transport_actions: HBoxContainer
var video_option_actions: HBoxContainer
var video_rewind_button: Button
var video_play_button: Button
var video_forward_button: Button
var video_progress_slider: HSlider
var video_time_label: Label
var video_rate_button: OptionButton
var video_subtitle_button: OptionButton
var video_seek_feedback: PanelContainer
var video_seek_feedback_label: Label
var active_video_path := ""
var active_video_state := {}
var active_video_duration := 0.0
var video_pending_resume_position := 0.0
var active_video_was_playing := false
var active_video_scrubbing := false
var active_video_end_handled := false
var video_controls_visible := false
var video_controls_idle_sec := 0.0
var video_controls_panel_height := 144.0
var video_controls_tween: Tween
var video_touch_mouse_suppress_until_msec := 0
var video_seek_touch_index := -1
var video_seek_mouse_pressed := false
var video_seek_gesture_active := false
var video_seek_start_point := Vector2.ZERO
var video_seek_start_position := 0.0
var video_seek_target_position := 0.0
var video_previous_mouse_mode := Input.MOUSE_MODE_VISIBLE
var active_subtitle_tracks: Array[Dictionary] = []
var active_subtitle_cues: Array[Dictionary] = []
var active_subtitle_index := 0
var video_progress_data := {}
var video_progress_save_accum := 0.0
var app_lifecycle_paused := false
var render_errors := 0
var last_renderer_info_logged := ""
var last_texture_size := Vector2i.ZERO
var last_source_texture_size := Vector2i.ZERO
var capture_after_open_path := ""
var capture_after_open_done := false
var capture_after_open_delay_sec := 0.0
var capture_after_open_ready_usec := 0
var auto_probe_clicks: Array[Vector2] = []
var remembered_button_positions: Array[Vector2] = []
var observed_button_positions: Array[Vector2] = []
var button_position_memory_key := ""
var auto_probe_running := false
var auto_probe_done := false
var startup_click_stream_enabled := false
var startup_click_stream_running := false
var startup_click_stream_done := false
var log_drain_accum := 0.0
var perf_accum := 0.0
var perf_log_accum := 0.0
var state_log_accum := 0.0
var memory_observed_peak_bytes := 0
var startup_poll_accum := 0.0
var cached_startup_state := STARTUP_IDLE
var runtime_exit_cleanup_pending := false
var perf_log_interval := PERF_LOG_INTERVAL
var frame_spike_ms := 0.0
var frame_probe_enabled := false
var frame_probe_interval := 1.0
var frame_probe_accum := 0.0
var input_trace_enabled := false
var input_trace_accum := 0.0
var input_trace_received := 0
var input_trace_forwarded := 0
var input_trace_blocked := 0
var input_trace_throttled := 0
var input_trace_busy := 0
var input_trace_outside := 0
var input_trace_send_failed := 0
var input_trace_present_holds := 0
var input_trace_move_suppressed := 0
var tick_trace_serial := 0
var tick_trace_active_serial := 0
var tick_trace_until_msec := 0
var artemis_input_trace_sequence := 0
var artemis_input_trace_samples: Array[Dictionary] = []
var black_frame_guard_until_msec := 0
var black_frame_next_sample_msec := 0
var black_frame_consecutive := 0
var black_frame_last_log_msec := 0
var black_frame_guard_enabled := false
var cli_probe_script := ""
var cli_probe_runtime_debug := false
var verbose_render_log := false
var diagnostics_enabled := false
var ui_log_enabled := false
var web_auto_start_attempted := false
var perf_log_file: FileAccess
var log_lines: PackedStringArray = []
var last_tick_ms := 0.0
var last_update_ms := 0.0
var last_frame_ms := 0.0
var debug_last_input_event := ""
var debug_last_input_target := ""
var debug_last_input_position := Vector2.ZERO
var log_view_dirty := false
var log_view_flush_accum := 0.0
var suppress_mouse_until_msec := 0
var active_touch_points := {}
var active_mouse_buttons := {}
var suppressed_touch_points := {}
var touch_down_points := {}
var dragging_touch_points := {}
var pending_touch_index := -1
var pending_touch_mapped := Vector2.ZERO
var pending_touch_down_msec := 0
var pending_touch_quarantined := false
var delayed_touch_releases := {}
var last_forwarded_touch_down_msec := 0
var last_forwarded_touch_up_msec := 0
var last_forwarded_touch_move_msec_by_id := {}
var touch_secondary_quarantine_until_msec := 0
var touch_input_busy_until_msec := 0
var game_text_input_active := false
var game_text_input_forced := false
var game_text_input_attention_position := Vector2i(-1, -1)
var game_text_input_reopen_requested := false
var game_text_input_last_show_msec := 0
var game_text_input_suspended := false
var device_probe_enabled := false
var follow_texture_surface_size := false
var present_hold_frames := 0
var last_present_hold_msec := 0
var current_surface_size := Vector2i.ZERO
var render_surface_base_size := RENDER_SURFACE_SIZE
var render_surface_max_size := RENDER_SURFACE_MAX_SIZE
const LOG_DRAIN_INTERVAL := 0.50
const STARTUP_POLL_INTERVAL := 0.16
const PERF_UPDATE_INTERVAL := 0.25
const PERF_LOG_INTERVAL := 2.0
const UI_LOG_FLUSH_INTERVAL := 0.50
const MAX_LOG_LINES := 240
const RENDER_SURFACE_SIZE := Vector2i(1920, 1080)
const RENDER_SURFACE_MAX_SIZE := Vector2i(3840, 2160)
const RENDER_SURFACE_MODE_GAME := "game"
const RENDER_SURFACE_MODE_DISPLAY := "display"
const OUTPUT_RESOLUTION_DEFAULT := "1080p"
const OUTPUT_RESOLUTION_MODES := ["original", "1080p", "2k", "4k"]
const FRAME_ENHANCEMENT_KINDS := ["off", "preset", "custom"]
const FRAME_ENHANCEMENT_MODE_DEFAULT := "chain_soft"
const FRAME_ENHANCEMENT_MODES := [
    "chain_4k_max", "chain_lossless", "chain_ultra", "chain_detail",
    "chain_balanced", "chain_soft", "chain_light", "chain_basic",
]
const FRAME_ENHANCEMENT_PRESET_MODES := [
    "chain_4k_max", "chain_lossless", "chain_ultra", "chain_detail",
    "chain_balanced", "chain_soft", "chain_light", "chain_basic",
]
const FRAME_ENHANCEMENT_ALGORITHMS := [
    "anime4k_upscale_s", "anime4k_upscale_l", "anime4k_upscale_vl",
    "anime4k_restore_s", "anime4k_restore_soft_s", "anime4k_restore_soft_m",
    "anime4k_restore_l", "anime4k_restore_vl",
    "fsr1_easu", "fsr1_rcas", "bicubic", "lanczos", "fxaa",
    "ravu_lite_r2", "cunny_2x4c", "nnedi3_nns16",
]
const FRAME_ENHANCEMENT_ALGORITHM_LABELS := {
    "anime4k_upscale_s": "Anime4K Upscale S",
    "anime4k_upscale_l": "Anime4K Upscale L",
    "anime4k_upscale_vl": "Anime4K Upscale VL",
    "anime4k_restore_s": "Anime4K Restore S",
    "anime4k_restore_soft_s": "Anime4K Restore Soft S",
    "anime4k_restore_soft_m": "Anime4K Restore Soft M",
    "anime4k_restore_l": "Anime4K Restore L",
    "anime4k_restore_vl": "Anime4K Restore VL",
    "fsr1_easu": "FSR1 EASU",
    "fsr1_rcas": "FSR1 RCAS",
    "bicubic": "Bicubic",
    "lanczos": "Lanczos",
    "fxaa": "FXAA",
    "ravu_lite_r2": "RAVU-Lite R2",
    "cunny_2x4c": "CuNNy 2x4C",
    "nnedi3_nns16": "NNEDI3 nns16",
}
const FRAME_ENHANCEMENT_CUSTOM_DEFAULT := [
    "anime4k_upscale_s", "bicubic", "anime4k_restore_soft_s",
]
const FRAME_ENHANCEMENT_CUSTOM_MAX_STEPS := 32
const AETHER_SELECT_OVERLAY_INPUT_GROUP := "aether_select_input_overlay"
const OUTPUT_RESOLUTION_LIMITS := {
    "1080p": Vector2i(1920, 1080),
    "2k": Vector2i(2560, 1440),
    "4k": Vector2i(3840, 2160),
}
const POST_INPUT_PRESENT_HOLD_FRAMES := 1
const POST_CLICK_PRESENT_HOLD_FRAMES := 1
const POST_INPUT_PRESENT_HOLD_MIN_INTERVAL_MS := 120
const TOUCH_TAP_MIN_INTERVAL_MS := 0
const TOUCH_ACTION_COOLDOWN_MS := 0
const TOUCH_DRAG_MIN_INTERVAL_MS := 80
const TOUCH_DRAG_DISTANCE_THRESHOLD := 18.0
const ONS_TOUCH_CURSOR_DISTANCE_THRESHOLD := 4.0
const TOUCH_BUSY_TICK_MS := 120.0
const TOUCH_BUSY_SUPPRESS_MS := 0
const VIRTUAL_KEYBOARD_REOPEN_DELAY_MS := 750
const TOUCH_POINTER_ID_OFFSET := 100000
const TOUCH_SECONDARY_POINTER_ID := 0
const VIRTUAL_CONTROLS_POINTER_ID := TOUCH_POINTER_ID_OFFSET + 65535
const GAME_VIRTUAL_KEYBOARD_OPACITY_MIN := 0.2
const GAME_VIRTUAL_KEYBOARD_OPACITY_MAX := 1.0
const GAME_VIRTUAL_KEYBOARD_OPACITY_STEP := 0.05
const TOUCH_SECONDARY_TAP_WINDOW_MS := 180
const TOUCH_SECONDARY_QUARANTINE_MS := 320
const TOUCH_SINGLE_TAP_DELAY_MS := 90
const TOUCH_CLICK_HOLD_MS := 48
const ARTEMIS_INPUT_TRACE_DELAYS_MS := [0, 80, 240, 800]
const INPUT_DEVICE_ID_EMULATION := -1
const BLACK_FRAME_GUARD_MS := 3200
const BLACK_FRAME_SAMPLE_INTERVAL_MS := 120
const BLACK_FRAME_VISIBLE_MIN := 8
const INITIAL_WINDOW_SIZE := Vector2i(1920, 1080)
const DEFAULT_UI_DPI_SCALE := 1.35
const TOUCH_MOUSE_SUPPRESS_MS := 700
const MOBILE_EDGE_BACK_MAX_START_WIDTH := 36.0
const MOBILE_EDGE_BACK_MIN_TRIGGER_DISTANCE := 64.0
const PILL_ICON_SIZE := Vector2(24, 24)
const PILL_ICON_VISUAL_OFFSET_Y := 2.0
const SETTINGS_ACTION_BUTTON_SIZE := Vector2(150, 54)
const HOME_TILE_MIN_WIDTH := 340.0
const HOME_TILE_HEIGHT := 132.0
const HOME_TILE_COVER_WIDTH := 108.0
const HOME_ROW_HEIGHT := 112.0
const HOME_ROW_COVER_WIDTH := 116.0
const HOME_COMPACT_BREAKPOINT := 700.0
const HOME_PHONE_BREAKPOINT := 520.0
const DETAIL_COMPACT_BREAKPOINT := 960.0

var color_bg := Color(0.055, 0.059, 0.071, 1.0)
var color_game_bg := Color(0, 0, 0, 1)
var color_card := Color(0.098, 0.102, 0.118, 1.0)
var color_card_alt := Color(0.132, 0.137, 0.157, 1.0)
var color_card_hover := Color(0.170, 0.177, 0.202, 1.0)
var color_text := Color(0.961, 0.961, 0.973, 1.0)
var color_muted := Color(0.635, 0.643, 0.682, 1.0)
var color_accent := Color(0.039, 0.518, 1.000, 1.0)
var color_accent_soft := Color(0.392, 0.824, 1.000, 1.0)
var color_accent_dim := Color(0.067, 0.218, 0.369, 1.0)
var color_warn := Color(1.000, 0.624, 0.039, 1.0)
var color_danger := Color(1.000, 0.271, 0.227, 1.0)
var color_success := Color(0.188, 0.820, 0.345, 1.0)
var color_line := Color(1, 1, 1, 0.090)

func _normalize_style_mode(value: String) -> String:
    return value if value in STYLE_MODES else DEFAULT_STYLE_MODE

func _apply_style_mode(update_theme: bool = true) -> void:
    style_mode = _normalize_style_mode(style_mode)
    ui_tokens.configure(style_mode)
    if style_mode == STYLE_CLASSIC:
        color_bg = Color(0.961, 0.961, 0.973, 1.0)
        color_game_bg = Color(0, 0, 0, 1)
        color_card = Color(1.000, 1.000, 1.000, 1.0)
        color_card_alt = Color(0.910, 0.910, 0.929, 1.0)
        color_card_hover = Color(0.875, 0.878, 0.902, 1.0)
        color_text = Color(0.110, 0.110, 0.122, 1.0)
        color_muted = Color(0.388, 0.388, 0.424, 1.0)
        color_accent = Color(0.000, 0.478, 1.000, 1.0)
        color_accent_soft = Color(0.000, 0.478, 1.000, 1.0)
        color_accent_dim = Color(0.820, 0.902, 1.000, 1.0)
        color_warn = Color(1.000, 0.584, 0.000, 1.0)
        color_danger = Color(1.000, 0.231, 0.188, 1.0)
        color_success = Color(0.196, 0.690, 0.278, 1.0)
        color_line = Color(0, 0, 0, 0.105)
    else:
        color_bg = Color(0.055, 0.059, 0.071, 1.0)
        color_game_bg = Color(0, 0, 0, 1)
        color_card = Color(0.098, 0.102, 0.118, 1.0)
        color_card_alt = Color(0.132, 0.137, 0.157, 1.0)
        color_card_hover = Color(0.170, 0.177, 0.202, 1.0)
        color_text = Color(0.961, 0.961, 0.973, 1.0)
        color_muted = Color(0.635, 0.643, 0.682, 1.0)
        color_accent = Color(0.039, 0.518, 1.000, 1.0)
        color_accent_soft = Color(0.392, 0.824, 1.000, 1.0)
        color_accent_dim = Color(0.067, 0.218, 0.369, 1.0)
        color_warn = Color(1.000, 0.624, 0.039, 1.0)
        color_danger = Color(1.000, 0.271, 0.227, 1.0)
        color_success = Color(0.188, 0.820, 0.345, 1.0)
        color_line = Color(1, 1, 1, 0.090)

    color_bg = ui_tokens.background
    color_card = ui_tokens.surface
    color_card_alt = ui_tokens.surface_raised
    color_card_hover = ui_tokens.surface_hover
    color_text = ui_tokens.text_primary
    color_muted = ui_tokens.text_secondary
    color_accent = ui_tokens.accent
    color_accent_soft = ui_tokens.accent.lightened(0.12)
    color_accent_dim = ui_tokens.accent_fill
    color_warn = ui_tokens.warning
    color_danger = ui_tokens.danger
    color_success = ui_tokens.success
    color_line = ui_tokens.separator

    if update_theme:
        _apply_ui_font()
    if bg_rect != null:
        _set_game_background(game_running)
    else:
        RenderingServer.set_default_clear_color(color_game_bg if game_running else color_bg)

func _normalize_language_mode(value: String) -> String:
    return value if value in LANGUAGE_MODES else LANG_SYSTEM

func _system_language_code() -> String:
    var locale := OS.get_locale().to_lower().replace("-", "_")
    if locale.begins_with("zh_tw") or locale.begins_with("zh_hk") or locale.begins_with("zh_mo") or locale.contains("hant"):
        return LANG_ZH_HANT
    if locale.begins_with("zh"):
        return LANG_ZH_HANS
    if locale.begins_with("ja"):
        return LANG_JA
    if locale.begins_with("ko"):
        return LANG_KO
    if locale.begins_with("en"):
        return LANG_EN
    return LANG_EN

func _effective_language_code() -> String:
    var mode := _normalize_language_mode(language_mode)
    return _system_language_code() if mode == LANG_SYSTEM else mode

func _language_native_name(code: String) -> String:
    if code == LANG_ZH_HANS:
        return "简体中文"
    if code == LANG_ZH_HANT:
        return "繁體中文"
    if code == LANG_JA:
        return "日本語"
    if code == LANG_KO:
        return "한국어"
    return "English"

func _t(key: String, args: Array = []) -> String:
    var lang := active_language if UI_TEXT.has(active_language) else LANG_EN
    var table: Dictionary = UI_TEXT.get(lang, {})
    var value := ""
    if DiagnosticLocalization.has_key(key):
        value = DiagnosticLocalization.get_text(lang, key)
    elif table.has(key):
        value = String(table[key])
    else:
        value = String(UI_TEXT[LANG_ZH_HANS].get(key, key))
    return value % args if not args.is_empty() else value

func _language_option_label(mode: String) -> String:
    if mode == LANG_SYSTEM:
        return _t("language.system_with_value", [_language_native_name(_system_language_code())])
    return _language_native_name(mode)

func _style_option_label(mode: String) -> String:
    if mode == STYLE_CLASSIC:
        return _t("style.classic")
    return _t("style.dark")

func _apply_language_mode() -> void:
    language_mode = _normalize_language_mode(language_mode)
    active_language = _effective_language_code()

func _detect_cli_probe_script() -> String:
    var env_script := _normalize_cli_probe_script(OS.get_environment("AETHERKIRI_CLI_PROBE_SCRIPT"))
    if not env_script.is_empty():
        return env_script
    var args: Array[String] = []
    args.append_array(OS.get_cmdline_args())
    args.append_array(OS.get_cmdline_user_args())
    for i in range(args.size()):
        var arg := String(args[i])
        if arg == "--script" or arg == "-s" or arg == "--aether-probe-script":
            if i + 1 < args.size():
                return _normalize_cli_probe_script(String(args[i + 1]))
        elif arg.begins_with("--script="):
            return _normalize_cli_probe_script(arg.substr("--script=".length()))
        elif arg.begins_with("--aether-probe-script="):
            return _normalize_cli_probe_script(arg.substr("--aether-probe-script=".length()))
    if FileAccess.file_exists(ProbeConfig.debug_request_path()):
        var request := ProbeConfig.load()
        return _normalize_cli_probe_script(String(request.get("probe_script", "")))
    return ""

func _normalize_cli_probe_script(path: String) -> String:
    var normalized := path.strip_edges()
    if normalized.is_empty():
        return ""
    var known := [
        "res://scripts/smoke_test.gd",
        "res://scripts/step_render_probe.gd",
        "res://scripts/gui_render_probe.gd",
        "res://scripts/perf_input_probe.gd",
        "res://scripts/wa2_gui.gd",
    ]
    for item in known:
        if normalized == item or normalized.ends_with("/" + item.get_file()):
            return item
    return ""

func _mobile_runtime() -> bool:
    var platform := OS.get_name()
    return platform == "iOS" or platform == "Android"

func _apply_ui_font() -> void:
    var fallbacks: Array[Font] = [UI_SYMBOL_FONT]
    UI_FONT.set_fallbacks(fallbacks)
    BODY_FONT.set_fallbacks([UI_FONT, UI_SYMBOL_FONT])
    DISPLAY_CJK_FONT.set_fallbacks([BODY_FONT, UI_FONT, UI_SYMBOL_FONT])
    DISPLAY_FONT.set_fallbacks([DISPLAY_CJK_FONT, BODY_FONT, UI_FONT, UI_SYMBOL_FONT])
    var ui_theme := Theme.new()
    ui_theme.set_default_font(BODY_FONT)
    ui_theme.set_color("font_color", "Label", color_text)
    ui_theme.set_color("font_color", "Button", color_text)
    ui_theme.set_color("font_color", "OptionButton", color_text)
    ui_theme.set_color("font_hover_color", "OptionButton", color_text)
    ui_theme.set_color("font_pressed_color", "OptionButton", color_text)
    ui_theme.set_color("font_color", "LineEdit", color_text)
    ui_theme.set_color("font_color", "TextEdit", color_text)
    ui_theme.set_color("font_placeholder_color", "LineEdit", color_muted)
    ui_theme.set_color("font_disabled_color", "Button", _disabled_text_color())
    ui_theme.set_color("font_color", "CheckButton", color_text)
    ui_theme.set_color("font_hover_color", "CheckButton", color_text)
    ui_theme.set_color("font_pressed_color", "CheckButton", color_text)
    ui_theme.set_stylebox("normal", "OptionButton", _panel_style(8, color_card_alt, color_line, 1))
    ui_theme.set_stylebox("hover", "OptionButton", _panel_style(8, color_card_hover, Color.TRANSPARENT, 0))
    ui_theme.set_stylebox("pressed", "OptionButton", _panel_style(8, color_accent_dim, Color.TRANSPARENT, 0))
    ui_theme.set_stylebox("focus", "OptionButton", _focus_outline(8))
    ui_theme.set_stylebox("normal", "LineEdit", _panel_style(8, color_card_alt, color_line, 1))
    ui_theme.set_stylebox("focus", "LineEdit", _panel_style(8, color_card_hover, color_line, 1))
    ui_theme.set_stylebox("normal", "TextEdit", _panel_style(8, Color(0, 0, 0, 0.18), color_line, 1))
    ui_theme.set_stylebox("focus", "TextEdit", _panel_style(8, Color(0, 0, 0, 0.24), color_line, 1))
    ui_theme.set_stylebox("scroll", "VScrollBar", _scroll_track_style())
    ui_theme.set_stylebox("grabber", "VScrollBar", _scroll_thumb_style(color_muted.darkened(0.18)))
    ui_theme.set_stylebox("grabber_highlight", "VScrollBar", _scroll_thumb_style(color_muted))
    ui_theme.set_stylebox("grabber_pressed", "VScrollBar", _scroll_thumb_style(color_accent))
    ui_theme.set_constant("minimum_grab_thickness", "VScrollBar", 36)
    theme = ui_theme

func _game_title_font() -> FontVariation:
    if display_title_font == null:
        display_title_font = FontVariation.new()
        display_title_font.base_font = DISPLAY_FONT
        var text_server := TextServerManager.get_primary_interface()
        display_title_font.opentype_features = {
            text_server.name_to_tag("lnum"): 1,
            text_server.name_to_tag("onum"): 0,
        }
    return display_title_font

func _write_runtime_font(font: FontFile, target_path: String) -> bool:
    # Exported projects remap source font paths to Godot resources, so opening
    # the original .otf/.ttf path as a raw file is not portable. FontFile keeps
    # the original bytes and works identically in editor and exported builds.
    var data := font.get_data()
    if data.is_empty():
        return false
    var output := FileAccess.open(target_path, FileAccess.WRITE)
    if output == null:
        return false
    output.store_buffer(data)
    output.flush()
    return true

func _stage_runtime_fonts() -> void:
    runtime_default_font_path = ""
    runtime_font_dir_path = ""
    if OS.get_name() == "Web":
        return

    var native_dir := ProjectSettings.globalize_path(RUNTIME_FONT_DIR)
    DirAccess.make_dir_recursive_absolute(native_dir)

    var default_target := RUNTIME_FONT_DIR.path_join(RUNTIME_DEFAULT_FONT_FILE)
    var symbols_target := RUNTIME_FONT_DIR.path_join(RUNTIME_SYMBOL_FONT_FILE)
    var copied_any := false

    if _write_runtime_font(UI_FONT, default_target):
        runtime_default_font_path = ProjectSettings.globalize_path(default_target)
        copied_any = true
    else:
        _append_log("Runtime CJK font staging failed.")

    if _write_runtime_font(UI_SYMBOL_FONT, symbols_target):
        copied_any = true
    else:
        _append_log("Runtime symbol font staging failed.")

    if copied_any:
        runtime_font_dir_path = native_dir

func _build_ui() -> void:
    bg_rect = ColorRect.new()
    bg_rect.color = color_bg
    bg_rect.set_anchors_preset(Control.PRESET_FULL_RECT)
    add_child(bg_rect)

    game_path = LineEdit.new()
    game_path.visible = false
    add_child(game_path)

    backend = OptionButton.new()
    backend.visible = false
    add_child(backend)

    viewport = TextureRect.new()
    viewport.name = "GameViewport"
    viewport.set_anchors_preset(Control.PRESET_FULL_RECT)
    viewport.mouse_filter = Control.MOUSE_FILTER_STOP
    viewport.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
    viewport.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
    viewport.texture_filter = CanvasItem.TEXTURE_FILTER_LINEAR
    viewport.visible = false
    add_child(viewport)
    viewport.gui_input.connect(_on_viewport_input)
    _apply_upscale_algorithm()

    game_view = Control.new()
    game_view.set_anchors_preset(Control.PRESET_FULL_RECT)
    game_view.mouse_filter = Control.MOUSE_FILTER_IGNORE
    game_view.visible = false
    add_child(game_view)

    game_virtual_controls = GameVirtualControls.new()
    add_child(game_virtual_controls)
    game_virtual_controls.setup(ui_tokens)
    game_virtual_controls.set_input_mode(game_virtual_input_mode)
    _apply_game_virtual_control_preferences()
    game_virtual_controls.key_event_requested.connect(
        _on_game_virtual_key_event
    )
    game_virtual_controls.pointer_move_requested.connect(
        _on_game_virtual_pointer_move
    )
    game_virtual_controls.pointer_button_requested.connect(
        _on_game_virtual_pointer_button
    )
    game_virtual_controls.pointer_scroll_requested.connect(
        _on_game_virtual_pointer_scroll
    )
    game_virtual_controls.keyboard_requested.connect(
        _on_game_virtual_keyboard_requested
    )
    game_virtual_controls.virtual_controls_requested.connect(
        _on_game_virtual_controls_requested
    )
    game_virtual_controls.input_mode_changed.connect(
        _on_game_virtual_input_mode_changed
    )

    _build_video_view()

    shell_root = Control.new()
    shell_root.set_anchors_preset(Control.PRESET_FULL_RECT)
    add_child(shell_root)

    _build_shell_chrome()
    _build_home_view()
    _build_settings_view()
    _build_detail_view()
    _build_modal_layer()

    # Keep diagnostics above the game CanvasItem stack. GameViewport moves to
    # the front while playing, which can otherwise hide a sibling Control.
    perf_layer = CanvasLayer.new()
    perf_layer.name = "PerformanceOverlay"
    perf_layer.layer = 100
    add_child(perf_layer)

    perf_panel = PanelContainer.new()
    perf_panel.name = "PerformancePanel"
    perf_panel.mouse_filter = Control.MOUSE_FILTER_IGNORE
    perf_panel.add_theme_stylebox_override(
        "panel",
        ui_tokens.material_panel(true)
    )
    perf_panel.visible = false
    perf_layer.add_child(perf_panel)

    perf = Label.new()
    perf.mouse_filter = Control.MOUSE_FILTER_IGNORE
    perf.add_theme_font_size_override("font_size", 12)
    perf.add_theme_color_override("font_color", ui_tokens.text_secondary)
    perf.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
    perf_panel.add_child(perf)

    restart_notice = Label.new()
    restart_notice.position = Vector2(24, 44)
    restart_notice.add_theme_font_size_override("font_size", 14)
    restart_notice.add_theme_color_override("font_color", Color(1, 0.82, 0.65, 1))
    restart_notice.visible = false
    game_view.add_child(restart_notice)

    _build_loading_panel()
    _fit_full_rects()

func _build_video_view() -> void:
    video_view = Control.new()
    video_view.name = "VideoPlayerView"
    video_view.set_anchors_preset(Control.PRESET_FULL_RECT)
    video_view.visible = false
    add_child(video_view)

    var black := ColorRect.new()
    black.color = Color.BLACK
    black.set_anchors_preset(Control.PRESET_FULL_RECT)
    black.mouse_filter = Control.MOUSE_FILTER_IGNORE
    video_view.add_child(black)

    video_texture = TextureRect.new()
    video_texture.set_anchors_preset(Control.PRESET_FULL_RECT)
    video_texture.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
    video_texture.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
    video_texture.texture_filter = CanvasItem.TEXTURE_FILTER_LINEAR
    video_texture.mouse_filter = Control.MOUSE_FILTER_IGNORE
    video_view.add_child(video_texture)

    video_subtitle_label = Label.new()
    video_subtitle_label.anchor_left = 0.08
    video_subtitle_label.anchor_top = 1.0
    video_subtitle_label.anchor_right = 0.92
    video_subtitle_label.anchor_bottom = 1.0
    video_subtitle_label.offset_top = -190.0
    video_subtitle_label.offset_bottom = -42.0
    video_subtitle_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
    video_subtitle_label.vertical_alignment = VERTICAL_ALIGNMENT_BOTTOM
    video_subtitle_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
    video_subtitle_label.add_theme_font_size_override("font_size", 20 if _mobile_runtime() else 24)
    video_subtitle_label.add_theme_color_override("font_color", Color.WHITE)
    video_subtitle_label.add_theme_color_override("font_shadow_color", Color(0, 0, 0, 0.95))
    video_subtitle_label.add_theme_constant_override("shadow_offset_x", 2)
    video_subtitle_label.add_theme_constant_override("shadow_offset_y", 2)
    video_subtitle_label.mouse_filter = Control.MOUSE_FILTER_IGNORE
    video_view.add_child(video_subtitle_label)

    video_seek_feedback = PanelContainer.new()
    video_seek_feedback.anchor_left = 0.5
    video_seek_feedback.anchor_top = 0.5
    video_seek_feedback.anchor_right = 0.5
    video_seek_feedback.anchor_bottom = 0.5
    video_seek_feedback.offset_left = -150.0
    video_seek_feedback.offset_top = -42.0
    video_seek_feedback.offset_right = 150.0
    video_seek_feedback.offset_bottom = 42.0
    video_seek_feedback.mouse_filter = Control.MOUSE_FILTER_IGNORE
    video_seek_feedback.add_theme_stylebox_override(
        "panel",
        _panel_style(
            14,
            Color(0.015, 0.018, 0.026, 0.82),
            Color(1, 1, 1, 0.18),
            1
        )
    )
    video_seek_feedback.visible = false
    video_view.add_child(video_seek_feedback)
    video_seek_feedback_label = Label.new()
    video_seek_feedback_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
    video_seek_feedback_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
    video_seek_feedback_label.add_theme_font_size_override("font_size", 17 if _mobile_runtime() else 19)
    video_seek_feedback_label.add_theme_color_override("font_color", Color.WHITE)
    video_seek_feedback_label.mouse_filter = Control.MOUSE_FILTER_IGNORE
    video_seek_feedback.add_child(video_seek_feedback_label)

    video_top_bar = PanelContainer.new()
    video_top_bar.anchor_right = 1.0
    video_top_bar.offset_bottom = 76.0
    video_top_bar.add_theme_stylebox_override("panel", _panel_style(0, Color(0.015, 0.018, 0.026, 0.68), Color(0, 0, 0, 0), 0))
    video_view.add_child(video_top_bar)
    video_top_margin = MarginContainer.new()
    video_top_margin.add_theme_constant_override("margin_left", 20)
    video_top_margin.add_theme_constant_override("margin_top", 10)
    video_top_margin.add_theme_constant_override("margin_right", 24)
    video_top_margin.add_theme_constant_override("margin_bottom", 10)
    video_top_bar.add_child(video_top_margin)
    var top_row := HBoxContainer.new()
    top_row.add_theme_constant_override("separation", 14)
    video_top_margin.add_child(top_row)
    # Use the same vector icon as the rest of the shell. The text glyph could
    # be clipped vertically by the compact iPhone button/font metrics.
    video_back_button = _video_overlay_button("", 56.0)
    _attach_centered_button_icon(video_back_button, ICON_BACK, Vector2(24, 24))
    video_back_button.tooltip_text = _t("video.back")
    video_back_button.accessibility_name = _t("video.back")
    video_back_button.pressed.connect(_close_video_player)
    top_row.add_child(video_back_button)
    video_title_label = Label.new()
    video_title_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    video_title_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
    video_title_label.clip_text = true
    video_title_label.add_theme_font_size_override("font_size", 15 if _mobile_runtime() else 16)
    video_title_label.add_theme_color_override("font_color", Color.WHITE)
    top_row.add_child(video_title_label)

    video_controls = PanelContainer.new()
    video_controls.anchor_top = 1.0
    video_controls.anchor_right = 1.0
    video_controls.anchor_bottom = 1.0
    video_controls.offset_top = -144.0
    video_controls.add_theme_stylebox_override("panel", _panel_style(0, Color(0.015, 0.018, 0.026, 0.74), Color(0, 0, 0, 0), 0))
    video_view.add_child(video_controls)
    video_controls_margin = MarginContainer.new()
    video_controls_margin.add_theme_constant_override("margin_left", 22)
    video_controls_margin.add_theme_constant_override("margin_top", 12)
    video_controls_margin.add_theme_constant_override("margin_right", 22)
    video_controls_margin.add_theme_constant_override("margin_bottom", 14)
    video_controls.add_child(video_controls_margin)
    video_controls_box = VBoxContainer.new()
    video_controls_box.add_theme_constant_override("separation", 10)
    video_controls_margin.add_child(video_controls_box)

    video_timeline = HBoxContainer.new()
    video_timeline.add_theme_constant_override("separation", 14)
    video_controls_box.add_child(video_timeline)
    video_progress_slider = HSlider.new()
    video_progress_slider.min_value = 0.0
    video_progress_slider.max_value = 1.0
    video_progress_slider.step = 0.1
    video_progress_slider.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    video_progress_slider.drag_started.connect(func():
        active_video_scrubbing = true
        _set_video_controls_visible(true)
    )
    video_progress_slider.drag_ended.connect(func(value_changed: bool):
        active_video_scrubbing = false
        video_controls_idle_sec = 0.0
        if value_changed and player != null:
            player.media_seek(video_progress_slider.value)
    )
    video_timeline.add_child(video_progress_slider)
    video_time_label = Label.new()
    video_time_label.custom_minimum_size = Vector2(155, 0)
    video_time_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
    video_time_label.text = "00:00 / 00:00"
    video_time_label.add_theme_color_override("font_color", Color.WHITE)
    video_timeline.add_child(video_time_label)

    video_action_groups = BoxContainer.new()
    video_action_groups.alignment = BoxContainer.ALIGNMENT_CENTER
    video_action_groups.add_theme_constant_override("separation", 12)
    video_controls_box.add_child(video_action_groups)

    video_transport_actions = HBoxContainer.new()
    video_transport_actions.alignment = BoxContainer.ALIGNMENT_CENTER
    video_transport_actions.add_theme_constant_override("separation", 12)
    video_action_groups.add_child(video_transport_actions)
    video_rewind_button = _video_overlay_button("−10s", 86.0)
    video_rewind_button.pressed.connect(func(): _seek_video_relative(-10.0))
    video_transport_actions.add_child(video_rewind_button)
    video_play_button = _video_overlay_button("Ⅱ", 82.0)
    video_play_button.pressed.connect(_toggle_video_playback)
    video_transport_actions.add_child(video_play_button)
    video_forward_button = _video_overlay_button("+10s", 86.0)
    video_forward_button.pressed.connect(func(): _seek_video_relative(10.0))
    video_transport_actions.add_child(video_forward_button)

    video_option_actions = HBoxContainer.new()
    video_option_actions.alignment = BoxContainer.ALIGNMENT_CENTER
    video_option_actions.add_theme_constant_override("separation", 12)
    video_action_groups.add_child(video_option_actions)

    video_rate_button = OptionButton.new()
    video_rate_button.custom_minimum_size = Vector2(104, 48)
    _style_video_option_button(video_rate_button)
    _configure_video_option_popup(video_rate_button)
    for rate in [0.5, 0.75, 1.0, 1.25, 1.5, 2.0]:
        video_rate_button.add_item("%sx" % str(rate))
        video_rate_button.set_item_metadata(video_rate_button.item_count - 1, rate)
    video_rate_button.select(2)
    video_rate_button.item_selected.connect(func(index: int):
        _set_video_controls_visible(true)
        if player != null:
            player.media_set_rate(float(video_rate_button.get_item_metadata(index)))
    )
    video_option_actions.add_child(video_rate_button)

    video_subtitle_button = OptionButton.new()
    video_subtitle_button.custom_minimum_size = Vector2(180, 48)
    _style_video_option_button(video_subtitle_button)
    _configure_video_option_popup(video_subtitle_button)
    video_subtitle_button.item_selected.connect(_select_video_subtitle)
    video_option_actions.add_child(video_subtitle_button)
    video_top_bar.visible = false
    video_controls.visible = false

func _video_controls_layout_spec(safe_size: Vector2) -> Dictionary:
    var phone_portrait := safe_size.y > safe_size.x and safe_size.x < HOME_COMPACT_BREAKPOINT
    return {
        "phone_portrait": phone_portrait,
        "top_height": 64.0 if phone_portrait else 76.0,
        "panel_height": 196.0 if phone_portrait else 144.0,
        "horizontal_margin": 12 if phone_portrait else 22,
        "vertical_margin": 10 if phone_portrait else 12,
        "group_separation": 8 if phone_portrait else 12,
        "transport_width": 70.0 if phone_portrait else 86.0,
        "play_width": 66.0 if phone_portrait else 82.0,
        "rate_width": 86.0 if phone_portrait else 104.0,
        "subtitle_width": 148.0 if phone_portrait else 180.0,
        "button_height": 42.0 if phone_portrait else 48.0,
        "time_width": 108.0 if phone_portrait else 155.0,
    }

func _apply_video_controls_layout(safe_size: Vector2) -> Dictionary:
    var spec := _video_controls_layout_spec(safe_size)
    var phone_portrait: bool = spec["phone_portrait"]
    video_controls_panel_height = float(spec["panel_height"])
    if is_instance_valid(video_top_margin):
        video_top_margin.add_theme_constant_override("margin_left", 12 if phone_portrait else 20)
        video_top_margin.add_theme_constant_override("margin_top", 8 if phone_portrait else 10)
        video_top_margin.add_theme_constant_override("margin_right", 12 if phone_portrait else 24)
        video_top_margin.add_theme_constant_override("margin_bottom", 8 if phone_portrait else 10)
    if is_instance_valid(video_controls_margin):
        var horizontal_margin := int(spec["horizontal_margin"])
        var vertical_margin := int(spec["vertical_margin"])
        video_controls_margin.add_theme_constant_override("margin_left", horizontal_margin)
        video_controls_margin.add_theme_constant_override("margin_top", vertical_margin)
        video_controls_margin.add_theme_constant_override("margin_right", horizontal_margin)
        video_controls_margin.add_theme_constant_override("margin_bottom", vertical_margin)
    if is_instance_valid(video_controls_box):
        video_controls_box.add_theme_constant_override("separation", 8 if phone_portrait else 10)
    if is_instance_valid(video_timeline):
        video_timeline.add_theme_constant_override("separation", 8 if phone_portrait else 14)
    if is_instance_valid(video_action_groups):
        video_action_groups.vertical = phone_portrait
        video_action_groups.add_theme_constant_override("separation", int(spec["group_separation"]))
    if is_instance_valid(video_transport_actions):
        video_transport_actions.add_theme_constant_override("separation", int(spec["group_separation"]))
    if is_instance_valid(video_option_actions):
        video_option_actions.add_theme_constant_override("separation", int(spec["group_separation"]))

    var button_height := float(spec["button_height"])
    if is_instance_valid(video_back_button):
        video_back_button.custom_minimum_size = Vector2(48.0 if phone_portrait else 56.0, button_height)
        video_back_button.add_theme_font_size_override("font_size", 14 if phone_portrait else 15)
    if is_instance_valid(video_rewind_button):
        video_rewind_button.custom_minimum_size = Vector2(float(spec["transport_width"]), button_height)
        video_rewind_button.add_theme_font_size_override("font_size", 14 if phone_portrait else 15)
    if is_instance_valid(video_play_button):
        video_play_button.custom_minimum_size = Vector2(float(spec["play_width"]), button_height)
        video_play_button.add_theme_font_size_override("font_size", 14 if phone_portrait else 15)
    if is_instance_valid(video_forward_button):
        video_forward_button.custom_minimum_size = Vector2(float(spec["transport_width"]), button_height)
        video_forward_button.add_theme_font_size_override("font_size", 14 if phone_portrait else 15)
    if is_instance_valid(video_rate_button):
        video_rate_button.custom_minimum_size = Vector2(float(spec["rate_width"]), button_height)
        video_rate_button.add_theme_font_size_override("font_size", 13 if phone_portrait else 14)
    if is_instance_valid(video_subtitle_button):
        video_subtitle_button.custom_minimum_size = Vector2(float(spec["subtitle_width"]), button_height)
        video_subtitle_button.add_theme_font_size_override("font_size", 13 if phone_portrait else 14)
    if is_instance_valid(video_time_label):
        video_time_label.custom_minimum_size = Vector2(float(spec["time_width"]), 0)
        video_time_label.add_theme_font_size_override("font_size", 12 if phone_portrait else 14)
    if is_instance_valid(video_title_label):
        video_title_label.add_theme_font_size_override("font_size", 14 if phone_portrait else (15 if _mobile_runtime() else 16))
    return spec

func _set_video_controls_visible(show: bool, animate: bool = true) -> void:
    if not is_instance_valid(video_top_bar) or not is_instance_valid(video_controls):
        return
    video_controls_idle_sec = 0.0
    if video_controls_tween != null and video_controls_tween.is_valid():
        video_controls_tween.kill()
    video_controls_visible = show
    _layout_video_subtitles_for_controls(show)
    if show:
        video_top_bar.visible = true
        video_controls.visible = true
        video_top_bar.mouse_filter = Control.MOUSE_FILTER_STOP
        video_controls.mouse_filter = Control.MOUSE_FILTER_STOP
        if video_playing and not _is_touch_platform():
            Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
    else:
        video_top_bar.mouse_filter = Control.MOUSE_FILTER_IGNORE
        video_controls.mouse_filter = Control.MOUSE_FILTER_IGNORE
    if not animate:
        video_top_bar.modulate = Color(1, 1, 1, 1.0 if show else 0.0)
        video_controls.modulate = Color(1, 1, 1, 1.0 if show else 0.0)
        if not show:
            _finish_hide_video_controls()
        return
    video_controls_tween = create_tween()
    video_controls_tween.set_parallel(true)
    video_controls_tween.tween_property(video_top_bar, "modulate:a", 1.0 if show else 0.0, VIDEO_CONTROLS_FADE_SEC)
    video_controls_tween.tween_property(video_controls, "modulate:a", 1.0 if show else 0.0, VIDEO_CONTROLS_FADE_SEC)
    if not show:
        video_controls_tween.chain().tween_callback(_finish_hide_video_controls)

func _finish_hide_video_controls() -> void:
    if video_controls_visible:
        return
    video_top_bar.visible = false
    video_controls.visible = false
    if video_playing and not _is_touch_platform():
        Input.mouse_mode = Input.MOUSE_MODE_HIDDEN

func _layout_video_subtitles_for_controls(controls_shown: bool) -> void:
    if video_subtitle_label == null:
        return
    var viewport_size := get_viewport_rect().size
    var safe_rect := _ui_safe_rect(viewport_size)
    var bottom_inset := maxf(
        0.0,
        viewport_size.y - safe_rect.position.y - safe_rect.size.y
    )
    if controls_shown:
        video_subtitle_label.offset_bottom = -video_controls_panel_height - 10.0 - bottom_inset
        video_subtitle_label.offset_top = video_subtitle_label.offset_bottom - 132.0
    else:
        video_subtitle_label.offset_top = -190.0 - bottom_inset
        video_subtitle_label.offset_bottom = -42.0 - bottom_inset

func _video_controls_interacting() -> bool:
    if active_video_scrubbing:
        return true
    if is_instance_valid(video_rate_button) and video_rate_button.get_popup().visible:
        return true
    return is_instance_valid(video_subtitle_button) and video_subtitle_button.get_popup().visible

func _process_video_controls(delta: float) -> void:
    if not video_controls_visible:
        return
    if _video_controls_interacting():
        video_controls_idle_sec = 0.0
        return
    if int(active_video_state.get("status", MEDIA_STATUS_PLAYING)) != MEDIA_STATUS_PLAYING:
        video_controls_idle_sec = 0.0
        return
    video_controls_idle_sec += delta
    if video_controls_idle_sec >= VIDEO_CONTROLS_AUTO_HIDE_SEC:
        _set_video_controls_visible(false)

func _video_pointer_over_controls(position: Vector2) -> bool:
    if not video_controls_visible:
        return false
    return (
        is_instance_valid(video_top_bar)
        and video_top_bar.visible
        and video_top_bar.get_global_rect().has_point(position)
    ) or (
        is_instance_valid(video_controls)
        and video_controls.visible
        and video_controls.get_global_rect().has_point(position)
    )

func _reset_video_seek_gesture() -> void:
    video_seek_touch_index = -1
    video_seek_mouse_pressed = false
    video_seek_gesture_active = false
    video_seek_start_point = Vector2.ZERO
    video_seek_start_position = 0.0
    video_seek_target_position = 0.0
    active_video_scrubbing = false
    if is_instance_valid(video_seek_feedback):
        video_seek_feedback.visible = false

func _begin_video_seek_gesture(position: Vector2) -> void:
    video_seek_gesture_active = false
    video_seek_start_point = position
    video_seek_start_position = float(active_video_state.get("position", 0.0))
    video_seek_target_position = video_seek_start_position

func _update_video_seek_gesture(position: Vector2) -> bool:
    var delta := position - video_seek_start_point
    var duration := maxf(
        active_video_duration,
        float(active_video_state.get("duration", 0.0))
    )
    if not video_seek_gesture_active:
        if absf(delta.x) < VIDEO_SEEK_DRAG_THRESHOLD:
            return false
        if absf(delta.x) < absf(delta.y):
            return false
        if duration <= 0.0:
            return false
        video_seek_gesture_active = true
        active_video_scrubbing = true
        _set_video_controls_visible(true)
        if is_instance_valid(video_seek_feedback):
            video_seek_feedback.visible = true

    var view_width := maxf(1.0, video_view.size.x)
    var seek_span := clampf(
        duration * 0.10,
        VIDEO_SEEK_MIN_SPAN_SEC,
        VIDEO_SEEK_MAX_SPAN_SEC
    )
    var seek_delta := delta.x / view_width * seek_span
    video_seek_target_position = clampf(
        video_seek_start_position + seek_delta,
        0.0,
        duration
    )
    video_progress_slider.value = video_seek_target_position
    video_time_label.text = "%s / %s" % [
        _format_video_time(video_seek_target_position),
        _format_video_time(duration),
    ]
    var rounded_delta := roundi(
        video_seek_target_position - video_seek_start_position
    )
    var delta_text := "+%ds" % rounded_delta
    if rounded_delta < 0:
        delta_text = "−%ds" % absi(rounded_delta)
    elif rounded_delta == 0:
        delta_text = "0s"
    video_seek_feedback_label.text = "%s  ·  %s" % [
        delta_text,
        _format_video_time(video_seek_target_position),
    ]
    video_controls_idle_sec = 0.0
    return true

func _finish_video_seek_gesture(position: Vector2) -> void:
    var committed_seek := video_seek_gesture_active
    var moved := position.distance_to(video_seek_start_point)
    if committed_seek and player != null:
        player.media_seek(video_seek_target_position)
    video_seek_gesture_active = false
    active_video_scrubbing = false
    if is_instance_valid(video_seek_feedback):
        video_seek_feedback.visible = false
    if committed_seek or moved >= VIDEO_SEEK_DRAG_THRESHOLD:
        _set_video_controls_visible(true)
    else:
        _set_video_controls_visible(not video_controls_visible)
func _build_shell_chrome() -> void:
    shell_safe_top_fill = ColorRect.new()
    shell_safe_top_fill.name = "ShellSafeTopFill"
    shell_safe_top_fill.color = ui_tokens.sidebar_material
    shell_safe_top_fill.mouse_filter = Control.MOUSE_FILTER_IGNORE
    shell_root.add_child(shell_safe_top_fill)

    shell_sidebar_backdrop = PanelContainer.new()
    shell_sidebar_backdrop.name = "ShellSidebarBackdrop"
    shell_sidebar_backdrop.mouse_filter = Control.MOUSE_FILTER_IGNORE
    shell_sidebar_backdrop.add_theme_stylebox_override("panel", ui_tokens.sidebar_panel())
    shell_root.add_child(shell_sidebar_backdrop)

    shell_content = Control.new()
    shell_content.name = "ShellContent"
    shell_content.set_anchors_preset(Control.PRESET_FULL_RECT)
    shell_root.add_child(shell_content)

    shell_sidebar = PanelContainer.new()
    shell_sidebar.name = "ShellSidebar"
    shell_sidebar.clip_contents = true
    shell_sidebar.add_theme_stylebox_override("panel", ui_tokens.sidebar_panel())
    shell_root.add_child(shell_sidebar)

    var sidebar := VBoxContainer.new()
    sidebar.add_theme_constant_override("separation", 10)
    shell_sidebar.add_child(sidebar)

    shell_sidebar_brand = HBoxContainer.new()
    shell_sidebar_brand.custom_minimum_size = Vector2(0, 62)
    shell_sidebar_brand.add_theme_constant_override("separation", 12)
    sidebar.add_child(shell_sidebar_brand)
    shell_sidebar_brand.add_child(_icon_rect(ICON_GAMEPAD, Vector2(30, 30), ui_tokens.accent))

    shell_sidebar_brand_labels = VBoxContainer.new()
    shell_sidebar_brand_labels.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    shell_sidebar_brand_labels.add_theme_constant_override("separation", 1)
    shell_sidebar_brand.add_child(shell_sidebar_brand_labels)
    var brand_title := Label.new()
    brand_title.text = APP_DISPLAY_NAME
    brand_title.add_theme_font_size_override("font_size", 20)
    brand_title.add_theme_color_override("font_color", ui_tokens.text_primary)
    shell_sidebar_brand_labels.add_child(brand_title)
    var brand_caption := Label.new()
    brand_caption.text = _t("home.subtitle")
    brand_caption.clip_text = true
    brand_caption.add_theme_font_size_override("font_size", 11)
    brand_caption.add_theme_color_override("font_color", ui_tokens.text_tertiary)
    shell_sidebar_brand_labels.add_child(brand_caption)

    var nav_spacer := Control.new()
    nav_spacer.custom_minimum_size = Vector2(0, 10)
    sidebar.add_child(nav_spacer)

    shell_library_button = _shell_nav_button(_t("nav.library"), ICON_LIBRARY, _show_home)
    sidebar.add_child(shell_library_button)
    shell_video_button = _shell_nav_button(_t("nav.videos"), ICON_VIDEO, _show_video_library)
    sidebar.add_child(shell_video_button)
    shell_settings_button = _shell_nav_button(_t("settings.title"), ICON_SETTINGS, _show_settings)
    sidebar.add_child(shell_settings_button)

    var flexible_space := Control.new()
    flexible_space.size_flags_vertical = Control.SIZE_EXPAND_FILL
    sidebar.add_child(flexible_space)

    shell_sidebar_toggle = Button.new()
    shell_sidebar_toggle.text = "☰"
    shell_sidebar_toggle.tooltip_text = _t("nav.collapse_sidebar")
    shell_sidebar_toggle.accessibility_name = shell_sidebar_toggle.tooltip_text
    shell_sidebar_toggle.custom_minimum_size = Vector2(44, 44)
    shell_sidebar_toggle.focus_mode = Control.FOCUS_ALL
    shell_sidebar_toggle.pressed.connect(_toggle_sidebar)
    shell_sidebar_toggle.size_flags_horizontal = Control.SIZE_SHRINK_CENTER
    ui_widgets.toolbar_button(shell_sidebar_toggle)
    shell_sidebar_toggle.add_theme_font_size_override("font_size", 23)
    sidebar.add_child(shell_sidebar_toggle)

    shell_sidebar_version = Label.new()
    shell_sidebar_version.text = _application_version_text()
    shell_sidebar_version.add_theme_font_size_override("font_size", 10)
    shell_sidebar_version.add_theme_color_override("font_color", ui_tokens.text_tertiary)
    sidebar.add_child(shell_sidebar_version)

    shell_compact_header = PanelContainer.new()
    shell_compact_header.name = "ShellCompactHeader"
    shell_compact_header.anchor_right = 1.0
    var compact_header_style := ui_tokens.panel(ui_tokens.sidebar_material, 0, ui_tokens.separator, 1)
    compact_header_style.border_width_left = 0
    compact_header_style.border_width_top = 0
    compact_header_style.border_width_right = 0
    shell_compact_header.add_theme_stylebox_override("panel", compact_header_style)
    shell_root.add_child(shell_compact_header)

    var compact_margin := MarginContainer.new()
    compact_margin.add_theme_constant_override("margin_left", 18)
    compact_margin.add_theme_constant_override("margin_top", 10)
    compact_margin.add_theme_constant_override("margin_right", 12)
    compact_margin.add_theme_constant_override("margin_bottom", 10)
    shell_compact_header.add_child(compact_margin)
    var compact_row := HBoxContainer.new()
    compact_row.add_theme_constant_override("separation", 10)
    compact_margin.add_child(compact_row)
    compact_row.add_child(_icon_rect(ICON_GAMEPAD, Vector2(25, 25), ui_tokens.accent))
    shell_route_label = Label.new()
    shell_route_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    shell_route_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
    shell_route_label.add_theme_font_size_override("font_size", 19)
    shell_route_label.add_theme_color_override("font_color", ui_tokens.text_primary)
    compact_row.add_child(shell_route_label)
    shell_compact_library_button = _shell_compact_button(ICON_LIBRARY, _t("nav.library"), _show_home)
    compact_row.add_child(shell_compact_library_button)
    shell_compact_video_button = _shell_compact_button(ICON_VIDEO, _t("nav.videos"), _show_video_library)
    compact_row.add_child(shell_compact_video_button)
    shell_compact_settings_button = _shell_compact_button(ICON_SETTINGS, _t("settings.title"), _show_settings)
    compact_row.add_child(shell_compact_settings_button)

    _sync_shell_route(shell_route)
    _apply_sidebar_presentation(false)

func _shell_nav_button(text: String, icon_path: String, callback: Callable) -> Button:
    var button := Button.new()
    button.text = text
    button.icon = _load_ui_icon(icon_path)
    button.expand_icon = true
    button.icon_alignment = HORIZONTAL_ALIGNMENT_LEFT
    button.alignment = HORIZONTAL_ALIGNMENT_LEFT
    button.custom_minimum_size = Vector2(0, 48)
    button.add_theme_constant_override("icon_max_width", 21)
    button.add_theme_constant_override("h_separation", 11)
    button.add_theme_font_size_override("font_size", 15)
    button.focus_mode = Control.FOCUS_ALL
    button.pressed.connect(callback)
    return button

func _shell_compact_button(icon_path: String, tooltip: String, callback: Callable) -> Button:
    var button := Button.new()
    button.icon = _load_ui_icon(icon_path)
    button.expand_icon = true
    button.custom_minimum_size = Vector2(44, 44)
    button.size_flags_horizontal = Control.SIZE_SHRINK_CENTER
    button.size_flags_vertical = Control.SIZE_SHRINK_CENTER
    button.tooltip_text = tooltip
    button.add_theme_constant_override("icon_max_width", 22)
    button.focus_mode = Control.FOCUS_ALL
    button.pressed.connect(callback)
    return button

func _sync_shell_route(route: String) -> void:
    shell_route = route
    if shell_route_label != null:
        shell_route_label.text = APP_DISPLAY_NAME
    _apply_shell_nav_state(shell_library_button, route == "library")
    _apply_shell_nav_state(shell_video_button, route == "videos")
    _apply_shell_nav_state(shell_settings_button, route == "settings")
    _apply_shell_compact_state(shell_compact_library_button, route == "library")
    _apply_shell_compact_state(shell_compact_video_button, route == "videos")
    _apply_shell_compact_state(shell_compact_settings_button, route == "settings")

func _apply_shell_nav_state(button: Button, selected: bool) -> void:
    if button == null:
        return
    if shell_sidebar_collapsed or shell_sidebar_animating_expand:
        button.custom_minimum_size = Vector2(44, 44)
        button.size_flags_horizontal = Control.SIZE_SHRINK_CENTER
        ui_widgets.toolbar_button(button, selected)
    else:
        button.custom_minimum_size = Vector2(0, 48)
        button.size_flags_horizontal = Control.SIZE_EXPAND_FILL
        ui_widgets.navigation_button(button, selected)

func _apply_shell_compact_state(button: Button, selected: bool) -> void:
    if button == null:
        return
    ui_widgets.toolbar_button(button, selected)

func _toggle_sidebar() -> void:
    if AetherDisplayScale.use_compact_shell(get_viewport_rect().size):
        return
    var expanding := shell_sidebar_collapsed
    if shell_sidebar_tween != null and shell_sidebar_tween.is_valid():
        shell_sidebar_tween.kill()
    _reset_sidebar_item_modulates()
    shell_sidebar_collapsed = not shell_sidebar_collapsed
    shell_sidebar_animating_expand = expanding
    var target_width: float = ui_tokens.SIDEBAR_COLLAPSED_WIDTH if shell_sidebar_collapsed else ui_tokens.SIDEBAR_WIDTH
    if ui_motion.reduced_motion:
        shell_sidebar_animating_expand = false
        _apply_sidebar_width(target_width)
        _apply_sidebar_presentation(false)
        return
    if expanding:
        _apply_sidebar_presentation(false)
        _animate_sidebar_width(target_width, true)
        return
    shell_sidebar_tween = shell_sidebar.create_tween().set_parallel(true)
    for item in [shell_sidebar_brand_labels, shell_library_button, shell_video_button, shell_settings_button, shell_sidebar_version]:
        shell_sidebar_tween.tween_property(item, "modulate:a", 0.0, 0.10).set_trans(Tween.TRANS_QUART).set_ease(Tween.EASE_OUT)
    shell_sidebar_tween.chain().tween_callback(func():
        _apply_sidebar_presentation(false)
        _reset_sidebar_item_modulates()
        _animate_sidebar_width(target_width, false)
    )

func _animate_sidebar_width(target_width: float, expanding: bool) -> void:
    if ui_motion.reduced_motion:
        shell_sidebar_animating_expand = false
        _apply_sidebar_width(target_width)
        _apply_sidebar_presentation(expanding)
        return
    shell_sidebar_tween = shell_sidebar.create_tween()
    shell_sidebar_tween.tween_method(
        _apply_sidebar_width,
        shell_sidebar_layout_width,
        target_width,
        0.36
    ).set_trans(Tween.TRANS_QUINT).set_ease(Tween.EASE_OUT)
    shell_sidebar_tween.tween_callback(func():
        shell_sidebar_animating_expand = false
        _apply_sidebar_width(target_width)
        _apply_sidebar_presentation(expanding)
    )

func _reset_sidebar_item_modulates() -> void:
    for item in [shell_sidebar_brand_labels, shell_library_button, shell_video_button, shell_settings_button, shell_sidebar_version]:
        if item != null and is_instance_valid(item):
            item.modulate.a = 1.0

func _apply_sidebar_presentation(animate_labels: bool) -> void:
    if shell_sidebar_brand_labels == null:
        return
    var compact_visual := shell_sidebar_collapsed or shell_sidebar_animating_expand
    shell_sidebar_brand.alignment = BoxContainer.ALIGNMENT_CENTER if compact_visual else BoxContainer.ALIGNMENT_BEGIN
    shell_sidebar_brand_labels.visible = not compact_visual
    shell_sidebar_version.visible = not compact_visual
    shell_library_button.text = "" if compact_visual else _t("nav.library")
    shell_video_button.text = "" if compact_visual else _t("nav.videos")
    shell_settings_button.text = "" if compact_visual else _t("settings.title")
    shell_library_button.tooltip_text = _t("nav.library") if compact_visual else ""
    shell_video_button.tooltip_text = _t("nav.videos") if compact_visual else ""
    shell_settings_button.tooltip_text = _t("settings.title") if compact_visual else ""
    shell_library_button.icon_alignment = HORIZONTAL_ALIGNMENT_CENTER if compact_visual else HORIZONTAL_ALIGNMENT_LEFT
    shell_video_button.icon_alignment = HORIZONTAL_ALIGNMENT_CENTER if compact_visual else HORIZONTAL_ALIGNMENT_LEFT
    shell_settings_button.icon_alignment = HORIZONTAL_ALIGNMENT_CENTER if compact_visual else HORIZONTAL_ALIGNMENT_LEFT
    shell_library_button.alignment = HORIZONTAL_ALIGNMENT_CENTER if compact_visual else HORIZONTAL_ALIGNMENT_LEFT
    shell_video_button.alignment = HORIZONTAL_ALIGNMENT_CENTER if compact_visual else HORIZONTAL_ALIGNMENT_LEFT
    shell_settings_button.alignment = HORIZONTAL_ALIGNMENT_CENTER if compact_visual else HORIZONTAL_ALIGNMENT_LEFT
    shell_sidebar_toggle.text = "☰"
    shell_sidebar_toggle.tooltip_text = _t("nav.expand_sidebar") if shell_sidebar_collapsed else _t("nav.collapse_sidebar")
    shell_sidebar_toggle.accessibility_name = shell_sidebar_toggle.tooltip_text
    _apply_shell_nav_state(shell_library_button, shell_route == "library")
    _apply_shell_nav_state(shell_video_button, shell_route == "videos")
    _apply_shell_nav_state(shell_settings_button, shell_route == "settings")
    if animate_labels and not compact_visual:
        ui_motion.enter(shell_sidebar_brand_labels, Vector2.ZERO, 0.03)
        ui_motion.enter(shell_library_button, Vector2.ZERO, 0.04)
        ui_motion.enter(shell_video_button, Vector2.ZERO, 0.06)
        ui_motion.enter(shell_settings_button, Vector2.ZERO, 0.08)
        ui_motion.enter(shell_sidebar_version, Vector2.ZERO, 0.10)

func _apply_sidebar_width(width: float) -> void:
    if shell_sidebar == null or shell_content == null:
        return
    shell_sidebar_layout_width = width
    var layout_size := shell_root.size
    if layout_size.x <= 0.0 or layout_size.y <= 0.0:
        layout_size = _ui_safe_rect(get_viewport_rect().size).size
    shell_sidebar.size = Vector2(width, layout_size.y)
    shell_content.offset_left = width
    _layout_shell_safe_area_fills(get_viewport_rect().size, _ui_safe_rect(get_viewport_rect().size))
    _layout_home_view(Vector2(maxf(0.0, layout_size.x - width), layout_size.y))

func _layout_shell(window_size: Vector2) -> void:
    if shell_content == null or shell_sidebar == null or shell_compact_header == null:
        return
    var compact := AetherDisplayScale.use_compact_shell(window_size)
    if shell_sidebar_tween != null and shell_sidebar_tween.is_valid():
        shell_sidebar_tween.kill()
        shell_sidebar_animating_expand = false
        _reset_sidebar_item_modulates()
        _apply_sidebar_presentation(false)
    shell_sidebar.visible = not compact
    shell_compact_header.visible = compact
    shell_sidebar.position = Vector2.ZERO
    shell_sidebar_layout_width = ui_tokens.SIDEBAR_COLLAPSED_WIDTH if shell_sidebar_collapsed else ui_tokens.SIDEBAR_WIDTH
    shell_sidebar.size = Vector2(shell_sidebar_layout_width, window_size.y)
    shell_compact_header.offset_left = 0.0
    shell_compact_header.offset_top = 0.0
    shell_compact_header.offset_right = 0.0
    shell_compact_header.offset_bottom = ui_tokens.TOOLBAR_HEIGHT
    shell_content.set_anchors_preset(Control.PRESET_FULL_RECT)
    shell_content.offset_left = 0.0 if compact else shell_sidebar_layout_width
    shell_content.offset_top = ui_tokens.TOOLBAR_HEIGHT if compact else 0.0
    shell_content.offset_right = 0.0
    shell_content.offset_bottom = 0.0

func _load_shell_settings() -> void:
    var cfg := ConfigFile.new()
    var env_style := _runtime_string("AETHERKIRI_STYLE_MODE", "")
    var env_translation_model_path := _runtime_string(
        "AETHERKIRI_TRANSLATION_MODEL", ""
    )
    var env_frame_enhancement_kind := _runtime_string(
        "AETHERKIRI_FRAME_ENHANCEMENT_KIND",
        ""
    )
    var env_frame_enhancement_mode := _runtime_string(
        "AETHERKIRI_FRAME_ENHANCEMENT_MODE",
        ""
    )
    if cfg.load(SETTINGS_FILE) != OK:
        text_translation_model_path = env_translation_model_path
        var env_surface_mode := _runtime_string("AETHERKIRI_SURFACE_MODE", "")
        if not env_surface_mode.is_empty():
            _select_config_surface_mode(env_surface_mode)
        output_resolution = _normalize_output_resolution(_runtime_string(
            "AETHERKIRI_OUTPUT_RESOLUTION",
            output_resolution
        ))
        if not env_frame_enhancement_kind.is_empty():
            frame_enhancement_kind = _normalize_frame_enhancement_kind(
                env_frame_enhancement_kind
            )
            frame_enhancement_enabled = frame_enhancement_kind != "off"
        if not env_frame_enhancement_mode.is_empty():
            frame_enhancement_mode = _normalize_frame_enhancement_mode(
                env_frame_enhancement_mode
            )
        _apply_language_mode()
        if not env_style.is_empty():
            style_mode = _normalize_style_mode(env_style)
        _apply_style_mode()
        return
    language_mode = _normalize_language_mode(String(cfg.get_value("interface", "language", language_mode)))
    text_translation_model_path = String(cfg.get_value(
        "translation", "model_path", text_translation_model_path
    ))
    if not env_translation_model_path.is_empty():
        text_translation_model_path = env_translation_model_path
    _apply_language_mode()
    style_mode = _normalize_style_mode(String(cfg.get_value("interface", "style", style_mode)))
    ios_ui_scale_mode = String(cfg.get_value("interface", "ios_ui_scale_mode", ios_ui_scale_mode))
    if not ios_ui_scale_mode in IOS_UI_SCALE_MODES:
        ios_ui_scale_mode = "comfortable"
    if not env_style.is_empty():
        style_mode = _normalize_style_mode(env_style)
    _apply_style_mode()
    selected_backend = _normalize_backend_name(String(cfg.get_value("rendering", "backend", selected_backend)))
    upscale_algorithm = String(cfg.get_value("rendering", "upscale_algorithm", upscale_algorithm))
    # `sharp` was an obsolete alias from the original settings schema. Keep
    # migrating that value, but do not fold the supported `nearest` mode back
    # into the default while loading the saved settings.
    if upscale_algorithm == "sharp":
        upscale_algorithm = "bicubic"
    if not upscale_algorithm in ["smooth", "nearest", "linear", "bicubic", "lanczos"]:
        upscale_algorithm = "bicubic"
    output_resolution = _normalize_output_resolution(String(cfg.get_value(
        "rendering",
        "output_resolution",
        output_resolution
    )))
    output_resolution = _normalize_output_resolution(_runtime_string(
        "AETHERKIRI_OUTPUT_RESOLUTION",
        output_resolution
    ))
    render_surface_mode = String(cfg.get_value("rendering", "surface_mode", render_surface_mode))
    _select_config_surface_mode(_runtime_string("AETHERKIRI_SURFACE_MODE", render_surface_mode))
    var legacy_frame_enhancement_enabled := bool(cfg.get_value(
        "rendering",
        "frame_enhancement_enabled",
        frame_enhancement_enabled
    ))
    frame_enhancement_kind = _normalize_frame_enhancement_kind(String(cfg.get_value(
        "rendering",
        "frame_enhancement_kind",
        "preset" if legacy_frame_enhancement_enabled else "off"
    )))
    frame_enhancement_enabled = frame_enhancement_kind != "off"
    frame_enhancement_mode = _normalize_frame_enhancement_mode(String(cfg.get_value(
        "rendering",
        "frame_enhancement_mode",
        frame_enhancement_mode
    )))
    if not env_frame_enhancement_kind.is_empty():
        frame_enhancement_kind = _normalize_frame_enhancement_kind(
            env_frame_enhancement_kind
        )
        frame_enhancement_enabled = frame_enhancement_kind != "off"
    if not env_frame_enhancement_mode.is_empty():
        frame_enhancement_mode = _normalize_frame_enhancement_mode(
            env_frame_enhancement_mode
        )
    frame_enhancement_custom_chain = _normalize_frame_enhancement_custom_chain(
        cfg.get_value(
            "rendering",
            "frame_enhancement_custom_chain",
            FRAME_ENHANCEMENT_CUSTOM_DEFAULT
        )
    )
    var legacy_perf_overlay := bool(cfg.get_value("rendering", "perf_overlay", show_perf_monitor))
    debug_overlay_mode = String(cfg.get_value("diagnostics", "overlay_mode", "summary" if legacy_perf_overlay else "off"))
    if not debug_overlay_mode in DEBUG_OVERLAY_MODES:
        debug_overlay_mode = "off"
    show_perf_monitor = debug_overlay_mode != "off"
    diagnostic_profile = String(cfg.get_value("diagnostics", "profile", diagnostic_profile))
    if not diagnostic_profile in DIAGNOSTIC_PROFILES:
        diagnostic_profile = "baseline" if OS.is_debug_build() else "off"
    frame_limit_enabled = bool(cfg.get_value("rendering", "fps_limit_enabled", frame_limit_enabled))
    target_fps = int(cfg.get_value("rendering", "target_fps", target_fps))
    lock_landscape = bool(cfg.get_value("rendering", "force_landscape", lock_landscape))
    var orientation_schema := int(cfg.get_value("rendering", "orientation_schema", 0))
    if _mobile_runtime() and orientation_schema < MOBILE_ORIENTATION_SCHEMA_VERSION:
        lock_landscape = false
    game_virtual_input_mode = _normalize_game_virtual_input_mode(String(
        cfg.get_value("input", "virtual_control_mode", game_virtual_input_mode)
    ))
    game_virtual_menu_enabled = bool(cfg.get_value(
        "input", "virtual_control_menu_enabled", game_virtual_menu_enabled
    ))
    game_virtual_keyboard_opacity = _normalize_game_virtual_keyboard_opacity(
        float(cfg.get_value(
            "input",
            "virtual_control_keyboard_opacity",
            game_virtual_keyboard_opacity
        ))
    )
    plugin_load_mode = String(cfg.get_value("developer", "plugin_load_mode", plugin_load_mode))
    if not plugin_load_mode in ["krkrsdl3", "aether_all"]:
        plugin_load_mode = "krkrsdl3"
    mock_enabled = bool(cfg.get_value("developer", "mock_enabled", mock_enabled))
    error_dialog_logs = bool(cfg.get_value("developer", "error_dialog_logs", error_dialog_logs))
    legal_accepted_version = String(cfg.get_value("legal", "accepted_version", ""))
    legal_accepted_at = int(cfg.get_value("legal", "accepted_at", 0))
    ios_statement_accepted_version = String(cfg.get_value("legal", "ios_statement_accepted_version", ""))
    ios_statement_accepted_at = int(cfg.get_value("legal", "ios_statement_accepted_at", 0))
    secret_iap_unlocked = bool(cfg.get_value("unlock", "secret_iap_unlocked", false))
    secret_coffee_until_unix = int(cfg.get_value("unlock", "secret_coffee_until_unix", 0))

func _configure_runtime_diagnostics() -> void:
    diagnostics_enabled = _runtime_flag("AETHERKIRI_DIAGNOSTICS")
    diagnostics_enabled = diagnostics_enabled or diagnostic_profile != "off"
    diagnostics_enabled = diagnostics_enabled or DiagnosticSession.external_request_present()
    diagnostics_enabled = diagnostics_enabled or device_probe_enabled
    diagnostics_enabled = diagnostics_enabled or verbose_render_log
    diagnostics_enabled = diagnostics_enabled or trace_log
    diagnostics_enabled = diagnostics_enabled or frame_probe_enabled
    diagnostics_enabled = diagnostics_enabled or input_trace_enabled
    diagnostics_enabled = diagnostics_enabled or frame_spike_ms > 0.0
    diagnostics_enabled = diagnostics_enabled or perf_log_file != null
    ui_log_enabled = _runtime_flag("AETHERKIRI_UI_LOG")

func _normalize_backend_name(value: String) -> String:
    var backend_name := value.strip_edges()
    var key := backend_name.to_lower().replace("_", "").replace(" ", "")
    if key == "debugcpu":
        return "Debug CPU"
    if key == "gpubridge":
        return "GPU Bridge"
    if key == "godotnative":
        return "Godot Native"
    return backend_name

func _normalize_game_virtual_input_mode(value: String) -> String:
    return (
        value
        if value in GameVirtualControls.INPUT_MODES
        else GameVirtualControls.INPUT_MODE_MOUSE
    )

func _normalize_game_virtual_keyboard_opacity(value: float) -> float:
    return snappedf(
        clampf(
            value,
            GAME_VIRTUAL_KEYBOARD_OPACITY_MIN,
            GAME_VIRTUAL_KEYBOARD_OPACITY_MAX
        ),
        GAME_VIRTUAL_KEYBOARD_OPACITY_STEP
    )

func _save_shell_settings() -> void:
    var cfg := ConfigFile.new()
    cfg.set_value("interface", "language", language_mode)
    cfg.set_value("interface", "style", style_mode)
    cfg.set_value("interface", "ios_ui_scale_mode", ios_ui_scale_mode)
    cfg.set_value("translation", "model_path", text_translation_model_path)
    cfg.set_value("rendering", "backend", selected_backend)
    cfg.set_value("rendering", "upscale_algorithm", upscale_algorithm)
    cfg.set_value("rendering", "output_resolution", output_resolution)
    cfg.set_value("rendering", "surface_mode", render_surface_mode)
    cfg.set_value("rendering", "frame_enhancement_enabled", frame_enhancement_enabled)
    cfg.set_value("rendering", "frame_enhancement_kind", frame_enhancement_kind)
    cfg.set_value("rendering", "frame_enhancement_mode", frame_enhancement_mode)
    cfg.set_value(
        "rendering", "frame_enhancement_custom_chain",
        frame_enhancement_custom_chain
    )
    cfg.set_value("diagnostics", "profile", diagnostic_profile)
    cfg.set_value("diagnostics", "overlay_mode", debug_overlay_mode)
    cfg.set_value("rendering", "fps_limit_enabled", frame_limit_enabled)
    cfg.set_value("rendering", "target_fps", target_fps)
    cfg.set_value("rendering", "force_landscape", lock_landscape)
    cfg.set_value("rendering", "orientation_schema", MOBILE_ORIENTATION_SCHEMA_VERSION)
    cfg.set_value("input", "virtual_control_mode", game_virtual_input_mode)
    cfg.set_value(
        "input", "virtual_control_menu_enabled", game_virtual_menu_enabled
    )
    cfg.set_value(
        "input",
        "virtual_control_keyboard_opacity",
        game_virtual_keyboard_opacity
    )
    cfg.set_value("developer", "plugin_load_mode", plugin_load_mode)
    cfg.set_value("developer", "mock_enabled", mock_enabled)
    cfg.set_value("developer", "error_dialog_logs", error_dialog_logs)
    cfg.set_value("legal", "accepted_version", legal_accepted_version)
    cfg.set_value("legal", "accepted_at", legal_accepted_at)
    cfg.set_value("legal", "ios_statement_accepted_version", ios_statement_accepted_version)
    cfg.set_value("legal", "ios_statement_accepted_at", ios_statement_accepted_at)
    cfg.set_value("unlock", "secret_iap_unlocked", secret_iap_unlocked)
    cfg.set_value("unlock", "secret_coffee_until_unix", secret_coffee_until_unix)
    cfg.save(SETTINGS_FILE)
    ProjectSettings.set_setting(SETTINGS_KEY, selected_backend)
    _apply_engine_options()
    _apply_frame_enhancement_settings()
    _apply_shell_runtime_settings()
    if diagnostic_session != null:
        diagnostic_session.apply_preference(diagnostic_profile, player, selected_backend)
        diagnostic_session.set_game_active(game_running)
    _sync_debug_console_state()
    dirty_settings = false
    if save_button != null:
        save_button.disabled = true
        _sync_pill_button_content_state(save_button)

func _save_game_virtual_input_mode() -> void:
    var cfg := ConfigFile.new()
    cfg.load(SETTINGS_FILE)
    cfg.set_value("input", "virtual_control_mode", game_virtual_input_mode)
    cfg.save(SETTINGS_FILE)

func _current_settings_snapshot() -> Dictionary:
    return {
        "language": language_mode,
        "style": style_mode,
        "ios_ui_scale_mode": ios_ui_scale_mode,
        "game_virtual_menu_enabled": game_virtual_menu_enabled,
        "game_virtual_keyboard_opacity": game_virtual_keyboard_opacity,
        "backend": selected_backend,
        "upscale_algorithm": upscale_algorithm,
        "output_resolution": output_resolution,
        "surface_mode": render_surface_mode,
        "frame_enhancement_enabled": frame_enhancement_enabled,
        "frame_enhancement_kind": frame_enhancement_kind,
        "frame_enhancement_mode": frame_enhancement_mode,
        "frame_enhancement_custom_chain": frame_enhancement_custom_chain.duplicate(),
        "diagnostic_profile": diagnostic_profile,
        "debug_overlay_mode": debug_overlay_mode,
        "fps_limit_enabled": frame_limit_enabled,
        "target_fps": target_fps,
        "force_landscape": lock_landscape,
        "plugin_load_mode": plugin_load_mode,
        "mock_enabled": mock_enabled,
        "error_dialog_logs": error_dialog_logs,
        "text_translation_model_path": text_translation_model_path,
    }

func _settings_snapshots_equal(left: Dictionary, right: Dictionary) -> bool:
    for key in SETTINGS_DRAFT_KEYS:
        if left.get(key) != right.get(key):
            return false
    return true

func _begin_settings_edit() -> void:
    settings_draft = _current_settings_snapshot()
    dirty_settings = false
    _sync_save_button_enabled()

func _discard_settings_draft() -> void:
    settings_draft.clear()
    dirty_settings = false
    _sync_save_button_enabled()

func _should_confirm_settings_navigation() -> bool:
    return shell_route == "settings" and dirty_settings

func _request_settings_navigation(destination: Callable) -> bool:
    if not _should_confirm_settings_navigation():
        return false
    _show_unsaved_settings_prompt(destination)
    return true

func _show_unsaved_settings_prompt(destination: Callable) -> void:
    var dialog := _modal_dialog(Vector2(560, 270), 0.46)
    var box := _modal_stack(
        dialog,
        _t("settings.unsaved_title"),
        ICON_SAVE
    )

    var header := box.get_child(0) as HBoxContainer
    var close := Button.new()
    close.name = "UnsavedSettingsCloseButton"
    close.text = "×"
    close.tooltip_text = _t("settings.unsaved_close")
    close.accessibility_name = close.tooltip_text
    close.custom_minimum_size = Vector2(38, 38)
    close.focus_mode = Control.FOCUS_ALL
    close.add_theme_font_size_override("font_size", 22)
    ui_widgets.toolbar_button(close)
    close.pressed.connect(_dismiss_modal)
    header.add_child(close)

    var body := Label.new()
    body.text = _t("settings.unsaved_body")
    body.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
    body.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    body.size_flags_vertical = Control.SIZE_EXPAND_FILL
    body.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
    body.add_theme_font_size_override("font_size", 16)
    body.add_theme_color_override("font_color", ui_tokens.text_secondary)
    box.add_child(body)

    var buttons := HBoxContainer.new()
    buttons.add_theme_constant_override("separation", 12)
    buttons.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    buttons.alignment = BoxContainer.ALIGNMENT_END
    buttons.custom_minimum_size = Vector2(0, 48)
    box.add_child(buttons)

    var discard := Button.new()
    discard.name = "UnsavedSettingsDiscardButton"
    discard.text = _t("settings.unsaved_discard")
    discard.custom_minimum_size = Vector2(132, 48)
    ui_widgets.secondary_button(discard)
    discard.pressed.connect(func():
        _discard_settings_draft()
        _dismiss_modal(destination)
    )
    buttons.add_child(discard)

    var save := _pill_button(_t("settings.save"), ICON_SAVE)
    save.name = "UnsavedSettingsSaveButton"
    save.custom_minimum_size = Vector2(132, 48)
    save.pressed.connect(func():
        _save_settings_draft()
        _dismiss_modal(destination)
    )
    buttons.add_child(save)

func _sync_save_button_enabled() -> void:
    if save_button != null and is_instance_valid(save_button):
        save_button.disabled = not dirty_settings
        _sync_pill_button_content_state(save_button)

func _refresh_settings_dirty() -> void:
    if settings_draft.is_empty():
        dirty_settings = false
    else:
        dirty_settings = not _settings_snapshots_equal(settings_draft, _current_settings_snapshot())
    _sync_save_button_enabled()

func _set_settings_draft_value(key: String, value) -> void:
    if settings_draft.is_empty():
        settings_draft = _current_settings_snapshot()
    settings_draft[key] = value
    _refresh_settings_dirty()

func _settings_draft_string(key: String, fallback: String) -> String:
    return String(settings_draft.get(key, fallback))

func _settings_draft_bool(key: String, fallback: bool) -> bool:
    return bool(settings_draft.get(key, fallback))

func _settings_draft_int(key: String, fallback: int) -> int:
    return int(settings_draft.get(key, fallback))

func _settings_draft_float(key: String, fallback: float) -> float:
    return float(settings_draft.get(key, fallback))

func _settings_draft_custom_chain() -> PackedStringArray:
    return _normalize_frame_enhancement_custom_chain(settings_draft.get(
        "frame_enhancement_custom_chain",
        frame_enhancement_custom_chain
    ))

func _apply_settings_snapshot(snapshot: Dictionary) -> void:
    language_mode = _normalize_language_mode(String(snapshot.get("language", language_mode)))
    _apply_language_mode()
    style_mode = _normalize_style_mode(String(snapshot.get("style", style_mode)))
    _apply_style_mode()
    ios_ui_scale_mode = String(snapshot.get("ios_ui_scale_mode", ios_ui_scale_mode))
    if not ios_ui_scale_mode in IOS_UI_SCALE_MODES:
        ios_ui_scale_mode = "comfortable"
    game_virtual_menu_enabled = bool(snapshot.get(
        "game_virtual_menu_enabled", game_virtual_menu_enabled
    ))
    game_virtual_keyboard_opacity = _normalize_game_virtual_keyboard_opacity(
        float(snapshot.get(
            "game_virtual_keyboard_opacity",
            game_virtual_keyboard_opacity
        ))
    )
    _apply_game_virtual_control_preferences()

    selected_backend = _normalize_backend_name(String(snapshot.get("backend", selected_backend)))
    if not selected_backend in BACKENDS:
        selected_backend = "Godot Native"

    upscale_algorithm = String(snapshot.get("upscale_algorithm", upscale_algorithm))
    if not upscale_algorithm in ["smooth", "nearest", "linear", "bicubic", "lanczos"]:
        upscale_algorithm = "bicubic"
    _apply_upscale_algorithm()
    output_resolution = _normalize_output_resolution(String(snapshot.get(
        "output_resolution",
        output_resolution
    )))

    var next_surface_mode := String(snapshot.get("surface_mode", render_surface_mode))
    render_surface_mode = next_surface_mode if next_surface_mode in [RENDER_SURFACE_MODE_GAME, RENDER_SURFACE_MODE_DISPLAY] else _default_render_surface_mode()
    frame_enhancement_kind = _normalize_frame_enhancement_kind(String(snapshot.get(
        "frame_enhancement_kind",
        "preset" if bool(snapshot.get(
            "frame_enhancement_enabled", frame_enhancement_enabled
        )) else "off"
    )))
    frame_enhancement_enabled = frame_enhancement_kind != "off"
    frame_enhancement_mode = _normalize_frame_enhancement_mode(String(snapshot.get(
        "frame_enhancement_mode",
        frame_enhancement_mode
    )))
    frame_enhancement_custom_chain = _normalize_frame_enhancement_custom_chain(
        snapshot.get("frame_enhancement_custom_chain", frame_enhancement_custom_chain)
    )
    diagnostic_profile = String(snapshot.get("diagnostic_profile", diagnostic_profile))
    if not diagnostic_profile in DIAGNOSTIC_PROFILES:
        diagnostic_profile = "baseline" if OS.is_debug_build() else "off"
    debug_overlay_mode = String(snapshot.get("debug_overlay_mode", debug_overlay_mode))
    if not debug_overlay_mode in DEBUG_OVERLAY_MODES:
        debug_overlay_mode = "off"
    show_perf_monitor = debug_overlay_mode != "off"
    _set_perf_visible(game_running and show_perf_monitor)
    frame_limit_enabled = bool(snapshot.get("fps_limit_enabled", frame_limit_enabled))
    target_fps = int(snapshot.get("target_fps", target_fps))
    lock_landscape = bool(snapshot.get("force_landscape", lock_landscape))
    plugin_load_mode = String(snapshot.get("plugin_load_mode", plugin_load_mode))
    if not plugin_load_mode in ["krkrsdl3", "aether_all"]:
        plugin_load_mode = "krkrsdl3"
    mock_enabled = bool(snapshot.get("mock_enabled", mock_enabled))
    error_dialog_logs = bool(snapshot.get("error_dialog_logs", error_dialog_logs))
    text_translation_model_path = String(snapshot.get(
        "text_translation_model_path", text_translation_model_path
    ))

func _save_settings_draft() -> void:
    if settings_draft.is_empty() or not dirty_settings:
        return

    var previous_language := language_mode
    var previous_active_language := active_language
    var previous_style := style_mode
    var previous_ios_ui_scale_mode := ios_ui_scale_mode
    var previous_backend := selected_backend
    var previous_surface_mode := render_surface_mode
    var previous_output_resolution := output_resolution
    var snapshot := settings_draft.duplicate()

    _apply_settings_snapshot(snapshot)
    _save_shell_settings()
    settings_draft.clear()

    if previous_ios_ui_scale_mode != ios_ui_scale_mode:
        _apply_global_dpi_scale()

    if previous_backend != selected_backend:
        var backend_index := BACKENDS.find(selected_backend)
        if backend_index >= 0 and backend != null and is_instance_valid(backend):
            backend.select(backend_index)
        if player != null and player.is_initialized():
            if game_running:
                restart_notice.text = "Restart current game session to apply renderer."
                _append_log("Renderer change queued: %s" % selected_backend)
            else:
                _apply_backend(true)

    if (previous_surface_mode != render_surface_mode or
            previous_output_resolution != output_resolution) and game_running:
        _sync_player_surface_size(true)

    var language_changed := previous_language != language_mode or previous_active_language != active_language
    var style_changed := previous_style != style_mode
    if style_changed:
        call_deferred("_rebuild_shell_views_after_style_change")
    elif language_changed:
        _refresh_language_texts()
        if settings_view != null and settings_view.visible:
            call_deferred("_rebuild_settings_view")
        if detail_view != null and detail_view.visible and not selected_game.is_empty():
            call_deferred("_show_detail", selected_game)
        _refresh_games()

func _mark_settings_dirty() -> void:
    _refresh_settings_dirty()

func _apply_engine_options() -> void:
    if player == null:
        return
    if not player.is_initialized():
        return
    var effective_plugin_load_mode := _runtime_string("AETHERKIRI_PLUGIN_LOAD_MODE", plugin_load_mode)
    if not effective_plugin_load_mode in ["krkrsdl3", "aether_all"]:
        effective_plugin_load_mode = "krkrsdl3"
    var effective_diagnostic_profile := DiagnosticSession.requested_profile() if DiagnosticSession.external_request_present() else diagnostic_profile
    _apply_diagnostic_profile_environment(effective_diagnostic_profile)
    var effective_plugin_trace := plugin_trace or effective_diagnostic_profile in ["plugin", "full"] or _runtime_flag("AETHERKIRI_PLUGIN_TRACE", false)
    var effective_trace_log := trace_log or effective_diagnostic_profile == "full" or _runtime_flag("AETHERKIRI_TRACE_LOG", false)
    var effective_input_trace := input_trace_enabled or effective_diagnostic_profile in ["input", "full"]
    player.set_engine_option("fps_limit", str(target_fps) if frame_limit_enabled else "0")
    player.set_engine_option("plugin_load_mode", effective_plugin_load_mode)
    player.set_engine_option("plugin_trace", "1" if effective_plugin_trace else "0")
    player.set_engine_option("mock_enabled", "1" if mock_enabled else "0")
    player.set_engine_option("console_log_file", "1" if console_log_file else "0")
    player.set_engine_option("trace_log", "1" if effective_trace_log else "0")
    player.set_engine_option("input_trace", "1" if effective_input_trace else "0")
    if player.has_method("is_text_translation_available") and player.is_text_translation_available():
        _restore_native_translation_model_access()
        player.set_engine_option(
            "text_translation.model_path", text_translation_model_path
        )
        player.set_engine_option(
            "text_translation.target_language", active_language
        )
        player.set_engine_option(
            "text_translation.enabled",
            "0" if text_translation_model_path.is_empty() else "1"
        )
    # A synchronous Artemis resource load can make the next Godot frame carry
    # hundreds of milliseconds. Keep visual evolution incremental so authored
    # E-mote expressions and fades cannot collapse into a one-frame flash.
    player.set_engine_option("artemis.max_visual_delta_ms", "34")
    var effective_export_scripts := export_scripts or _runtime_flag("AETHERKIRI_EXPORT_SCRIPTS", false)
    player.set_engine_option("export_scripts", "1" if effective_export_scripts else "0")
    if not runtime_default_font_path.is_empty():
        player.set_engine_option("default_font", runtime_default_font_path)
    if not runtime_font_dir_path.is_empty():
        player.set_engine_option("font_dir", runtime_font_dir_path)
    player.set_engine_option("error_dialog_logs", "1" if error_dialog_logs else "0")
    var onscripter_encoding := OS.get_environment("AETHERKIRI_ONS_ENCODING").strip_edges()
    if not onscripter_encoding.is_empty():
        player.set_engine_option("onscripter_encoding", onscripter_encoding)

func _apply_frame_enhancement_settings() -> void:
    if player == null or not player.has_method("set_frame_enhancement_enabled"):
        return
    if player.has_method("set_frame_native_output_enabled"):
        player.set_frame_native_output_enabled(output_resolution == "original")
    if player.has_method("set_frame_enhancement_custom_chain"):
        player.set_frame_enhancement_custom_chain(frame_enhancement_custom_chain)
    player.set_frame_enhancement_mode(
        "custom" if frame_enhancement_kind == "custom" else frame_enhancement_mode
    )
    player.set_frame_enhancement_enabled(frame_enhancement_kind != "off")

func _frame_enhancement_description() -> String:
    if player == null or not player.has_method("is_frame_enhancement_available"):
        return _t("settings.frame_enhancement_unavailable_desc")
    if not bool(player.is_frame_enhancement_available()):
        return _t("settings.frame_enhancement_unavailable_desc")
    return _t("settings.frame_enhancement_desc")

func _apply_diagnostic_profile_environment(profile_name: String) -> void:
    var catalog := DiagnosticSession.profile_catalog()
    var profile_flags := {}
    var protected_names := ["AETHERKIRI_DIAGNOSTICS", "AETHERKIRI_FRAME_SPIKE_MS", "AETHERKIRI_ENGINE_TICK_SPIKE_MS"]
    for catalog_name in catalog:
        var definition: Dictionary = catalog[catalog_name]
        var names := PackedStringArray()
        for name in (definition.get("env", {}) as Dictionary).keys():
            if not String(name) in protected_names:
                names.append(String(name))
        profile_flags[catalog_name] = names
    var managed := PackedStringArray()
    for values in profile_flags.values():
        for value in values:
            if not managed.has(String(value)):
                managed.append(String(value))
    if diagnostic_env_originals.is_empty():
        for name in managed:
            diagnostic_env_originals[name] = OS.get_environment(name)
    for name in managed:
        var original := String(diagnostic_env_originals.get(name, ""))
        if original.is_empty():
            OS.unset_environment(name)
        else:
            OS.set_environment(name, original)
    for name in profile_flags.get(profile_name, []):
        OS.set_environment(String(name), "1")

func _set_advanced_tool(option: String, enabled: bool) -> void:
    if option == "plugin_trace":
        plugin_trace = enabled
    elif option == "trace_log":
        trace_log = enabled
    elif option == "console_log_file":
        console_log_file = enabled
    elif option == "export_scripts":
        export_scripts = enabled
    else:
        return
    if enabled and option in ["plugin_trace", "trace_log"]:
        advanced_expiry_msec[option] = Time.get_ticks_msec() + ADVANCED_TRACE_TIMEOUT_MS
    else:
        advanced_expiry_msec.erase(option)
    _apply_engine_options()
    if diagnostic_session != null and diagnostic_session.active:
        diagnostic_session.record("godot", "diagnostics", "warning" if enabled else "info", "advanced_tool_changed", 0, {
            "option": option,
            "enabled": enabled,
            "temporary": true,
        })

func _update_advanced_tool_timeouts() -> void:
    if advanced_expiry_msec.is_empty():
        return
    var now := Time.get_ticks_msec()
    for option in advanced_expiry_msec.keys().duplicate():
        if now >= int(advanced_expiry_msec[option]):
            _set_advanced_tool(String(option), false)

func _advanced_snapshot() -> Dictionary:
    var now := Time.get_ticks_msec()
    var result := {
        "plugin_trace": plugin_trace,
        "trace_log": trace_log,
        "console_log_file": console_log_file,
        "export_scripts": export_scripts,
    }
    for option in advanced_expiry_msec:
        result["%s_remaining_sec" % option] = maxi(0, int(ceil(float(int(advanced_expiry_msec[option]) - now) / 1000.0)))
    return result

func _apply_shell_runtime_settings() -> void:
    if OS.get_name() == "iOS" or OS.get_name() == "Android":
        var orientation := DisplayServer.SCREEN_LANDSCAPE if lock_landscape else DisplayServer.SCREEN_SENSOR
        DisplayServer.screen_set_orientation(orientation)

func _game_runtime_restore_orientation(previous_screen_size: Vector2i, fallback: int) -> int:
    if lock_landscape:
        return DisplayServer.SCREEN_LANDSCAPE
    if previous_screen_size.y > previous_screen_size.x:
        return DisplayServer.SCREEN_PORTRAIT
    if previous_screen_size.x > previous_screen_size.y:
        return DisplayServer.SCREEN_LANDSCAPE
    return fallback

func _set_game_runtime_orientation(active: bool) -> void:
    if OS.get_name() != "iOS" and OS.get_name() != "Android":
        return
    if active:
        if OS.get_name() == "Android" and not game_runtime_shell_orientation_captured:
            # Android reports the requested orientation here rather than the
            # physical display rotation, so retain the current screen size as
            # the authoritative portrait/landscape state to restore on exit.
            game_runtime_shell_orientation = DisplayServer.screen_get_orientation()
            game_runtime_shell_screen_size = DisplayServer.screen_get_size()
            game_runtime_shell_orientation_captured = true
        DisplayServer.screen_set_orientation(DisplayServer.SCREEN_SENSOR_LANDSCAPE)
    else:
        if OS.get_name() != "Android" or not game_runtime_shell_orientation_captured:
            _apply_shell_runtime_settings()
            return
        var orientation := _game_runtime_restore_orientation(
            game_runtime_shell_screen_size,
            game_runtime_shell_orientation
        )
        game_runtime_shell_orientation_captured = false
        game_runtime_shell_screen_size = Vector2i.ZERO
        DisplayServer.screen_set_orientation(orientation)

func _scaled_display_safe_rect(
    viewport_size: Vector2,
    screen_size: Vector2,
    display_safe_rect: Rect2
) -> Rect2:
    var full_rect := Rect2(Vector2.ZERO, viewport_size)
    if (
        viewport_size.x <= 0.0
        or viewport_size.y <= 0.0
        or screen_size.x <= 0.0
        or screen_size.y <= 0.0
        or display_safe_rect.size.x <= 0.0
        or display_safe_rect.size.y <= 0.0
    ):
        return full_rect
    var scale := Vector2(
        viewport_size.x / screen_size.x,
        viewport_size.y / screen_size.y
    )
    var position := Vector2(
        display_safe_rect.position.x * scale.x,
        display_safe_rect.position.y * scale.y
    )
    var safe_size := Vector2(
        display_safe_rect.size.x * scale.x,
        display_safe_rect.size.y * scale.y
    )
    position.x = clampf(position.x, 0.0, viewport_size.x)
    position.y = clampf(position.y, 0.0, viewport_size.y)
    safe_size.x = clampf(safe_size.x, 0.0, viewport_size.x - position.x)
    safe_size.y = clampf(safe_size.y, 0.0, viewport_size.y - position.y)
    return Rect2(position, safe_size)

func _ui_safe_rect(viewport_size: Vector2) -> Rect2:
    var full_rect := Rect2(Vector2.ZERO, viewport_size)
    if OS.get_name() != "iOS":
        return full_rect
    var screen_size := Vector2(DisplayServer.screen_get_size())
    var display_safe_rect := Rect2(DisplayServer.get_display_safe_area())
    return _scaled_display_safe_rect(viewport_size, screen_size, display_safe_rect)

func _rect_inside(relative_to: Rect2, left: float, top: float, right: float, bottom: float) -> Rect2:
    var position := relative_to.position + Vector2(
        relative_to.size.x * left,
        relative_to.size.y * top
    )
    var end := relative_to.position + Vector2(
        relative_to.size.x * right,
        relative_to.size.y * bottom
    )
    return Rect2(position, (end - position).max(Vector2.ONE))

func _set_control_rect(control: Control, rect: Rect2) -> void:
    if control == null:
        return
    control.set_anchors_preset(Control.PRESET_TOP_LEFT)
    control.position = rect.position
    control.size = rect.size

func _mark_centered_safe_dialog(dialog: Control, preferred_size: Vector2) -> void:
    dialog.set_meta("aether_safe_dialog_kind", "centered")
    dialog.set_meta("aether_safe_dialog_size", preferred_size)

func _mark_legal_safe_dialog(dialog: Control, first_use: bool, statement: bool) -> void:
    dialog.set_meta("aether_safe_dialog_kind", "store" if statement else "privacy")
    dialog.set_meta("aether_safe_dialog_first_use", first_use)

func _layout_safe_dialog(dialog: Control, safe_rect: Rect2) -> void:
    var kind := String(dialog.get_meta("aether_safe_dialog_kind", ""))
    if kind.is_empty():
        return
    if kind == "centered":
        var preferred := Vector2(dialog.get_meta("aether_safe_dialog_size", Vector2(560, 320)))
        var dialog_size := Vector2(
            minf(preferred.x, maxf(280.0, safe_rect.size.x - 32.0)),
            minf(preferred.y, maxf(180.0, safe_rect.size.y - 32.0))
        )
        _set_control_rect(dialog, Rect2(safe_rect.get_center() - dialog_size * 0.5, dialog_size))
        return
    var compact := safe_rect.size.y > safe_rect.size.x or safe_rect.size.x < 700.0
    if kind == "declined":
        _set_control_rect(
            dialog,
            _rect_inside(safe_rect, 0.06, 0.14, 0.94, 0.86) if compact else _rect_inside(safe_rect, 0.18, 0.24, 0.82, 0.76)
        )
        return
    var first_use := bool(dialog.get_meta("aether_safe_dialog_first_use", false))
    if compact and not first_use:
        _set_control_rect(dialog, _rect_inside(safe_rect, 0.06, 0.10, 0.94, 0.90))
    elif not first_use and kind == "store":
        _set_control_rect(dialog, _rect_inside(safe_rect, 0.12, 0.10, 0.88, 0.90))
    elif not first_use:
        _set_control_rect(dialog, _rect_inside(safe_rect, 0.10, 0.09, 0.90, 0.91))
    elif kind == "store":
        _set_control_rect(
            dialog,
            _rect_inside(safe_rect, 0.035, 0.025, 0.965, 0.975) if compact else _rect_inside(safe_rect, 0.08, 0.06, 0.92, 0.94)
        )
    else:
        _set_control_rect(
            dialog,
            _rect_inside(safe_rect, 0.035, 0.025, 0.965, 0.975) if compact else _rect_inside(safe_rect, 0.06, 0.04, 0.94, 0.96)
        )

func _layout_modal_safe_area(safe_rect: Rect2) -> void:
    if modal_layer == null:
        return
    for child in modal_layer.get_children():
        if child is Control and child.has_meta("aether_safe_dialog_kind"):
            _layout_safe_dialog(child as Control, safe_rect)

func _layout_video_safe_area(window_size: Vector2, safe_rect: Rect2) -> void:
    var safe_end := safe_rect.position + safe_rect.size
    var right_inset := maxf(0.0, window_size.x - safe_end.x)
    var bottom_inset := maxf(0.0, window_size.y - safe_end.y)
    var layout_spec := _apply_video_controls_layout(safe_rect.size)
    var top_height := float(layout_spec["top_height"])
    var panel_height := float(layout_spec["panel_height"])
    if is_instance_valid(video_top_bar):
        video_top_bar.offset_left = safe_rect.position.x
        video_top_bar.offset_top = safe_rect.position.y
        video_top_bar.offset_right = -right_inset
        video_top_bar.offset_bottom = safe_rect.position.y + top_height
    if is_instance_valid(video_controls):
        _set_control_rect(
            video_controls,
            _video_controls_panel_rect(window_size, safe_rect, panel_height)
        )
        if is_instance_valid(video_controls_margin):
            var vertical_margin := int(layout_spec["vertical_margin"])
            video_controls_margin.add_theme_constant_override(
                "margin_bottom",
                vertical_margin + ceili(bottom_inset)
            )
    if is_instance_valid(video_subtitle_label):
        video_subtitle_label.anchor_left = 0.0
        video_subtitle_label.anchor_right = 0.0
        video_subtitle_label.offset_left = safe_rect.position.x + safe_rect.size.x * 0.08
        video_subtitle_label.offset_right = safe_end.x - safe_rect.size.x * 0.08
        _layout_video_subtitles_for_controls(video_controls_visible)
    if is_instance_valid(video_seek_feedback):
        var feedback_size := Vector2(300.0, 84.0)
        _set_control_rect(
            video_seek_feedback,
            Rect2(safe_rect.get_center() - feedback_size * 0.5, feedback_size)
        )
    if is_instance_valid(restart_notice):
        restart_notice.position = safe_rect.position + Vector2(24, 44)

func _video_controls_panel_rect(
    window_size: Vector2,
    safe_rect: Rect2,
    panel_height: float
) -> Rect2:
    var safe_end := safe_rect.position + safe_rect.size
    var bottom_inset := maxf(0.0, window_size.y - safe_end.y)
    return Rect2(
        Vector2(safe_rect.position.x, safe_end.y - panel_height),
        Vector2(safe_rect.size.x, panel_height + bottom_inset)
    )

func _fit_full_rects() -> void:
    var window_size := get_viewport_rect().size
    var safe_rect := _ui_safe_rect(window_size)
    anchor_left = 0.0
    anchor_top = 0.0
    anchor_right = 0.0
    anchor_bottom = 0.0
    position = Vector2.ZERO
    size = window_size
    var controls: Array[Control] = [bg_rect, game_view, video_view, shell_root, home_view, settings_view, detail_view, detail_scroll, modal_layer]
    for control in controls:
        if control == null:
            continue
        control.set_anchors_preset(Control.PRESET_FULL_RECT)
        control.offset_left = 0.0
        control.offset_top = 0.0
        control.offset_right = 0.0
        control.offset_bottom = 0.0
    _set_control_rect(shell_root, safe_rect)
    if is_instance_valid(loading_center):
        loading_center.set_anchors_preset(Control.PRESET_FULL_RECT)
        loading_center.offset_left = safe_rect.position.x
        loading_center.offset_top = safe_rect.position.y
        loading_center.offset_right = -(window_size.x - safe_rect.position.x - safe_rect.size.x)
        loading_center.offset_bottom = -(window_size.y - safe_rect.position.y - safe_rect.size.y)
    _layout_modal_safe_area(safe_rect)
    _layout_video_safe_area(window_size, safe_rect)
    _layout_game_viewport(window_size)
    if game_virtual_controls != null:
        game_virtual_controls.layout(window_size, safe_rect)
    _layout_shell(safe_rect.size)
    _layout_shell_safe_area_fills(window_size, safe_rect)
    var compact_shell := AetherDisplayScale.use_compact_shell(safe_rect.size)
    var shell_size := Vector2(
        safe_rect.size.x if compact_shell else safe_rect.size.x - shell_sidebar_layout_width,
        safe_rect.size.y - (ui_tokens.TOOLBAR_HEIGHT if compact_shell else 0.0)
    )
    _layout_home_view(shell_size)
    _layout_perf_overlay(safe_rect)

func _sidebar_backdrop_rect(window_size: Vector2, safe_rect: Rect2, width: float) -> Rect2:
    return Rect2(
        Vector2(-safe_rect.position.x, -safe_rect.position.y),
        Vector2(safe_rect.position.x + width, window_size.y)
    )

func _layout_shell_safe_area_fills(window_size: Vector2, safe_rect: Rect2) -> void:
    if not is_instance_valid(shell_safe_top_fill):
        return
    var top_inset := maxf(0.0, safe_rect.position.y)
    var compact_shell := AetherDisplayScale.use_compact_shell(safe_rect.size)
    shell_safe_top_fill.visible = OS.get_name() == "iOS" and compact_shell and top_inset > 0.0
    shell_safe_top_fill.color = ui_tokens.sidebar_material
    if shell_safe_top_fill.visible:
        # shell_root starts at the safe-area origin. Extending this
        # non-interactive fill upward colors the status-bar region without
        # moving app controls into the sensor housing.
        shell_safe_top_fill.position = Vector2(-safe_rect.position.x, -top_inset)
        shell_safe_top_fill.size = Vector2(window_size.x, top_inset + 1.0)

    if is_instance_valid(shell_sidebar_backdrop):
        shell_sidebar_backdrop.visible = OS.get_name() == "iOS" and not compact_shell
        shell_sidebar_backdrop.add_theme_stylebox_override("panel", ui_tokens.sidebar_panel())
        if shell_sidebar_backdrop.visible:
            _set_control_rect(
                shell_sidebar_backdrop,
                _sidebar_backdrop_rect(window_size, safe_rect, shell_sidebar_layout_width)
            )

func _layout_perf_overlay(safe_rect: Rect2) -> void:
    if perf_panel == null:
        return
    var horizontal_margin := 16.0
    perf_panel.position = safe_rect.position + Vector2(horizontal_margin, 12.0)
    var translation_height := 36.0 if _translation_model_configured() else 0.0
    perf_panel.size = Vector2(
        maxf(240.0, safe_rect.size.x - horizontal_margin * 2.0),
        (136.0 if debug_overlay_mode == "detail" else 104.0) + translation_height
    )

func _set_perf_visible(visible: bool) -> void:
    if perf_panel != null:
        perf_panel.visible = visible

func _layout_game_viewport(window_size: Vector2) -> void:
    if viewport == null:
        return
    viewport.anchor_left = 0.0
    viewport.anchor_top = 0.0
    viewport.anchor_right = 0.0
    viewport.anchor_bottom = 0.0
    viewport.offset_left = 0.0
    viewport.offset_top = 0.0
    viewport.offset_right = 0.0
    viewport.offset_bottom = 0.0

    var tex_size := Vector2(
        max(1.0, float(last_texture_size.x)),
        max(1.0, float(last_texture_size.y))
    )
    if viewport.texture != null:
        tex_size = Vector2(
            max(1.0, float(viewport.texture.get_width())),
            max(1.0, float(viewport.texture.get_height()))
        )

    var scale := minf(window_size.x / tex_size.x, window_size.y / tex_size.y)
    scale = minf(scale, _max_game_view_scale())
    if scale <= 0.0:
        scale = 1.0
    var draw_size := Vector2(
        floor(tex_size.x * scale),
        floor(tex_size.y * scale)
    )
    viewport.position = ((window_size - draw_size) * 0.5).floor()
    viewport.size = draw_size
    viewport.custom_minimum_size = draw_size

func _max_game_view_scale() -> float:
    var value := OS.get_environment("AETHERKIRI_GAME_VIEW_MAX_SCALE").strip_edges()
    if not value.is_empty():
        return clampf(value.to_float(), 0.25, 8.0)
    return 8.0

func _opaque_frame_material() -> ShaderMaterial:
    if opaque_frame_shader == null:
        opaque_frame_shader = Shader.new()
        opaque_frame_shader.code = """
shader_type canvas_item;

void fragment() {
    vec4 tex = texture(TEXTURE, UV);
    COLOR = vec4(tex.rgb, 1.0);
}
"""
    var material := ShaderMaterial.new()
    material.shader = opaque_frame_shader
    return material

func _resampling_frame_material(kind: String) -> ShaderMaterial:
    if kind == "bicubic":
        if bicubic_frame_material != null:
            return bicubic_frame_material
        bicubic_frame_shader = Shader.new()
        bicubic_frame_shader.code = """
shader_type canvas_item;

vec4 cubic_weights(float v) {
    float v2 = v * v;
    float v3 = v2 * v;
    return vec4(
        -0.5 * v3 + v2 - 0.5 * v,
        1.5 * v3 - 2.5 * v2 + 1.0,
        -1.5 * v3 + 2.0 * v2 + 0.5 * v,
        0.5 * v3 - 0.5 * v2
    );
}

void fragment() {
    vec2 texel = TEXTURE_PIXEL_SIZE;
    vec2 coord = UV / texel - vec2(0.5);
    vec2 base = floor(coord);
    vec2 fraction = fract(coord);
    vec4 weights_x = cubic_weights(fraction.x);
    vec4 weights_y = cubic_weights(fraction.y);
    vec4 color = vec4(0.0);

    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            vec2 sample_uv = (base + vec2(float(x - 1), float(y - 1)) + vec2(0.5)) * texel;
            sample_uv = clamp(sample_uv, vec2(0.0), vec2(1.0));
            color += texture(TEXTURE, sample_uv) * weights_x[x] * weights_y[y];
        }
    }
    COLOR = vec4(clamp(color.rgb, vec3(0.0), vec3(1.0)), 1.0);
}
"""
        bicubic_frame_material = ShaderMaterial.new()
        bicubic_frame_material.shader = bicubic_frame_shader
        return bicubic_frame_material

    if kind == "lanczos":
        if lanczos_frame_material != null:
            return lanczos_frame_material
        lanczos_frame_shader = Shader.new()
        lanczos_frame_shader.code = """
shader_type canvas_item;

const float KERNEL_PI = 3.14159265359;

float sinc(float value) {
    float distance = abs(value);
    if (distance < 0.0001) {
        return 1.0;
    }
    return sin(KERNEL_PI * distance) / (KERNEL_PI * distance);
}

float lanczos_weight(float value) {
    float distance = abs(value);
    if (distance >= 3.0) {
        return 0.0;
    }
    return sinc(distance) * sinc(distance / 3.0);
}

void fragment() {
    vec2 texel = TEXTURE_PIXEL_SIZE;
    vec2 coord = UV / texel - vec2(0.5);
    vec2 base = floor(coord);
    vec2 fraction = fract(coord);
    vec4 color = vec4(0.0);
    float weight_sum = 0.0;

    for (int y = -2; y <= 3; y++) {
        for (int x = -2; x <= 3; x++) {
            float weight = lanczos_weight(float(x) - fraction.x) * lanczos_weight(float(y) - fraction.y);
            vec2 sample_uv = (base + vec2(float(x), float(y)) + vec2(0.5)) * texel;
            sample_uv = clamp(sample_uv, vec2(0.0), vec2(1.0));
            color += texture(TEXTURE, sample_uv) * weight;
            weight_sum += weight;
        }
    }
    color /= max(weight_sum, 0.0001);
    COLOR = vec4(clamp(color.rgb, vec3(0.0), vec3(1.0)), 1.0);
}
"""
        lanczos_frame_material = ShaderMaterial.new()
        lanczos_frame_material.shader = lanczos_frame_shader
        return lanczos_frame_material

    return _opaque_frame_material()

func _apply_upscale_algorithm() -> void:
    if viewport == null:
        return
    match upscale_algorithm:
        "nearest":
            viewport.texture_filter = CanvasItem.TEXTURE_FILTER_NEAREST
            # The opaque presentation shader samples TEXTURE through its own
            # sampler and can retain linear filtering on some renderers. Use
            # the native TextureRect path for nearest so the CanvasItem filter
            # is the actual sampler used by the final presentation pass.
            viewport.material = null
        "linear", "smooth":
            viewport.texture_filter = CanvasItem.TEXTURE_FILTER_LINEAR
            viewport.material = _opaque_frame_material()
        "bicubic", "lanczos":
            viewport.texture_filter = CanvasItem.TEXTURE_FILTER_LINEAR
            viewport.material = _resampling_frame_material(upscale_algorithm)
        _:
            viewport.texture_filter = CanvasItem.TEXTURE_FILTER_LINEAR
            viewport.material = _opaque_frame_material()

func _set_game_background(active: bool) -> void:
    var color := color_game_bg if active else color_bg
    if bg_rect != null:
        bg_rect.color = color
    RenderingServer.set_default_clear_color(color)

func _layout_home_view(window_size: Vector2) -> void:
    if home_page_margin == null or home_header_box == null or game_list == null:
        return
    var compact := window_size.x < HOME_COMPACT_BREAKPOINT
    var phone := minf(window_size.x, window_size.y) < HOME_PHONE_BREAKPOINT
    var margin: float = 16.0 if phone else (24.0 if compact else ui_tokens.PAGE_GUTTER)
    home_page_margin.add_theme_constant_override("margin_left", int(margin))
    home_page_margin.add_theme_constant_override("margin_top", 16 if phone else (22 if compact else 28))
    home_page_margin.add_theme_constant_override("margin_right", int(margin))
    home_page_margin.add_theme_constant_override("margin_bottom", 16 if phone else (22 if compact else 28))
    home_header_box.vertical = false
    home_header_box.custom_minimum_size = Vector2(0, 62 if phone else (68 if compact else 72))
    home_header_box.add_theme_constant_override("separation", 12 if phone else (16 if compact else 24))
    home_title_label.add_theme_font_size_override("font_size", 27 if phone else 31)
    home_subtitle_label.add_theme_font_size_override("font_size", 13 if phone else 14)
    if is_instance_valid(home_search_host):
        home_search_host.custom_minimum_size.y = 60.0 if phone else 64.0
    if is_instance_valid(home_search_input):
        home_search_input.add_theme_font_size_override("font_size", 16 if phone else 17)
    home_actions.alignment = BoxContainer.ALIGNMENT_END
    home_primary_button.text = ""
    _sync_home_action_labels()
    var fab_size := 52.0 if phone else 56.0
    var fab_inset := 12.0 if phone else (16.0 if compact else 20.0)
    home_primary_button.offset_left = -fab_size - fab_inset
    home_primary_button.offset_top = -fab_size - fab_inset
    home_primary_button.offset_right = -fab_inset
    home_primary_button.offset_bottom = -fab_inset
    home_primary_button.size = Vector2(fab_size, fab_size)
    var scroll_bar_width := game_scroll.get_v_scroll_bar().get_combined_minimum_size().x
    var list_width := maxf(HOME_TILE_MIN_WIDTH, window_size.x - margin * 2.0 - scroll_bar_width)
    var gap := 10.0 if phone else (14.0 if compact else 16.0)
    var columns := AetherDisplayScale.home_columns(list_width, HOME_TILE_MIN_WIDTH, gap, compact)
    game_list.columns = columns
    game_list.add_theme_constant_override("h_separation", int(gap))
    game_list.add_theme_constant_override("v_separation", int(gap if compact else 18.0))
    game_list.custom_minimum_size = Vector2(list_width, 0)
    if video_list != null:
        video_list.columns = columns
        video_list.custom_minimum_size = Vector2(list_width, 0)
    if not home_layout_initialized or home_compact_layout != compact:
        home_compact_layout = compact
        home_layout_initialized = true
        if home_library_mode == "video" and not known_videos.is_empty():
            call_deferred("_refresh_videos")
        elif not known_games.is_empty():
            call_deferred("_refresh_games")

func _sync_home_header_text() -> void:
    if is_instance_valid(home_title_label):
        home_title_label.text = _t("nav.videos") if home_library_mode == "video" else _t("nav.library")
    _sync_home_search_box()
    _sync_home_subtitle_text()

func _current_home_search_query() -> String:
    return String(home_search_queries.get(home_library_mode, "")).strip_edges()

func _sync_home_search_box() -> void:
    if not is_instance_valid(home_search_input):
        return
    home_search_input.placeholder_text = _t(
        "search.videos_placeholder" if home_library_mode == "video" else "search.games_placeholder"
    )
    home_search_input.accessibility_name = home_search_input.placeholder_text
    var desired_text := String(home_search_queries.get(home_library_mode, ""))
    if home_search_input.text == desired_text:
        return
    home_search_syncing = true
    home_search_input.text = desired_text
    home_search_syncing = false

func _sync_home_subtitle_text() -> void:
    if not is_instance_valid(home_subtitle_label):
        return
    var total := known_videos.size() if home_library_mode == "video" else known_games.size()
    var visible := home_filtered_video_count if home_library_mode == "video" else home_filtered_game_count
    if _current_home_search_query().is_empty():
        home_subtitle_label.text = _t(
            "video.video_count" if home_library_mode == "video" else "home.game_count",
            [total]
        )
    else:
        home_subtitle_label.text = _t("search.filtered_count", [visible, total])

func _library_search_matches(values: Array, query: String) -> bool:
    var normalized_query := query.strip_edges().to_lower()
    if normalized_query.is_empty():
        return true
    normalized_query = normalized_query.replace("\t", " ").replace("\n", " ")
    var haystack_parts := PackedStringArray()
    for value in values:
        haystack_parts.append(String(value).to_lower())
    var haystack := " ".join(haystack_parts)
    for token in normalized_query.split(" ", false):
        if not haystack.contains(String(token)):
            return false
    return true

func _game_matches_home_search(game: Dictionary, query: String) -> bool:
    return _library_search_matches([
        _game_display_title(game),
        game.get("name", ""),
        game.get("developer", ""),
    ], query)

func _video_matches_home_search(video: Dictionary, query: String) -> bool:
    return _library_search_matches([
        video.get("name", ""),
        video.get("fileName", ""),
    ], query)

func _on_home_search_text_changed(value: String) -> void:
    if home_search_syncing:
        return
    home_search_queries[home_library_mode] = value
    if home_library_mode == "video":
        _rebuild_video_cards(false)
    else:
        _rebuild_game_cards(false)

func _home_search_outer_style() -> StyleBoxFlat:
    var style := ui_tokens.panel(ui_tokens.background, 29, ui_tokens.separator, 1)
    style.content_margin_left = 20
    style.content_margin_top = 4
    style.content_margin_right = 18
    style.content_margin_bottom = 4
    return style

func _sync_home_action_labels() -> void:
    if is_instance_valid(home_primary_button):
        var primary_text := _t("video.refresh") if OS.get_name() == "iOS" else _t("video.import")
        if home_library_mode == "game":
            primary_text = _t("home.refresh") if OS.get_name() == "iOS" else _t("home.import")
        home_primary_button.tooltip_text = primary_text
        home_primary_button.accessibility_name = primary_text
    if is_instance_valid(home_guide_button):
        home_guide_button.tooltip_text = _t("video.guide") if home_library_mode == "video" else _t("home.import_guide")

func _build_home_view() -> void:
    home_layout_initialized = false
    home_header_layout_initialized = false
    home_view = Control.new()
    home_view.name = "LibraryView"
    home_view.set_anchors_preset(Control.PRESET_FULL_RECT)
    shell_content.add_child(home_view)

    home_page_margin = MarginContainer.new()
    home_page_margin.set_anchors_preset(Control.PRESET_FULL_RECT)
    home_view.add_child(home_page_margin)

    var page := VBoxContainer.new()
    page.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    page.size_flags_vertical = Control.SIZE_EXPAND_FILL
    page.add_theme_constant_override("separation", 20)
    home_page_margin.add_child(page)

    home_header_box = BoxContainer.new()
    home_header_box.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    page.add_child(home_header_box)

    var title_stack := VBoxContainer.new()
    title_stack.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    title_stack.add_theme_constant_override("separation", 3)
    home_header_box.add_child(title_stack)

    home_title_label = Label.new()
    home_title_label.text = _t("nav.library")
    home_title_label.add_theme_font_override("font", DISPLAY_FONT)
    home_title_label.add_theme_font_size_override("font_size", 31)
    home_title_label.add_theme_color_override("font_color", ui_tokens.text_primary)
    title_stack.add_child(home_title_label)

    home_subtitle_label = Label.new()
    home_subtitle_label.text = _t("home.game_count", [0])
    home_subtitle_label.add_theme_font_size_override("font_size", 14)
    home_subtitle_label.add_theme_color_override("font_color", ui_tokens.text_secondary)
    title_stack.add_child(home_subtitle_label)

    home_actions = HBoxContainer.new()
    home_actions.size_flags_vertical = Control.SIZE_SHRINK_CENTER
    home_actions.add_theme_constant_override("separation", 8)
    home_header_box.add_child(home_actions)

    home_guide_button = _shell_compact_button(ICON_HELP, _t("home.import_guide"), _show_import_guide)
    home_guide_button.custom_minimum_size = Vector2(ui_tokens.CONTROL_HEIGHT, ui_tokens.CONTROL_HEIGHT)
    _apply_shell_compact_state(home_guide_button, false)
    home_actions.add_child(home_guide_button)

    home_search_host = PanelContainer.new()
    home_search_host.name = "LibrarySearchBar"
    home_search_host.custom_minimum_size = Vector2(0, 64)
    home_search_host.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    home_search_host.add_theme_stylebox_override("panel", _home_search_outer_style())
    page.add_child(home_search_host)

    var search_row := HBoxContainer.new()
    search_row.add_theme_constant_override("separation", 12)
    home_search_host.add_child(search_row)

    var search_icon := _icon_rect(ICON_SEARCH, Vector2(23, 23), ui_tokens.text_secondary)
    search_icon.size_flags_vertical = Control.SIZE_SHRINK_CENTER
    search_row.add_child(search_icon)

    home_search_input = LineEdit.new()
    home_search_input.name = "LibrarySearch"
    home_search_input.clear_button_enabled = true
    home_search_input.custom_minimum_size = Vector2(0, 44)
    home_search_input.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    home_search_input.max_length = 200
    home_search_input.add_theme_font_size_override("font_size", 17)
    home_search_input.add_theme_color_override("font_color", ui_tokens.text_primary)
    home_search_input.add_theme_color_override("font_placeholder_color", ui_tokens.text_secondary)
    home_search_input.add_theme_color_override("caret_color", ui_tokens.text_primary)
    home_search_input.caret_blink = true
    home_search_input.caret_blink_interval = 0.5
    home_search_input.add_theme_stylebox_override("normal", _empty_style())
    home_search_input.add_theme_stylebox_override("focus", _empty_style())
    home_search_input.add_theme_stylebox_override("read_only", _empty_style())
    home_search_input.text_changed.connect(_on_home_search_text_changed)
    search_row.add_child(home_search_input)
    _sync_home_search_box()

    var library_body := Control.new()
    library_body.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    library_body.size_flags_vertical = Control.SIZE_EXPAND_FILL
    page.add_child(library_body)

    game_scroll = ScrollContainer.new()
    game_scroll.set_anchors_preset(Control.PRESET_FULL_RECT)
    _configure_shell_scroll(game_scroll)
    library_body.add_child(game_scroll)

    game_list = GridContainer.new()
    game_list.columns = 1
    game_list.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    game_list.add_theme_constant_override("h_separation", 18)
    game_list.add_theme_constant_override("v_separation", 18)
    game_scroll.add_child(game_list)

    video_scroll = ScrollContainer.new()
    video_scroll.set_anchors_preset(Control.PRESET_FULL_RECT)
    _configure_shell_scroll(video_scroll)
    video_scroll.visible = false
    library_body.add_child(video_scroll)

    video_list = GridContainer.new()
    video_list.columns = 1
    video_list.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    video_list.add_theme_constant_override("h_separation", 18)
    video_list.add_theme_constant_override("v_separation", 18)
    video_scroll.add_child(video_list)

    empty_state = CenterContainer.new()
    empty_state.set_anchors_preset(Control.PRESET_FULL_RECT)
    empty_state.mouse_filter = Control.MOUSE_FILTER_IGNORE
    library_body.add_child(empty_state)

    home_primary_button = Button.new()
    var home_action_is_refresh := OS.get_name() == "iOS"
    home_primary_button.text = ""
    home_primary_button.tooltip_text = _t("home.refresh") if OS.get_name() == "iOS" else _t("home.import")
    home_primary_button.accessibility_name = home_primary_button.tooltip_text
    home_primary_button.anchor_left = 1.0
    home_primary_button.anchor_top = 1.0
    home_primary_button.anchor_right = 1.0
    home_primary_button.anchor_bottom = 1.0
    ui_widgets.floating_action_button(home_primary_button)
    _attach_centered_button_icon(
        home_primary_button,
        ICON_REFRESH if home_action_is_refresh else ICON_ADD,
        Vector2(23, 23)
    )
    home_primary_button.pressed.connect(_on_refresh_or_import)
    library_body.add_child(home_primary_button)
    home_primary_button.move_to_front()

    var empty_box := VBoxContainer.new()
    empty_box.custom_minimum_size = Vector2(280, 0)
    empty_box.add_theme_constant_override("separation", 12)
    empty_state.add_child(empty_box)

    var empty_icon := _centered_icon(ICON_LIBRARY, Vector2(44, 44), ui_tokens.text_tertiary)
    empty_icon.custom_minimum_size = Vector2(0, 56)
    empty_box.add_child(empty_icon)

    empty_title_label = Label.new()
    empty_title_label.text = _t("home.empty_title")
    empty_title_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
    empty_title_label.add_theme_font_size_override("font_size", 21)
    empty_title_label.add_theme_color_override("font_color", ui_tokens.text_primary)
    empty_box.add_child(empty_title_label)

    empty_help_label = Label.new()
    empty_help_label.text = _empty_help_text()
    empty_help_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
    empty_help_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
    empty_help_label.add_theme_font_size_override("font_size", 14)
    empty_help_label.add_theme_color_override("font_color", ui_tokens.text_secondary)
    empty_box.add_child(empty_help_label)

    empty_primary_button = _pill_button(
        _t("home.refresh") if OS.get_name() == "iOS" else _t("home.import"),
        ICON_REFRESH if OS.get_name() == "iOS" else ICON_ADD
    )
    empty_primary_button.custom_minimum_size = Vector2(164, 48)
    empty_primary_button.size_flags_horizontal = Control.SIZE_SHRINK_CENTER
    empty_primary_button.pressed.connect(_on_refresh_or_import)
    empty_box.add_child(empty_primary_button)

    video_empty_state = CenterContainer.new()
    video_empty_state.set_anchors_preset(Control.PRESET_FULL_RECT)
    video_empty_state.mouse_filter = Control.MOUSE_FILTER_IGNORE
    video_empty_state.visible = false
    library_body.add_child(video_empty_state)
    var video_empty_box := VBoxContainer.new()
    video_empty_box.custom_minimum_size = Vector2(300, 0)
    video_empty_box.add_theme_constant_override("separation", 14)
    video_empty_state.add_child(video_empty_box)
    var video_empty_icon := _centered_icon(ICON_VIDEO, Vector2(64, 64), color_accent)
    video_empty_icon.custom_minimum_size = Vector2(0, 72)
    video_empty_box.add_child(video_empty_icon)
    video_empty_title_label = Label.new()
    video_empty_title_label.text = _t("video.empty_title")
    video_empty_title_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
    video_empty_title_label.add_theme_font_size_override("font_size", 28)
    video_empty_title_label.add_theme_color_override("font_color", color_text)
    video_empty_box.add_child(video_empty_title_label)
    video_empty_help_label = Label.new()
    video_empty_help_label.text = _video_empty_help_text()
    video_empty_help_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
    video_empty_help_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
    video_empty_help_label.add_theme_font_size_override("font_size", 18)
    video_empty_help_label.add_theme_color_override("font_color", color_muted)
    video_empty_box.add_child(video_empty_help_label)
    _apply_home_library_visibility()
    call_deferred("_animate_home_header")

func _animate_home_header() -> void:
    if home_header_box != null and is_instance_valid(home_header_box):
        ui_motion.enter(home_header_box, Vector2(0, 6))

func _build_settings_view() -> void:
    settings_view = ScrollContainer.new()
    settings_view.set_anchors_preset(Control.PRESET_FULL_RECT)
    _configure_shell_scroll(settings_view)
    settings_view.visible = false
    shell_content.add_child(settings_view)

func _settings_layout_spec(available_size: Vector2, scroll_bar_width: float = 0.0) -> Dictionary:
    var measured_size := available_size
    measured_size.x = maxf(320.0, measured_size.x - scroll_bar_width)
    # The sidebar is already excluded from measured_size. Keep two columns only
    # when both columns have enough room for copy and their controls.
    var compact := measured_size.x < 1140.0
    var gutter := 20 if compact else 32
    var max_content_width := 760.0 if compact else 1120.0
    var content_width := minf(max_content_width, maxf(320.0, measured_size.x - float(gutter * 2)))
    return {
        "available_size": measured_size,
        "compact": compact,
        "gutter": gutter,
        "content_width": content_width,
        "stack_controls": content_width < 640.0,
    }

func _queue_settings_relayout_after_resize() -> void:
    if not is_instance_valid(settings_view) or not settings_view.visible:
        return
    settings_relayout_scroll_vertical = settings_view.scroll_vertical
    if settings_relayout_pending:
        return
    settings_relayout_pending = true
    # iOS updates the viewport and safe-area insets in separate layout passes.
    # Waiting for two deferred calls ensures shell_content has its landscape
    # dimensions before the settings page chooses its responsive layout.
    call_deferred("_settle_settings_relayout_after_resize")

func _settle_settings_relayout_after_resize() -> void:
    call_deferred("_apply_settings_relayout_after_resize")

func _apply_settings_relayout_after_resize() -> void:
    settings_relayout_pending = false
    if not is_instance_valid(settings_view) or not settings_view.visible:
        return
    var restore_scroll := settings_relayout_scroll_vertical
    _fit_full_rects()
    settings_animate_next = false
    _rebuild_settings_view()
    call_deferred("_restore_settings_scroll_after_relayout", restore_scroll)

func _restore_settings_scroll_after_relayout(scroll_vertical: int) -> void:
    if not is_instance_valid(settings_view) or not settings_view.visible:
        return
    var scroll_bar := settings_view.get_v_scroll_bar()
    var maximum_scroll := maxi(0, int(scroll_bar.max_value - scroll_bar.page))
    settings_view.scroll_vertical = mini(scroll_vertical, maximum_scroll)

func _rebuild_settings_view() -> void:
    for child in settings_view.get_children():
        settings_view.remove_child(child)
        child.queue_free()

    var available_size := shell_content.size
    if available_size.x <= 0.0 or available_size.y <= 0.0:
        available_size = get_viewport_rect().size
    var scroll_bar_width := settings_view.get_v_scroll_bar().get_combined_minimum_size().x
    var layout_spec := _settings_layout_spec(available_size, scroll_bar_width)
    available_size = layout_spec["available_size"]
    var compact: bool = layout_spec["compact"]
    var gutter: int = layout_spec["gutter"]
    var settings_content_width: float = layout_spec["content_width"]
    var stack_settings_controls: bool = layout_spec["stack_controls"]
    var animate_page := settings_animate_next
    settings_animate_next = false

    var margin := MarginContainer.new()
    margin.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    margin.add_theme_constant_override("margin_left", gutter)
    margin.add_theme_constant_override("margin_top", 20 if compact else 28)
    margin.add_theme_constant_override("margin_right", gutter)
    margin.add_theme_constant_override("margin_bottom", 40)
    settings_view.add_child(margin)

    var center := CenterContainer.new()
    center.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    margin.add_child(center)

    var page := VBoxContainer.new()
    page.custom_minimum_size = Vector2(settings_content_width, 0)
    page.size_flags_horizontal = Control.SIZE_SHRINK_CENTER
    page.add_theme_constant_override("separation", 24)
    center.add_child(page)

    var top := HBoxContainer.new()
    top.custom_minimum_size = Vector2(0, 72)
    top.add_theme_constant_override("separation", 14)
    page.add_child(top)

    var title_stack := VBoxContainer.new()
    title_stack.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    title_stack.add_theme_constant_override("separation", 2)
    top.add_child(title_stack)
    var title := Label.new()
    title.text = _t("settings.title")
    title.add_theme_font_override("font", DISPLAY_FONT)
    title.add_theme_font_size_override("font_size", 31)
    title.add_theme_color_override("font_color", ui_tokens.text_primary)
    title_stack.add_child(title)
    var subtitle := Label.new()
    subtitle.text = _t("home.subtitle")
    subtitle.add_theme_font_size_override("font_size", 13)
    subtitle.add_theme_color_override("font_color", ui_tokens.text_secondary)
    title_stack.add_child(subtitle)

    save_button = _icon_action_button(ICON_SAVE, _t("settings.save"), _save_settings_draft, true)
    save_button.disabled = not dirty_settings
    _sync_pill_button_content_state(save_button)
    save_button.size_flags_horizontal = Control.SIZE_SHRINK_END
    save_button.size_flags_vertical = Control.SIZE_SHRINK_CENTER
    top.add_child(save_button)

    var groups: BoxContainer = VBoxContainer.new() if compact else HBoxContainer.new()
    groups.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    groups.add_theme_constant_override("separation", 18)
    page.add_child(groups)

    var primary_column := VBoxContainer.new()
    primary_column.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    primary_column.add_theme_constant_override("separation", 20)
    groups.add_child(primary_column)
    var secondary_column := primary_column
    if not compact:
        secondary_column = VBoxContainer.new()
        secondary_column.size_flags_horizontal = Control.SIZE_EXPAND_FILL
        secondary_column.add_theme_constant_override("separation", 20)
        groups.add_child(secondary_column)

    var interface_group := _settings_group(primary_column, _t("settings.section.interface"), ICON_SETTINGS, animate_page, 0.03)
    _add_settings_row(interface_group, _settings_block(_t("settings.language"), _t("settings.language_desc"), _language_select(), stack_settings_controls))
    _add_settings_row(interface_group, _settings_block(_t("settings.style"), _t("settings.style_desc"), _style_select(), stack_settings_controls))
    if OS.get_name() == "iOS":
        _add_settings_row(interface_group, _settings_block(_t("settings.ui_scale"), _t("settings.ui_scale_desc"), _ios_ui_scale_segment(), stack_settings_controls))
    _add_settings_row(interface_group, _settings_toggle_row(
        _t("settings.virtual_control_menu"),
        _t("settings.virtual_control_menu_desc"),
        _settings_draft_bool(
            "game_virtual_menu_enabled", game_virtual_menu_enabled
        ),
        "game_virtual_menu"
    ))
    _add_settings_row(interface_group, _settings_block(
        _t("settings.keyboard_control_opacity"),
        _t("settings.keyboard_control_opacity_desc"),
        _keyboard_controls_opacity_control(),
        stack_settings_controls
    ))

    var render_group := _settings_group(primary_column, _t("settings.section.render"), ICON_PERFORMANCE, animate_page, 0.055)
    _add_settings_row(render_group, _settings_block(_t("settings.render_backend"), _t("settings.render_backend_desc"), _backend_segment(), stack_settings_controls))
    _add_settings_row(render_group, _settings_block(_t("settings.surface_mode"), _t("settings.surface_mode_desc"), _surface_mode_select(), stack_settings_controls))
    _add_settings_row(render_group, _settings_block(_t("settings.upscale"), _t("settings.upscale_desc"), _upscale_select(), stack_settings_controls))
    _add_settings_row(render_group, _settings_block(
        _t("settings.output_resolution"),
        _t("settings.output_resolution_desc"),
        _output_resolution_select(),
        stack_settings_controls
    ))
    _add_settings_row(render_group, _settings_block(
        _t("settings.frame_enhancement"),
        _frame_enhancement_description(),
        _frame_enhancement_kind_select(),
        stack_settings_controls
    ))
    var enhancement_kind := _normalize_frame_enhancement_kind(
        _settings_draft_string("frame_enhancement_kind", frame_enhancement_kind)
    )
    if enhancement_kind == "preset":
        _add_settings_row(render_group, _settings_block(
            _t("settings.frame_enhancement_mode"),
            _t("settings.frame_enhancement_mode_desc"),
            _frame_enhancement_mode_select(),
            stack_settings_controls
        ))
    elif enhancement_kind == "custom":
        _add_settings_row(render_group, _settings_block(
            _t("settings.frame_enhancement_mode"),
            _t("settings.frame_enhancement_custom_desc"),
            _frame_enhancement_custom_editor(),
            true
        ))
    _add_settings_row(render_group, _settings_toggle_row(_t("settings.fps_limit"), _t("settings.fps_limit_desc"), _settings_draft_bool("fps_limit_enabled", frame_limit_enabled), "fps_limit"))
    if _settings_draft_bool("fps_limit_enabled", frame_limit_enabled):
        _add_settings_row(render_group, _settings_fps_row())
    if OS.get_name() == "iOS" or OS.get_name() == "Android":
        _add_settings_row(render_group, _settings_toggle_row(_t("settings.landscape"), _t("settings.landscape_desc"), _settings_draft_bool("force_landscape", lock_landscape), "landscape"))

    var diagnostic_group := _settings_group(secondary_column, _t("settings.section.diagnostics"), ICON_PERFORMANCE, animate_page, 0.08)
    _add_settings_row(diagnostic_group, _settings_block(_t("settings.diagnostic_profile"), _t("settings.diagnostic_profile_desc"), _diagnostic_profile_select(), stack_settings_controls))
    _add_settings_row(diagnostic_group, _settings_block(_t("settings.debug_overlay"), _t("settings.debug_overlay_desc"), _debug_overlay_select(), stack_settings_controls))
    _add_settings_row(diagnostic_group, _settings_toggle_row(_t("settings.error_dialog_logs"), _t("settings.error_dialog_logs_desc"), _settings_draft_bool("error_dialog_logs", error_dialog_logs), "error_dialog_logs"))

    var compatibility_group := _settings_group(secondary_column, _t("settings.section.compatibility"), ICON_PLUGIN, animate_page, 0.105)
    _add_settings_row(compatibility_group, _settings_block(_t("settings.plugin_load_mode"), _t("settings.plugin_load_mode_desc"), _plugin_load_mode_select(), stack_settings_controls))
    _add_settings_row(compatibility_group, _settings_toggle_row(_t("settings.mock"), _t("settings.mock_desc"), _settings_draft_bool("mock_enabled", mock_enabled), "mock"))
    if player != null and player.has_method("is_text_translation_available") and player.is_text_translation_available():
        _add_settings_row(compatibility_group, _settings_action_row(
            _t("settings.translation_model"),
            _t("settings.translation_model_desc"),
            _t("settings.translation_model_select"),
            _choose_translation_model
        ))
        var draft_model_path := _settings_draft_string(
            "text_translation_model_path", text_translation_model_path
        )
        if not draft_model_path.is_empty():
            _add_settings_row(compatibility_group, _settings_value_row(
                _t("settings.translation_model_selected"),
                draft_model_path.get_file()
            ))
            _add_settings_row(compatibility_group, _settings_action_row(
                _t("settings.translation_model_clear"),
                _t("settings.translation_model_clear_desc"),
                _t("settings.translation_model_clear"),
                _clear_translation_model
            ))

    var advanced_group := _settings_group(secondary_column, _t("settings.section.advanced"), ICON_PLUGIN, animate_page, 0.13)
    var advanced_disclosure = AetherDisclosure.new()
    advanced_disclosure.setup(
        ui_tokens,
        ui_motion,
        _t("settings.advanced_desc"),
        _load_ui_icon(ICON_CHEVRON_RIGHT),
        advanced_tool_expanded
    )
    ui_widgets.disclosure_button(advanced_disclosure)
    _add_settings_row(advanced_group, advanced_disclosure)
    var advanced_content := VBoxContainer.new()
    advanced_content.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    advanced_content.add_theme_constant_override("separation", 0)
    advanced_content.visible = advanced_tool_expanded
    advanced_group.add_child(advanced_content)
    _add_settings_row(advanced_content, _settings_toggle_row(_t("settings.plugin_trace"), _t("settings.plugin_trace_desc"), plugin_trace, "advanced_plugin_trace"))
    _add_settings_row(advanced_content, _settings_toggle_row(_t("settings.trace_log"), _t("settings.trace_log_desc"), trace_log, "advanced_trace_log"))
    _add_settings_row(advanced_content, _settings_toggle_row(_t("settings.console_log"), _t("settings.console_log_desc"), console_log_file, "advanced_console_log"))
    _add_settings_row(advanced_content, _settings_toggle_row(_t("settings.export_tjs"), _t("settings.export_tjs_desc"), export_scripts, "advanced_export_tjs"))
    advanced_disclosure.expanded_changed.connect(func(value: bool):
        advanced_tool_expanded = value
        ui_motion.set_visible(advanced_content, value)
    )

    if _iap_supported_platform():
        var purchase_group := _settings_group(secondary_column, _t("settings.section.purchases"), ICON_LIBRARY, animate_page, 0.155)
        _add_settings_row(purchase_group, _settings_iap_product_row())
        _add_settings_row(purchase_group, _settings_iap_coffee_row())
        _add_settings_row(purchase_group, _settings_action_row(
            _t("iap.restore"),
            _t("iap.restore_desc"),
            _t("iap.restore_action"),
            func(): _begin_iap_restore()
        ))

    var about_group := _settings_group(secondary_column, _t("settings.section.about"), ICON_HELP, animate_page, 0.18)
    if OS.get_name() == "Android":
        _add_settings_row(about_group, _settings_action_row(
            _t("support.coffee.title"),
            _t("support.coffee.desc"),
            _t("support.coffee.open"),
            _open_android_coffee
        ))
    _add_settings_row(about_group, _settings_action_row(
        _t("settings.legal"),
        _t("settings.legal_desc"),
        _t("settings.legal_open"),
        func(): _show_legal_agreement(false)
    ))
    if _apple_app_store_platform():
        _add_settings_row(about_group, _settings_action_row(
            _t("settings.ios_statement"),
            _t("settings.ios_statement_desc"),
            _t("settings.ios_statement_open"),
            _show_ios_additional_statement
        ))
    _add_settings_row(about_group, _settings_link_value_row(
        _t("settings.app_service_filing"),
        APP_SERVICE_FILING_NUMBER,
        _open_app_service_filing
    ))
    var version_row := _settings_value_row(
        _t("settings.version"),
        _application_version_text()
    )
    _attach_secret_version_tap(version_row)
    _add_settings_row(about_group, version_row)

    if animate_page:
        ui_motion.reveal(top)

func _application_version_text() -> String:
    return str(ProjectSettings.get_setting("application/config/version", "development"))

func _build_detail_view() -> void:
    detail_view = Control.new()
    detail_view.set_anchors_preset(Control.PRESET_FULL_RECT)
    detail_view.visible = false
    shell_content.add_child(detail_view)

    detail_scroll = ScrollContainer.new()
    detail_scroll.set_anchors_preset(Control.PRESET_FULL_RECT)
    _configure_shell_scroll(detail_scroll)
    detail_view.add_child(detail_scroll)

func _build_modal_layer() -> void:
    modal_layer = Control.new()
    modal_layer.set_anchors_preset(Control.PRESET_FULL_RECT)
    modal_layer.mouse_filter = Control.MOUSE_FILTER_STOP
    modal_layer.visible = false
    add_child(modal_layer)

func _legal_document_path() -> String:
    match active_language:
        LANG_ZH_HANS:
            return LEGAL_AGREEMENT_ZH_HANS
        LANG_ZH_HANT:
            return LEGAL_AGREEMENT_ZH_HANT
        LANG_JA:
            return LEGAL_AGREEMENT_JA
        LANG_KO:
            return LEGAL_AGREEMENT_KO
        _:
            return LEGAL_AGREEMENT_EN

func _load_legal_document() -> String:
    var file := FileAccess.open(_legal_document_path(), FileAccess.READ)
    if file == null:
        return _t("legal.title")
    return file.get_as_text()

func _ios_statement_document_path() -> String:
    match active_language:
        LANG_ZH_HANS:
            return IOS_STATEMENT_ZH_HANS
        LANG_ZH_HANT:
            return IOS_STATEMENT_ZH_HANT
        LANG_JA:
            return IOS_STATEMENT_JA
        LANG_KO:
            return IOS_STATEMENT_KO
        _:
            return IOS_STATEMENT_EN

func _load_ios_statement_document() -> String:
    var file := FileAccess.open(_ios_statement_document_path(), FileAccess.READ)
    if file == null:
        return _t("ios_statement.title")
    return file.get_as_text()

func _show_ios_additional_statement(first_use: bool = false) -> void:
    modal_layer.visible = true
    modal_layer.move_to_front()
    for child in modal_layer.get_children():
        child.queue_free()

    var dim := ColorRect.new()
    dim.color = Color(0, 0, 0, 0.68)
    dim.set_anchors_preset(Control.PRESET_FULL_RECT)
    dim.mouse_filter = Control.MOUSE_FILTER_STOP
    modal_layer.add_child(dim)

    var viewport_size := get_viewport_rect().size
    var safe_rect := _ui_safe_rect(viewport_size)
    var compact := safe_rect.size.y > safe_rect.size.x or safe_rect.size.x < 700.0
    var dialog := PanelContainer.new()
    _mark_legal_safe_dialog(dialog, first_use, true)
    _layout_safe_dialog(dialog, safe_rect)
    dialog.add_theme_stylebox_override("panel", _panel_style(22, color_card, color_line, 1))
    modal_layer.add_child(dialog)

    var margin := MarginContainer.new()
    margin.add_theme_constant_override("margin_left", 16 if compact and not first_use else (18 if compact else 30))
    margin.add_theme_constant_override("margin_top", 16 if compact and not first_use else (18 if compact else 24))
    margin.add_theme_constant_override("margin_right", 16 if compact and not first_use else (18 if compact else 30))
    margin.add_theme_constant_override("margin_bottom", 16 if compact and not first_use else (18 if compact else 24))
    dialog.add_child(margin)

    var content := VBoxContainer.new()
    content.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    content.size_flags_vertical = Control.SIZE_EXPAND_FILL
    content.add_theme_constant_override("separation", 12 if compact else 16)
    margin.add_child(content)

    var title := Label.new()
    title.text = _t("ios_statement.title")
    title.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
    title.add_theme_font_size_override("font_size", 22 if compact and not first_use else (24 if compact else 30))
    title.add_theme_color_override("font_color", color_text)
    content.add_child(title)

    if first_use:
        var summary := Label.new()
        summary.text = _t("ios_statement.first_summary")
        summary.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
        summary.add_theme_font_size_override("font_size", 15 if compact else 17)
        summary.add_theme_color_override("font_color", color_accent_soft)
        content.add_child(summary)

    var scroll := ScrollContainer.new()
    scroll.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
    scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
    scroll.vertical_scroll_mode = ScrollContainer.SCROLL_MODE_AUTO
    content.add_child(scroll)

    var statement := Label.new()
    statement.text = _load_ios_statement_document()
    statement.custom_minimum_size = Vector2.ZERO
    statement.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    statement.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
    statement.add_theme_font_size_override("font_size", 15 if compact else 17)
    statement.add_theme_color_override("font_color", color_text)
    statement.add_theme_constant_override("line_spacing", 4 if compact else 6)
    scroll.add_child(statement)

    var buttons := HBoxContainer.new()
    buttons.alignment = BoxContainer.ALIGNMENT_END
    buttons.add_theme_constant_override("separation", 14)
    buttons.custom_minimum_size = Vector2(0, 58)
    content.add_child(buttons)

    if first_use:
        var decline := Button.new()
        decline.text = _t("legal.decline")
        decline.custom_minimum_size = Vector2(0 if compact else 150, 52 if compact else 56)
        decline.size_flags_horizontal = Control.SIZE_EXPAND_FILL if compact else Control.SIZE_FILL
        decline.size_flags_stretch_ratio = 0.72 if compact else 1.0
        decline.add_theme_font_size_override("font_size", 16 if compact else 19)
        decline.add_theme_color_override("font_color", color_text)
        decline.pressed.connect(_decline_legal_agreement)
        buttons.add_child(decline)

        var accept := _pill_button(_t("legal.accept"))
        accept.custom_minimum_size = Vector2(0 if compact else 220, 52 if compact else 56)
        accept.size_flags_horizontal = Control.SIZE_EXPAND_FILL if compact else Control.SIZE_FILL
        accept.size_flags_stretch_ratio = 1.28 if compact else 1.0
        accept.pressed.connect(_accept_ios_additional_statement)
        buttons.add_child(accept)
    else:
        var close := _pill_button(_t("legal.close"))
        close.custom_minimum_size = Vector2(136 if compact else 150, 48 if compact else 56)
        close.size_flags_horizontal = Control.SIZE_SHRINK_END
        close.pressed.connect(func(): modal_layer.visible = false)
        buttons.add_child(close)

func _effective_legal_platform_name() -> String:
    var platform_override := String(
        ProjectSettings.get_setting("aether_kiri/legal_platform_override", "")
    ).strip_edges()
    if platform_override in ["iOS", "macOS"]:
        return platform_override
    return OS.get_name()

func _apple_app_store_platform(platform_name: String = "") -> bool:
    var effective_platform := platform_name if not platform_name.is_empty() else _effective_legal_platform_name()
    return effective_platform in ["iOS", "macOS"]

func _ios_statement_required(platform_name: String = "") -> bool:
    if not _apple_app_store_platform(platform_name):
        return false
    if OS.is_debug_build() and _runtime_flag("AETHERKIRI_BYPASS_LEGAL_GATE"):
        return false
    return ios_statement_accepted_version != IOS_STATEMENT_VERSION

func _legal_agreement_required() -> bool:
    if OS.is_debug_build() and _runtime_flag("AETHERKIRI_BYPASS_LEGAL_GATE"):
        return false
    if OS.get_environment("AETHERKIRI_CAPTURE_UI_ACTION") in ["legal", "legal_declined"]:
        return true
    return legal_accepted_version != LEGAL_AGREEMENT_VERSION

func _next_required_legal_document(platform_name: String = "") -> String:
    if _legal_agreement_required():
        return "privacy"
    if _ios_statement_required(platform_name):
        return "ios_statement"
    return ""

func _show_next_required_legal_document() -> bool:
    match _next_required_legal_document():
        "privacy":
            _show_legal_agreement(true)
            return true
        "ios_statement":
            _show_ios_additional_statement(true)
            return true
    return false

func _require_legal_documents_for_media() -> bool:
    if _next_required_legal_document().is_empty():
        return true
    _show_next_required_legal_document()
    return false

func _show_legal_agreement(first_use: bool) -> void:
    modal_layer.visible = true
    modal_layer.move_to_front()
    for child in modal_layer.get_children():
        child.queue_free()

    var dim := ColorRect.new()
    dim.color = Color(0, 0, 0, 0.68)
    dim.set_anchors_preset(Control.PRESET_FULL_RECT)
    dim.mouse_filter = Control.MOUSE_FILTER_STOP
    modal_layer.add_child(dim)

    var viewport_size := get_viewport_rect().size
    var safe_rect := _ui_safe_rect(viewport_size)
    var compact := safe_rect.size.y > safe_rect.size.x or safe_rect.size.x < 700.0
    var dialog := PanelContainer.new()
    _mark_legal_safe_dialog(dialog, first_use, false)
    _layout_safe_dialog(dialog, safe_rect)
    dialog.add_theme_stylebox_override("panel", _panel_style(22, color_card, color_line, 1))
    modal_layer.add_child(dialog)

    var margin := MarginContainer.new()
    margin.add_theme_constant_override("margin_left", 16 if compact and not first_use else (18 if compact else 30))
    margin.add_theme_constant_override("margin_top", 16 if compact and not first_use else (18 if compact else 24))
    margin.add_theme_constant_override("margin_right", 16 if compact and not first_use else (18 if compact else 30))
    margin.add_theme_constant_override("margin_bottom", 16 if compact and not first_use else (18 if compact else 24))
    dialog.add_child(margin)

    var content := VBoxContainer.new()
    content.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    content.size_flags_vertical = Control.SIZE_EXPAND_FILL
    content.add_theme_constant_override("separation", 12 if compact else 16)
    margin.add_child(content)

    var title := Label.new()
    title.text = _t("legal.title")
    title.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
    title.add_theme_font_size_override("font_size", 22 if compact and not first_use else (24 if compact else 30))
    title.add_theme_color_override("font_color", color_text)
    content.add_child(title)

    if first_use:
        var summary := Label.new()
        summary.text = _t(
            "legal.first_summary_ios"
            if _apple_app_store_platform()
            else "legal.first_summary"
        )
        summary.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
        summary.add_theme_font_size_override("font_size", 15 if compact else 17)
        summary.add_theme_color_override("font_color", color_accent_soft)
        content.add_child(summary)

    var scroll := ScrollContainer.new()
    scroll.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
    scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
    scroll.vertical_scroll_mode = ScrollContainer.SCROLL_MODE_AUTO
    content.add_child(scroll)

    var policy := Label.new()
    policy.text = _load_legal_document()
    policy.custom_minimum_size = Vector2.ZERO
    policy.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    policy.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
    policy.add_theme_font_size_override("font_size", 15 if compact else 17)
    policy.add_theme_color_override("font_color", color_text)
    policy.add_theme_constant_override("line_spacing", 4 if compact else 6)
    scroll.add_child(policy)

    var buttons := HBoxContainer.new()
    buttons.alignment = BoxContainer.ALIGNMENT_END
    buttons.add_theme_constant_override("separation", 14)
    buttons.custom_minimum_size = Vector2(0, 58)
    content.add_child(buttons)

    if first_use:
        var decline := Button.new()
        decline.text = _t("legal.decline")
        decline.custom_minimum_size = Vector2(0 if compact else 150, 52 if compact else 56)
        decline.size_flags_horizontal = Control.SIZE_EXPAND_FILL if compact else Control.SIZE_FILL
        decline.size_flags_stretch_ratio = 0.72 if compact else 1.0
        decline.add_theme_font_size_override("font_size", 16 if compact else 19)
        decline.add_theme_color_override("font_color", color_text)
        decline.pressed.connect(_decline_legal_agreement)
        buttons.add_child(decline)

        var accept := _pill_button(_t("legal.accept"))
        accept.custom_minimum_size = Vector2(0 if compact else 220, 52 if compact else 56)
        accept.size_flags_horizontal = Control.SIZE_EXPAND_FILL if compact else Control.SIZE_FILL
        accept.size_flags_stretch_ratio = 1.28 if compact else 1.0
        accept.pressed.connect(_accept_legal_agreement)
        buttons.add_child(accept)
    else:
        var close := _pill_button(_t("legal.close"))
        close.custom_minimum_size = Vector2(136 if compact else 150, 48 if compact else 56)
        close.size_flags_horizontal = Control.SIZE_SHRINK_END
        close.pressed.connect(func(): modal_layer.visible = false)
        buttons.add_child(close)

func _accept_legal_agreement() -> void:
    legal_accepted_version = LEGAL_AGREEMENT_VERSION
    legal_accepted_at = int(Time.get_unix_time_from_system())
    _save_shell_settings()
    modal_layer.visible = false
    if _show_next_required_legal_document():
        return
    _continue_ready_after_legal_gate()

func _accept_ios_additional_statement() -> void:
    ios_statement_accepted_version = IOS_STATEMENT_VERSION
    ios_statement_accepted_at = int(Time.get_unix_time_from_system())
    _save_shell_settings()
    modal_layer.visible = false
    if _show_next_required_legal_document():
        return
    _continue_ready_after_legal_gate()

func _decline_legal_agreement() -> void:
    if _effective_legal_platform_name() == "iOS":
        _show_legal_declined_screen()
        return
    get_tree().quit(0)

func _review_required_legal_documents() -> void:
    if _show_next_required_legal_document():
        return
    modal_layer.visible = false
    _continue_ready_after_legal_gate()

func _show_legal_declined_screen() -> void:
    modal_layer.visible = true
    modal_layer.move_to_front()
    for child in modal_layer.get_children():
        child.queue_free()

    var dim := ColorRect.new()
    dim.color = Color(0, 0, 0, 0.82)
    dim.set_anchors_preset(Control.PRESET_FULL_RECT)
    dim.mouse_filter = Control.MOUSE_FILTER_STOP
    modal_layer.add_child(dim)

    var dialog := PanelContainer.new()
    dialog.set_meta("aether_safe_dialog_kind", "declined")
    _layout_safe_dialog(dialog, _ui_safe_rect(get_viewport_rect().size))
    dialog.add_theme_stylebox_override("panel", _panel_style(22, color_card, color_line, 1))
    modal_layer.add_child(dialog)

    var margin := MarginContainer.new()
    margin.add_theme_constant_override("margin_left", 34)
    margin.add_theme_constant_override("margin_top", 30)
    margin.add_theme_constant_override("margin_right", 34)
    margin.add_theme_constant_override("margin_bottom", 30)
    dialog.add_child(margin)

    var box := VBoxContainer.new()
    box.size_flags_vertical = Control.SIZE_EXPAND_FILL
    box.add_theme_constant_override("separation", 22)
    margin.add_child(box)

    var title := Label.new()
    title.text = _t("legal.declined_title")
    title.add_theme_font_size_override("font_size", 30)
    title.add_theme_color_override("font_color", color_text)
    box.add_child(title)

    var body := Label.new()
    body.text = _t("legal.declined_body")
    body.size_flags_vertical = Control.SIZE_EXPAND_FILL
    body.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
    body.add_theme_font_size_override("font_size", 20)
    body.add_theme_color_override("font_color", color_text)
    box.add_child(body)

    var review := _pill_button(_t("legal.review_again"))
    review.custom_minimum_size = Vector2(180, 56)
    review.size_flags_horizontal = Control.SIZE_SHRINK_END
    review.pressed.connect(_review_required_legal_documents)
    box.add_child(review)

func _build_loading_panel() -> void:
    loading_panel = PanelContainer.new()
    loading_panel.set_anchors_preset(Control.PRESET_FULL_RECT)
    loading_panel.mouse_filter = Control.MOUSE_FILTER_STOP
    loading_panel.visible = false
    var scrim_alpha := 0.48 if style_mode == STYLE_DARK else 0.34
    loading_panel.add_theme_stylebox_override(
        "panel",
        ui_tokens.panel(Color(0, 0, 0, scrim_alpha), 0)
    )
    add_child(loading_panel)

    loading_center = CenterContainer.new()
    loading_panel.add_child(loading_center)

    loading_card = PanelContainer.new()
    var viewport_width := get_viewport_rect().size.x
    var preferred_width := 720.0 if ui_log_enabled and not _mobile_runtime() else 420.0
    loading_card.custom_minimum_size = Vector2(minf(preferred_width, maxf(300.0, viewport_width - 40.0)), 420 if ui_log_enabled and not _mobile_runtime() else 136)
    var loading_style := ui_tokens.material_panel(true)
    loading_style.content_margin_left = 20
    loading_style.content_margin_top = 18
    loading_style.content_margin_right = 20
    loading_style.content_margin_bottom = 18
    loading_card.add_theme_stylebox_override("panel", loading_style)
    loading_center.add_child(loading_card)

    var box := VBoxContainer.new()
    box.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    box.size_flags_vertical = Control.SIZE_EXPAND_FILL
    box.add_theme_constant_override("separation", 14)
    if not ui_log_enabled or _mobile_runtime():
        box.alignment = BoxContainer.ALIGNMENT_CENTER
    loading_card.add_child(box)

    var status_row := HBoxContainer.new()
    status_row.custom_minimum_size = Vector2(0, 60)
    status_row.add_theme_constant_override("separation", 12)
    box.add_child(status_row)

    var spinner_holder := Control.new()
    spinner_holder.custom_minimum_size = Vector2(44, 44)
    spinner_holder.size_flags_vertical = Control.SIZE_SHRINK_CENTER
    status_row.add_child(spinner_holder)
    var spinner_plate := PanelContainer.new()
    spinner_plate.position = Vector2.ZERO
    spinner_plate.size = Vector2(44, 44)
    spinner_plate.add_theme_stylebox_override("panel", ui_tokens.panel(ui_tokens.accent_fill, 8))
    spinner_holder.add_child(spinner_plate)
    loading_spinner = _icon_rect(ICON_REFRESH, Vector2(20, 20), ui_tokens.accent)
    loading_spinner.position = Vector2(12, 12)
    loading_spinner.size = Vector2(20, 20)
    loading_spinner.pivot_offset = Vector2(10, 10)
    loading_spinner.flip_h = LOADING_SPINNER_FLIP_H
    spinner_holder.add_child(loading_spinner)

    var loading_labels := VBoxContainer.new()
    loading_labels.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    loading_labels.size_flags_vertical = Control.SIZE_EXPAND_FILL
    loading_labels.alignment = BoxContainer.ALIGNMENT_CENTER
    loading_labels.add_theme_constant_override("separation", 1)
    status_row.add_child(loading_labels)

    loading_title_label = Label.new()
    loading_title_label.text = _t("loading.title")
    loading_title_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
    loading_title_label.add_theme_font_size_override("font_size", 18)
    loading_title_label.add_theme_color_override("font_color", ui_tokens.text_primary)
    loading_labels.add_child(loading_title_label)

    loading_detail_label = Label.new()
    loading_detail_label.text = ""
    loading_detail_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
    loading_detail_label.add_theme_font_size_override("font_size", 13)
    loading_detail_label.add_theme_color_override("font_color", ui_tokens.text_secondary)
    loading_detail_label.visible = false
    loading_labels.add_child(loading_detail_label)
    if not ui_motion.reduced_motion:
        var spinner_tween := loading_spinner.create_tween().set_loops()
        # Mirror the counter-clockwise refresh glyph so it follows this
        # clockwise loading motion without changing shared refresh icons.
        spinner_tween.tween_property(
            loading_spinner, "rotation", LOADING_SPINNER_ROTATION, 0.85
        ).from(0.0).set_trans(Tween.TRANS_LINEAR).set_ease(Tween.EASE_IN_OUT)

    if ui_log_enabled and not _mobile_runtime():
        log_view = TextEdit.new()
        log_view.size_flags_horizontal = Control.SIZE_EXPAND_FILL
        log_view.size_flags_vertical = Control.SIZE_EXPAND_FILL
        log_view.mouse_filter = Control.MOUSE_FILTER_IGNORE
        log_view.editable = false
        log_view.wrap_mode = TextEdit.LINE_WRAPPING_BOUNDARY
        log_view.scroll_fit_content_height = false
        log_view.add_theme_font_size_override("font_size", 13)
        log_view.add_theme_color_override("font_color", ui_tokens.text_secondary)
        log_view.add_theme_color_override("background_color", Color(0, 0, 0, 0))
        box.add_child(log_view)

func _show_loading_overlay(immediate: bool = false) -> void:
    loading_hiding = false
    loading_panel.move_to_front()
    if loading_card != null:
        ui_motion.loading_in(loading_panel, loading_card, immediate)

func _translation_model_configured() -> bool:
    return (
        not text_translation_model_path.is_empty()
        and player != null
        and player.has_method("is_text_translation_available")
        and player.is_text_translation_available()
    )

func _set_translation_loading_notice(active: bool) -> void:
    translation_loading_notice_active = active
    if is_instance_valid(loading_title_label):
        loading_title_label.text = _t(
            "loading.translation_model" if active else "loading.title"
        )
    if is_instance_valid(loading_detail_label):
        loading_detail_label.text = (
            _t("loading.translation_model_detail") if active else ""
        )
        loading_detail_label.visible = active

func _hide_loading_overlay(finished: Callable = Callable()) -> void:
    if loading_panel == null or not loading_panel.visible:
        if finished.is_valid():
            finished.call()
        return
    if loading_hiding:
        return
    loading_hiding = true
    ui_motion.loading_out(loading_panel, loading_card, func():
        loading_hiding = false
        if finished.is_valid():
            finished.call()
    )

func _panel_style(radius: int, fill: Color, border: Color, border_width: int = 1) -> StyleBoxFlat:
    var style := StyleBoxFlat.new()
    style.bg_color = fill
    style.border_color = border
    style.border_width_left = border_width
    style.border_width_top = border_width
    style.border_width_right = border_width
    style.border_width_bottom = border_width
    style.corner_radius_top_left = radius
    style.corner_radius_top_right = radius
    style.corner_radius_bottom_left = radius
    style.corner_radius_bottom_right = radius
    style.content_margin_left = 18
    style.content_margin_top = 16
    style.content_margin_right = 18
    style.content_margin_bottom = 16
    return style

func _scroll_track_style() -> StyleBoxFlat:
    var style := StyleBoxFlat.new()
    style.bg_color = Color(0, 0, 0, 0)
    style.content_margin_left = 5
    style.content_margin_right = 5
    return style

func _scroll_thumb_style(fill: Color) -> StyleBoxFlat:
    var style := StyleBoxFlat.new()
    style.bg_color = Color(fill.r, fill.g, fill.b, 0.72)
    style.corner_radius_top_left = 4
    style.corner_radius_top_right = 4
    style.corner_radius_bottom_left = 4
    style.corner_radius_bottom_right = 4
    style.expand_margin_left = -3
    style.expand_margin_right = -3
    return style

func _empty_style() -> StyleBoxEmpty:
    return StyleBoxEmpty.new()

func _focus_outline(radius: int = 8) -> StyleBoxFlat:
    return _panel_style(radius, color_card_hover, Color.TRANSPARENT, 0)

func _load_ui_icon(icon_path: String):
    if icon_path.is_empty():
        return null
    if not ui_icon_cache.has(icon_path):
        var imported := ResourceLoader.load(icon_path, "Texture2D")
        if imported is Texture2D:
            ui_icon_cache[icon_path] = imported
            return imported

        var file := FileAccess.open(icon_path, FileAccess.READ)
        if file == null:
            push_warning("UI icon missing: %s" % icon_path)
            ui_icon_cache[icon_path] = null
            return null
        var svg := file.get_as_text()
        var image := Image.new()
        var err := image.load_svg_from_string(svg, _svg_icon_scale(svg))
        if err != OK:
            push_warning("UI icon failed to load: %s" % icon_path)
            ui_icon_cache[icon_path] = null
            return null
        ui_icon_cache[icon_path] = ImageTexture.create_from_image(image)
    return ui_icon_cache.get(icon_path)

func _svg_icon_scale(svg: String) -> float:
    if svg.find('width="1em"') >= 0:
        return 64.0
    var regex := RegEx.new()
    if regex.compile('width="([0-9]+(?:\\.[0-9]+)?)"') != OK:
        return 1.0
    var match := regex.search(svg)
    if match == null:
        return 1.0
    var width := maxf(1.0, float(match.get_string(1)))
    return maxf(1.0, 64.0 / width)

func _icon_rect(icon_path: String, size: Vector2, tint: Color = Color(-1, -1, -1, -1)) -> TextureRect:
    var icon := TextureRect.new()
    icon.texture = _load_ui_icon(icon_path)
    icon.mouse_filter = Control.MOUSE_FILTER_IGNORE
    icon.custom_minimum_size = size
    icon.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
    icon.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
    icon.modulate = color_text if tint.a < 0.0 else tint
    return icon

func _centered_icon(icon_path: String, size: Vector2, tint: Color = Color(-1, -1, -1, -1)) -> CenterContainer:
    var holder := CenterContainer.new()
    holder.mouse_filter = Control.MOUSE_FILTER_IGNORE
    holder.add_child(_icon_rect(icon_path, size, tint))
    return holder

func _nearest_scroll_container(control: Control) -> ScrollContainer:
    var current := control.get_parent()
    while current != null:
        if current is ScrollContainer:
            return current as ScrollContainer
        current = current.get_parent()
    return null

func _find_shell_scroll_at_position(position: Vector2) -> ScrollContainer:
    var scrolls: Array[ScrollContainer] = [settings_view, detail_scroll, game_scroll, video_scroll]
    for scroll in scrolls:
        if scroll != null and scroll.is_visible_in_tree() and scroll.get_global_rect().has_point(position):
            return scroll
    return null

func _control_at_pointer(position: Vector2) -> Control:
    var current_viewport := get_viewport()
    var hovered := (
        current_viewport.gui_get_hovered_control()
        if current_viewport != null
        else null
    )
    if hovered is Control:
        var control := hovered as Control
        if control.is_visible_in_tree() and control.get_global_rect().has_point(position):
            return control
    if shell_root != null and shell_root.is_visible_in_tree():
        return _deep_control_at_position(shell_root, position)
    return null

func _deep_control_at_position(node: Node, position: Vector2) -> Control:
    for i in range(node.get_child_count() - 1, -1, -1):
        var child := node.get_child(i)
        if not child is Control:
            continue
        var child_control := child as Control
        if not child_control.is_visible_in_tree() or not child_control.get_global_rect().has_point(position):
            continue
        var nested := _deep_control_at_position(child_control, position)
        return nested if nested != null else child_control
    return null

func _configure_shell_scroll(scroll: ScrollContainer) -> void:
    scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
    scroll.vertical_scroll_mode = ScrollContainer.SCROLL_MODE_AUTO
    scroll.scroll_deadzone = 0

func _start_shell_scroll_drag(key: int, position: Vector2) -> void:
    var scroll := _find_shell_scroll_at_position(position)
    if scroll == null:
        shell_scroll_drag_states.erase(key)
        return
    _stop_shell_scroll_tween(scroll)
    var control := _control_at_pointer(position)
    var button := _nearest_base_button(control) if control != null else null
    var horizontal_slider := (
        _nearest_horizontal_slider(control) if control != null else null
    )
    shell_scroll_drag_states[key] = {
        # Controls can be rebuilt between the touch press and the following
        # drag/release event (for example after changing a settings selector).
        # Keeping raw Object Variants here leaves a dangling pointer for the
        # next `as Control`/`as ScrollContainer` cast in optimized builds.
        "scroll_id": scroll.get_instance_id(),
        "control_id": control.get_instance_id() if control != null else 0,
        "last": position,
        "last_motion_msec": Time.get_ticks_msec(),
        "velocity_y": 0.0,
        "distance": 0.0,
        "pending_y": 0.0,
        "dragging": false,
        "threshold": SHELL_SCROLL_BUTTON_DRAG_THRESHOLD if button != null else SHELL_SCROLL_DRAG_THRESHOLD,
        # A Range control owns its complete press/drag/release gesture. Do not
        # let vertical finger wobble hand that same pointer to the surrounding
        # settings ScrollContainer midway through a slider adjustment.
        "scroll_locked": horizontal_slider != null,
        "axis_lock": SHELL_SCROLL_AXIS_PENDING if horizontal_slider != null else SHELL_SCROLL_AXIS_NONE,
        "gesture_delta": Vector2.ZERO,
    }

func _update_shell_scroll_drag(
    key: int,
    position: Vector2,
    relative: Vector2,
    reported_velocity_y: float = 0.0
) -> bool:
    var state: Dictionary = shell_scroll_drag_states.get(key, {})
    if state.is_empty():
        _start_shell_scroll_drag(key, position)
        state = shell_scroll_drag_states.get(key, {})
        if state.is_empty():
            return false
    var scroll := _shell_scroll_from_drag_state(state)
    if scroll == null or not scroll.is_visible_in_tree():
        shell_scroll_drag_states.erase(key)
        return false
    if bool(state.get("scroll_locked", false)):
        return false
    var last_position := state.get("last", position) as Vector2
    var delta := position - last_position
    if delta.is_zero_approx():
        delta = relative
    var now_msec := Time.get_ticks_msec()
    var last_motion_msec := int(state.get("last_motion_msec", now_msec))
    var elapsed_msec := maxi(1, now_msec - last_motion_msec)
    var estimated_velocity_y := delta.y * 1000.0 / float(elapsed_msec)
    var sample_velocity_y := reported_velocity_y if absf(reported_velocity_y) > 1.0 else estimated_velocity_y
    var previous_velocity_y := float(state.get("velocity_y", sample_velocity_y))
    state["velocity_y"] = lerpf(previous_velocity_y, sample_velocity_y, 0.45)
    state["last_motion_msec"] = now_msec
    state["last"] = position
    var distance := float(state.get("distance", 0.0)) + absf(delta.y)
    var pending_y := float(state.get("pending_y", 0.0)) + delta.y
    var axis_lock := _update_shell_scroll_axis_lock(state, delta)
    if axis_lock == SHELL_SCROLL_AXIS_PENDING or axis_lock == SHELL_SCROLL_AXIS_HORIZONTAL:
        state["distance"] = distance
        state["pending_y"] = pending_y
        shell_scroll_drag_states[key] = state
        return false
    var was_dragging := bool(state.get("dragging", false))
    var threshold := float(state.get("threshold", SHELL_SCROLL_DRAG_THRESHOLD))
    var dragging := was_dragging or distance >= threshold
    state["distance"] = distance
    state["dragging"] = dragging
    state["pending_y"] = 0.0 if dragging else pending_y
    shell_scroll_drag_states[key] = state
    if not dragging:
        return false
    var drag_delta := delta.y if was_dragging else pending_y
    _scroll_container_by(scroll, -drag_delta * SHELL_SCROLL_DRAG_SPEED)
    _cancel_shell_scroll_press(state)
    return true

func _finish_shell_scroll_drag(key: int) -> bool:
    var state: Dictionary = shell_scroll_drag_states.get(key, {})
    var dragging := bool(state.get("dragging", false))
    if dragging:
        _cancel_shell_scroll_press(state)
        var scroll := _shell_scroll_from_drag_state(state)
        var last_motion_msec := int(state.get("last_motion_msec", 0))
        var velocity_is_fresh := Time.get_ticks_msec() - last_motion_msec <= SHELL_SCROLL_MOMENTUM_STALE_MSEC
        if key != SHELL_SCROLL_MOUSE_KEY and velocity_is_fresh and scroll != null and is_instance_valid(scroll):
            var scroll_velocity := -float(state.get("velocity_y", 0.0)) * SHELL_SCROLL_DRAG_SPEED
            _start_shell_scroll_momentum(scroll, scroll_velocity)
    shell_scroll_drag_states.erase(key)
    return dragging

func _reset_shell_scroll_drag() -> void:
    for key in shell_scroll_drag_states.keys():
        var state: Dictionary = shell_scroll_drag_states.get(key, {})
        _cancel_shell_scroll_press(state)
    shell_scroll_drag_states.clear()
    for key_variant in shell_scroll_tweens.keys():
        var tween: Tween = shell_scroll_tweens.get(key_variant) as Tween
        if tween is Tween and tween.is_valid():
            tween.kill()
    shell_scroll_tweens.clear()
    shell_scroll_targets.clear()

func _cancel_shell_scroll_press(state: Dictionary) -> void:
    get_viewport().gui_release_focus()
    var control := _shell_control_from_drag_state(state)
    if control == null:
        return
    control.release_focus()
    var button := _nearest_base_button(control)
    if button != null:
        button.release_focus()
        ui_motion.cancel_press(button)
        if not button.toggle_mode:
            button.set_pressed_no_signal(false)

func _shell_scroll_from_drag_state(state: Dictionary) -> ScrollContainer:
    var instance_id := int(state.get("scroll_id", 0))
    if instance_id <= 0:
        return null
    var candidate := instance_from_id(instance_id)
    return candidate as ScrollContainer if candidate is ScrollContainer else null

func _shell_control_from_drag_state(state: Dictionary) -> Control:
    var instance_id := int(state.get("control_id", 0))
    if instance_id <= 0:
        return null
    var candidate := instance_from_id(instance_id)
    return candidate as Control if candidate is Control else null

func _nearest_base_button(control: Control) -> BaseButton:
    var current: Node = control
    while current != null:
        if current is BaseButton:
            return current as BaseButton
        current = current.get_parent()
    return null

func _nearest_horizontal_slider(control: Control) -> HSlider:
    var current: Node = control
    while current != null:
        if current is HSlider:
            return current as HSlider
        current = current.get_parent()
    return null

func _update_shell_scroll_axis_lock(state: Dictionary, delta: Vector2) -> String:
    var axis_lock := String(state.get("axis_lock", SHELL_SCROLL_AXIS_NONE))
    if axis_lock != SHELL_SCROLL_AXIS_PENDING:
        return axis_lock
    var gesture_delta: Vector2 = state.get("gesture_delta", Vector2.ZERO)
    gesture_delta += delta
    state["gesture_delta"] = gesture_delta
    var horizontal_distance := absf(gesture_delta.x)
    var vertical_distance := absf(gesture_delta.y)
    if maxf(horizontal_distance, vertical_distance) < SHELL_SCROLL_SLIDER_AXIS_THRESHOLD:
        return SHELL_SCROLL_AXIS_PENDING
    # A gesture that begins on a horizontal slider is biased toward the
    # slider. Only a clearly vertical initial motion may become page scrolling;
    # once horizontal wins, later vertical wobble cannot change ownership.
    if vertical_distance > horizontal_distance * SHELL_SCROLL_SLIDER_VERTICAL_DOMINANCE:
        axis_lock = SHELL_SCROLL_AXIS_VERTICAL
    else:
        axis_lock = SHELL_SCROLL_AXIS_HORIZONTAL
    state["axis_lock"] = axis_lock
    return axis_lock

func _is_scroll_bar_control(control: Control) -> bool:
    var current: Node = control
    while current != null:
        if current is ScrollBar:
            return true
        current = current.get_parent()
    return false

func _scroll_container_by(scroll: ScrollContainer, delta: float, smooth: bool = false) -> void:
    var bar := scroll.get_v_scroll_bar()
    if bar == null:
        return
    var minimum_scroll := bar.min_value
    var maximum_scroll := _shell_scroll_maximum(bar.min_value, bar.max_value, bar.page)
    var scroll_key := scroll.get_instance_id()
    var remainder := float(shell_scroll_remainders.get(scroll_key, 0.0))
    var current := clampf(float(scroll.scroll_vertical), minimum_scroll, maximum_scroll)
    # ScrollBar.max_value describes the full content extent. The last valid
    # ScrollContainer position is max_value - page. Keeping an animation target
    # beyond that real boundary makes the first reverse trackpad gestures appear
    # to do nothing while they consume the invisible overshoot.
    var tracked_target := float(shell_scroll_targets.get(scroll_key, current))
    var base := clampf(tracked_target, minimum_scroll, maximum_scroll) if smooth else current
    var next := clampf(base + remainder + delta, minimum_scroll, maximum_scroll)
    var snapped := int(roundf(next))
    snapped = int(clampf(float(snapped), minimum_scroll, maximum_scroll))
    if smooth:
        _stop_shell_scroll_tween(scroll)
        shell_scroll_targets[scroll_key] = next
        var tween := scroll.create_tween()
        shell_scroll_tweens[scroll_key] = tween
        tween.tween_method(func(value: float):
            if is_instance_valid(scroll):
                scroll.scroll_vertical = int(roundf(value))
        , current, next, SHELL_SCROLL_TWEEN_DURATION).set_trans(Tween.TRANS_QUAD).set_ease(Tween.EASE_OUT)
        tween.tween_callback(func():
            shell_scroll_tweens.erase(scroll_key)
            shell_scroll_targets[scroll_key] = next
        )
    else:
        scroll.scroll_vertical = snapped
        shell_scroll_targets[scroll_key] = float(snapped)
    var clamped_to_min := is_equal_approx(next, minimum_scroll) and delta < 0.0
    var clamped_to_max := is_equal_approx(next, maximum_scroll) and delta > 0.0
    if clamped_to_min or clamped_to_max:
        shell_scroll_remainders.erase(scroll_key)
    else:
        shell_scroll_remainders[scroll_key] = next - float(snapped)

func _stop_shell_scroll_tween(scroll: ScrollContainer) -> void:
    if scroll == null or not is_instance_valid(scroll):
        return
    var scroll_key := scroll.get_instance_id()
    var tween: Tween = shell_scroll_tweens.get(scroll_key) as Tween
    if tween is Tween and tween.is_valid():
        tween.kill()
    shell_scroll_tweens.erase(scroll_key)
    shell_scroll_targets[scroll_key] = float(scroll.scroll_vertical)

func _shell_scroll_momentum_spec(
    scroll_velocity: float,
    current: float,
    minimum: float,
    maximum: float
) -> Dictionary:
    var velocity := clampf(
        scroll_velocity,
        -SHELL_SCROLL_MOMENTUM_MAX_SPEED,
        SHELL_SCROLL_MOMENTUM_MAX_SPEED
    )
    var speed := absf(velocity)
    if speed < SHELL_SCROLL_MOMENTUM_MIN_SPEED or maximum <= minimum:
        return {"active": false, "target": current, "duration": 0.0}
    var intended_distance := velocity * SHELL_SCROLL_MOMENTUM_DISTANCE_FACTOR
    var target := clampf(current + intended_distance, minimum, maximum)
    var actual_distance := target - current
    if absf(actual_distance) < 1.0:
        return {"active": false, "target": current, "duration": 0.0}
    var duration := minf(
        SHELL_SCROLL_MOMENTUM_MAX_DURATION,
        0.18 + speed / 7500.0
    )
    var boundary_ratio := clampf(absf(actual_distance / intended_distance), 0.0, 1.0)
    duration = maxf(0.12, duration * sqrt(boundary_ratio))
    return {"active": true, "target": target, "duration": duration}

func _shell_scroll_maximum(minimum: float, maximum: float, page: float) -> float:
    return maxf(minimum, maximum - maxf(0.0, page))

func _start_shell_scroll_momentum(scroll: ScrollContainer, scroll_velocity: float) -> void:
    var bar := scroll.get_v_scroll_bar()
    if bar == null:
        return
    var current := float(scroll.scroll_vertical)
    var maximum_scroll := _shell_scroll_maximum(bar.min_value, bar.max_value, bar.page)
    var spec := _shell_scroll_momentum_spec(
        scroll_velocity,
        current,
        bar.min_value,
        maximum_scroll
    )
    if not bool(spec["active"]):
        return
    _stop_shell_scroll_tween(scroll)
    var scroll_key := scroll.get_instance_id()
    var target := float(spec["target"])
    shell_scroll_targets[scroll_key] = target
    var tween := scroll.create_tween()
    shell_scroll_tweens[scroll_key] = tween
    tween.tween_method(func(value: float):
        if is_instance_valid(scroll):
            scroll.scroll_vertical = int(roundf(value))
    , current, target, float(spec["duration"])).set_trans(Tween.TRANS_CUBIC).set_ease(Tween.EASE_OUT)
    tween.tween_callback(func():
        shell_scroll_tweens.erase(scroll_key)
        shell_scroll_targets[scroll_key] = target
    )

func _disabled_text_color() -> Color:
    return ui_tokens.text_tertiary

func _library_tab_button(text: String) -> Button:
    var button := Button.new()
    button.text = text
    button.alignment = HORIZONTAL_ALIGNMENT_LEFT
    button.clip_text = true
    button.focus_mode = Control.FOCUS_ALL
    button.add_theme_font_size_override("font_size", 15)
    button.add_theme_color_override("font_color", color_accent_soft)
    button.add_theme_stylebox_override("focus", _focus_outline(8))
    _set_home_tab_active(button, false)
    return button

func _video_overlay_button(text: String, min_width: float) -> Button:
    var button := Button.new()
    button.text = text
    button.alignment = HORIZONTAL_ALIGNMENT_CENTER
    button.clip_text = true
    button.focus_mode = Control.FOCUS_ALL
    button.custom_minimum_size = Vector2(min_width, 48)
    button.add_theme_font_size_override("font_size", 15)
    button.add_theme_color_override("font_color", Color.WHITE)
    button.add_theme_stylebox_override("normal", _panel_style(18, Color(0.12, 0.13, 0.17, 0.82), Color(1, 1, 1, 0.12), 1))
    button.add_theme_stylebox_override("hover", _panel_style(18, Color(0.22, 0.23, 0.28, 0.94), Color(1, 1, 1, 0.28), 1))
    button.add_theme_stylebox_override("pressed", _panel_style(18, Color(0.32, 0.27, 0.44, 0.96), color_accent, 1))
    button.add_theme_stylebox_override("focus", _focus_outline(18))
    return button

func _style_video_option_button(button: OptionButton) -> void:
    button.add_theme_font_size_override("font_size", 14)
    button.add_theme_color_override("font_color", Color.WHITE)
    button.add_theme_color_override("font_hover_color", Color.WHITE)
    button.add_theme_color_override("font_pressed_color", Color.WHITE)
    button.add_theme_stylebox_override("normal", _panel_style(14, Color(0.12, 0.13, 0.17, 0.82), Color(1, 1, 1, 0.12), 1))
    button.add_theme_stylebox_override("hover", _panel_style(14, Color(0.22, 0.23, 0.28, 0.94), Color(1, 1, 1, 0.28), 1))
    button.add_theme_stylebox_override("pressed", _panel_style(14, Color(0.32, 0.27, 0.44, 0.96), color_accent, 1))
    button.add_theme_stylebox_override("focus", _focus_outline(14))

func _configure_video_option_popup(button: OptionButton) -> void:
    var popup := button.get_popup()
    popup.about_to_popup.connect(func():
        var available_height := maxi(120, int(button.get_global_rect().position.y - 16.0))
        popup.max_size = Vector2i(0, available_height)
        call_deferred("_position_video_option_popup_above", button)
    )

func _position_video_option_popup_above(button: OptionButton) -> void:
    if not is_instance_valid(button):
        return
    var popup := button.get_popup()
    if not popup.visible:
        return
    # Godot may already align the selected row with the OptionButton when it
    # auto-flips a tall menu above the bottom bar. Subtracting from that
    # automatic position moves multi-row menus far away. Anchor the popup's
    # bottom edge directly above the button instead.
    var button_rect := button.get_global_rect()
    popup.position.y = maxi(
        0,
        int(round(button_rect.position.y)) - popup.size.y - 8
    )

func _pill_button(text: String, icon_path: String = "") -> Button:
    var button := Button.new()
    button.text = text if icon_path.is_empty() else ""
    button.alignment = HORIZONTAL_ALIGNMENT_CENTER
    button.clip_text = true
    button.clip_contents = true
    button.focus_mode = Control.FOCUS_ALL
    button.add_theme_font_size_override("font_size", 15)
    if not icon_path.is_empty():
        _attach_pill_button_content(button, text, icon_path)
    ui_widgets.primary_button(button)
    return button

func _icon_action_button(
    icon_path: String,
    tooltip: String,
    callback: Callable = Callable(),
    primary: bool = false,
    destructive: bool = false,
    control_size: float = 44.0
) -> Button:
    var button := Button.new()
    button.text = ""
    button.icon = _load_ui_icon(icon_path)
    button.expand_icon = true
    button.tooltip_text = tooltip
    button.accessibility_name = tooltip
    button.custom_minimum_size = Vector2(control_size, control_size)
    button.add_theme_constant_override("icon_max_width", int(control_size * 0.44))
    if destructive:
        ui_widgets.toolbar_button(button)
        for state in ["normal", "hover", "pressed", "focus"]:
            button.add_theme_color_override("icon_%s_color" % state, ui_tokens.danger)
    elif primary:
        ui_widgets.primary_button(button)
        for state in ["normal", "hover", "pressed", "focus"]:
            button.add_theme_color_override("icon_%s_color" % state, Color.WHITE)
    else:
        ui_widgets.toolbar_button(button)
    if callback.is_valid():
        button.pressed.connect(callback)
    return button

func _reveal_icon_action_label_on_hover(button: Button, label: String) -> Button:
    button.tooltip_text = ""
    button.add_theme_constant_override("h_separation", 8)
    button.mouse_entered.connect(func(): button.text = label)
    button.mouse_exited.connect(func():
        if not button.has_focus():
            button.text = ""
    )
    button.focus_entered.connect(func(): button.text = label)
    button.focus_exited.connect(func():
        if not button.is_hovered():
            button.text = ""
    )
    return button

func _attach_pill_button_content(button: Button, text: String, icon_path: String) -> void:
    var center := CenterContainer.new()
    center.mouse_filter = Control.MOUSE_FILTER_IGNORE
    center.set_anchors_preset(Control.PRESET_FULL_RECT)
    button.add_child(center)

    var row := HBoxContainer.new()
    row.mouse_filter = Control.MOUSE_FILTER_IGNORE
    row.add_theme_constant_override("separation", 8)
    center.add_child(row)

    var icon_holder := Control.new()
    icon_holder.mouse_filter = Control.MOUSE_FILTER_IGNORE
    icon_holder.custom_minimum_size = PILL_ICON_SIZE
    icon_holder.size_flags_vertical = Control.SIZE_SHRINK_CENTER
    row.add_child(icon_holder)

    var icon := _icon_rect(icon_path, PILL_ICON_SIZE, Color.WHITE)
    icon.position = Vector2(0, PILL_ICON_VISUAL_OFFSET_Y)
    icon.size = PILL_ICON_SIZE
    icon_holder.add_child(icon)

    var label := Label.new()
    label.text = text
    label.mouse_filter = Control.MOUSE_FILTER_IGNORE
    label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
    label.size_flags_vertical = Control.SIZE_SHRINK_CENTER
    label.add_theme_font_size_override("font_size", 15)
    label.add_theme_color_override("font_color", Color.WHITE)
    row.add_child(label)

    button.set_meta("pill_icon_path", button.get_path_to(icon))
    button.set_meta("pill_label_path", button.get_path_to(label))

func _attach_centered_button_icon(button: Button, icon_path: String, icon_size: Vector2) -> void:
    var center := CenterContainer.new()
    center.mouse_filter = Control.MOUSE_FILTER_IGNORE
    center.set_anchors_preset(Control.PRESET_FULL_RECT)
    button.add_child(center)

    var icon := _icon_rect(icon_path, icon_size, Color.WHITE)
    icon.mouse_filter = Control.MOUSE_FILTER_IGNORE
    center.add_child(icon)

func _set_pill_button_text(button: Button, text: String) -> void:
    var label_path = button.get_meta("pill_label_path", NodePath(""))
    var label := button.get_node_or_null(label_path) as Label
    if label != null:
        label.text = text
        return
    button.text = text

func _sync_pill_button_content_state(button: Button) -> void:
    var tint := _disabled_text_color() if button.disabled else Color.WHITE
    var label_path = button.get_meta("pill_label_path", NodePath(""))
    var label := button.get_node_or_null(label_path) as Label
    if label != null:
        label.add_theme_color_override("font_color", tint)
    var icon_path = button.get_meta("pill_icon_path", NodePath(""))
    var icon := button.get_node_or_null(icon_path) as TextureRect
    if icon != null:
        icon.modulate = tint

func _section_title(text: String, _icon_path: String) -> HBoxContainer:
    var row := HBoxContainer.new()
    row.custom_minimum_size = Vector2(0, 24)
    var label := Label.new()
    label.text = text
    label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
    label.add_theme_font_size_override("font_size", 13)
    label.add_theme_color_override("font_color", ui_tokens.text_secondary)
    row.add_child(label)
    return row

func _settings_group(page: VBoxContainer, title: String, icon_path: String, animate: bool, delay: float) -> VBoxContainer:
    var group := VBoxContainer.new()
    group.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    group.add_theme_constant_override("separation", 6)
    page.add_child(group)
    group.add_child(_section_title(title, icon_path))
    var panel := PanelContainer.new()
    panel.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    panel.add_theme_stylebox_override("panel", ui_tokens.material_panel())
    group.add_child(panel)
    var rows := VBoxContainer.new()
    rows.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    rows.add_theme_constant_override("separation", 0)
    panel.add_child(rows)
    if animate:
        ui_motion.reveal(group, delay)
    return rows

func _add_settings_row(group: VBoxContainer, row: Control) -> void:
    if group.get_child_count() > 0:
        group.add_child(_detail_separator())
    group.add_child(row)

func _settings_block(title: String, subtitle: String, control: Control, stack_control: bool = false) -> Control:
    var margin := MarginContainer.new()
    margin.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    margin.add_theme_constant_override("margin_left", 2)
    margin.add_theme_constant_override("margin_top", 10)
    margin.add_theme_constant_override("margin_right", 2)
    margin.add_theme_constant_override("margin_bottom", 10)
    var box: BoxContainer = VBoxContainer.new() if stack_control else HBoxContainer.new()
    box.custom_minimum_size = Vector2(0, 94 if stack_control else 68)
    box.add_theme_constant_override("separation", 12 if stack_control else 18)
    margin.add_child(box)
    var labels := VBoxContainer.new()
    labels.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    labels.add_theme_constant_override("separation", 4)
    box.add_child(labels)
    var title_label := Label.new()
    title_label.text = title
    title_label.add_theme_font_size_override("font_size", 16)
    title_label.add_theme_color_override("font_color", ui_tokens.text_primary)
    labels.add_child(title_label)
    if not subtitle.is_empty():
        var sub := Label.new()
        sub.text = subtitle
        sub.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
        sub.add_theme_font_size_override("font_size", 13)
        sub.add_theme_color_override("font_color", ui_tokens.text_secondary)
        labels.add_child(sub)
    control.size_flags_horizontal = Control.SIZE_EXPAND_FILL if stack_control else Control.SIZE_SHRINK_END
    control.size_flags_vertical = Control.SIZE_SHRINK_CENTER
    box.add_child(control)
    return margin

func _settings_toggle_row(title: String, subtitle: String, initial: bool, key: String) -> Control:
    var margin := MarginContainer.new()
    margin.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    margin.add_theme_constant_override("margin_left", 2)
    margin.add_theme_constant_override("margin_top", 10)
    margin.add_theme_constant_override("margin_right", 2)
    margin.add_theme_constant_override("margin_bottom", 10)
    var row := HBoxContainer.new()
    row.custom_minimum_size = Vector2(0, 62)
    row.add_theme_constant_override("separation", 14)
    margin.add_child(row)
    var labels := VBoxContainer.new()
    labels.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    labels.add_theme_constant_override("separation", 4)
    var title_label := Label.new()
    title_label.text = title
    title_label.add_theme_font_size_override("font_size", 16)
    title_label.add_theme_color_override("font_color", ui_tokens.text_primary)
    labels.add_child(title_label)
    var sub := Label.new()
    sub.text = subtitle
    sub.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
    sub.add_theme_font_size_override("font_size", 13)
    sub.add_theme_color_override("font_color", ui_tokens.text_secondary)
    labels.add_child(sub)
    row.add_child(labels)

    var toggle := _settings_switch(initial, key)
    row.add_child(toggle)
    return margin

func _settings_switch(initial: bool, key: String) -> Button:
    var toggle = AetherSwitch.new()
    toggle.setup(ui_tokens, ui_motion, initial)
    toggle.toggled.connect(func(value: bool):
        _on_setting_toggle(key, value)
    )
    return toggle

func _settings_value_row(title: String, value: String) -> Control:
    var margin := MarginContainer.new()
    margin.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    margin.add_theme_constant_override("margin_left", 2)
    margin.add_theme_constant_override("margin_top", 8)
    margin.add_theme_constant_override("margin_right", 2)
    margin.add_theme_constant_override("margin_bottom", 8)
    var row := HBoxContainer.new()
    row.custom_minimum_size = Vector2(0, 44)
    row.add_theme_constant_override("separation", 18)
    margin.add_child(row)
    var label := Label.new()
    label.text = title
    label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
    label.add_theme_font_size_override("font_size", 17)
    label.add_theme_color_override("font_color", ui_tokens.text_primary)
    row.add_child(label)
    var value_label := Label.new()
    value_label.text = value
    value_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
    value_label.add_theme_font_size_override("font_size", 15)
    value_label.add_theme_color_override("font_color", ui_tokens.text_secondary)
    row.add_child(value_label)
    return margin

func _settings_link_value_row(title: String, value: String, action: Callable) -> Control:
    var margin := MarginContainer.new()
    margin.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    margin.add_theme_constant_override("margin_left", 2)
    margin.add_theme_constant_override("margin_top", 8)
    margin.add_theme_constant_override("margin_right", 2)
    margin.add_theme_constant_override("margin_bottom", 8)
    var row := HBoxContainer.new()
    row.custom_minimum_size = Vector2(0, 44)
    row.add_theme_constant_override("separation", 18)
    margin.add_child(row)
    var label := Label.new()
    label.text = title
    label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
    label.add_theme_font_size_override("font_size", 17)
    label.add_theme_color_override("font_color", ui_tokens.text_primary)
    row.add_child(label)
    var link := LinkButton.new()
    link.text = value
    link.underline = LinkButton.UNDERLINE_MODE_ALWAYS
    link.size_flags_vertical = Control.SIZE_SHRINK_CENTER
    link.mouse_default_cursor_shape = Control.CURSOR_POINTING_HAND
    link.add_theme_font_size_override("font_size", 15)
    var link_color := Color("0969da") if style_mode == STYLE_CLASSIC else Color("58a6ff")
    link.add_theme_color_override("font_color", link_color)
    link.add_theme_color_override("font_focus_color", link_color)
    link.add_theme_color_override("font_hover_color", link_color.lightened(0.12))
    link.add_theme_color_override("font_pressed_color", link_color.darkened(0.12))
    link.pressed.connect(action)
    row.add_child(link)
    return margin

func _settings_action_row(title: String, subtitle: String, action_text: String, action: Callable) -> Control:
    var margin := MarginContainer.new()
    margin.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    margin.add_theme_constant_override("margin_left", 2)
    margin.add_theme_constant_override("margin_top", 10)
    margin.add_theme_constant_override("margin_right", 2)
    margin.add_theme_constant_override("margin_bottom", 10)
    var compact := shell_content.size.x < 640.0
    var row: BoxContainer = VBoxContainer.new() if compact else HBoxContainer.new()
    row.custom_minimum_size = Vector2(0, 126 if compact else 92)
    row.add_theme_constant_override("separation", 12 if compact else 18)
    margin.add_child(row)
    var labels := VBoxContainer.new()
    labels.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    labels.add_theme_constant_override("separation", 6)
    row.add_child(labels)
    var title_label := Label.new()
    title_label.text = title
    title_label.add_theme_font_size_override("font_size", 16)
    title_label.add_theme_color_override("font_color", ui_tokens.text_primary)
    labels.add_child(title_label)
    var sub := Label.new()
    sub.text = subtitle
    sub.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
    sub.add_theme_font_size_override("font_size", 13)
    sub.add_theme_color_override("font_color", ui_tokens.text_secondary)
    labels.add_child(sub)
    var open := _pill_button(action_text)
    _configure_settings_action_button(open)
    open.pressed.connect(action)
    row.add_child(open)
    return margin

func _configure_settings_action_button(button: Button) -> void:
    button.custom_minimum_size = SETTINGS_ACTION_BUTTON_SIZE
    button.size_flags_horizontal = Control.SIZE_SHRINK_END
    button.size_flags_vertical = Control.SIZE_SHRINK_CENTER

func _settings_iap_product_row() -> Control:
    var margin := MarginContainer.new()
    margin.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    margin.add_theme_constant_override("margin_left", 2)
    margin.add_theme_constant_override("margin_top", 10)
    margin.add_theme_constant_override("margin_right", 2)
    margin.add_theme_constant_override("margin_bottom", 10)
    var compact := shell_content.size.x < 640.0
    var row: BoxContainer = VBoxContainer.new() if compact else HBoxContainer.new()
    row.custom_minimum_size = Vector2(0, 150 if compact else 112)
    row.add_theme_constant_override("separation", 12 if compact else 18)
    margin.add_child(row)

    var labels := VBoxContainer.new()
    labels.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    labels.add_theme_constant_override("separation", 6)
    row.add_child(labels)

    var title_label := Label.new()
    title_label.text = _t("iap.list_limit.title")
    title_label.add_theme_font_size_override("font_size", 16)
    title_label.add_theme_color_override("font_color", ui_tokens.text_primary)
    labels.add_child(title_label)

    var description := Label.new()
    description.text = _t("iap.list_limit.desc")
    description.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
    description.add_theme_font_size_override("font_size", 13)
    description.add_theme_color_override("font_color", ui_tokens.text_secondary)
    labels.add_child(description)

    var status := Label.new()
    status.text = _iap_product_status_text()
    status.add_theme_font_size_override("font_size", 15)
    status.add_theme_color_override("font_color", ui_tokens.accent)
    labels.add_child(status)

    var entitled := bool(iap_state.get("entitled", false)) or _secret_iap_unlock_active()
    var product_ready := String(iap_state.get("product_state", "idle")) == "ready"
    var price := String(iap_state.get("display_price", ""))
    var action_text := _t("iap.status.purchased") if entitled else _t("iap.buy")
    if not entitled and not price.is_empty():
        action_text = "%s  %s" % [_t("iap.buy"), price]
    var purchase := _pill_button(action_text)
    _configure_settings_action_button(purchase)
    purchase.tooltip_text = action_text
    purchase.disabled = entitled or not product_ready or iap_pending_operation_id > 0
    _sync_pill_button_content_state(purchase)
    purchase.pressed.connect(func(): _begin_iap_purchase("settings"))
    row.add_child(purchase)
    return margin

func _iap_product_status_text() -> String:
    if bool(iap_state.get("entitled", false)) or _secret_iap_unlock_active():
        return _t("iap.status.purchased")
    var product_state := String(iap_state.get("product_state", "idle"))
    if product_state in ["idle", "loading"]:
        return _t("iap.status.loading")
    if product_state != "ready":
        return _t("iap.status.unavailable")
    var price := String(iap_state.get("display_price", ""))
    if price.is_empty():
        return _t("iap.status.not_purchased")
    return "%s  ·  %s" % [_t("iap.status.not_purchased"), price]

func _settings_iap_coffee_row() -> Control:
    var margin := MarginContainer.new()
    margin.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    margin.add_theme_constant_override("margin_left", 2)
    margin.add_theme_constant_override("margin_top", 10)
    margin.add_theme_constant_override("margin_right", 2)
    margin.add_theme_constant_override("margin_bottom", 10)
    var compact := shell_content.size.x < 640.0
    var row: BoxContainer = VBoxContainer.new() if compact else HBoxContainer.new()
    row.custom_minimum_size = Vector2(0, 150 if compact else 112)
    row.add_theme_constant_override("separation", 12 if compact else 18)
    margin.add_child(row)

    var labels := VBoxContainer.new()
    labels.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    labels.add_theme_constant_override("separation", 6)
    row.add_child(labels)

    var title_label := Label.new()
    title_label.text = _t("iap.coffee.title")
    title_label.add_theme_font_size_override("font_size", 16)
    title_label.add_theme_color_override("font_color", ui_tokens.text_primary)
    labels.add_child(title_label)

    var description := Label.new()
    description.text = _t("iap.coffee.desc")
    description.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
    description.add_theme_font_size_override("font_size", 13)
    description.add_theme_color_override("font_color", ui_tokens.text_secondary)
    labels.add_child(description)

    var status := Label.new()
    status.text = _iap_coffee_status_text()
    status.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
    status.add_theme_font_size_override("font_size", 15)
    status.add_theme_color_override("font_color", ui_tokens.accent)
    labels.add_child(status)

    var product_ready := String(iap_coffee_state.get("product_state", "idle")) == "ready"
    var price := String(iap_coffee_state.get("display_price", ""))
    var action_text := _t("iap.buy")
    if not price.is_empty():
        action_text = "%s  %s" % [_t("iap.buy"), price]
    var purchase := _pill_button(action_text)
    _configure_settings_action_button(purchase)
    purchase.tooltip_text = action_text
    # This is a consumable product. Keep it purchasable while an earlier
    # 30-day grant is active so another purchase can extend the expiry.
    purchase.disabled = not product_ready or iap_pending_operation_id > 0
    _sync_pill_button_content_state(purchase)
    purchase.pressed.connect(func():
        _begin_iap_purchase("settings", IAP_COFFEE_PRODUCT_ID)
    )
    row.add_child(purchase)
    return margin

func _iap_coffee_status_text() -> String:
    var expiration := String(iap_coffee_state.get(
        "entitlement_expiration_display", ""
    )).strip_edges()
    if bool(iap_coffee_state.get("entitled", false)) and not expiration.is_empty():
        return _t("iap.coffee.active_until", [expiration])
    if _secret_coffee_active():
        return _t("iap.coffee.active_until", [_secret_coffee_expiry_text()])
    var product_state := String(iap_coffee_state.get("product_state", "idle"))
    if product_state in ["idle", "loading"]:
        return _t("iap.status.loading")
    if product_state != "ready":
        return _t("iap.status.unavailable")
    return _t("iap.coffee.inactive")

func _secret_iap_unlock_active() -> bool:
    return secret_iap_unlocked

func _secret_coffee_active() -> bool:
    return secret_coffee_until_unix > int(Time.get_unix_time_from_system())

func _secret_coffee_expiry_text() -> String:
    if secret_coffee_until_unix <= 0:
        return ""
    var expiry := Time.get_datetime_dict_from_unix_time(secret_coffee_until_unix)
    return "%04d-%02d-%02d %02d:%02d" % [
        int(expiry.get("year", 0)),
        int(expiry.get("month", 1)),
        int(expiry.get("day", 1)),
        int(expiry.get("hour", 0)),
        int(expiry.get("minute", 0)),
    ]

func _attach_secret_version_tap(row: Control) -> void:
    row.gui_input.connect(func(event: InputEvent):
        if event is InputEventMouseButton \
                and event.pressed \
                and event.button_index == MOUSE_BUTTON_LEFT:
            _register_secret_version_tap()
    )

func _register_secret_version_tap() -> void:
    var now := Time.get_ticks_msec()
    if now - secret_version_last_tap_msec > SECRET_UNLOCK_TAP_WINDOW_MSEC:
        secret_version_tap_count = 0
    secret_version_last_tap_msec = now
    secret_version_tap_count += 1
    if secret_version_tap_count >= SECRET_UNLOCK_TAP_TARGET:
        secret_version_tap_count = 0
        _show_secret_unlock_dialog()

func _verify_secret_unlock(candidate: String) -> bool:
    if candidate.is_empty():
        return false
    if player == null or not player.has_method("verify_unlock_secret"):
        return false
    return bool(player.verify_unlock_secret(candidate))

func _apply_secret_unlock() -> void:
    var now := int(Time.get_unix_time_from_system())
    secret_iap_unlocked = true
    secret_coffee_until_unix = maxi(secret_coffee_until_unix, now) + SECRET_UNLOCK_COFFEE_SEC
    _persist_secret_unlock_state()
    if is_instance_valid(settings_view) and settings_view.visible:
        _rebuild_settings_view()

func _persist_secret_unlock_state() -> void:
    # Merge into the settings file without saving unrelated in-progress drafts.
    var cfg := ConfigFile.new()
    cfg.load(SETTINGS_FILE)
    cfg.set_value("unlock", "secret_iap_unlocked", secret_iap_unlocked)
    cfg.set_value("unlock", "secret_coffee_until_unix", secret_coffee_until_unix)
    cfg.save(SETTINGS_FILE)

func _show_secret_unlock_dialog() -> void:
    modal_layer.visible = true
    modal_layer.move_to_front()
    for child in modal_layer.get_children():
        child.queue_free()
    var dim := ColorRect.new()
    dim.color = Color(0, 0, 0, 0.52)
    dim.set_anchors_preset(Control.PRESET_FULL_RECT)
    dim.mouse_filter = Control.MOUSE_FILTER_STOP
    modal_layer.add_child(dim)
    var width := 560.0
    if is_inside_tree():
        width = minf(width, get_viewport_rect().size.x * 0.92)
    var height := 380.0
    var dialog := PanelContainer.new()
    dialog.anchor_left = 0.5
    dialog.anchor_top = 0.5
    dialog.anchor_right = 0.5
    dialog.anchor_bottom = 0.5
    dialog.position = Vector2(-width * 0.5, -height * 0.5)
    dialog.size = Vector2(width, height)
    dialog.add_theme_stylebox_override(
        "panel",
        _panel_style(22, color_card, Color(0, 0, 0, 0.06), 1)
    )
    modal_layer.add_child(dialog)
    var box := VBoxContainer.new()
    box.add_theme_constant_override("separation", 20)
    dialog.add_child(box)
    var title := Label.new()
    title.text = _t("secret.unlock.title")
    title.add_theme_font_size_override("font_size", 30)
    title.add_theme_color_override("font_color", color_text)
    box.add_child(title)
    var body := Label.new()
    body.text = _t("secret.unlock.body")
    body.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
    body.add_theme_font_size_override("font_size", 20)
    body.add_theme_color_override("font_color", color_text)
    box.add_child(body)
    var input := LineEdit.new()
    input.secret = true
    input.max_length = 64
    input.placeholder_text = _t("secret.unlock.placeholder")
    input.custom_minimum_size = Vector2(0, 62)
    input.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    input.add_theme_font_size_override("font_size", 22)
    box.add_child(input)
    var error_label := Label.new()
    error_label.text = _t("secret.unlock.failed")
    error_label.visible = false
    error_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
    error_label.add_theme_font_size_override("font_size", 16)
    error_label.add_theme_color_override("font_color", Color(0.94, 0.35, 0.32))
    box.add_child(error_label)
    var buttons := HBoxContainer.new()
    buttons.alignment = BoxContainer.ALIGNMENT_END
    buttons.add_theme_constant_override("separation", 14)
    buttons.custom_minimum_size = Vector2(0, 62)
    box.add_child(buttons)
    var cancel := Button.new()
    cancel.text = _t("dialog.cancel")
    cancel.flat = true
    cancel.custom_minimum_size = Vector2(130, 60)
    cancel.add_theme_font_size_override("font_size", 20)
    cancel.add_theme_color_override("font_color", color_text)
    cancel.pressed.connect(func():
        modal_layer.visible = false
    )
    buttons.add_child(cancel)
    var confirm := _pill_button(_t("secret.unlock.confirm"))
    confirm.custom_minimum_size = Vector2(180, 60)
    confirm.pressed.connect(func(): _submit_secret_unlock(input, error_label))
    buttons.add_child(confirm)
    input.text_submitted.connect(func(_text: String):
        _submit_secret_unlock(input, error_label)
    )
    if input.is_inside_tree():
        input.call_deferred("grab_focus")

func _submit_secret_unlock(input: LineEdit, error_label: Label) -> void:
    var candidate := input.text if is_instance_valid(input) else ""
    if _verify_secret_unlock(candidate):
        modal_layer.visible = false
        _apply_secret_unlock()
        _show_system_alert(
            _t("secret.unlock.success", [_secret_coffee_expiry_text()]),
            _t("secret.unlock.title")
        )
        return
    input.text = ""
    if is_instance_valid(error_label):
        error_label.visible = true

func _apple_select(width: float = 220.0):
    var select = AetherSelect.new()
    select.setup(
        ui_tokens,
        ui_motion,
        _load_ui_icon(ICON_CHEVRON_DOWN),
        _load_ui_icon(ICON_CHECK)
    )
    select.custom_minimum_size.x = width
    return select

func _keyboard_controls_opacity_control() -> Control:
    var row := HBoxContainer.new()
    row.name = "KeyboardControlsOpacityControl"
    row.custom_minimum_size = Vector2(272.0, 40.0)
    row.add_theme_constant_override("separation", 8)

    var slider = AetherSlider.new()
    slider.name = "KeyboardControlsOpacitySlider"
    slider.min_value = GAME_VIRTUAL_KEYBOARD_OPACITY_MIN
    slider.max_value = GAME_VIRTUAL_KEYBOARD_OPACITY_MAX
    slider.step = GAME_VIRTUAL_KEYBOARD_OPACITY_STEP
    slider.setup(
        ui_tokens,
        _normalize_game_virtual_keyboard_opacity(_settings_draft_float(
            "game_virtual_keyboard_opacity",
            game_virtual_keyboard_opacity
        ))
    )
    row.add_child(slider)

    var value_label := Label.new()
    value_label.name = "KeyboardControlsOpacityValue"
    value_label.custom_minimum_size = Vector2(44.0, 40.0)
    value_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
    value_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
    value_label.add_theme_font_size_override("font_size", 13)
    value_label.add_theme_color_override("font_color", ui_tokens.text_secondary)
    value_label.text = _opacity_percentage_text(slider.value)
    row.add_child(value_label)

    slider.value_changed.connect(func(value: float):
        var normalized := _normalize_game_virtual_keyboard_opacity(value)
        value_label.text = _opacity_percentage_text(normalized)
        _set_settings_draft_value("game_virtual_keyboard_opacity", normalized)
    )
    return row

func _opacity_percentage_text(value: float) -> String:
    return "%d%%" % int(round(clampf(value, 0.0, 1.0) * 100.0))

func _settings_fps_row() -> Control:
    var margin := MarginContainer.new()
    margin.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    margin.add_theme_constant_override("margin_left", 2)
    margin.add_theme_constant_override("margin_top", 10)
    margin.add_theme_constant_override("margin_right", 2)
    margin.add_theme_constant_override("margin_bottom", 10)
    var row := HBoxContainer.new()
    row.custom_minimum_size = Vector2(0, 62)
    row.add_theme_constant_override("separation", 14)
    margin.add_child(row)
    var labels := VBoxContainer.new()
    labels.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    labels.add_theme_constant_override("separation", 6)
    var title_label := Label.new()
    title_label.text = _t("settings.target_fps")
    title_label.add_theme_font_size_override("font_size", 18)
    title_label.add_theme_color_override("font_color", ui_tokens.text_primary)
    labels.add_child(title_label)
    var sub := Label.new()
    sub.text = _t("settings.target_fps_desc")
    sub.add_theme_font_size_override("font_size", 14)
    sub.add_theme_color_override("font_color", ui_tokens.text_secondary)
    labels.add_child(sub)
    row.add_child(labels)

    var fps_select = _apple_select(150)
    var options := [60, 80, 90, 120, 144]
    var selected_index := 0
    var draft_target_fps := _settings_draft_int("target_fps", target_fps)
    for i in range(options.size()):
        fps_select.add_item("%d FPS" % options[i])
        fps_select.set_item_metadata(i, options[i])
        if options[i] == draft_target_fps:
            selected_index = i
    fps_select.select(selected_index)
    fps_select.item_selected.connect(func(index: int):
        _set_settings_draft_value("target_fps", int(fps_select.get_item_metadata(index)))
    )
    row.add_child(fps_select)
    return margin

func _language_select() -> Control:
    var select = _apple_select()
    var selected_index := 0
    var draft_language := _normalize_language_mode(_settings_draft_string("language", language_mode))
    for i in range(LANGUAGE_MODES.size()):
        var mode := String(LANGUAGE_MODES[i])
        select.add_item(_language_option_label(mode))
        select.set_item_metadata(i, mode)
        if mode == draft_language:
            selected_index = i
    select.select(selected_index)
    select.item_selected.connect(func(index: int):
        _select_language_mode(String(select.get_item_metadata(index)))
    )
    return select

func _style_select() -> Control:
    var select = _apple_select()
    var selected_index := 0
    var draft_style := _normalize_style_mode(_settings_draft_string("style", style_mode))
    for i in range(STYLE_MODES.size()):
        var mode := String(STYLE_MODES[i])
        select.add_item(_style_option_label(mode))
        select.set_item_metadata(i, mode)
        if mode == draft_style:
            selected_index = i
    select.select(selected_index)
    select.item_selected.connect(func(index: int):
        _select_style_mode(String(select.get_item_metadata(index)))
    )
    return select

func _ios_ui_scale_segment() -> Control:
    var draft_mode := _settings_draft_string("ios_ui_scale_mode", ios_ui_scale_mode)
    var selected_index := IOS_UI_SCALE_MODES.find(draft_mode)
    var segment = AetherSegmentedControl.new()
    segment.setup(
        ui_tokens,
        ui_motion,
        PackedStringArray([
            _t("ui_scale.compact"),
            _t("ui_scale.comfortable"),
            _t("ui_scale.standard"),
        ]),
        maxi(0, selected_index)
    )
    segment.item_selected.connect(func(index: int):
        if index >= 0 and index < IOS_UI_SCALE_MODES.size():
            _set_settings_draft_value("ios_ui_scale_mode", IOS_UI_SCALE_MODES[index])
    )
    return segment

func _upscale_select() -> Control:
    var select = _apple_select()
    var options := [
        {"label": "Smooth", "value": "smooth"},
        {"label": "Linear", "value": "linear"},
        {"label": "Nearest", "value": "nearest"},
        {"label": "Bicubic", "value": "bicubic"},
        {"label": "Lanczos", "value": "lanczos"},
    ]
    var selected_index := 0
    var draft_upscale := _settings_draft_string("upscale_algorithm", upscale_algorithm)
    for i in range(options.size()):
        select.add_item(String(options[i]["label"]))
        select.set_item_metadata(i, String(options[i]["value"]))
        if String(options[i]["value"]) == draft_upscale:
            selected_index = i
    select.select(selected_index)
    select.item_selected.connect(func(index: int):
        _select_upscale_algorithm(String(select.get_item_metadata(index)))
    )
    return select

func _output_resolution_select() -> Control:
    var select = _apple_select()
    var options := [
        {"label": _t("settings.output_resolution.original"), "value": "original"},
        {"label": "1080p", "value": "1080p"},
        {"label": "2K (2560×1440)", "value": "2k"},
        {"label": "4K (3840×2160)", "value": "4k"},
    ]
    var selected_index := 0
    var draft_resolution := _normalize_output_resolution(_settings_draft_string(
        "output_resolution",
        output_resolution
    ))
    for i in range(options.size()):
        select.add_item(String(options[i]["label"]))
        select.set_item_metadata(i, String(options[i]["value"]))
        if String(options[i]["value"]) == draft_resolution:
            selected_index = i
    select.select(selected_index)
    select.item_selected.connect(func(index: int):
        _select_output_resolution(String(select.get_item_metadata(index)))
    )
    return select

func _frame_enhancement_mode_select() -> Control:
    var select = _apple_select()
    var selected_index := 0
    var draft_mode := _normalize_frame_enhancement_mode(_settings_draft_string(
        "frame_enhancement_mode",
        frame_enhancement_mode
    ))
    for value in FRAME_ENHANCEMENT_PRESET_MODES:
        select.add_item(_t("settings.frame_enhancement_mode.%s" % value))
        select.set_item_metadata(select.item_count - 1, value)
        if value == draft_mode:
            selected_index = select.item_count - 1
    select.select(selected_index)
    select.item_selected.connect(func(index: int):
        _select_frame_enhancement_mode(String(select.get_item_metadata(index)))
    )
    return select

func _frame_enhancement_kind_select() -> Control:
    var select = _apple_select()
    var draft_kind := _normalize_frame_enhancement_kind(_settings_draft_string(
        "frame_enhancement_kind",
        frame_enhancement_kind
    ))
    var selected_index := 0
    for kind in FRAME_ENHANCEMENT_KINDS:
        select.add_item(_t("settings.frame_enhancement_kind.%s" % kind))
        select.set_item_metadata(select.item_count - 1, kind)
        if kind == draft_kind:
            selected_index = select.item_count - 1
    select.select(selected_index)
    select.item_selected.connect(func(index: int):
        _select_frame_enhancement_kind(String(select.get_item_metadata(index)))
    )
    return select

func _frame_enhancement_custom_editor() -> Control:
    var editor := VBoxContainer.new()
    editor.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    editor.add_theme_constant_override("separation", 12)
    var chain := _settings_draft_custom_chain()
    if chain.is_empty():
        var empty := Label.new()
        empty.text = _t("settings.frame_enhancement_custom_empty")
        empty.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
        empty.add_theme_font_size_override("font_size", 13)
        empty.add_theme_color_override("font_color", ui_tokens.text_secondary)
        editor.add_child(empty)
    for index in range(chain.size()):
        editor.add_child(_frame_enhancement_custom_row(index, chain[index]))
    var add_button := _pill_button(_t("settings.frame_enhancement_custom_add"))
    # Clipped Button text is excluded from Godot's minimum-width calculation.
    # This control shrinks to its content, so give the localized label a real
    # width and keep it visible instead of leaving only the stylebox margins.
    add_button.clip_text = false
    add_button.custom_minimum_size = Vector2(220, 44)
    add_button.size_flags_horizontal = Control.SIZE_SHRINK_BEGIN
    add_button.disabled = chain.size() >= FRAME_ENHANCEMENT_CUSTOM_MAX_STEPS
    _sync_pill_button_content_state(add_button)
    add_button.pressed.connect(_add_frame_enhancement_custom_algorithm)
    editor.add_child(add_button)
    return editor

func _frame_enhancement_custom_row(index: int, algorithm_id: String) -> Control:
    var row := HBoxContainer.new()
    row.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    row.custom_minimum_size = Vector2(0, 72)
    row.add_theme_constant_override("separation", 10)
    var summary := VBoxContainer.new()
    summary.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    summary.add_theme_constant_override("separation", 3)
    row.add_child(summary)
    var title := Label.new()
    title.text = "%s · %s" % [
        _t("settings.frame_enhancement_custom_step") % (index + 1),
        String(FRAME_ENHANCEMENT_ALGORITHM_LABELS.get(algorithm_id, algorithm_id)),
    ]
    title.add_theme_font_size_override("font_size", 14)
    title.add_theme_color_override("font_color", ui_tokens.text_primary)
    summary.add_child(title)
    var description := Label.new()
    description.text = _t("settings.frame_enhancement_algorithm.%s.desc" % algorithm_id)
    description.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
    description.add_theme_font_size_override("font_size", 12)
    description.add_theme_color_override("font_color", ui_tokens.text_secondary)
    summary.add_child(description)
    var select = _apple_select(250)
    var selected_index := 0
    for option_index in range(FRAME_ENHANCEMENT_ALGORITHMS.size()):
        var option_id: String = FRAME_ENHANCEMENT_ALGORITHMS[option_index]
        select.add_item(String(FRAME_ENHANCEMENT_ALGORITHM_LABELS[option_id]))
        select.set_item_metadata(option_index, option_id)
        if option_id == algorithm_id:
            selected_index = option_index
    select.select(selected_index)
    select.item_selected.connect(func(option_index: int):
        _select_frame_enhancement_custom_algorithm(
            index,
            String(select.get_item_metadata(option_index))
        )
    )
    row.add_child(select)
    var remove := Button.new()
    remove.text = "−"
    remove.tooltip_text = _t("settings.frame_enhancement_custom_remove")
    remove.accessibility_name = remove.tooltip_text
    remove.custom_minimum_size = Vector2(40, 40)
    remove.size_flags_vertical = Control.SIZE_SHRINK_CENTER
    remove.add_theme_font_size_override("font_size", 20)
    remove.add_theme_color_override("font_color", ui_tokens.text_secondary)
    remove.add_theme_color_override("font_hover_color", ui_tokens.danger)
    remove.add_theme_stylebox_override("normal", ui_tokens.button_style(Color.TRANSPARENT, ui_tokens.separator, 8))
    remove.add_theme_stylebox_override("hover", ui_tokens.button_style(ui_tokens.surface_hover, ui_tokens.danger, 8))
    remove.add_theme_stylebox_override("pressed", ui_tokens.button_style(ui_tokens.accent_fill, ui_tokens.danger, 8))
    remove.pressed.connect(func(): _remove_frame_enhancement_custom_algorithm(index))
    ui_motion.bind_pressable(remove)
    row.add_child(remove)
    return row

func _surface_mode_select() -> Control:
    var select = _apple_select()
    var options := [
        {"label": "Game Native", "value": RENDER_SURFACE_MODE_GAME},
        {"label": "Display Fit", "value": RENDER_SURFACE_MODE_DISPLAY},
    ]
    var selected_index := 0
    var draft_surface_mode := _settings_draft_string("surface_mode", render_surface_mode)
    for i in range(options.size()):
        select.add_item(String(options[i]["label"]))
        select.set_item_metadata(i, String(options[i]["value"]))
        if String(options[i]["value"]) == draft_surface_mode:
            selected_index = i
    select.select(selected_index)
    select.item_selected.connect(func(index: int):
        _select_surface_mode(String(select.get_item_metadata(index)))
    )
    return select

func _plugin_load_mode_select() -> Control:
    var select = _apple_select()
    var options := [
        {"label": _t("settings.plugin_load.core"), "value": "krkrsdl3"},
        {"label": _t("settings.plugin_load.all"), "value": "aether_all"},
    ]
    var selected_index := 0
    var draft_plugin_load_mode := _settings_draft_string("plugin_load_mode", plugin_load_mode)
    for i in range(options.size()):
        select.add_item(String(options[i]["label"]))
        select.set_item_metadata(i, String(options[i]["value"]))
        if String(options[i]["value"]) == draft_plugin_load_mode:
            selected_index = i
    select.select(selected_index)
    select.item_selected.connect(func(index: int):
        _select_plugin_load_mode(String(select.get_item_metadata(index)))
    )
    return select

func _diagnostic_profile_select() -> Control:
    var select = _apple_select()
    var selected_index := 0
    var draft_value := _settings_draft_string("diagnostic_profile", diagnostic_profile)
    for value in DIAGNOSTIC_PROFILES:
        select.add_item(_t("profile.%s" % value))
        select.set_item_metadata(select.item_count - 1, value)
        if value == draft_value:
            selected_index = select.item_count - 1
    select.select(selected_index)
    select.item_selected.connect(func(index: int):
        _set_settings_draft_value("diagnostic_profile", String(select.get_item_metadata(index)))
    )
    return select

func _debug_overlay_select() -> Control:
    var select = _apple_select()
    var selected_index := 0
    var draft_value := _settings_draft_string("debug_overlay_mode", debug_overlay_mode)
    for value in DEBUG_OVERLAY_MODES:
        select.add_item(_t("overlay.%s" % value))
        select.set_item_metadata(select.item_count - 1, value)
        if value == draft_value:
            selected_index = select.item_count - 1
    select.select(selected_index)
    select.item_selected.connect(func(index: int):
        _set_settings_draft_value("debug_overlay_mode", String(select.get_item_metadata(index)))
    )
    return select

func _backend_segment() -> Control:
    var draft_backend := _normalize_backend_name(_settings_draft_string("backend", selected_backend))
    var segment = AetherSegmentedControl.new()
    segment.setup(
        ui_tokens,
        ui_motion,
        PackedStringArray(["Godot Native", "Debug CPU"]),
        1 if draft_backend == "Debug CPU" else 0
    )
    segment.item_selected.connect(func(index: int):
        _select_backend("Debug CPU" if index == 1 else "Godot Native")
    )
    return segment

func _on_setting_toggle(key: String, value: bool) -> void:
    if key == "fps_limit":
        _set_settings_draft_value("fps_limit_enabled", value)
    elif key == "game_virtual_menu":
        _set_settings_draft_value("game_virtual_menu_enabled", value)
    elif key == "landscape":
        _set_settings_draft_value("force_landscape", value)
    elif key == "mock":
        _set_settings_draft_value("mock_enabled", value)
    elif key == "error_dialog_logs":
        _set_settings_draft_value("error_dialog_logs", value)
    elif key.begins_with("advanced_"):
        var option: String = String({
            "advanced_plugin_trace": "plugin_trace",
            "advanced_trace_log": "trace_log",
            "advanced_console_log": "console_log_file",
            "advanced_export_tjs": "export_scripts",
        }.get(key, ""))
        if not option.is_empty():
            _set_advanced_tool(option, value)
    if key == "fps_limit":
        call_deferred("_rebuild_settings_view")

func _select_backend(value: String) -> void:
    var index := BACKENDS.find(value)
    if index < 0:
        return
    _set_settings_draft_value("backend", BACKENDS[index])

func _select_upscale_algorithm(value: String) -> void:
    if not value in ["smooth", "nearest", "linear", "bicubic", "lanczos"]:
        return
    _set_settings_draft_value("upscale_algorithm", value)

func _normalize_output_resolution(value: String) -> String:
    var normalized := value.strip_edges().to_lower()
    if normalized == "1440p":
        normalized = "2k"
    elif normalized == "2160p":
        normalized = "4k"
    return normalized if normalized in OUTPUT_RESOLUTION_MODES else OUTPUT_RESOLUTION_DEFAULT

func _select_output_resolution(value: String) -> void:
    var normalized := _normalize_output_resolution(value)
    if normalized != value.strip_edges().to_lower():
        return
    _set_settings_draft_value("output_resolution", normalized)

func _normalize_frame_enhancement_mode(value: String) -> String:
    var normalized := value.strip_edges().to_lower()
    if normalized == "auto":
        normalized = FRAME_ENHANCEMENT_MODE_DEFAULT
    return normalized if normalized in FRAME_ENHANCEMENT_MODES else FRAME_ENHANCEMENT_MODE_DEFAULT

func _normalize_frame_enhancement_kind(value: String) -> String:
    var normalized := value.strip_edges().to_lower()
    return normalized if normalized in FRAME_ENHANCEMENT_KINDS else "off"

func _normalize_frame_enhancement_custom_chain(value) -> PackedStringArray:
    var normalized := PackedStringArray()
    if not (value is Array or value is PackedStringArray):
        value = FRAME_ENHANCEMENT_CUSTOM_DEFAULT
    for entry in value:
        var algorithm_id := String(entry).strip_edges().to_lower()
        if algorithm_id in FRAME_ENHANCEMENT_ALGORITHMS:
            normalized.push_back(algorithm_id)
        if normalized.size() >= FRAME_ENHANCEMENT_CUSTOM_MAX_STEPS:
            break
    return normalized

func _select_frame_enhancement_kind(value: String) -> void:
    var normalized := _normalize_frame_enhancement_kind(value)
    if normalized != value.strip_edges().to_lower():
        return
    _set_settings_draft_value("frame_enhancement_kind", normalized)
    _set_settings_draft_value("frame_enhancement_enabled", normalized != "off")
    call_deferred("_rebuild_settings_after_enhancement_change")

func _select_frame_enhancement_mode(value: String) -> void:
    var normalized := _normalize_frame_enhancement_mode(value)
    if normalized != value.strip_edges().to_lower():
        return
    _set_settings_draft_value("frame_enhancement_mode", normalized)

func _select_frame_enhancement_custom_algorithm(index: int, value: String) -> void:
    if not value in FRAME_ENHANCEMENT_ALGORITHMS:
        return
    var chain := _settings_draft_custom_chain()
    if index < 0 or index >= chain.size():
        return
    chain[index] = value
    _set_settings_draft_value("frame_enhancement_custom_chain", chain)
    call_deferred("_rebuild_settings_after_enhancement_change")

func _add_frame_enhancement_custom_algorithm() -> void:
    var chain := _settings_draft_custom_chain()
    if chain.size() >= FRAME_ENHANCEMENT_CUSTOM_MAX_STEPS:
        return
    chain.push_back("anime4k_upscale_s")
    _set_settings_draft_value("frame_enhancement_custom_chain", chain)
    call_deferred("_rebuild_settings_after_enhancement_change")

func _remove_frame_enhancement_custom_algorithm(index: int) -> void:
    var chain := _settings_draft_custom_chain()
    if index < 0 or index >= chain.size():
        return
    chain.remove_at(index)
    _set_settings_draft_value("frame_enhancement_custom_chain", chain)
    call_deferred("_rebuild_settings_after_enhancement_change")

func _rebuild_settings_after_enhancement_change() -> void:
    if settings_view == null or not is_instance_valid(settings_view):
        return
    var restore_scroll := settings_view.scroll_vertical
    settings_animate_next = false
    _rebuild_settings_view()
    call_deferred("_restore_settings_scroll_after_relayout", restore_scroll)

func _default_render_surface_mode() -> String:
    return RENDER_SURFACE_MODE_GAME

func _select_config_surface_mode(value: String) -> void:
    if value in [RENDER_SURFACE_MODE_GAME, RENDER_SURFACE_MODE_DISPLAY]:
        render_surface_mode = value
    else:
        render_surface_mode = _default_render_surface_mode()

func _select_surface_mode(value: String) -> void:
    if not value in [RENDER_SURFACE_MODE_GAME, RENDER_SURFACE_MODE_DISPLAY]:
        return
    _set_settings_draft_value("surface_mode", value)

func _select_plugin_load_mode(value: String) -> void:
    if not value in ["krkrsdl3", "aether_all"]:
        return
    _set_settings_draft_value("plugin_load_mode", value)

func _select_language_mode(value: String) -> void:
    var next_language := _normalize_language_mode(value)
    if next_language == _normalize_language_mode(_settings_draft_string("language", language_mode)):
        return
    _set_settings_draft_value("language", next_language)

func _select_style_mode(value: String) -> void:
    var next_style := _normalize_style_mode(value)
    if next_style == _normalize_style_mode(_settings_draft_string("style", style_mode)):
        return
    _set_settings_draft_value("style", next_style)

func _rebuild_shell_views_after_style_change() -> void:
    if shell_root == null:
        return
    var was_home := is_instance_valid(home_view) and home_view.visible
    var was_settings := is_instance_valid(settings_view) and settings_view.visible
    var was_detail := is_instance_valid(detail_view) and detail_view.visible

    _remove_shell_view(shell_root)
    shell_root = Control.new()
    shell_root.set_anchors_preset(Control.PRESET_FULL_RECT)
    add_child(shell_root)
    _build_shell_chrome()
    _build_home_view()
    _build_settings_view()
    _build_detail_view()
    _fit_full_rects()
    if modal_layer != null:
        modal_layer.move_to_front()
    if loading_panel != null:
        loading_panel.move_to_front()

    if was_settings:
        home_view.visible = false
        detail_view.visible = false
        settings_view.visible = true
        _rebuild_settings_view()
    elif was_detail and not selected_game.is_empty():
        _show_detail(selected_game)
    else:
        home_view.visible = was_home or not was_detail
        settings_view.visible = false
        detail_view.visible = false
        _refresh_games()

func _remove_shell_view(view: Control) -> void:
    if view == null or not is_instance_valid(view):
        return
    var parent := view.get_parent()
    if parent != null:
        parent.remove_child(view)
    view.queue_free()

func _refresh_language_texts() -> void:
    if diagnostic_session != null:
        diagnostic_session.refresh_language()
    if debug_console != null:
        debug_console.refresh_language()
    if is_instance_valid(shell_library_button):
        shell_library_button.text = _t("nav.library")
    if is_instance_valid(shell_video_button):
        shell_video_button.text = _t("nav.videos")
    if is_instance_valid(shell_settings_button):
        shell_settings_button.text = _t("settings.title")
    if is_instance_valid(shell_compact_library_button):
        shell_compact_library_button.tooltip_text = _t("nav.library")
    if is_instance_valid(shell_compact_video_button):
        shell_compact_video_button.tooltip_text = _t("nav.videos")
    if is_instance_valid(shell_compact_settings_button):
        shell_compact_settings_button.tooltip_text = _t("settings.title")
    if is_instance_valid(shell_sidebar_toggle):
        _apply_sidebar_presentation(false)
    _sync_shell_route(shell_route)
    _sync_home_header_text()
    if is_instance_valid(home_game_tab):
        _set_pill_button_text(home_game_tab, _t("home.status"))
    if is_instance_valid(home_video_tab):
        _set_pill_button_text(home_video_tab, _t("video.status"))
    if is_instance_valid(empty_title_label):
        empty_title_label.text = _t("home.empty_title")
    if is_instance_valid(empty_help_label):
        empty_help_label.text = _empty_help_text()
    if is_instance_valid(video_empty_title_label):
        video_empty_title_label.text = _t("video.empty_title")
    if is_instance_valid(video_empty_help_label):
        video_empty_help_label.text = _video_empty_help_text()
    _sync_home_empty_state_text()
    if is_instance_valid(empty_primary_button):
        _set_pill_button_text(empty_primary_button, _t("home.refresh") if OS.get_name() == "iOS" else _t("home.import"))
    _sync_home_action_labels()
    if is_instance_valid(loading_title_label):
        _set_translation_loading_notice(translation_loading_notice_active)

func _empty_help_text() -> String:
    if OS.get_name() == "iOS":
        return _t("home.empty_help_ios")
    if OS.get_name() == "Web":
        return _t("home.empty_help_web")
    return _t("home.empty_help_desktop")

func _video_empty_help_text() -> String:
    return _t("video.empty_help_ios") if OS.get_name() == "iOS" else _t("video.empty_help_desktop")

func _select_home_library(mode: String) -> void:
    if not mode in ["game", "video"]:
        return
    home_library_mode = mode
    _sync_shell_route("videos" if mode == "video" else "library")
    _apply_home_library_visibility()
    _refresh_language_texts()
    if mode == "video":
        _refresh_videos()
    else:
        _refresh_games()

func _apply_home_library_visibility() -> void:
    var video_mode := home_library_mode == "video"
    if is_instance_valid(game_scroll):
        game_scroll.visible = not video_mode and home_filtered_game_count > 0
    if is_instance_valid(empty_state):
        empty_state.visible = not video_mode and home_filtered_game_count == 0
    if is_instance_valid(video_scroll):
        video_scroll.visible = video_mode and home_filtered_video_count > 0
    if is_instance_valid(video_empty_state):
        video_empty_state.visible = video_mode and home_filtered_video_count == 0
    _sync_home_empty_state_text()
    if is_instance_valid(home_game_tab):
        _set_home_tab_active(home_game_tab, not video_mode)
    if is_instance_valid(home_video_tab):
        _set_home_tab_active(home_video_tab, video_mode)

func _sync_home_empty_state_text() -> void:
    var searching := not _current_home_search_query().is_empty()
    if is_instance_valid(empty_title_label):
        empty_title_label.text = _t("search.no_results_title") if searching else _t("home.empty_title")
    if is_instance_valid(empty_help_label):
        empty_help_label.text = _t("search.no_results_help") if searching else _empty_help_text()
    if is_instance_valid(empty_primary_button):
        empty_primary_button.visible = not searching
    if is_instance_valid(video_empty_title_label):
        video_empty_title_label.text = _t("search.no_results_title") if searching else _t("video.empty_title")
    if is_instance_valid(video_empty_help_label):
        video_empty_help_label.text = _t("search.no_results_help") if searching else _video_empty_help_text()

func _set_home_tab_active(button: Button, active: bool) -> void:
    button.disabled = false
    button.add_theme_stylebox_override("normal", _panel_style(8, color_card_alt, color_line, 1))
    button.add_theme_stylebox_override("hover", _panel_style(8, color_card_hover, color_accent_soft, 1))
    button.add_theme_stylebox_override("pressed", _panel_style(8, color_card_alt.darkened(0.08), color_accent_soft, 1))
    button.add_theme_color_override("font_color", color_accent_soft if active else color_muted)
    button.add_theme_color_override("font_hover_color", color_accent_soft)
    button.add_theme_color_override("font_pressed_color", color_accent_soft)

func _shell_view_for_route(route: String) -> Control:
    match route:
        "settings":
            return settings_view
        "detail":
            return detail_view
        "videos":
            return home_view
        _:
            return home_view

func _stage_shell_route(previous_route: String, incoming: Control) -> Control:
    var outgoing := _shell_view_for_route(previous_route)
    for view in [home_view, settings_view, detail_view]:
        if view == null:
            continue
        ui_motion.settle_route(view, view == incoming or view == outgoing)
    return outgoing

func _animate_shell_route(outgoing: Control, incoming: Control, lift: bool = true) -> void:
    if outgoing == incoming:
        ui_motion.settle_route(incoming, true)
        return
    ui_motion.route_transition(outgoing, incoming, lift)

func _show_home() -> void:
    _show_library("game")

func _show_video_library() -> void:
    _show_library("video")

func _show_library(mode: String) -> void:
    if not mode in ["game", "video"]:
        mode = "game"
    if _request_settings_navigation(
        Callable(self, "_show_library").bind(mode)
    ):
        return
    var previous_route := shell_route
    var returning_from_detail := previous_route == "detail" and not hero_source_path.is_empty()
    var detail_rect := detail_hero_cover.get_global_rect() if is_instance_valid(detail_hero_cover) else Rect2()
    var outgoing := _stage_shell_route(previous_route, home_view)
    _reset_shell_scroll_drag()
    _discard_settings_draft()
    _set_game_background(false)
    modal_layer.visible = false
    home_library_mode = mode
    _sync_home_header_text()
    _sync_home_action_labels()
    _sync_shell_route("videos" if mode == "video" else "library")
    if mode == "video":
        _refresh_videos()
    else:
        _refresh_games()
    if previous_route != "library":
        _animate_shell_route(outgoing, home_view, not returning_from_detail)
    if returning_from_detail:
        call_deferred("_animate_hero_back", detail_rect)

func _show_settings() -> void:
    if shell_route == "settings":
        return
    var previous_route := shell_route
    var outgoing := _stage_shell_route(previous_route, settings_view)
    _finish_hero_overlay()
    _clear_hero_state()
    _reset_shell_scroll_drag()
    settings_animate_next = shell_route != "settings"
    _begin_settings_edit()
    _set_game_background(false)
    modal_layer.visible = false
    _sync_shell_route("settings")
    _fit_full_rects()
    call_deferred("_rebuild_settings_view")
    if previous_route != "settings":
        _animate_shell_route(outgoing, settings_view)

func _show_detail(game: Dictionary, source: Control = null) -> void:
    if _request_settings_navigation(
        Callable(self, "_show_detail").bind(game, source)
    ):
        return
    var previous_route := shell_route
    var animate_hero := false
    if source != null and is_instance_valid(source):
        var source_cover: Control = source.get_meta("hero_cover", null)
        if source_cover != null and is_instance_valid(source_cover):
            hero_source_rect = source_cover.get_global_rect()
            hero_source_path = String(game.get("path", ""))
            hero_source_texture = _load_cover_texture(game)
            animate_hero = true
    var outgoing := _stage_shell_route(previous_route, detail_view)
    _reset_shell_scroll_drag()
    _set_game_background(false)
    selected_game = game
    modal_layer.visible = false
    _sync_shell_route("detail")
    if previous_route != "detail" and not animate_hero:
        _animate_shell_route(outgoing, detail_view)
    elif previous_route != "detail":
        _animate_shell_route(outgoing, detail_view, false)
    _rebuild_detail_contents(game, animate_hero, true)

func _detail_layout_spec(available_size: Vector2, scroll_bar_width: float = 0.0) -> Dictionary:
    var measured_size := available_size
    measured_size.x = maxf(320.0, measured_size.x - scroll_bar_width)
    var phone_landscape := (
        measured_size.x > measured_size.y
        and measured_size.y < HOME_PHONE_BREAKPOINT
    )
    var compact := measured_size.x < DETAIL_COMPACT_BREAKPOINT and not phone_landscape
    var gutter := 20 if compact or phone_landscape else 32
    return {
        "available_size": measured_size,
        "compact": compact,
        "phone_landscape": phone_landscape,
        "gutter": gutter,
        "content_width": minf(
            1080.0,
            maxf(320.0, measured_size.x - float(gutter * 2))
        ),
    }

func _queue_detail_relayout_after_resize() -> void:
    if not is_instance_valid(detail_view) or not detail_view.visible:
        return
    detail_relayout_scroll_vertical = detail_scroll.scroll_vertical
    if detail_relayout_pending:
        return
    detail_relayout_pending = true
    # Match the settings relayout: iOS publishes the rotated viewport and its
    # new safe-area insets over separate layout passes.
    call_deferred("_settle_detail_relayout_after_resize")

func _settle_detail_relayout_after_resize() -> void:
    call_deferred("_apply_detail_relayout_after_resize")

func _apply_detail_relayout_after_resize() -> void:
    detail_relayout_pending = false
    if not is_instance_valid(detail_view) or not detail_view.visible or selected_game.is_empty():
        return
    var restore_scroll := detail_relayout_scroll_vertical
    _fit_full_rects()
    _finish_hero_overlay()
    _rebuild_detail_contents(selected_game, false, false)
    call_deferred("_restore_detail_scroll_after_relayout", restore_scroll)

func _restore_detail_scroll_after_relayout(scroll_vertical: int) -> void:
    if not is_instance_valid(detail_scroll) or not detail_view.visible:
        return
    var scroll_bar := detail_scroll.get_v_scroll_bar()
    var maximum_scroll := maxi(0, int(scroll_bar.max_value - scroll_bar.page))
    detail_scroll.scroll_vertical = mini(scroll_vertical, maximum_scroll)

func _rebuild_detail_contents(game: Dictionary, animate_hero: bool, animate_content: bool) -> void:
    for child in detail_scroll.get_children():
        detail_scroll.remove_child(child)
        child.queue_free()

    var available_size := shell_content.size
    if available_size.x <= 0.0 or available_size.y <= 0.0:
        available_size = get_viewport_rect().size
    var scroll_bar_width := detail_scroll.get_v_scroll_bar().get_combined_minimum_size().x
    var layout_spec := _detail_layout_spec(available_size, scroll_bar_width)
    available_size = layout_spec["available_size"]
    var compact: bool = layout_spec["compact"]
    var phone_landscape: bool = layout_spec["phone_landscape"]
    var gutter: int = layout_spec["gutter"]
    var detail_content_width: float = layout_spec["content_width"]

    var content := MarginContainer.new()
    content.custom_minimum_size = Vector2(maxf(360.0, available_size.x), 0)
    content.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    content.add_theme_constant_override("margin_left", gutter)
    content.add_theme_constant_override("margin_top", 20 if compact else 28)
    content.add_theme_constant_override("margin_right", gutter)
    content.add_theme_constant_override("margin_bottom", 40)
    detail_scroll.add_child(content)

    var center := CenterContainer.new()
    center.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    content.add_child(center)

    var page := VBoxContainer.new()
    page.custom_minimum_size = Vector2(detail_content_width, 0)
    page.size_flags_horizontal = Control.SIZE_SHRINK_CENTER
    page.add_theme_constant_override("separation", 24)
    center.add_child(page)

    var top := HBoxContainer.new()
    top.custom_minimum_size = Vector2(0, 48)
    top.add_theme_constant_override("separation", 10)
    page.add_child(top)

    var back := _shell_compact_button(ICON_BACK, _t("nav.library"), _show_home)
    back.custom_minimum_size = Vector2(44, 44)
    _apply_shell_compact_state(back, false)
    top.add_child(back)

    var eyebrow := Label.new()
    eyebrow.text = _t("detail.eyebrow")
    eyebrow.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    eyebrow.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
    eyebrow.add_theme_font_size_override("font_size", 16)
    eyebrow.add_theme_color_override("font_color", ui_tokens.text_secondary)
    top.add_child(eyebrow)

    var body := _build_compact_detail(game) if compact else _build_desktop_detail(game, phone_landscape)
    page.add_child(body)

    if animate_content:
        ui_motion.reveal(top)
    if animate_hero:
        body.modulate.a = 0.0
        call_deferred("_animate_hero_forward", body)
    elif animate_content:
        ui_motion.reveal(body, 0.04)

func _build_desktop_detail(game: Dictionary, phone_landscape: bool = false) -> Control:
    var body := HBoxContainer.new()
    body.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    body.add_theme_constant_override("separation", 20 if phone_landscape else 32)
    body.add_child(_detail_cover_with_action(game, Vector2(176, 248) if phone_landscape else Vector2(252, 354)))

    var information := VBoxContainer.new()
    information.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    information.add_theme_constant_override("separation", 14)
    body.add_child(information)
    information.add_child(_detail_identity(game, false))
    information.add_child(_detail_tools(game))
    information.add_child(_detail_information_panel(game))
    return body

func _build_compact_detail(game: Dictionary) -> Control:
    var body := VBoxContainer.new()
    body.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    body.add_theme_constant_override("separation", 14)

    var summary := HBoxContainer.new()
    summary.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    summary.add_theme_constant_override("separation", 16)
    body.add_child(summary)
    summary.add_child(_detail_cover_with_action(game, Vector2(112, 158)))

    var primary := VBoxContainer.new()
    primary.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    primary.add_theme_constant_override("separation", 10)
    summary.add_child(primary)
    primary.add_child(_detail_identity(game, true))

    body.add_child(_detail_tools(game))
    body.add_child(_detail_information_panel(game))
    return body

func _detail_cover(game: Dictionary, cover_size: Vector2) -> PanelContainer:
    var cover := PanelContainer.new()
    cover.clip_contents = true
    cover.custom_minimum_size = cover_size
    cover.size_flags_horizontal = Control.SIZE_SHRINK_BEGIN
    cover.size_flags_vertical = Control.SIZE_SHRINK_BEGIN
    var cover_style := ui_tokens.detail_outline_style()
    cover_style.content_margin_left = 0
    cover_style.content_margin_top = 0
    cover_style.content_margin_right = 0
    cover_style.content_margin_bottom = 0
    cover.add_theme_stylebox_override("panel", cover_style)
    detail_hero_cover = cover
    var cover_texture := _load_cover_texture(game, Vector2i(int(cover_size.x), int(cover_size.y)), 0)
    if cover_texture != null:
        var image := TextureRect.new()
        image.texture = cover_texture
        image.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
        image.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_COVERED
        cover.add_child(image)
    else:
        var icon := _centered_icon(ICON_GAMEPAD, Vector2(48, 48), ui_tokens.accent)
        cover.add_child(icon)
    return cover

func _detail_cover_with_action(game: Dictionary, cover_size: Vector2) -> VBoxContainer:
    var column := VBoxContainer.new()
    column.size_flags_horizontal = Control.SIZE_SHRINK_BEGIN
    column.add_theme_constant_override("separation", 6)
    column.add_child(_detail_cover(game, cover_size))
    var cover_path := _resolve_cover_path(game)
    var has_cover := not cover_path.is_empty() and FileAccess.file_exists(cover_path)
    var actions := HBoxContainer.new()
    actions.alignment = BoxContainer.ALIGNMENT_CENTER
    actions.add_theme_constant_override("separation", 6)
    column.add_child(actions)
    var action := _pill_button(_t("detail.set_cover"), ICON_PAGE)
    action.custom_minimum_size = Vector2(128, 40)
    action.size_flags_horizontal = Control.SIZE_SHRINK_CENTER
    action.pressed.connect(_set_cover_for_selected)
    actions.add_child(action)
    if has_cover:
        var clear := _icon_action_button(ICON_REFRESH, _t("detail.clear_cover"), _clear_cover_for_selected)
        clear.text = _t("detail.clear_cover")
        clear.add_theme_constant_override("h_separation", 8)
        clear.custom_minimum_size = Vector2(112, 40)
        clear.size_flags_horizontal = Control.SIZE_SHRINK_CENTER
        actions.add_child(clear)
    return column

func _detail_identity(game: Dictionary, compact: bool) -> VBoxContainer:
    var identity := VBoxContainer.new()
    identity.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    identity.add_theme_constant_override("separation", 6)

    var type_label := Label.new()
    type_label.text = _game_type_label(String(game.get("type", "Directory"))).to_upper()
    type_label.add_theme_font_size_override("font_size", 11 if compact else 12)
    type_label.add_theme_color_override("font_color", ui_tokens.accent)
    identity.add_child(type_label)

    var title := Label.new()
    title.text = _game_display_title(game)
    title.custom_minimum_size = Vector2(0, 64 if compact else 72)
    title.horizontal_alignment = HORIZONTAL_ALIGNMENT_LEFT
    title.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
    title.max_lines_visible = 3 if compact else 2
    title.text_overrun_behavior = TextServer.OVERRUN_TRIM_ELLIPSIS
    title.add_theme_font_override("font", _game_title_font())
    title.add_theme_font_size_override("font_size", 23 if compact else 32)
    title.add_theme_color_override("font_color", ui_tokens.text_primary)
    identity.add_child(title)

    if not compact:
        var subtitle := Label.new()
        subtitle.text = _t("detail.runtime_profile", [_game_type_label(String(game.get("type", "Directory")))])
        subtitle.add_theme_font_size_override("font_size", 14)
        subtitle.add_theme_color_override("font_color", ui_tokens.text_secondary)
        identity.add_child(subtitle)
    return identity

func _detail_launch_button() -> Button:
    # Use the explicit content row here instead of Button.icon + Button.text.
    # FlowContainer sizes a native Button from its text first, which can clip
    # the icon when the action is laid out at its shrink-to-fit width.
    var start := _pill_button(_t("detail.launch"), ICON_PLAY)
    # Keep enough room for the longest localized label as well as the icon.
    start.custom_minimum_size = Vector2(220, 52)
    start.size_flags_horizontal = Control.SIZE_SHRINK_BEGIN
    start.pressed.connect(_start_selected_game)
    start.button_down.connect(func(): _android_input_debug_log("detail launch button_down"))
    start.button_up.connect(func(): _android_input_debug_log("detail launch button_up"))
    start.pressed.connect(func(): _android_input_debug_log("detail launch pressed"))
    return start

func _detail_tools(game: Dictionary) -> FlowContainer:
    var tools := HFlowContainer.new()
    tools.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    tools.add_theme_constant_override("h_separation", 8)
    tools.add_theme_constant_override("v_separation", 8)
    tools.add_child(_detail_launch_button())
    if _can_configure_launch_file(game):
        var set_launch := _icon_action_button(
            ICON_PLAY,
            _t("detail.set_launch_file"),
            func(): _set_launch_file_for_selected()
        )
        _reveal_icon_action_label_on_hover(set_launch, _t("detail.set_launch_file"))
        set_launch.size_flags_horizontal = Control.SIZE_SHRINK_BEGIN
        tools.add_child(set_launch)
        if not GameLaunchEntry.configured_relative_path(game).is_empty():
            var reset_launch := _icon_action_button(
                ICON_REFRESH,
                _t("detail.reset_launch_file"),
                func(): _reset_launch_file_for_selected()
            )
            _reveal_icon_action_label_on_hover(reset_launch, _t("detail.reset_launch_file"))
            reset_launch.size_flags_horizontal = Control.SIZE_SHRINK_BEGIN
            tools.add_child(reset_launch)
    var rename := _icon_action_button(ICON_RENAME, _t("detail.rename"), func(): _rename_selected_game())
    _reveal_icon_action_label_on_hover(rename, _t("detail.rename"))
    tools.add_child(rename)
    tools.add_child(_detail_remove_button(game))
    return tools

func _detail_information_panel(game: Dictionary) -> PanelContainer:
    var info_panel := PanelContainer.new()
    info_panel.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    info_panel.add_theme_stylebox_override("panel", ui_tokens.material_panel())
    var info := VBoxContainer.new()
    info.add_theme_constant_override("separation", 0)
    info_panel.add_child(info)
    info.add_child(_detail_line(ICON_PAGE, String(game.get("path", ""))))
    info.add_child(_detail_separator())
    info.add_child(_detail_line(ICON_REFRESH, _t("detail.last_played", [_last_played_label(game)])))
    info.add_child(_detail_separator())
    info.add_child(_detail_line(ICON_PERFORMANCE, _t("detail.played", [_format_play_duration(int(game.get("playDurationSeconds", 0)))])))
    info.add_child(_detail_separator())
    info.add_child(_detail_line(ICON_LIBRARY, _game_type_label(String(game.get("type", "Directory")))))
    info.add_child(_detail_separator())
    info.add_child(_detail_line(ICON_PLAY, _t("detail.launch_entry", [_game_launch_entry_label(game)])))
    return info_panel

func _detail_remove_button(game: Dictionary) -> Button:
    var remove_label := "detail.delete_builtin" if builtin_demo.is_game(game) else "detail.remove"
    var remove := _icon_action_button(ICON_DELETE, _t(remove_label), func(): _confirm_remove_selected(), false, true)
    _reveal_icon_action_label_on_hover(remove, _t(remove_label))
    remove.size_flags_horizontal = Control.SIZE_SHRINK_BEGIN
    return remove

func _detail_line(icon_path: String, text: String) -> HBoxContainer:
    var row := HBoxContainer.new()
    row.custom_minimum_size = Vector2(0, 44)
    row.add_theme_constant_override("separation", 12)
    row.add_child(_icon_rect(icon_path, Vector2(19, 19), ui_tokens.text_tertiary))
    var label := Label.new()
    label.text = text
    label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    label.autowrap_mode = TextServer.AUTOWRAP_ARBITRARY
    label.add_theme_font_size_override("font_size", 14)
    label.add_theme_color_override("font_color", ui_tokens.text_secondary)
    row.add_child(label)
    return row

func _detail_separator() -> ColorRect:
    var separator := ColorRect.new()
    separator.color = ui_tokens.separator
    separator.custom_minimum_size = Vector2(0, 1)
    separator.mouse_filter = Control.MOUSE_FILTER_IGNORE
    return separator

func _detail_action(icon_path: String, text: String, callback: Callable = Callable(), destructive: bool = false) -> Button:
    var button := Button.new()
    button.text = text
    button.icon = _load_ui_icon(icon_path)
    button.expand_icon = true
    button.icon_alignment = HORIZONTAL_ALIGNMENT_LEFT
    button.alignment = HORIZONTAL_ALIGNMENT_LEFT
    button.clip_text = true
    button.focus_mode = Control.FOCUS_ALL
    button.custom_minimum_size = Vector2(0, 50)
    button.add_theme_constant_override("icon_max_width", 20)
    button.add_theme_constant_override("h_separation", 9)
    button.add_theme_font_size_override("font_size", 15)
    var foreground: Color = ui_tokens.danger if destructive else ui_tokens.text_primary
    button.add_theme_color_override("font_color", foreground)
    button.add_theme_color_override("icon_normal_color", foreground)
    button.add_theme_color_override("icon_hover_color", foreground)
    button.add_theme_color_override("icon_pressed_color", foreground)
    ui_widgets.secondary_button(button, destructive)
    if callback.is_valid():
        button.pressed.connect(callback)
    return button

func _danger_button(text: String) -> Button:
    var button := Button.new()
    button.text = text
    button.focus_mode = Control.FOCUS_ALL
    button.custom_minimum_size = Vector2(132, 52)
    ui_widgets.destructive_button(button)
    return button

func _modal_dialog(preferred_size: Vector2, dim_alpha: float = 0.44) -> PanelContainer:
    modal_layer.visible = true
    modal_layer.move_to_front()
    for child in modal_layer.get_children():
        child.queue_free()
    active_modal_scrim = null
    active_modal_dialog = null

    var dim := ColorRect.new()
    dim.color = Color(0, 0, 0, dim_alpha)
    dim.set_anchors_preset(Control.PRESET_FULL_RECT)
    dim.mouse_filter = Control.MOUSE_FILTER_STOP
    dim.gui_input.connect(func(event: InputEvent):
        var dismiss: bool = event is InputEventMouseButton and event.pressed
        dismiss = dismiss or (event is InputEventScreenTouch and event.pressed)
        if dismiss:
            _dismiss_modal()
    )
    modal_layer.add_child(dim)

    var dialog := PanelContainer.new()
    dialog.clip_contents = true
    _mark_centered_safe_dialog(dialog, preferred_size)
    _layout_safe_dialog(dialog, _ui_safe_rect(get_viewport_rect().size))
    var dialog_style := ui_tokens.material_panel(true)
    dialog_style.content_margin_left = 20
    dialog_style.content_margin_top = 18
    dialog_style.content_margin_right = 20
    dialog_style.content_margin_bottom = 18
    dialog.add_theme_stylebox_override("panel", dialog_style)
    modal_layer.add_child(dialog)
    active_modal_scrim = dim
    active_modal_dialog = dialog
    ui_motion.modal_in(dim, dialog, shell_root)
    return dialog

func _modal_stack(dialog: PanelContainer, title_text: String, icon_path: String) -> VBoxContainer:
    var box := VBoxContainer.new()
    box.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    box.size_flags_vertical = Control.SIZE_EXPAND_FILL
    box.add_theme_constant_override("separation", 16)
    dialog.add_child(box)

    var header := HBoxContainer.new()
    header.custom_minimum_size = Vector2(0, 44)
    header.add_theme_constant_override("separation", 12)
    box.add_child(header)

    var icon_plate := PanelContainer.new()
    icon_plate.custom_minimum_size = Vector2(42, 42)
    icon_plate.size_flags_vertical = Control.SIZE_SHRINK_CENTER
    icon_plate.add_theme_stylebox_override("panel", ui_tokens.panel(ui_tokens.accent_fill, 8))
    icon_plate.add_child(_centered_icon(icon_path, Vector2(21, 21), ui_tokens.accent))
    header.add_child(icon_plate)

    var title := Label.new()
    title.text = title_text
    title.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    title.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
    title.add_theme_font_size_override("font_size", 22)
    title.add_theme_color_override("font_color", ui_tokens.text_primary)
    header.add_child(title)
    return box

func _dismiss_modal(after: Callable = Callable()) -> void:
    if modal_layer == null or not modal_layer.visible:
        if after.is_valid():
            after.call()
        return
    var scrim := active_modal_scrim
    var dialog := active_modal_dialog
    ui_motion.modal_out(scrim, dialog, shell_root, func():
        modal_layer.visible = false
        active_modal_scrim = null
        active_modal_dialog = null
        if after.is_valid():
            after.call()
    )

func _show_import_guide() -> void:
    var guide_title := _t("dialog.import_title")
    var guide_body := _t("dialog.import_guide_body")
    if home_library_mode == "video":
        guide_title = _t("video.guide_title")
        guide_body = _t("video.guide_body_ios") if OS.get_name() == "iOS" else _t("video.guide_body_desktop")
    var dialog := _modal_dialog(Vector2(560, 400), 0.46)
    var box := _modal_stack(dialog, guide_title, ICON_LIBRARY)
    var body := Label.new()
    body.text = guide_body
    body.size_flags_vertical = Control.SIZE_EXPAND_FILL
    body.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
    body.add_theme_font_size_override("font_size", 15)
    body.add_theme_color_override("font_color", ui_tokens.text_secondary)
    box.add_child(body)
    var ok := _pill_button(_t("dialog.ok"))
    ok.custom_minimum_size = Vector2(112, 44)
    ok.size_flags_horizontal = Control.SIZE_SHRINK_END
    ok.pressed.connect(_dismiss_modal)
    box.add_child(ok)

func _show_message(message: String) -> void:
    _show_system_alert(message, "Aether")

func _show_system_alert(message: String, title: String = "Aether") -> void:
    if message.strip_edges().is_empty():
        return
    OS.alert(message, title)

func _show_system_alert_once(key: String, message: String, title: String = "Aether") -> void:
    if shown_system_alerts.has(key):
        return
    shown_system_alerts[key] = true
    _show_system_alert(message, title)

func _open_android_coffee() -> void:
    if OS.get_name() != "Android":
        return
    var result := OS.shell_open(ANDROID_COFFEE_URL)
    if result == OK:
        _show_system_alert(
            _t("support.coffee.thanks"),
            _t("support.coffee.thanks_title")
        )
    else:
        _show_system_alert(
            _t("support.coffee.open_failed"),
            _t("support.coffee.title")
        )

func _open_app_service_filing() -> void:
    var result := OS.shell_open(APP_SERVICE_FILING_URL)
    if result != OK:
        _show_system_alert(
            _t("settings.app_service_filing_open_failed"),
            _t("settings.app_service_filing")
        )

func _iap_supported_platform() -> bool:
    return OS.get_name() in ["iOS", "macOS"]

func _iap_enforcement_enabled() -> bool:
    # Local/debug artifacts are for compatibility and UI testing. Catalog
    # enforcement is enabled only in Release/TestFlight/App Store builds. A
    # verified secret unlock removes the catalog limit for this installation.
    return _iap_supported_platform() and not OS.is_debug_build() and not secret_iap_unlocked

func _initialize_iap() -> void:
    if not _iap_supported_platform() or player == null:
        return
    if not player.has_method("iap_start") or not player.has_method("iap_get_state_json"):
        iap_state = {
            "available": false,
            "product_state": "unsupported",
            "entitled": false,
            "last_error": "StoreKit bridge unavailable",
        }
        iap_coffee_state = iap_state.duplicate(true)
        iap_coffee_state["product_id"] = IAP_COFFEE_PRODUCT_ID
        return
    player.iap_start(IAP_LIST_LIMIT_PRODUCT_ID)
    player.iap_start(IAP_COFFEE_PRODUCT_ID)
    # Populate Settings at startup. Launch authorization never trusts this
    # snapshot and always starts a new entitlement check.
    player.iap_refresh_entitlement(IAP_LIST_LIMIT_PRODUCT_ID)
    player.iap_refresh_entitlement(IAP_COFFEE_PRODUCT_ID)
    _read_iap_state(IAP_LIST_LIMIT_PRODUCT_ID)
    _read_iap_state(IAP_COFFEE_PRODUCT_ID)

func _read_iap_state(product_id: String = IAP_LIST_LIMIT_PRODUCT_ID) -> Dictionary:
    if player == null or not player.has_method("iap_get_state_json"):
        return iap_coffee_state if product_id == IAP_COFFEE_PRODUCT_ID else iap_state
    var parsed = JSON.parse_string(String(player.iap_get_state_json(product_id)))
    if parsed is Dictionary:
        if product_id == IAP_COFFEE_PRODUCT_ID:
            iap_coffee_state = parsed
        else:
            iap_state = parsed
    return iap_coffee_state if product_id == IAP_COFFEE_PRODUCT_ID else iap_state

func _iap_item_is_first(kind: String, item: Dictionary) -> bool:
    var items: Array[Dictionary] = known_games if kind == "game" else known_videos
    if items.is_empty():
        return false
    var path := String(item.get("path", ""))
    return not path.is_empty() and path == String(items[0].get("path", ""))

func _iap_item_authorization_key(kind: String, item: Dictionary) -> String:
    var path := String(item.get("path", "")).strip_edges()
    return "" if path.is_empty() else "%s:%s" % [kind, path]

func _consume_iap_detail_authorization(kind: String, item: Dictionary) -> bool:
    var key := _iap_item_authorization_key(kind, item)
    var authorized := (
        not key.is_empty()
        and key == iap_detail_authorization_key
        and Time.get_ticks_msec() <= iap_detail_authorization_until_msec
    )
    iap_detail_authorization_key = ""
    iap_detail_authorization_until_msec = 0
    return authorized

func _begin_iap_checked_access(
    kind: String,
    item: Dictionary,
    action: String = "launch"
) -> bool:
    if not _iap_enforcement_enabled():
        return false
    # The first item is always outside the paid catalog limit. Avoid making
    # its button wait for a StoreKit entitlement round trip on every launch.
    if _iap_item_is_first(kind, item):
        return false
    if action == "launch" and _consume_iap_detail_authorization(kind, item):
        return false
    iap_pending_launch = {
        "kind": kind,
        "item": item.duplicate(true),
        "action": action,
    }
    if player == null or not player.has_method("iap_refresh_entitlement"):
        if _iap_item_is_first(kind, item):
            _run_iap_pending_launch()
        else:
            _clear_iap_pending_launch()
            _show_system_alert(
                _t("iap.verify_failed", ["StoreKit unavailable"]),
                _t("iap.checking_title")
            )
        return true
    iap_pending_check_id = int(player.iap_refresh_entitlement(
        IAP_LIST_LIMIT_PRODUCT_ID
    ))
    if iap_pending_check_id <= 0:
        if _iap_item_is_first(kind, item):
            _run_iap_pending_launch()
        else:
            _clear_iap_pending_launch()
            _show_system_alert(
                _t("iap.verify_failed", ["StoreKit request failed"]),
                _t("iap.checking_title")
            )
        return true
    # Transaction.currentEntitlements reads StoreKit's locally verified
    # entitlement cache and may refresh it in the background. Keep this launch
    # authorization silent: only show UI when the selected item is actually
    # limited or when StoreKit cannot verify its state.
    return true

func _begin_iap_checked_launch(kind: String, item: Dictionary) -> bool:
    return _begin_iap_checked_access(kind, item, "launch")

func _open_game_detail_with_iap(game: Dictionary, source: Control = null) -> void:
    # The first catalog item is always available without purchasing. Other
    # items are authorized before exposing their detail-page actions, then
    # checked again immediately before launch.
    if _iap_item_is_first("game", game):
        _show_detail(game, source)
        return
    if _begin_iap_checked_access("game", game, "detail"):
        return
    _show_detail(game, source)

func _clear_iap_pending_launch() -> void:
    iap_pending_launch.clear()
    iap_pending_check_id = 0

func _run_iap_pending_launch() -> void:
    if iap_pending_launch.is_empty():
        return
    var pending := iap_pending_launch.duplicate(true)
    _clear_iap_pending_launch()
    modal_layer.visible = false
    var item: Dictionary = pending.get("item", {})
    if String(pending.get("action", "launch")) == "detail":
        _show_detail(item)
        return
    if String(pending.get("kind", "")) == "game":
        selected_game = item
        _start_selected_game_after_iap()
    else:
        _open_video_player_after_iap(item)

func _show_iap_progress_dialog(title_text: String, body_text: String) -> void:
    _create_iap_modal(title_text, body_text, 660.0, 310.0)

func _show_iap_limit_prompt() -> void:
    var box := _create_iap_modal(
        _t("iap.limit_title"),
        _t("iap.limit_body"),
        720.0,
        350.0
    )
    var buttons := HBoxContainer.new()
    var compact := _ui_safe_rect(get_viewport_rect().size).size.x < 520.0
    buttons.alignment = BoxContainer.ALIGNMENT_END
    buttons.add_theme_constant_override("separation", 10 if compact else 14)
    buttons.custom_minimum_size = Vector2(0, 56 if compact else 64)
    box.add_child(buttons)
    var cancel := Button.new()
    cancel.text = _t("dialog.cancel")
    cancel.flat = true
    cancel.custom_minimum_size = Vector2(0 if compact else 130, 54 if compact else 62)
    cancel.size_flags_horizontal = Control.SIZE_EXPAND_FILL if compact else Control.SIZE_FILL
    cancel.size_flags_stretch_ratio = 0.75 if compact else 1.0
    cancel.add_theme_font_size_override("font_size", 17 if compact else 20)
    cancel.add_theme_color_override("font_color", color_text)
    cancel.pressed.connect(func():
        _clear_iap_pending_launch()
        modal_layer.visible = false
    )
    buttons.add_child(cancel)
    var purchase := _pill_button(_iap_purchase_button_text())
    purchase.custom_minimum_size = Vector2(0 if compact else 210, 54 if compact else 62)
    purchase.size_flags_horizontal = Control.SIZE_EXPAND_FILL if compact else Control.SIZE_FILL
    purchase.size_flags_stretch_ratio = 1.25 if compact else 1.0
    purchase.pressed.connect(func(): _begin_iap_purchase("limit"))
    buttons.add_child(purchase)

func _create_iap_modal(title_text: String, body_text: String, width: float, height: float) -> VBoxContainer:
    modal_layer.visible = true
    modal_layer.move_to_front()
    for child in modal_layer.get_children():
        child.queue_free()
    var dim := ColorRect.new()
    dim.color = Color(0, 0, 0, 0.52)
    dim.set_anchors_preset(Control.PRESET_FULL_RECT)
    modal_layer.add_child(dim)
    var dialog := PanelContainer.new()
    _mark_centered_safe_dialog(dialog, Vector2(width, height))
    _layout_safe_dialog(dialog, _ui_safe_rect(get_viewport_rect().size))
    dialog.add_theme_stylebox_override(
        "panel",
        _panel_style(22, color_card, Color(0, 0, 0, 0.06), 1)
    )
    modal_layer.add_child(dialog)
    var box := VBoxContainer.new()
    var compact := _ui_safe_rect(get_viewport_rect().size).size.x < 520.0
    box.add_theme_constant_override("separation", 16 if compact else 22)
    dialog.add_child(box)
    var title := Label.new()
    title.text = title_text
    title.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
    title.add_theme_font_size_override("font_size", 24 if compact else 30)
    title.add_theme_color_override("font_color", color_text)
    box.add_child(title)
    var body := Label.new()
    body.text = body_text
    body.size_flags_vertical = Control.SIZE_EXPAND_FILL
    body.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
    body.add_theme_font_size_override("font_size", 17 if compact else 21)
    body.add_theme_color_override("font_color", color_text)
    box.add_child(body)
    return box

func _iap_purchase_button_text() -> String:
    var price := String(iap_state.get("display_price", ""))
    return _t("iap.buy") if price.is_empty() else "%s  %s" % [_t("iap.buy"), price]

func _begin_iap_purchase(
    source: String = "settings",
    product_id: String = IAP_LIST_LIMIT_PRODUCT_ID
) -> void:
    var product_title := (
        _t("iap.coffee.title")
        if product_id == IAP_COFFEE_PRODUCT_ID
        else _t("iap.list_limit.title")
    )
    if player == null or not player.has_method("iap_purchase"):
        _show_system_alert(
            _t("iap.purchase_failed", ["StoreKit unavailable"]),
            product_title
        )
        return
    iap_pending_operation_id = int(player.iap_purchase(product_id))
    if iap_pending_operation_id <= 0:
        _show_system_alert(
            _t("iap.purchase_failed", ["StoreKit request failed"]),
            product_title
        )
        return
    iap_pending_operation_kind = "purchase:%s" % source
    iap_pending_operation_product_id = product_id
    _show_iap_progress_dialog(
        product_title,
        _t("iap.status.loading")
    )

func _begin_iap_restore() -> void:
    if player == null or not player.has_method("iap_restore"):
        _show_system_alert(
            _t("iap.purchase_failed", ["StoreKit unavailable"]),
            _t("iap.restore")
        )
        return
    iap_pending_operation_id = int(player.iap_restore(
        IAP_LIST_LIMIT_PRODUCT_ID
    ))
    if iap_pending_operation_id <= 0:
        _show_system_alert(
            _t("iap.purchase_failed", ["StoreKit request failed"]),
            _t("iap.restore")
        )
        return
    iap_pending_operation_kind = "restore"
    iap_pending_operation_product_id = IAP_LIST_LIMIT_PRODUCT_ID
    _show_iap_progress_dialog(_t("iap.restore"), _t("iap.status.loading"))

func _process_iap(delta: float) -> void:
    if not _iap_supported_platform() or player == null:
        return
    iap_poll_accum += delta
    if iap_poll_accum < IAP_POLL_INTERVAL_SEC:
        return
    iap_poll_accum = 0.0
    var previous_revision := iap_last_revision
    var previous_coffee_revision := iap_coffee_last_revision
    var state := _read_iap_state(IAP_LIST_LIMIT_PRODUCT_ID)
    var coffee_state := _read_iap_state(IAP_COFFEE_PRODUCT_ID)
    iap_last_revision = int(state.get("revision", iap_last_revision))
    iap_coffee_last_revision = int(coffee_state.get(
        "revision", iap_coffee_last_revision
    ))

    if iap_pending_check_id > 0 and int(state.get(
        "entitlement_check_completed", 0
    )) >= iap_pending_check_id:
        _complete_iap_launch_check()

    if iap_pending_beta_check_id > 0 and int(coffee_state.get(
        "entitlement_check_completed", 0
    )) >= iap_pending_beta_check_id:
        _complete_runtime_beta_check()

    var operation_state_source := (
        coffee_state
        if iap_pending_operation_product_id == IAP_COFFEE_PRODUCT_ID
        else state
    )
    if iap_pending_operation_id > 0 and int(operation_state_source.get(
        "operation_serial", 0
    )) == iap_pending_operation_id:
        var operation_state := String(operation_state_source.get(
            "operation_state", "idle"
        ))
        if operation_state not in ["idle", "purchasing", "restoring"]:
            _complete_iap_operation(operation_state)

    var state_changed := (
        previous_revision != iap_last_revision
        or previous_coffee_revision != iap_coffee_last_revision
    )
    if state_changed and is_instance_valid(settings_view) and settings_view.visible:
        if iap_pending_operation_id <= 0 and not iap_settings_refresh_pending:
            iap_settings_refresh_pending = true
            call_deferred("_refresh_iap_settings_view")

func _complete_iap_launch_check() -> void:
    iap_pending_check_id = 0
    if iap_pending_launch.is_empty():
        modal_layer.visible = false
        return
    var kind := String(iap_pending_launch.get("kind", ""))
    var item: Dictionary = iap_pending_launch.get("item", {})
    if bool(iap_state.get("entitled", false)) or _iap_item_is_first(kind, item):
        if String(iap_pending_launch.get("action", "")) == "detail":
            iap_detail_authorization_key = _iap_item_authorization_key(kind, item)
            iap_detail_authorization_until_msec = Time.get_ticks_msec() + IAP_DETAIL_AUTHORIZATION_TTL_MS
        _run_iap_pending_launch()
        return
    var entitlement_state := String(iap_state.get("entitlement_state", ""))
    if entitlement_state == "not_purchased":
        _show_iap_limit_prompt()
        return
    var error := String(iap_state.get("last_error", "")).strip_edges()
    if error.is_empty():
        error = entitlement_state
    _clear_iap_pending_launch()
    modal_layer.visible = false
    _show_system_alert(
        _t("iap.verify_failed", [error]),
        _t("iap.checking_title")
    )

func _complete_iap_operation(operation_state: String) -> void:
    var operation_kind := iap_pending_operation_kind
    var product_id := iap_pending_operation_product_id
    var product_state := (
        iap_coffee_state
        if product_id == IAP_COFFEE_PRODUCT_ID
        else iap_state
    )
    var product_title := (
        _t("iap.coffee.title")
        if product_id == IAP_COFFEE_PRODUCT_ID
        else _t("iap.list_limit.title")
    )
    iap_pending_operation_id = 0
    iap_pending_operation_kind = ""
    iap_pending_operation_product_id = ""
    modal_layer.visible = false
    if operation_state == "purchased":
        if product_id == IAP_COFFEE_PRODUCT_ID:
            var expiration := String(product_state.get(
                "entitlement_expiration_display", ""
            )).strip_edges()
            _show_system_alert(
                _t("iap.coffee.purchase_success", [expiration]),
                product_title
            )
        elif not iap_pending_launch.is_empty():
            _run_iap_pending_launch()
        else:
            _show_system_alert(
                _t("iap.purchase_success"),
                _t("iap.list_limit.title")
            )
    elif operation_state == "restored":
        _show_system_alert(_t("iap.restore_success"), _t("iap.restore"))
    elif operation_state == "not_purchased" and operation_kind == "restore":
        _show_system_alert(_t("iap.restore_none"), _t("iap.restore"))
    elif operation_state == "pending":
        if product_id == IAP_LIST_LIMIT_PRODUCT_ID:
            _clear_iap_pending_launch()
        _show_system_alert(
            _t("iap.purchase_pending"),
            product_title
        )
    elif operation_state == "cancelled":
        if product_id == IAP_LIST_LIMIT_PRODUCT_ID:
            _clear_iap_pending_launch()
        _show_system_alert(
            _t("iap.purchase_cancelled"),
            product_title
        )
    else:
        if product_id == IAP_LIST_LIMIT_PRODUCT_ID:
            _clear_iap_pending_launch()
        var error := String(product_state.get("last_error", "")).strip_edges()
        if error.is_empty():
            error = operation_state
        _show_system_alert(
            _t("iap.purchase_failed", [error]),
            product_title
        )

func _refresh_iap_settings_view() -> void:
    iap_settings_refresh_pending = false
    if is_instance_valid(settings_view) and settings_view.visible:
        _rebuild_settings_view()

func _maybe_show_log_alert(line: String) -> void:
    var message := line.strip_edges()
    var alert_parts := message.split("[ALERT_DIALOG]", true, 1)
    if alert_parts.size() > 1:
        var content := alert_parts[1].strip_edges()
        var parts := content.split(" | ", true, 1)
        var alert_title := parts[0].strip_edges() if parts.size() > 0 else "Aether"
        var alert_message := parts[1].strip_edges() if parts.size() > 1 else ""
        _show_system_alert(alert_message, alert_title)

func _create_file_dialog(title: String, file_mode: int, filters: PackedStringArray = PackedStringArray()) -> FileDialog:
    _finish_hero_overlay()
    var dialog := FileDialog.new()
    dialog.file_mode = file_mode
    dialog.access = FileDialog.ACCESS_FILESYSTEM
    dialog.use_native_dialog = true
    dialog.exclusive = true
    dialog.title = title
    for filter in filters:
        dialog.add_filter(filter)
    dialog.canceled.connect(func(): call_deferred("_release_file_dialog", dialog))
    dialog.file_selected.connect(func(_path: String): call_deferred("_release_file_dialog", dialog))
    dialog.dir_selected.connect(func(_path: String): call_deferred("_release_file_dialog", dialog))
    dialog.files_selected.connect(func(_paths: PackedStringArray): call_deferred("_release_file_dialog", dialog))
    return dialog

func _choose_translation_model() -> void:
    _finish_hero_overlay()
    var current_path := _settings_draft_string(
        "text_translation_model_path", text_translation_model_path
    )
    var initial_directory := (
        current_path.get_base_dir() if not current_path.is_empty() else ""
    )
    if player != null \
            and player.has_method("native_translation_model_file_picker_open") \
            and bool(player.native_translation_model_file_picker_open(
                _t("settings.translation_model_select"), initial_directory
            )):
        native_translation_model_file_picker_pending = true
        return
    _show_translation_model_native_dialog(current_path)

func _show_translation_model_native_dialog(current_path: String) -> void:
    var dialog := _create_file_dialog(
        _t("settings.translation_model_select"),
        FileDialog.FILE_MODE_OPEN_FILE,
        PackedStringArray(["*.gguf ; GGUF model"])
    )
    if not current_path.is_empty():
        dialog.current_dir = current_path.get_base_dir()
        dialog.current_file = current_path.get_file()
    dialog.file_selected.connect(func(path: String):
        _set_settings_draft_value("text_translation_model_path", path)
        call_deferred("_rebuild_settings_view")
    )
    add_child(dialog)
    dialog.popup_centered(Vector2i(900, 640))

func _restore_native_translation_model_access() -> void:
    if OS.get_name() not in ["iOS", "macOS"] \
            or text_translation_model_path.is_empty() \
            or not _runtime_string("AETHERKIRI_TRANSLATION_MODEL", "").is_empty() \
            or player == null \
            or not player.has_method("native_translation_model_restore_path"):
        return
    var restored_path := String(player.native_translation_model_restore_path(
        text_translation_model_path
    ))
    if restored_path.is_empty() or restored_path == text_translation_model_path:
        return
    text_translation_model_path = restored_path
    _save_shell_settings()

func _clear_translation_model() -> void:
    _set_settings_draft_value("text_translation_model_path", "")
    call_deferred("_rebuild_settings_view")

func _release_file_dialog(dialog: FileDialog) -> void:
    if dialog != null and is_instance_valid(dialog):
        dialog.queue_free()

func _offer_scrape_after_add(game: Dictionary) -> void:
    var dialog := _modal_dialog(Vector2(520, 280))
    var box := _modal_stack(dialog, _t("dialog.scrape_title"), ICON_GAMEPAD)
    var body := Label.new()
    body.text = _t("dialog.scrape_body", [_game_display_title(game)])
    body.size_flags_vertical = Control.SIZE_EXPAND_FILL
    body.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
    body.add_theme_font_size_override("font_size", 15)
    body.add_theme_color_override("font_color", ui_tokens.text_secondary)
    box.add_child(body)
    var buttons := HBoxContainer.new()
    buttons.add_theme_constant_override("separation", 12)
    buttons.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    buttons.alignment = BoxContainer.ALIGNMENT_END
    buttons.custom_minimum_size = Vector2(0, 44)
    box.add_child(buttons)
    var no := Button.new()
    no.text = _t("dialog.later")
    no.custom_minimum_size = Vector2(104, 44)
    ui_widgets.secondary_button(no)
    no.pressed.connect(_dismiss_modal)
    buttons.add_child(no)
    var yes := _pill_button(_t("dialog.open_detail"))
    yes.custom_minimum_size = Vector2(140, 44)
    yes.pressed.connect(func():
        _dismiss_modal(func(): _show_detail(game))
    )
    buttons.add_child(yes)

func _set_cover_for_selected() -> void:
    var path := String(selected_game.get("path", ""))
    if path.is_empty():
        return
    _finish_hero_overlay()
    if OS.get_name() == "iOS" \
            and player != null \
            and player.has_method("native_cover_file_picker_open"):
        var cover_directory := ProjectSettings.globalize_path("user://Covers")
        if bool(player.native_cover_file_picker_open(
                _t("dialog.choose_cover"), path, cover_directory
        )):
            native_cover_file_picker_pending = true
            native_cover_file_picker_library_path = path
            return
    _show_cover_godot_dialog(path)

func _delete_cover_for_selected() -> void:
    var path := String(selected_game.get("path", ""))
    if path.is_empty():
        return
    var cover_path := _resolve_cover_path(selected_game)
    if not cover_path.is_empty() and FileAccess.file_exists(cover_path):
        DirAccess.remove_absolute(cover_path)
    _clear_cover_for_selected()

func _clear_cover_for_selected() -> void:
    var path := String(selected_game.get("path", ""))
    if path.is_empty():
        return
    _update_game(path, {"coverPath": "", GAME_AUTO_COVER_SCANNED_FIELD: true})
    var index := CoverIndex.load_index()
    index[CoverIndex.key_for(selected_game)] = CoverIndex.record("")
    CoverIndex.save_index(index)
    _show_detail(selected_game)

func _show_cover_godot_dialog(path: String) -> void:
    var dialog := _create_file_dialog(
        _t("dialog.choose_cover"),
        FileDialog.FILE_MODE_OPEN_FILE,
        PackedStringArray(["*.png,*.jpg,*.jpeg,*.webp;Image;image/png,image/jpeg,image/webp"])
    )
    if DirAccess.dir_exists_absolute(path):
        dialog.current_dir = path
    dialog.file_selected.connect(func(cover_path: String):
        _apply_selected_cover(path, cover_path)
    )
    add_child(dialog)
    dialog.popup_centered(Vector2i(900, 640))

func _apply_selected_cover(library_path: String, cover_path: String) -> void:
    if cover_path.is_empty() or not FileAccess.file_exists(cover_path):
        _show_system_alert(
            _t("message.cover_file_missing", [cover_path]),
            _t("alert.warning_title")
        )
        return
    _update_game(library_path, {
        "coverPath": _portable_cover_path(library_path, cover_path),
        GAME_AUTO_COVER_SCANNED_FIELD: true,
    })
    var index := CoverIndex.load_index()
    for game in _load_game_list():
        if String(game.get("path", "")) == library_path:
            index[CoverIndex.key_for(game)] = CoverIndex.record(_portable_cover_path(library_path, cover_path))
            break
    CoverIndex.save_index(index)
    _show_detail(selected_game)

func _game_launch_entry_label(game: Dictionary) -> String:
    var relative_path := GameLaunchEntry.configured_relative_path(game)
    if relative_path.is_empty():
        return _t("detail.default_launch_entry")
    return relative_path

func _can_configure_launch_file(game: Dictionary) -> bool:
    return OS.get_name() != "Web" \
        and not builtin_demo.is_game(game) \
        and _game_runtime_kind(String(game.get("path", ""))) != RUNTIME_ONSCRIPTER \
        and String(game.get("type", "Directory")).to_lower() == "directory"

func _set_launch_file_for_selected() -> void:
    var library_path := String(selected_game.get("path", ""))
    if library_path.is_empty() or not _can_configure_launch_file(selected_game):
        return
    _finish_hero_overlay()
    if OS.get_name() in ["iOS", "macOS"] \
            and player != null \
            and player.has_method("native_launch_file_picker_open") \
            and bool(player.native_launch_file_picker_open(
                _t("dialog.choose_launch_file"), library_path
            )):
        native_launch_file_picker_pending = true
        native_launch_file_picker_library_path = library_path
        return
    _show_launch_file_godot_dialog(library_path)

func _show_launch_file_godot_dialog(library_path: String) -> void:
    var dialog := _create_file_dialog(
        _t("dialog.choose_launch_file"),
        FileDialog.FILE_MODE_OPEN_FILE,
        PackedStringArray(["*.exe,*.EXE,*.xp3,*.XP3;Visual novel launch file"])
    )
    if DirAccess.dir_exists_absolute(library_path):
        dialog.current_dir = library_path
    dialog.file_selected.connect(func(selected_path: String):
        _apply_selected_launch_file(library_path, selected_path)
        dialog.queue_free()
    )
    dialog.canceled.connect(func(): dialog.queue_free())
    add_child(dialog)
    dialog.popup_centered(Vector2i(900, 640))

func _apply_selected_launch_file(library_path: String, selected_path: String) -> void:
    if not GameLaunchEntry.is_supported_file(selected_path):
        _show_system_alert(
            _t("message.launch_file_unsupported"),
            _t("alert.warning_title")
        )
        return
    var relative_path := GameLaunchEntry.relative_path_for_selection(
        library_path,
        selected_path
    )
    if relative_path.is_empty():
        _show_system_alert(
            _t("message.launch_file_outside_game"),
            _t("alert.warning_title")
        )
        return
    _update_game(library_path, {GameLaunchEntry.FIELD: relative_path})
    _show_detail(selected_game)

func _poll_native_launch_file_picker() -> void:
    if not native_launch_file_picker_pending \
            or player == null \
            or not player.has_method("native_launch_file_picker_take_result_json"):
        return
    var result_json := String(player.native_launch_file_picker_take_result_json())
    if result_json.is_empty():
        return
    var library_path := native_launch_file_picker_library_path
    native_launch_file_picker_pending = false
    native_launch_file_picker_library_path = ""
    var parsed = JSON.parse_string(result_json)
    if typeof(parsed) != TYPE_DICTIONARY:
        _show_system_alert(result_json, _t("alert.warning_title"))
        return
    var result: Dictionary = parsed
    match String(result.get("status", "error")):
        "selected":
            _apply_selected_launch_file(library_path, String(result.get("path", "")))
        "cancelled":
            pass
        _:
            _show_system_alert(
                String(result.get("error", "System file picker failed")),
                _t("alert.warning_title")
            )

func _poll_native_cover_file_picker() -> void:
    if not native_cover_file_picker_pending \
            or player == null \
            or not player.has_method("native_launch_file_picker_take_result_json"):
        return
    var result_json := String(player.native_launch_file_picker_take_result_json())
    if result_json.is_empty():
        return
    var library_path := native_cover_file_picker_library_path
    native_cover_file_picker_pending = false
    native_cover_file_picker_library_path = ""
    var parsed = JSON.parse_string(result_json)
    if typeof(parsed) != TYPE_DICTIONARY:
        _show_system_alert(result_json, _t("alert.warning_title"))
        return
    var result: Dictionary = parsed
    match String(result.get("status", "error")):
        "selected":
            _apply_selected_cover(library_path, String(result.get("path", "")))
        "cancelled":
            pass
        _:
            _show_system_alert(
                String(result.get("error", "System file picker failed")),
                _t("alert.warning_title")
            )

func _poll_native_translation_model_file_picker() -> void:
    if not native_translation_model_file_picker_pending \
            or player == null \
            or not player.has_method("native_launch_file_picker_take_result_json"):
        return
    var result_json := String(player.native_launch_file_picker_take_result_json())
    if result_json.is_empty():
        return
    native_translation_model_file_picker_pending = false
    var parsed = JSON.parse_string(result_json)
    if typeof(parsed) != TYPE_DICTIONARY:
        _show_system_alert(result_json, _t("alert.warning_title"))
        return
    var result: Dictionary = parsed
    match String(result.get("status", "error")):
        "selected":
            var path := String(result.get("path", ""))
            if path.get_extension().to_lower() != "gguf":
                _show_system_alert(
                    "The selected file is not a GGUF model.",
                    _t("alert.warning_title")
                )
                return
            _set_settings_draft_value("text_translation_model_path", path)
            call_deferred("_rebuild_settings_view")
        "cancelled":
            pass
        _:
            _show_system_alert(
                String(result.get("error", "System file picker failed")),
                _t("alert.warning_title")
            )

func _reset_launch_file_for_selected() -> void:
    var library_path := String(selected_game.get("path", ""))
    if library_path.is_empty() or not _can_configure_launch_file(selected_game):
        return
    _update_game(library_path, {GameLaunchEntry.FIELD: ""})
    _show_detail(selected_game)

func _rename_selected_game() -> void:
    var path := String(selected_game.get("path", ""))
    if path.is_empty():
        return
    var dialog := _modal_dialog(Vector2(520, 260))
    var box := _modal_stack(dialog, _t("dialog.rename"), ICON_RENAME)
    var input := LineEdit.new()
    input.text = _game_display_title(selected_game)
    input.custom_minimum_size = Vector2(0, 44)
    ui_widgets.line_edit(input)
    box.add_child(input)
    var save := _pill_button(_t("settings.save"))
    save.custom_minimum_size = Vector2(112, 44)
    save.size_flags_horizontal = Control.SIZE_SHRINK_END
    save.pressed.connect(func():
        var new_title := input.text.strip_edges()
        if not new_title.is_empty():
            _dismiss_modal(func():
                _update_game(path, {"title": new_title})
                _show_detail(selected_game)
            )
    )
    box.add_child(save)

func _confirm_remove_selected() -> void:
    var path := String(selected_game.get("path", ""))
    if path.is_empty():
        return
    var deleting_builtin := builtin_demo.is_game(selected_game)
    var remove_label := "detail.delete_builtin" if deleting_builtin else "detail.remove"
    var dialog := _modal_dialog(Vector2(520, 260))
    var box := _modal_stack(dialog, _t(remove_label), ICON_DELETE)
    var label := Label.new()
    var body_key := "dialog.delete_builtin_body" if deleting_builtin else "dialog.remove_body"
    label.text = _t(body_key, [_game_display_title(selected_game)])
    label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
    label.size_flags_vertical = Control.SIZE_EXPAND_FILL
    label.add_theme_font_size_override("font_size", 15)
    label.add_theme_color_override("font_color", ui_tokens.text_secondary)
    box.add_child(label)
    var buttons := HBoxContainer.new()
    buttons.add_theme_constant_override("separation", 12)
    buttons.alignment = BoxContainer.ALIGNMENT_END
    buttons.custom_minimum_size = Vector2(0, 62)
    box.add_child(buttons)
    var cancel := Button.new()
    cancel.text = _t("dialog.cancel")
    cancel.flat = true
    cancel.custom_minimum_size = Vector2(112, 62)
    cancel.add_theme_font_size_override("font_size", 20)
    cancel.add_theme_color_override("font_color", color_text)
    cancel.pressed.connect(func(): modal_layer.visible = false)
    buttons.add_child(cancel)
    var remove := _danger_button(_t("dialog.delete" if deleting_builtin else "dialog.remove"))
    remove.custom_minimum_size = Vector2(148, 52)
    remove.pressed.connect(func():
        _dismiss_modal(func(): _remove_game(path))
    )
    buttons.add_child(remove)

func _on_refresh_or_import() -> void:
    if home_library_mode == "video":
        if OS.get_name() == "Android":
            _open_video_import_dialog()
            return
        if OS.get_name() == "iOS":
            _refresh_videos()
        else:
            _open_video_import_dialog()
        return
    if OS.get_name() == "iOS":
        _refresh_games()
        return
    if OS.get_name() == "Web":
        _show_web_import_picker()
        return
    if OS.get_name() == "Android":
        _android_storage_permission_log("add_game_click target=/storage/emulated/0")
    if not _ensure_android_storage_permission_for_import():
        return
    _show_import_picker()

func _show_import_picker() -> void:
    var dialog := _modal_dialog(Vector2(480, 220))
    var box := _modal_stack(dialog, _t("dialog.import_title"), ICON_ADD)
    var dir_button := _detail_action(ICON_LIBRARY, _t("dialog.select_game_dir"))
    dir_button.pressed.connect(func():
        _dismiss_modal(func(): _open_import_dialog())
    )
    box.add_child(dir_button)
    var cancel := Button.new()
    cancel.text = _t("dialog.cancel")
    cancel.custom_minimum_size = Vector2(108, 44)
    cancel.size_flags_horizontal = Control.SIZE_SHRINK_END
    ui_widgets.secondary_button(cancel)
    cancel.pressed.connect(_dismiss_modal)
    box.add_child(cancel)

func _web_eval_string(source: String) -> String:
    if OS.get_name() != "Web":
        return ""
    var value = JavaScriptBridge.eval(source, true)
    if value == null:
        return ""
    return String(value)

func _sync_web_user_fs(reason: String) -> void:
    if OS.get_name() != "Web":
        return
    _web_eval_string("(function(){if(typeof AetherKiriSyncUserFs==='function'){AetherKiriSyncUserFs(%s);return '1';}return '0';})()" % JSON.stringify(reason))

func _web_sync_get_json(path: String):
    var source := "(function(){var xhr=new XMLHttpRequest();xhr.open('GET',%s,false);xhr.send(null);if(xhr.status>=200&&xhr.status<300)return xhr.responseText;return JSON.stringify({error:'HTTP '+xhr.status});})()" % JSON.stringify(path)
    var text := _web_eval_string(source)
    if text.is_empty():
        return null
    return JSON.parse_string(text)

func _web_game_from_mount_info(info: Dictionary) -> Dictionary:
    return {
        "name": String(info.get("name", _t("game.local"))),
        "path": String(info.get("gamePath", info.get("path", ""))),
        "type": String(info.get("type", "Directory")),
        "lastPlayed": 0,
        "playDurationSeconds": 0,
        "coverPath": "",
        "developer": "",
        "title": String(info.get("title", "")),
        "webMountBackend": String(info.get("webMountBackend", "http")),
        "webMountBaseUrl": String(info.get("baseUrl", "")),
        "webMountGameId": String(info.get("webMountGameId", "")),
        "webMountPoint": String(info.get("mountPoint", info.get("webMountPoint", ""))),
    }

func _mount_web_game(game: Dictionary) -> bool:
    if OS.get_name() != "Web":
        return true
    var backend := String(game.get("webMountBackend", "http"))
    var base_url := String(game.get("webMountBaseUrl", ""))
    var game_id := String(game.get("webMountGameId", ""))
    var mount_point := String(game.get("webMountPoint", ""))
    if mount_point.is_empty() or (backend == "http" and base_url.is_empty()) or (backend == "blob" and game_id.is_empty()):
        return true
    var mounted_source := "(function(){return typeof AetherKiriIsHttpGameMounted==='function'&&AetherKiriIsHttpGameMounted(%s)?'1':'0';})()" % JSON.stringify(mount_point)
    if _web_eval_string(mounted_source) == "1":
        return true
    var mount_source := ""
    if backend == "blob":
        mount_source = "(function(){if(typeof AetherKiriMountLocalBlobGame!=='function')return JSON.stringify({ok:false,error:'Browser local mount API is not ready'});return AetherKiriMountLocalBlobGame(%s,%s);})()" % [
            JSON.stringify(mount_point),
            JSON.stringify(game_id),
        ]
    else:
        var manifest = _web_sync_get_json(base_url + "/manifest")
        if not manifest is Dictionary or not manifest.has("files"):
            _show_message(_t("message.web_manifest_failed"))
            return false
        var manifest_text := JSON.stringify(manifest)
        mount_source = "(function(){if(typeof AetherKiriMountHttpGame!=='function')return JSON.stringify({ok:false,error:'Web mount API is not ready'});return AetherKiriMountHttpGame(%s,%s,%s);})()" % [
            JSON.stringify(mount_point),
            JSON.stringify(base_url),
            JSON.stringify(manifest_text),
        ]
    var result_text := _web_eval_string(mount_source)
    var result = JSON.parse_string(result_text)
    if result is Dictionary and bool(result.get("ok", false)):
        return true
    var error := _t("message.unknown_error")
    if result is Dictionary:
        error = String(result.get("error", error))
    _show_message(_t("message.web_mount_failed", [error]))
    return false

func _web_local_picker_support() -> Dictionary:
    var source := "(function(){if(typeof AetherKiriLocalPickerSupport!=='function')return JSON.stringify({directory:false,archive:false});return AetherKiriLocalPickerSupport();})()"
    var text := _web_eval_string(source)
    var parsed = JSON.parse_string(text)
    return parsed if parsed is Dictionary else {}

func _web_local_game_restore_state() -> Dictionary:
    var source := "(function(){if(typeof AetherKiriLocalGameRestoreState!=='function')return JSON.stringify({done:true});return AetherKiriLocalGameRestoreState();})()"
    var text := _web_eval_string(source)
    var parsed = JSON.parse_string(text)
    return parsed if parsed is Dictionary else {"done": true}

func _web_dev_mounts() -> Array:
    var response = _web_sync_get_json("/__aetherkiri/games")
    if not response is Dictionary or not response.has("games"):
        return []
    var games = response.get("games", [])
    return games if games is Array else []

func _web_dev_config() -> Dictionary:
    var response = _web_sync_get_json("/__aetherkiri/config")
    return response if response is Dictionary else {}

func _select_web_auto_start_mount(config: Dictionary, games: Array) -> Dictionary:
    var desired_name := String(config.get("autoStartName", "")).strip_edges()
    if not desired_name.is_empty():
        for item in games:
            if not item is Dictionary:
                continue
            var name := String(item.get("name", ""))
            var id := String(item.get("id", ""))
            var path := String(item.get("gamePath", item.get("path", "")))
            if name == desired_name or id == desired_name or path == desired_name:
                var named_match: Dictionary = item
                return named_match
        return {}

    var index := int(config.get("autoStartIndex", 0))
    if index < 0:
        index = 0
    if index < games.size() and games[index] is Dictionary:
        var indexed_match: Dictionary = games[index]
        return indexed_match
    for item in games:
        if item is Dictionary:
            var fallback_match: Dictionary = item
            return fallback_match
    return {}

func _save_game_dictionary_silent(game: Dictionary) -> bool:
    var path := String(game.get("path", ""))
    if path.is_empty() or not _path_exists(path):
        return false
    var games := _load_game_list()
    var next: Array[Dictionary] = []
    var replaced := false
    for existing in games:
        if String(existing.get("path", "")) != path:
            next.append(existing)
            continue
        var merged := _merge_game_dictionary(existing, game)
        next.append(merged)
        replaced = true
    if not replaced:
        next.append(game)
    _save_game_list(_dedupe_games(next))
    ProjectSettings.set_setting(GAME_PATH_KEY, path)
    game_path.text = path
    return true

func _auto_start_web_dev_game() -> void:
    if OS.get_name() != "Web" or web_auto_start_attempted:
        return
    web_auto_start_attempted = true
    var config := _web_dev_config()
    if not bool(config.get("autoStartGame", false)):
        return
    var dev_games := _web_dev_mounts()
    if dev_games.is_empty():
        _append_log("Web dev auto-start requested, but no AETHERKIRI_GAME_ROOT(S) mount is configured.")
        print("AetherKiri Web dev auto-start requested, but no AETHERKIRI_GAME_ROOT(S) mount is configured.")
        return
    var mount_info := _select_web_auto_start_mount(config, dev_games)
    if mount_info.is_empty():
        _append_log("Web dev auto-start did not find a matching game mount.")
        print("AetherKiri Web dev auto-start did not find a matching game mount.")
        return
    var game := _web_game_from_mount_info(mount_info)
    _append_log("Web dev auto-start mounting: %s" % _game_display_title(game))
    if not _mount_web_game(game):
        return
    selected_game = game
    if not _save_game_dictionary_silent(game):
        _append_log("Web dev auto-start could not persist the selected game.")
    _refresh_games()
    call_deferred("_start_selected_game")

func _pick_web_local_game(kind: String) -> void:
    var start_source := "(function(){if(typeof AetherKiriPickLocalGame!=='function')return JSON.stringify({ok:false,error:'Browser local picker is not ready'});return AetherKiriPickLocalGame(%s);})()" % JSON.stringify(kind)
    var start_result = JSON.parse_string(_web_eval_string(start_source))
    if not start_result is Dictionary or not bool(start_result.get("ok", false)):
        var start_error := _t("message.browser_picker_unsupported")
        if start_result is Dictionary:
            start_error = String(start_result.get("error", start_error))
        _show_message(start_error)
        return

    var ticket := String(start_result.get("ticket", ""))
    if ticket.is_empty():
        _show_message(_t("message.browser_no_ticket"))
        return

    var deadline_msec := Time.get_ticks_msec() + 5 * 60 * 1000
    while Time.get_ticks_msec() < deadline_msec:
        await get_tree().create_timer(0.25).timeout
        var poll_source := "(function(){if(typeof AetherKiriTakeLocalGamePickResult!=='function')return JSON.stringify({status:'error',error:'Browser local picker is not ready'});return AetherKiriTakeLocalGamePickResult(%s);})()" % JSON.stringify(ticket)
        var poll_result = JSON.parse_string(_web_eval_string(poll_source))
        if not poll_result is Dictionary:
            continue
        var status := String(poll_result.get("status", ""))
        if status == "pending":
            continue
        if status == "cancelled":
            return
        if status == "error" or status == "missing":
            _show_message(_t("message.web_import_failed", [String(poll_result.get("error", _t("message.unknown_error")))]))
            return
        if status == "ok":
            var game_data = poll_result.get("game", {})
            if not game_data is Dictionary:
                _show_message(_t("message.web_game_invalid"))
                return
            var game := _web_game_from_mount_info(game_data)
            if not _mount_web_game(game):
                return
            _add_game_dictionary(game)
            return
    _show_message(_t("message.web_import_timeout"))

func _show_web_import_picker() -> void:
    var support := _web_local_picker_support()
    var dev_games := _web_dev_mounts()
    if not bool(support.get("directory", false)) and dev_games.is_empty():
        _show_message(_t("message.web_picker_unsupported_long"))
        return

    var dialog := _modal_dialog(Vector2(600, 400))
    var box := _modal_stack(dialog, _t("dialog.import_title"), ICON_ADD)

    if bool(support.get("directory", false)):
        var dir_button := _detail_action(ICON_LIBRARY, _t("dialog.select_local_game_dir"))
        dir_button.pressed.connect(func():
            _dismiss_modal(func(): _pick_web_local_game("directory"))
        )
        box.add_child(dir_button)

    for item in dev_games:
        if not item is Dictionary:
            continue
        var game := _web_game_from_mount_info(item)
        var captured_game := game.duplicate(true)
        var button := _pill_button(_t("dialog.dev_mount", [String(game.get("name", ""))]))
        button.pressed.connect(func():
            _dismiss_modal(func():
                if not _mount_web_game(captured_game):
                    return
                _add_game_dictionary(captured_game)
            )
        )
        box.add_child(button)
    var cancel := Button.new()
    cancel.text = _t("dialog.cancel")
    cancel.custom_minimum_size = Vector2(108, 44)
    cancel.size_flags_horizontal = Control.SIZE_SHRINK_END
    ui_widgets.secondary_button(cancel)
    cancel.pressed.connect(_dismiss_modal)
    box.add_child(cancel)

func _open_import_dialog() -> void:
    if not _ensure_android_storage_permission_for_import():
        return
    var dialog := _create_file_dialog(
        _t("dialog.select_game_dir"),
        FileDialog.FILE_MODE_OPEN_DIR,
        PackedStringArray()
    )
    if OS.get_name() == "macOS":
        var last_import_directory := _load_last_import_directory()
        if not last_import_directory.is_empty():
            dialog.current_dir = last_import_directory
    dialog.dir_selected.connect(func(path: String):
        if _add_game_path(path):
            _remember_import_directory(path)
    )
    dialog.file_selected.connect(func(path: String):
        if _add_game_path(path):
            _remember_import_directory(path)
    )
    add_child(dialog)
    dialog.popup_centered(Vector2i(900, 640))

func _load_last_import_directory() -> String:
    var cfg := ConfigFile.new()
    if cfg.load(IMPORT_STATE_FILE) != OK:
        return ""
    var path := String(cfg.get_value("import", "last_directory", ""))
    return path if DirAccess.dir_exists_absolute(path) else ""

func _remember_import_directory(imported_path: String) -> void:
    if OS.get_name() != "macOS":
        return
    var parent_directory := imported_path.simplify_path().get_base_dir()
    if not DirAccess.dir_exists_absolute(parent_directory):
        return
    var cfg := ConfigFile.new()
    cfg.set_value("import", "last_directory", parent_directory)
    cfg.save(IMPORT_STATE_FILE)

func _refresh_games() -> void:
    var loaded_games := _load_game_list()
    known_games = builtin_demo.reconcile_games(loaded_games)
    var library_changed := JSON.stringify(known_games) != JSON.stringify(loaded_games)
    if _backfill_game_metadata(known_games):
        library_changed = true
    if _backfill_default_game_covers(known_games):
        library_changed = true
    _sync_cover_index(known_games)
    if OS.get_name() == "iOS":
        known_games = _scan_ios_games_dir(known_games)
        _save_game_list(known_games)
    elif library_changed:
        _save_game_list(known_games)
    if library_changed:
        _sync_web_user_fs("builtin_demo_reconciled")
    known_games = _sorted_games(known_games)
    _rebuild_game_cards(not home_cards_animated_once)

func _rebuild_game_cards(animate_cards: bool = false) -> void:
    if game_list == null:
        return
    var query := String(home_search_queries.get("game", ""))
    var filtered_games: Array[Dictionary] = []
    for game in known_games:
        if _game_matches_home_search(game, query):
            filtered_games.append(game)
    home_filtered_game_count = filtered_games.size()
    for child in game_list.get_children():
        game_list.remove_child(child)
        child.queue_free()
    for index in range(filtered_games.size()):
        var card := _game_card(filtered_games[index])
        game_list.add_child(card)
        if animate_cards:
            ui_motion.reveal(card, minf(float(index) * 0.025, 0.15))
    if animate_cards and not filtered_games.is_empty():
        home_cards_animated_once = true
    _sync_home_header_text()
    _apply_home_library_visibility()

func _refresh_videos() -> void:
    if video_progress_data.is_empty():
        video_progress_data = _load_video_progress()
    known_videos = _load_video_list()
    if OS.get_name() == "iOS":
        var video_root := ProjectSettings.globalize_path("user://Video")
        DirAccess.make_dir_recursive_absolute(video_root)
        known_videos = _filter_hidden_ios_videos(_scan_video_directory(video_root))
        _save_video_list(known_videos)
    known_videos.sort_custom(func(a: Dictionary, b: Dictionary):
        return String(a.get("name", "")).naturalnocasecmp_to(String(b.get("name", ""))) < 0
    )
    _rebuild_video_cards(false)

func _rebuild_video_cards(_animate_cards: bool = false) -> void:
    var query := String(home_search_queries.get("video", ""))
    var filtered_videos: Array[Dictionary] = []
    for video in known_videos:
        if _video_matches_home_search(video, query):
            filtered_videos.append(video)
    home_filtered_video_count = filtered_videos.size()
    if video_list != null:
        for child in video_list.get_children():
            video_list.remove_child(child)
            child.queue_free()
        for video in filtered_videos:
            video_list.add_child(_video_card(video))
    _sync_home_header_text()
    _apply_home_library_visibility()

func _load_video_list() -> Array[Dictionary]:
    var file := FileAccess.open(VIDEO_LIST_FILE, FileAccess.READ)
    if file == null:
        return []
    var parsed = JSON.parse_string(file.get_as_text())
    if not parsed is Array:
        return []
    var videos: Array[Dictionary] = []
    for item in parsed:
        if not item is Dictionary:
            continue
        var path := String(item.get("path", ""))
        if FileAccess.file_exists(path) and _is_video_path(path):
            videos.append(_video_info(path))
    return videos

func _save_video_list(videos: Array[Dictionary]) -> void:
    var file := FileAccess.open(VIDEO_LIST_FILE, FileAccess.WRITE)
    if file != null:
        file.store_string(JSON.stringify(videos))

func _load_hidden_videos() -> Dictionary:
    var file := FileAccess.open(VIDEO_HIDDEN_FILE, FileAccess.READ)
    if file == null:
        return {}
    var parsed = JSON.parse_string(file.get_as_text())
    return parsed if parsed is Dictionary else {}

func _save_hidden_videos(hidden: Dictionary) -> void:
    var file := FileAccess.open(VIDEO_HIDDEN_FILE, FileAccess.WRITE)
    if file != null:
        file.store_string(JSON.stringify(hidden))

func _filter_hidden_ios_videos(videos: Array[Dictionary]) -> Array[Dictionary]:
    var hidden := _load_hidden_videos()
    var retained_hidden := {}
    var visible: Array[Dictionary] = []
    for video in videos:
        var path := String(video.get("path", ""))
        var modified := int(video.get("modified", 0))
        if int(hidden.get(path, -1)) == modified:
            retained_hidden[path] = modified
        else:
            visible.append(video)
    if JSON.stringify(retained_hidden) != JSON.stringify(hidden):
        _save_hidden_videos(retained_hidden)
    return visible

func _scan_video_directory(root: String) -> Array[Dictionary]:
    var videos: Array[Dictionary] = []
    _scan_video_directory_into(root, videos)
    return videos

func _scan_video_directory_into(root: String, videos: Array[Dictionary]) -> void:
    var dir := DirAccess.open(root)
    if dir == null:
        return
    dir.list_dir_begin()
    while true:
        var name := dir.get_next()
        if name.is_empty():
            break
        if name.begins_with("."):
            continue
        var path := root.path_join(name)
        if dir.current_is_dir():
            _scan_video_directory_into(path, videos)
        elif _is_video_path(path):
            videos.append(_video_info(path))
    dir.list_dir_end()

func _is_video_path(path: String) -> bool:
    return path.get_extension().to_lower() in VIDEO_EXTENSIONS

func _video_info(path: String) -> Dictionary:
    return {
        "path": path,
        "name": path.get_file().get_basename(),
        "fileName": path.get_file(),
        "modified": FileAccess.get_modified_time(path),
    }

func _add_video_path(path: String) -> void:
    var normalized := path.simplify_path()
    if not FileAccess.file_exists(normalized) or not _is_video_path(normalized):
        return
    var videos := _load_video_list()
    var next: Array[Dictionary] = []
    for video in videos:
        if String(video.get("path", "")) != normalized:
            next.append(video)
    next.append(_video_info(normalized))
    _save_video_list(next)
    _refresh_videos()

func _open_video_import_dialog() -> void:
    if OS.get_name() == "Android":
        if not android_video_import_notice_shown:
            android_video_import_notice_shown = true
            _show_android_storage_permission_prompt(
                Callable(self, "_continue_video_import_after_permission_notice"),
                "message.android_video_storage_permission_required"
            )
            return
        if not _ensure_android_storage_permission_for_import(true):
            return
    _show_video_file_dialog()

func _continue_video_import_after_permission_notice() -> void:
    if not _android_has_external_storage_permission():
        _request_android_storage_permissions()
        return
    _open_video_import_dialog()

func _show_video_file_dialog() -> void:
    var filters := PackedStringArray([
        "*.mp4,*.mkv,*.mov,*.m4v,*.avi,*.webm,*.flv,*.ts,*.m2ts,*.mpeg,*.mpg,*.wmv;Video files"
    ])
    var dialog := _create_file_dialog(
        _t("video.import"),
        FileDialog.FILE_MODE_OPEN_FILE,
        filters
    )
    dialog.file_selected.connect(_add_video_path)
    add_child(dialog)
    dialog.popup_centered(Vector2i(900, 640))

func _video_card(video: Dictionary) -> Control:
    var path := String(video.get("path", ""))
    var progress: Dictionary = video_progress_data.get(path, {})
    var position := float(progress.get("position", 0.0))
    var duration := float(progress.get("duration", 0.0))
    var detail := String(video.get("fileName", path.get_file()))
    if position > 1.0:
        detail = "%s  ·  %s" % [
            detail,
            _t("video.progress", [
                _format_video_time(position),
                _format_video_time(duration),
            ]),
        ]

    var card := Control.new()
    card.custom_minimum_size = _home_card_minimum_size(home_compact_layout)
    card.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    card.clip_contents = true

    var button := Button.new()
    button.set_anchors_preset(Control.PRESET_FULL_RECT)
    button.clip_contents = true
    button.focus_mode = Control.FOCUS_ALL
    button.text = ""
    _style_home_card_button(button, home_compact_layout)
    var captured := video.duplicate(true)
    button.pressed.connect(func(): _open_video_player(captured))
    card.add_child(button)

    if home_compact_layout:
        _build_compact_video_card(button, video, detail)
    else:
        _build_desktop_video_card(button, video, detail)

    ui_motion.bind_lift(button)
    _add_video_card_progress(card, position, duration)

    var remove := Button.new()
    remove.tooltip_text = _t("video.remove")
    remove.accessibility_name = _t("video.remove")
    remove.focus_mode = Control.FOCUS_ALL
    ui_widgets.toolbar_button(remove)
    _attach_centered_button_icon(remove, ICON_DELETE, Vector2(17, 17))
    remove.anchor_left = 1.0
    remove.anchor_top = 0.5
    remove.anchor_right = 1.0
    remove.anchor_bottom = 0.5
    var remove_size := 38.0 if home_compact_layout else 36.0
    remove.offset_left = -remove_size - (8.0 if home_compact_layout else 10.0)
    remove.offset_top = -remove_size * 0.5
    remove.offset_right = -(8.0 if home_compact_layout else 10.0)
    remove.offset_bottom = remove_size * 0.5
    remove.custom_minimum_size = Vector2(remove_size, remove_size)
    remove.pressed.connect(func(): _confirm_remove_video(captured))
    card.add_child(remove)
    return card

func _add_video_card_progress(card: Control, position: float, duration: float) -> void:
    if position <= 1.0 or duration <= 0.0:
        return
    var ratio := clampf(position / duration, 0.0, 1.0)
    var track := ColorRect.new()
    track.name = "PlaybackProgressTrack"
    track.mouse_filter = Control.MOUSE_FILTER_IGNORE
    track.color = Color(ui_tokens.text_tertiary, 0.28)
    track.anchor_top = 1.0
    track.anchor_right = 1.0
    track.anchor_bottom = 1.0
    track.offset_top = -4.0
    card.add_child(track)

    var fill := ColorRect.new()
    fill.name = "PlaybackProgressFill"
    fill.mouse_filter = Control.MOUSE_FILTER_IGNORE
    fill.color = ui_tokens.accent
    fill.anchor_right = ratio
    fill.anchor_bottom = 1.0
    track.add_child(fill)

func _build_desktop_video_card(button: Button, video: Dictionary, detail: String) -> void:
    var content_margin := MarginContainer.new()
    content_margin.mouse_filter = Control.MOUSE_FILTER_IGNORE
    content_margin.set_anchors_preset(Control.PRESET_FULL_RECT)
    content_margin.add_theme_constant_override("margin_left", 8)
    content_margin.add_theme_constant_override("margin_top", 8)
    content_margin.add_theme_constant_override("margin_right", 10)
    content_margin.add_theme_constant_override("margin_bottom", 8)
    button.add_child(content_margin)

    var frame := HBoxContainer.new()
    frame.mouse_filter = Control.MOUSE_FILTER_IGNORE
    frame.clip_contents = true
    frame.add_theme_constant_override("separation", 12)
    content_margin.add_child(frame)

    var cover_host := PanelContainer.new()
    cover_host.mouse_filter = Control.MOUSE_FILTER_IGNORE
    cover_host.custom_minimum_size = Vector2(HOME_TILE_COVER_WIDTH, HOME_TILE_HEIGHT - 16.0)
    cover_host.size_flags_vertical = Control.SIZE_EXPAND_FILL
    cover_host.clip_contents = true
    cover_host.add_theme_stylebox_override("panel", ui_tokens.panel(ui_tokens.surface_raised, 8))
    frame.add_child(cover_host)
    _populate_video_card_cover(cover_host, false)

    var metadata := PanelContainer.new()
    metadata.mouse_filter = Control.MOUSE_FILTER_IGNORE
    metadata.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    metadata.size_flags_vertical = Control.SIZE_EXPAND_FILL
    metadata.add_theme_stylebox_override("panel", ui_tokens.panel(Color.TRANSPARENT, 0))
    frame.add_child(metadata)
    _populate_video_card_metadata(metadata, video, detail, false)

    var action_space := Control.new()
    action_space.mouse_filter = Control.MOUSE_FILTER_IGNORE
    action_space.custom_minimum_size = Vector2(38, 0)
    frame.add_child(action_space)

func _build_compact_video_card(button: Button, video: Dictionary, detail: String) -> void:
    var frame := HBoxContainer.new()
    frame.mouse_filter = Control.MOUSE_FILTER_IGNORE
    frame.clip_contents = true
    frame.set_anchors_preset(Control.PRESET_FULL_RECT)
    frame.add_theme_constant_override("separation", 0)
    button.add_child(frame)

    var cover_host := Control.new()
    cover_host.mouse_filter = Control.MOUSE_FILTER_IGNORE
    cover_host.custom_minimum_size = Vector2(HOME_ROW_COVER_WIDTH, HOME_ROW_HEIGHT)
    cover_host.size_flags_vertical = Control.SIZE_EXPAND_FILL
    frame.add_child(cover_host)
    _populate_video_card_cover(cover_host, true)

    var metadata := PanelContainer.new()
    metadata.mouse_filter = Control.MOUSE_FILTER_IGNORE
    metadata.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    metadata.size_flags_vertical = Control.SIZE_EXPAND_FILL
    metadata.add_theme_stylebox_override("panel", ui_tokens.panel(ui_tokens.surface, 0))
    frame.add_child(metadata)
    _populate_video_card_metadata(metadata, video, detail, true)

    var action_space := Control.new()
    action_space.mouse_filter = Control.MOUSE_FILTER_IGNORE
    action_space.custom_minimum_size = Vector2(54, HOME_ROW_HEIGHT)
    frame.add_child(action_space)

func _populate_video_card_cover(cover_host: Control, compact: bool) -> void:
    var placeholder := PanelContainer.new()
    placeholder.mouse_filter = Control.MOUSE_FILTER_IGNORE
    placeholder.set_anchors_preset(Control.PRESET_FULL_RECT)
    placeholder.add_theme_stylebox_override(
        "panel",
        ui_tokens.panel(ui_tokens.surface_raised, 0 if compact else 8)
    )
    cover_host.add_child(placeholder)
    var icon_size := Vector2(34, 34) if compact else Vector2(28, 28)
    var icon := _centered_icon(ICON_VIDEO, icon_size, ui_tokens.accent)
    icon.set_anchors_preset(Control.PRESET_FULL_RECT)
    placeholder.add_child(icon)

func _populate_video_card_metadata(
    metadata: PanelContainer,
    video: Dictionary,
    detail: String,
    compact: bool
) -> void:
    var text_margin := MarginContainer.new()
    text_margin.mouse_filter = Control.MOUSE_FILTER_IGNORE
    text_margin.add_theme_constant_override("margin_left", 14 if compact else 2)
    text_margin.add_theme_constant_override("margin_top", 13 if compact else 7)
    text_margin.add_theme_constant_override("margin_right", 8 if compact else 2)
    text_margin.add_theme_constant_override("margin_bottom", 10 if compact else 7)
    metadata.add_child(text_margin)

    var labels := VBoxContainer.new()
    labels.mouse_filter = Control.MOUSE_FILTER_IGNORE
    labels.alignment = BoxContainer.ALIGNMENT_CENTER if compact else BoxContainer.ALIGNMENT_BEGIN
    labels.add_theme_constant_override("separation", 4)
    text_margin.add_child(labels)

    if not compact:
        var kicker := Label.new()
        var extension := String(video.get("fileName", "")).get_extension().to_upper()
        kicker.text = extension if not extension.is_empty() else _t("nav.videos").to_upper()
        kicker.mouse_filter = Control.MOUSE_FILTER_IGNORE
        kicker.add_theme_font_size_override("font_size", 9)
        kicker.add_theme_color_override("font_color", ui_tokens.text_tertiary)
        labels.add_child(kicker)

    var title := Label.new()
    title.text = String(video.get("name", String(video.get("path", "")).get_file()))
    title.mouse_filter = Control.MOUSE_FILTER_IGNORE
    title.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
    title.max_lines_visible = 2
    title.text_overrun_behavior = TextServer.OVERRUN_TRIM_ELLIPSIS
    title.custom_minimum_size = Vector2(0, 42 if compact else 34)
    title.add_theme_font_override("font", _game_title_font())
    title.add_theme_font_size_override("font_size", 16 if compact else 15)
    title.add_theme_color_override("font_color", ui_tokens.text_primary)
    labels.add_child(title)

    if not compact:
        var spacer := Control.new()
        spacer.mouse_filter = Control.MOUSE_FILTER_IGNORE
        spacer.size_flags_vertical = Control.SIZE_EXPAND_FILL
        labels.add_child(spacer)

    var sub := Label.new()
    sub.text = "%s  /  %s" % [_t("nav.videos"), detail] if compact else detail
    sub.mouse_filter = Control.MOUSE_FILTER_IGNORE
    sub.clip_text = true
    sub.add_theme_font_size_override("font_size", 12 if compact else 11)
    sub.add_theme_color_override("font_color", ui_tokens.text_secondary)
    labels.add_child(sub)

func _confirm_remove_video(video: Dictionary) -> void:
    var path := String(video.get("path", ""))
    if path.is_empty():
        return
    modal_layer.visible = true
    for child in modal_layer.get_children():
        child.queue_free()
    var dim := ColorRect.new()
    dim.color = Color(0, 0, 0, 0.38)
    dim.set_anchors_preset(Control.PRESET_FULL_RECT)
    modal_layer.add_child(dim)
    var dialog := PanelContainer.new()
    _mark_centered_safe_dialog(dialog, Vector2(560, 280))
    _layout_safe_dialog(dialog, _ui_safe_rect(get_viewport_rect().size))
    dialog.add_theme_stylebox_override("panel", _panel_style(20, color_card, Color(0, 0, 0, 0.06), 1))
    modal_layer.add_child(dialog)
    var box := VBoxContainer.new()
    box.add_theme_constant_override("separation", 18)
    dialog.add_child(box)
    var label := Label.new()
    label.text = _t("video.remove_body", [String(video.get("name", path.get_file()))])
    label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
    label.add_theme_font_size_override("font_size", 22)
    label.add_theme_color_override("font_color", color_text)
    box.add_child(label)
    var buttons := HBoxContainer.new()
    buttons.add_theme_constant_override("separation", 12)
    buttons.alignment = BoxContainer.ALIGNMENT_END
    buttons.custom_minimum_size = Vector2(0, 62)
    box.add_child(buttons)
    var cancel := Button.new()
    cancel.text = _t("dialog.cancel")
    cancel.flat = true
    cancel.custom_minimum_size = Vector2(112, 62)
    cancel.add_theme_font_size_override("font_size", 20)
    cancel.add_theme_color_override("font_color", color_text)
    cancel.pressed.connect(func(): modal_layer.visible = false)
    buttons.add_child(cancel)
    var remove := _pill_button(_t("dialog.remove"))
    remove.custom_minimum_size = Vector2(148, 62)
    remove.pressed.connect(func():
        modal_layer.visible = false
        _remove_video(path)
    )
    buttons.add_child(remove)

func _remove_video(path: String) -> void:
    var videos := _load_video_list()
    var next: Array[Dictionary] = []
    for video in videos:
        if String(video.get("path", "")) != path:
            next.append(video)
    _save_video_list(next)
    if OS.get_name() == "iOS":
        var hidden := _load_hidden_videos()
        hidden[path] = int(FileAccess.get_modified_time(path))
        _save_hidden_videos(hidden)
    video_progress_data.erase(path)
    _save_video_progress_file()
    _refresh_videos()

func _load_video_progress() -> Dictionary:
    var file := FileAccess.open(VIDEO_PROGRESS_FILE, FileAccess.READ)
    if file == null:
        return {}
    var parsed = JSON.parse_string(file.get_as_text())
    return parsed if parsed is Dictionary else {}

func _save_video_progress_file() -> void:
    var file := FileAccess.open(VIDEO_PROGRESS_FILE, FileAccess.WRITE)
    if file != null:
        file.store_string(JSON.stringify(video_progress_data))
        file.flush()

func _store_active_video_progress(finished: bool = false) -> void:
    if active_video_path.is_empty():
        return
    finished = finished or int(active_video_state.get("status", 0)) == MEDIA_STATUS_ENDED
    var position := float(active_video_state.get("position", 0.0))
    var duration := float(active_video_state.get("duration", active_video_duration))
    if finished or (duration > 0.0 and duration - position < 15.0):
        video_progress_data.erase(active_video_path)
    else:
        video_progress_data[active_video_path] = {
            "position": position,
            "duration": duration,
            "updated": Time.get_unix_time_from_system(),
        }
    _save_video_progress_file()

func _open_video_player(video: Dictionary) -> void:
    if not _require_legal_documents_for_media():
        return
    if _begin_iap_checked_launch("video", video):
        return
    _open_video_player_after_iap(video)

func _open_video_player_after_iap(video: Dictionary) -> void:
    var path := String(video.get("path", ""))
    if path.is_empty() or player == null or not _ensure_player_initialized():
        return
    if not bool(player.media_open(path)):
        _show_message(_t("video.open_failed", [String(player.get_last_error())]))
        return
    active_video_path = path
    active_video_duration = 0.0
    active_video_state = {}
    video_pending_resume_position = 0.0
    _reset_video_seek_gesture()
    active_video_end_handled = false
    active_video_was_playing = false
    active_subtitle_index = 0
    video_progress_save_accum = 0.0
    video_title_label.text = String(video.get("name", path.get_file()))
    video_texture.texture = null
    video_subtitle_label.text = ""
    video_progress_slider.value = 0.0
    video_progress_slider.max_value = 1.0
    video_rate_button.select(2)
    _load_video_subtitle_tracks(path)
    shell_root.visible = false
    video_view.visible = true
    video_view.move_to_front()
    video_previous_mouse_mode = Input.mouse_mode
    video_playing = true
    video_touch_mouse_suppress_until_msec = 0
    _set_video_controls_visible(false, false)
    player.media_play()
    var resume: Dictionary = video_progress_data.get(path, {})
    var resume_position := float(resume.get("position", 0.0))
    if resume_position > 2.0:
        # The native player initially reports a zero duration, so seeking in
        # this frame would be clamped back to the beginning. Defer the seek
        # until media metadata reports a seekable stream and a valid duration.
        video_pending_resume_position = resume_position
    _sync_video_play_button(MEDIA_STATUS_PLAYING)

func _close_video_player() -> void:
    if not video_playing:
        return
    _store_active_video_progress()
    if player != null:
        player.media_pause()
        player.media_close()
    video_playing = false
    active_video_path = ""
    active_video_state = {}
    video_pending_resume_position = 0.0
    active_subtitle_tracks.clear()
    active_subtitle_cues.clear()
    video_texture.texture = null
    if video_controls_tween != null and video_controls_tween.is_valid():
        video_controls_tween.kill()
    video_controls_visible = false
    video_controls_idle_sec = 0.0
    video_touch_mouse_suppress_until_msec = 0
    _reset_video_seek_gesture()
    video_top_bar.visible = false
    video_controls.visible = false
    video_view.visible = false
    shell_root.visible = true
    Input.mouse_mode = video_previous_mouse_mode
    _show_video_library()

func _sync_video_play_button(status: int) -> void:
    if not is_instance_valid(video_play_button):
        return
    var playing := status == MEDIA_STATUS_PLAYING
    video_play_button.text = "Ⅱ" if playing else "▶"
    video_play_button.tooltip_text = _t("video.pause") if playing else _t("video.play")

func _toggle_video_playback() -> void:
    if not video_playing or player == null:
        return
    _set_video_controls_visible(true)
    var status := int(active_video_state.get("status", MEDIA_STATUS_PAUSED))
    if status == MEDIA_STATUS_PLAYING:
        player.media_pause()
        _sync_video_play_button(MEDIA_STATUS_PAUSED)
    else:
        if status == MEDIA_STATUS_ENDED:
            player.media_seek(0.0)
        player.media_play()
        _sync_video_play_button(MEDIA_STATUS_PLAYING)

func _seek_video_relative(offset: float) -> void:
    if not video_playing or player == null:
        return
    _set_video_controls_visible(true)
    var position := float(active_video_state.get("position", 0.0))
    var duration := float(active_video_state.get("duration", active_video_duration))
    player.media_seek(clampf(position + offset, 0.0, maxf(0.0, duration)))

func _load_video_subtitle_tracks(video_path: String) -> void:
    active_subtitle_tracks.clear()
    active_subtitle_cues.clear()
    video_subtitle_button.clear()
    video_subtitle_button.add_item(_t("video.subtitle_off"))
    var external_track_count := 0
    var directory := video_path.get_base_dir()
    var stem := video_path.get_file().get_basename()
    var dir := DirAccess.open(directory)
    if dir != null:
        for file_name in dir.get_files():
            var extension := file_name.get_extension().to_lower()
            var subtitle_stem := file_name.get_basename()
            if extension in SUBTITLE_EXTENSIONS and (
                subtitle_stem == stem or subtitle_stem.begins_with(stem + ".")
            ):
                active_subtitle_tracks.append({
                    "kind": "external",
                    "path": directory.path_join(file_name),
                    "label": file_name,
                    "default": false,
                })
                external_track_count += 1
    active_subtitle_tracks.sort_custom(func(left: Dictionary, right: Dictionary):
        return String(left.get("label", "")) < String(right.get("label", ""))
    )
    if player != null and player.has_method("media_get_subtitle_tracks_json"):
        var embedded_value = JSON.parse_string(
            String(player.media_get_subtitle_tracks_json())
        )
        if embedded_value is Array:
            for embedded_track in embedded_value:
                if not embedded_track is Dictionary:
                    continue
                var title := String(embedded_track.get("title", "")).strip_edges()
                var language := String(embedded_track.get("language", "")).strip_edges()
                var codec := String(embedded_track.get("codec", "text")).to_upper()
                var track_name := title
                if track_name.is_empty():
                    track_name = language if not language.is_empty() else codec
                active_subtitle_tracks.append({
                    "kind": "embedded",
                    "stream_index": int(embedded_track.get("stream_index", -1)),
                    "label": _t("video.subtitle_embedded", [track_name]),
                    "default": bool(embedded_track.get("default", false)),
                })
    for track in active_subtitle_tracks:
        video_subtitle_button.add_item(String(track.get("label", "")))
    var selected_index := 0
    if external_track_count > 0:
        selected_index = 1
    else:
        for index in active_subtitle_tracks.size():
            if selected_index == 0:
                selected_index = index + 1
            if bool(active_subtitle_tracks[index].get("default", false)):
                selected_index = index + 1
                break
    video_subtitle_button.select(selected_index)
    if selected_index > 0:
        _select_video_subtitle(selected_index)

func _select_video_subtitle(index: int) -> void:
    if video_playing:
        _set_video_controls_visible(true)
    active_subtitle_cues.clear()
    active_subtitle_index = 0
    video_subtitle_label.text = ""
    if index <= 0 or index > active_subtitle_tracks.size():
        return
    var track: Dictionary = active_subtitle_tracks[index - 1]
    if String(track.get("kind", "")) == "external":
        active_subtitle_cues = VideoSubtitles.parse_file(
            String(track.get("path", ""))
        )
        return
    if player == null or not player.has_method("media_extract_subtitle"):
        return
    var stream_index := int(track.get("stream_index", -1))
    if stream_index < 0:
        return
    var cache_path := ProjectSettings.globalize_path(
        "user://.aether-embedded-subtitle-%d.ass" % stream_index
    )
    if bool(player.media_extract_subtitle(stream_index, cache_path)):
        active_subtitle_cues = VideoSubtitles.parse_file(cache_path)
    DirAccess.remove_absolute(cache_path)

func _update_video_subtitle(position: float) -> void:
    if active_subtitle_cues.is_empty():
        video_subtitle_label.text = ""
        return
    var match_info := VideoSubtitles.text_at(active_subtitle_cues, position, active_subtitle_index)
    active_subtitle_index = int(match_info.get("index", 0))
    video_subtitle_label.text = String(match_info.get("text", ""))

func _format_video_time(seconds: float) -> String:
    var total := maxi(0, int(seconds))
    var hours := total / 3600
    var minutes := (total % 3600) / 60
    var secs := total % 60
    if hours > 0:
        return "%02d:%02d:%02d" % [hours, minutes, secs]
    return "%02d:%02d" % [minutes, secs]

func _load_game_list() -> Array[Dictionary]:
    var file := FileAccess.open(GAME_LIST_FILE, FileAccess.READ)
    if file == null:
        var fallback: String = _resolve_game_path(String(ProjectSettings.get_setting(GAME_PATH_KEY, "")))
        var initial_games: Array[Dictionary] = []
        if not fallback.is_empty() and _path_exists(fallback):
            initial_games.append(_game_info_from_path(fallback))
        return initial_games
    var parsed = JSON.parse_string(file.get_as_text())
    if not parsed is Array:
        return []
    var games: Array[Dictionary] = []
    for item in parsed:
        if item is Dictionary and item.has("path"):
            var resolved_path := _resolve_game_path(String(item.get("path", "")))
            if not resolved_path.is_empty() and _path_exists(resolved_path) and _web_game_entry_available(item):
                var game: Dictionary = item.duplicate(true)
                game["path"] = resolved_path
                games.append(game)
    return games

func _save_game_list(games: Array[Dictionary]) -> void:
    var file := FileAccess.open(GAME_LIST_FILE, FileAccess.WRITE)
    if file != null:
        file.store_string(JSON.stringify(games))

func _scan_ios_games_dir(existing: Array[Dictionary]) -> Array[Dictionary]:
    var root := ProjectSettings.globalize_path("user://Games")
    DirAccess.make_dir_recursive_absolute(root)
    var by_entry := _games_by_library_entry(existing, root)
    var normalized_root := root.simplify_path().trim_suffix("/")
    var next: Array[Dictionary] = []
    for game in existing:
        var existing_path := String(game.get("path", "")).simplify_path()
        if not existing_path.begins_with(normalized_root + "/") \
                and _path_exists(existing_path):
            next.append(game)
    var dir := DirAccess.open(root)
    if dir == null:
        return next
    dir.list_dir_begin()
    var entry := dir.get_next()
    while not entry.is_empty():
        if not entry.begins_with("."):
            var path := root.path_join(entry)
            if dir.current_is_dir() or entry.to_lower().ends_with(".xp3"):
                var game: Dictionary = by_entry.get(entry, _game_info_from_path(path))
                game["path"] = path
                next.append(game)
        entry = dir.get_next()
    return _dedupe_games(next)

func _games_by_library_entry(existing: Array[Dictionary], root: String) -> Dictionary:
    var by_entry := {}
    var normalized_root := root.simplify_path().trim_suffix("/")
    for game in existing:
        var existing_path := String(game.get("path", "")).simplify_path()
        if existing_path.begins_with(normalized_root + "/"):
            by_entry[existing_path.get_file()] = game
    return by_entry

func _add_game_path(path: String) -> bool:
    var resolved_path := _resolve_game_path(path)
    if not _path_exists(resolved_path):
        _show_message(_t("message.path_missing"))
        return false
    return _add_game_dictionary(_game_info_from_path(resolved_path))

func _add_game_dictionary(game: Dictionary) -> bool:
    var path := String(game.get("path", ""))
    if not _path_exists(path):
        _show_message(_t("message.path_missing"))
        return false
    var games := _load_game_list()
    var next: Array[Dictionary] = []
    var final_game := game
    var replaced := false
    for existing in games:
        if String(existing.get("path", "")) == path:
            if OS.get_name() != "Web":
                _show_message(_t("message.game_exists", [_game_display_title(existing)]))
                return false
            final_game = _merge_game_dictionary(existing, game)
            next.append(final_game)
            replaced = true
        else:
            next.append(existing)
    if not replaced:
        next.append(final_game)
    _save_game_list(_dedupe_games(next))
    ProjectSettings.set_setting(GAME_PATH_KEY, path)
    game_path.text = path
    _refresh_games()
    if not replaced:
        _offer_scrape_after_add(final_game)
        _start_vndb_cover_lookup(final_game, true)
    return true

func _start_vndb_cover_lookup(game: Dictionary, force: bool = false) -> void:
    var index := CoverIndex.load_index()
    var key := CoverIndex.key_for(game)
    if key.is_empty() or (not force and not CoverIndex.needs_recognition(index, key)):
        return
    for queued in vndb_cover_queue:
        if String(queued.get("path", "")) == String(game.get("path", "")):
            return
    vndb_cover_queue.append(game)
    _process_next_vndb_cover()

func _process_next_vndb_cover() -> void:
    if vndb_cover_busy or vndb_cover_queue.is_empty():
        return
    var resolver := get_node_or_null("VNDBCoverResolver")
    if resolver == null:
        return
    vndb_cover_busy = true
    resolver.resolve(vndb_cover_queue.pop_front())

func _on_vndb_cover_resolved(game_path: String, cover_path: String, vndb_id: String) -> void:
    var games := _load_game_list()
    var index := CoverIndex.load_index()
    for game in games:
        if String(game.get("path", "")) != game_path:
            continue
        var key := CoverIndex.key_for(game)
        var resolved_cover := _portable_cover_path(game_path, cover_path) if not cover_path.is_empty() else ""
        index[key] = CoverIndex.record(resolved_cover)
        var values := {"coverPath": resolved_cover, "vndbId": vndb_id, GAME_AUTO_COVER_SCANNED_FIELD: true}
        _update_game(game_path, values)
        break
    CoverIndex.save_index(index)
    vndb_cover_busy = false
    _process_next_vndb_cover()

func _merge_game_dictionary(existing: Dictionary, game: Dictionary) -> Dictionary:
    var merged := existing.duplicate(true)
    for key in game.keys():
        var value = game[key]
        if (key == "lastPlayed" or key == "playDurationSeconds") and int(value) == 0:
            continue
        if (key == "coverPath" or key == "developer" or key == "title") and String(value).is_empty():
            continue
        merged[key] = value
    return merged

func _dedupe_games(games: Array[Dictionary]) -> Array[Dictionary]:
    var seen := {}
    var result: Array[Dictionary] = []
    for game in games:
        var path := String(game.get("path", ""))
        if path.is_empty() or seen.has(path):
            continue
        seen[path] = true
        result.append(game)
    return result

func _sorted_games(games: Array[Dictionary]) -> Array[Dictionary]:
    games.sort_custom(func(a: Dictionary, b: Dictionary) -> bool:
        var at := int(a.get("lastPlayed", 0))
        var bt := int(b.get("lastPlayed", 0))
        if at != bt:
            return at > bt
        return _game_display_title(a) < _game_display_title(b)
    )
    return games

func _path_exists(path: String) -> bool:
    if OS.get_name() == "Web" and path.begins_with("/webgames/"):
        return true
    var candidate := _android_external_storage_path_from_tree_uri(path) if OS.get_name() == "Android" else path
    if candidate.begins_with("content://"):
        return false
    return DirAccess.dir_exists_absolute(candidate) or FileAccess.file_exists(candidate)

func _resolve_game_path(path: String) -> String:
    var normalized := path.strip_edges()
    if normalized.is_empty():
        return normalized
    if OS.get_name() == "Android":
        normalized = _android_external_storage_path_from_tree_uri(normalized)
    if _path_exists(normalized) or OS.get_name() != "iOS":
        return normalized

    var current_root := ProjectSettings.globalize_path("user://Games")
    var marker := "/Documents/Games/"
    var marker_index := normalized.find(marker)
    if marker_index >= 0:
        var relative_path := normalized.substr(marker_index + marker.length())
        var current_path := current_root.path_join(relative_path)
        if _path_exists(current_path):
            return current_path

    var named_path := current_root.path_join(normalized.get_file())
    if _path_exists(named_path):
        return named_path
    return normalized

func _android_external_storage_path_from_tree_uri(path: String) -> String:
    var prefix := "content://com.android.externalstorage.documents/tree/"
    if not path.begins_with(prefix):
        return path
    var document_id := path.substr(prefix.length()).uri_decode()
    if document_id.begins_with("primary:"):
        var relative_path := document_id.substr("primary:".length()).strip_edges()
        if relative_path.is_empty():
            return "/storage/emulated/0"
        return "/storage/emulated/0".path_join(relative_path)
    if document_id.begins_with("home:"):
        var home_relative_path := document_id.substr("home:".length()).strip_edges()
        if home_relative_path.is_empty():
            return "/storage/emulated/0/Documents"
        return "/storage/emulated/0/Documents".path_join(home_relative_path)
    return path

func _web_game_entry_available(game: Dictionary) -> bool:
    if OS.get_name() != "Web":
        return true
    var backend := String(game.get("webMountBackend", ""))
    if backend.is_empty() and not String(game.get("webMountBaseUrl", "")).is_empty():
        backend = "http"
    if backend.is_empty() and not String(game.get("webMountGameId", "")).is_empty():
        backend = "blob"
    if backend.is_empty() and String(game.get("path", "")).begins_with("/webgames/"):
        return false
    if backend == "http":
        var base_url := String(game.get("webMountBaseUrl", ""))
        if base_url.begins_with("/__aetherkiri/game/"):
            var manifest = _web_sync_get_json(base_url + "/manifest")
            return manifest is Dictionary and manifest.has("files")
        return true
    if backend != "blob":
        return true
    var game_id := String(game.get("webMountGameId", ""))
    if game_id.is_empty():
        return false
    var source := "(function(){if(typeof AetherKiriLocalGameAvailable==='function')return AetherKiriLocalGameAvailable(%s)?'1':'0';var g=typeof window!=='undefined'?window:globalThis;var s=g.AetherKiriLocalGameStore;return s&&s.games&&s.games[%s]?'1':'0';})()" % [
        JSON.stringify(game_id),
        JSON.stringify(game_id),
    ]
    return _web_eval_string(source) == "1"

func _game_info_from_path(path: String) -> Dictionary:
    var name := path.get_file()
    if name.to_lower().ends_with(".xp3"):
        name = name.substr(0, name.length() - 4)
    var default_cover_path := _discover_default_cover_path(path)
    var metadata := GameMetadata.inspect(path)
    var detected_title := String(metadata.get("title", ""))
    var detected_engine := String(metadata.get("engine", RUNTIME_KIRIKIRI))
    return {
        "name": detected_title if not detected_title.is_empty() else name,
        "path": path,
        "type": "Archive" if path.to_lower().ends_with(".xp3") else "Directory",
        "lastPlayed": 0,
        "playDurationSeconds": 0,
        "coverPath": _portable_cover_path(path, default_cover_path),
        GAME_AUTO_COVER_SCANNED_FIELD: true,
        "developer": "",
        "title": detected_title,
        "titleCandidates": metadata.get("titleCandidates", PackedStringArray()),
        "metadataSignals": metadata.get("signals", PackedStringArray()),
        "engine": detected_engine,
        "launchFile": metadata.get("launchFile", ""),
    }

func _game_runtime_root(path: String) -> String:
    var resolved := _resolve_game_path(path)
    if FileAccess.file_exists(resolved):
        return resolved.get_base_dir()
    return resolved

func _game_runtime_kind(path: String) -> String:
    var root := _game_runtime_root(path)
    var runtime_kind := String(
        GameMetadata.inspect(root).get("engine", RUNTIME_KIRIKIRI)
    )
    if runtime_kind != RUNTIME_KIRIKIRI:
        return runtime_kind
    if player != null and player.has_method("probe_runtime") \
            and int(player.probe_runtime(RUNTIME_MINORI, root)) > 0:
        return RUNTIME_MINORI
    return runtime_kind

func _backfill_game_metadata(games: Array[Dictionary]) -> bool:
    var changed := false
    for game in games:
        var path := String(game.get("path", ""))
        if path.is_empty() or builtin_demo.is_game(game):
            continue
        var metadata := GameMetadata.inspect(path)
        var engine := String(metadata.get("engine", RUNTIME_KIRIKIRI))
        if String(game.get("engine", "")).is_empty() or String(game.get("engine", "")) == RUNTIME_KIRIKIRI:
            if String(game.get("engine", "")) != engine:
                game["engine"] = engine
                changed = true
        if String(game.get("title", "")).is_empty():
            var title := String(metadata.get("title", ""))
            if not title.is_empty():
                game["title"] = title
                if String(game.get("name", "")).is_empty():
                    game["name"] = title
                changed = true
        for key in ["titleCandidates", "metadataSignals", "launchFile"]:
            var value = metadata.get(key, null)
            if value != null and JSON.stringify(game.get(key, null)) != JSON.stringify(value):
                game[key] = value
                changed = true
    return changed

func _backfill_default_game_covers(games: Array[Dictionary]) -> bool:
    var changed := false
    for index in range(games.size()):
        var game := games[index]
        if builtin_demo.is_game(game):
            continue
        var game_path := String(game.get("path", ""))
        var stored_cover_path := String(game.get("coverPath", ""))
        var resolved_cover_path := _resolve_cover_path(game)
        var next_cover_path := stored_cover_path
        if not resolved_cover_path.is_empty() \
                and FileAccess.file_exists(resolved_cover_path):
            next_cover_path = _portable_cover_path(game_path, resolved_cover_path)
        else:
            var discovered_path := _discover_default_cover_path(game_path)
            next_cover_path = _portable_cover_path(game_path, discovered_path)
        if next_cover_path != stored_cover_path:
            game["coverPath"] = next_cover_path
            changed = true
        if not bool(game.get(GAME_AUTO_COVER_SCANNED_FIELD, false)):
            game[GAME_AUTO_COVER_SCANNED_FIELD] = true
            changed = true
        games[index] = game
    return changed

func _sync_cover_index(games: Array[Dictionary]) -> void:
    var index := CoverIndex.load_index()
    var changed := false
    var pending: Array[Dictionary] = []
    for game in games:
        if builtin_demo.is_game(game):
            continue
        var key := CoverIndex.key_for(game)
        if key.is_empty() or not CoverIndex.needs_recognition(index, key):
            continue
        var cover_path := _resolve_cover_path(game)
        var stored_path := _portable_cover_path(String(game.get("path", "")), cover_path) if not cover_path.is_empty() and FileAccess.file_exists(cover_path) else ""
        index[key] = CoverIndex.record(stored_path, false)
        changed = true
        pending.append(game)
    if changed:
        CoverIndex.save_index(index)
    for game in pending:
        _start_vndb_cover_lookup(game, true)

func _portable_cover_path(game_path: String, cover_path: String) -> String:
    var value := cover_path.strip_edges()
    if value.is_empty() or value.begins_with(GAME_COVER_PATH_PREFIX):
        return value
    var absolute_cover := ProjectSettings.globalize_path(value).simplify_path()
    var game_root := _game_runtime_root(game_path).simplify_path().trim_suffix("/")
    if not game_root.is_empty() and absolute_cover.begins_with(game_root + "/"):
        return GAME_COVER_PATH_PREFIX + absolute_cover.substr(game_root.length() + 1)
    var user_root := ProjectSettings.globalize_path("user://").simplify_path().trim_suffix("/")
    if absolute_cover.begins_with(user_root + "/"):
        return "user://" + absolute_cover.substr(user_root.length() + 1)
    return absolute_cover

func _resolve_cover_path(game: Dictionary) -> String:
    var stored_path := String(game.get("coverPath", "")).strip_edges()
    if stored_path.is_empty():
        return ""
    if stored_path.begins_with(GAME_COVER_PATH_PREFIX):
        var relative_path := stored_path.substr(GAME_COVER_PATH_PREFIX.length())
        return _game_runtime_root(String(game.get("path", ""))).path_join(relative_path).simplify_path()
    var resolved_path := ProjectSettings.globalize_path(stored_path).simplify_path()
    if FileAccess.file_exists(resolved_path):
        return resolved_path

    # Absolute paths stored by an older iOS install contain the previous data
    # container UUID. Rebase the stable Documents-relative suffix onto the
    # current sandbox before falling back to automatic discovery.
    var documents_marker := "/Documents/"
    var marker_index := resolved_path.find(documents_marker)
    if marker_index >= 0:
        var documents_relative := resolved_path.substr(
            marker_index + documents_marker.length()
        )
        var migrated_path := ProjectSettings.globalize_path("user://").path_join(
            documents_relative
        ).simplify_path()
        if FileAccess.file_exists(migrated_path):
            return migrated_path
    return resolved_path

func _discover_default_cover_path(game_path: String) -> String:
    var directory := game_path
    if not DirAccess.dir_exists_absolute(directory):
        directory = game_path.get_base_dir()
    var dir := DirAccess.open(directory)
    if dir == null:
        return ""

    var best_path := ""
    var best_score := DEFAULT_COVER_BASENAMES.size() * 100 + COVER_IMAGE_EXTENSIONS.size()
    for file_name in dir.get_files():
        var extension := file_name.get_extension().to_lower()
        var extension_priority := COVER_IMAGE_EXTENSIONS.find(extension)
        if extension_priority < 0:
            continue
        var base_name := file_name.get_basename().strip_edges().to_lower()
        var name_priority := DEFAULT_COVER_BASENAMES.find(base_name)
        if name_priority < 0:
            continue
        var score := name_priority * 100 + extension_priority
        var candidate_path := directory.path_join(file_name)
        if score < best_score or (score == best_score and candidate_path < best_path):
            best_score = score
            best_path = candidate_path
    return best_path

func _game_display_title(game: Dictionary) -> String:
    var title := String(game.get("title", ""))
    if not title.is_empty():
        return title
    return String(game.get("name", String(game.get("path", "")).get_file()))

func _game_type_label(value: String) -> String:
    var lower := value.to_lower()
    if lower == "archive":
        return _t("game.type_archive")
    if lower == "directory":
        return _t("game.type_directory")
    return value

func _format_play_duration(seconds: int) -> String:
    if seconds < 60:
        return "0m"
    var minutes := seconds / 60
    if minutes < 60:
        return "%dm" % minutes
    var hours := minutes / 60
    var mins := minutes % 60
    if mins == 0:
        return "%dh" % hours
    return "%dh %dm" % [hours, mins]

func _last_played_label(game: Dictionary) -> String:
    var last_played := int(game.get("lastPlayed", 0))
    if last_played <= 0:
        return _t("game.never_played")
    var elapsed: int = max(0, int(Time.get_unix_time_from_system()) - last_played)
    if elapsed < 86400:
        return _t("game.today")
    return _t("game.days_ago", [max(1, elapsed / 86400)])

func _game_subtitle(game: Dictionary) -> String:
    var parts: PackedStringArray = []
    if builtin_demo.is_game(game):
        parts.append(_t("game.builtin_demo"))
    if int(game.get("lastPlayed", 0)) > 0:
        parts.append(_last_played_label(game))
    var duration := int(game.get("playDurationSeconds", 0))
    if duration >= 60:
        parts.append(_t("game.played_duration", [_format_play_duration(duration)]))
    return " · ".join(parts) if not parts.is_empty() else _t("game.never_played")

func _mark_game_played(path: String) -> Dictionary:
    var games := _load_game_list()
    var updated := {}
    for i in range(games.size()):
        if String(games[i].get("path", "")) == path:
            games[i]["lastPlayed"] = int(Time.get_unix_time_from_system())
            updated = games[i]
            break
    _save_game_list(games)
    return updated

func _add_play_duration(path: String, seconds: int) -> void:
    if seconds <= 0:
        return
    var games := _load_game_list()
    for i in range(games.size()):
        if String(games[i].get("path", "")) == path:
            games[i]["playDurationSeconds"] = int(games[i].get("playDurationSeconds", 0)) + min(seconds, 86400)
            break
    _save_game_list(games)

func _update_game(path: String, values: Dictionary) -> void:
    var games := _load_game_list()
    for i in range(games.size()):
        if String(games[i].get("path", "")) == path:
            for key in values.keys():
                games[i][key] = values[key]
            selected_game = games[i]
            break
    _save_game_list(games)
    _refresh_games()

func _remove_game(path: String) -> void:
    var deleting_builtin := builtin_demo.is_path(path)
    var builtin_delete_result: Error = OK
    if deleting_builtin:
        builtin_delete_result = builtin_demo.remove_install()
        if not builtin_demo.is_removed():
            _show_message(_t("message.builtin_delete_failed", [error_string(builtin_delete_result)]))
            return
    var games := _load_game_list()
    var next: Array[Dictionary] = []
    for game in games:
        if String(game.get("path", "")) != path:
            next.append(game)
    _save_game_list(next)
    if deleting_builtin:
        _sync_web_user_fs("builtin_demo_deleted")
    selected_game = {}
    _show_home()
    if deleting_builtin and builtin_delete_result != OK:
        _show_message(_t("message.builtin_delete_failed", [error_string(builtin_delete_result)]))

func _game_card(game: Dictionary) -> Button:
    var button := Button.new()
    button.custom_minimum_size = _home_card_minimum_size(home_compact_layout)
    button.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    button.clip_contents = true
    button.focus_mode = Control.FOCUS_ALL
    button.text = ""
    _style_home_card_button(button, home_compact_layout)
    button.set_meta("game_path", String(game.get("path", "")))
    button.pressed.connect(func(): _open_game_detail_with_iap(game, button))

    if home_compact_layout:
        _build_compact_game_card(button, game)
        ui_motion.bind_lift(button)
    else:
        var hover_affordance := _build_desktop_game_card(button, game)
        ui_motion.bind_lift(button, hover_affordance, 0.42, 1.0)
    return button

func _home_card_minimum_size(compact: bool) -> Vector2:
    return Vector2(HOME_TILE_MIN_WIDTH, HOME_ROW_HEIGHT if compact else HOME_TILE_HEIGHT)

func _style_home_card_button(button: Button, compact: bool) -> void:
    if compact:
        button.add_theme_stylebox_override("normal", ui_tokens.panel(ui_tokens.surface, 8))
        button.add_theme_stylebox_override("hover", ui_tokens.panel(ui_tokens.surface_raised, 8))
        button.add_theme_stylebox_override("pressed", ui_tokens.panel(ui_tokens.surface_hover, 8))
    else:
        button.add_theme_stylebox_override("normal", ui_tokens.card_style())
        button.add_theme_stylebox_override("hover", ui_tokens.card_style(true))
        button.add_theme_stylebox_override("pressed", ui_tokens.card_style(true, true))
    button.add_theme_stylebox_override("focus", ui_tokens.focus_style())

func _build_desktop_game_card(button: Button, game: Dictionary) -> CanvasItem:
    var content_margin := MarginContainer.new()
    content_margin.mouse_filter = Control.MOUSE_FILTER_IGNORE
    content_margin.set_anchors_preset(Control.PRESET_FULL_RECT)
    content_margin.add_theme_constant_override("margin_left", 8)
    content_margin.add_theme_constant_override("margin_top", 8)
    content_margin.add_theme_constant_override("margin_right", 10)
    content_margin.add_theme_constant_override("margin_bottom", 8)
    button.add_child(content_margin)

    var frame := HBoxContainer.new()
    frame.mouse_filter = Control.MOUSE_FILTER_IGNORE
    frame.clip_contents = true
    frame.add_theme_constant_override("separation", 12)
    content_margin.add_child(frame)

    var cover_host := PanelContainer.new()
    cover_host.mouse_filter = Control.MOUSE_FILTER_IGNORE
    cover_host.custom_minimum_size = Vector2(HOME_TILE_COVER_WIDTH, HOME_TILE_HEIGHT - 16.0)
    cover_host.size_flags_vertical = Control.SIZE_EXPAND_FILL
    cover_host.clip_contents = true
    cover_host.add_theme_stylebox_override("panel", ui_tokens.panel(ui_tokens.surface_raised, 8))
    frame.add_child(cover_host)
    button.set_meta("hero_cover", cover_host)
    _populate_game_card_cover(
        cover_host,
        game,
        Vector2i(int(HOME_TILE_COVER_WIDTH), int(HOME_TILE_HEIGHT - 16.0))
    )

    var metadata := PanelContainer.new()
    metadata.mouse_filter = Control.MOUSE_FILTER_IGNORE
    metadata.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    metadata.size_flags_vertical = Control.SIZE_EXPAND_FILL
    metadata.add_theme_stylebox_override("panel", ui_tokens.panel(Color.TRANSPARENT, 0))
    frame.add_child(metadata)
    _populate_game_card_metadata(metadata, game, false)

    var affordance := CenterContainer.new()
    affordance.mouse_filter = Control.MOUSE_FILTER_IGNORE
    affordance.custom_minimum_size = Vector2(22, 0)
    affordance.add_child(_icon_rect(ICON_CHEVRON_RIGHT, Vector2(15, 15), ui_tokens.text_secondary))
    frame.add_child(affordance)
    return affordance

func _build_compact_game_card(button: Button, game: Dictionary) -> void:
    var frame := HBoxContainer.new()
    frame.mouse_filter = Control.MOUSE_FILTER_IGNORE
    frame.clip_contents = true
    frame.set_anchors_preset(Control.PRESET_FULL_RECT)
    frame.add_theme_constant_override("separation", 0)
    button.add_child(frame)

    var cover_host := Control.new()
    cover_host.mouse_filter = Control.MOUSE_FILTER_IGNORE
    cover_host.custom_minimum_size = Vector2(HOME_ROW_COVER_WIDTH, HOME_ROW_HEIGHT)
    cover_host.size_flags_vertical = Control.SIZE_EXPAND_FILL
    frame.add_child(cover_host)
    button.set_meta("hero_cover", cover_host)
    _populate_game_card_cover(
        cover_host,
        game,
        Vector2i(int(HOME_ROW_COVER_WIDTH), int(HOME_ROW_HEIGHT))
    )

    var metadata := PanelContainer.new()
    metadata.mouse_filter = Control.MOUSE_FILTER_IGNORE
    metadata.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    metadata.size_flags_vertical = Control.SIZE_EXPAND_FILL
    metadata.add_theme_stylebox_override("panel", ui_tokens.panel(ui_tokens.surface, 0))
    frame.add_child(metadata)
    _populate_game_card_metadata(metadata, game, true)

    var chevron_host := CenterContainer.new()
    chevron_host.mouse_filter = Control.MOUSE_FILTER_IGNORE
    chevron_host.custom_minimum_size = Vector2(42, HOME_ROW_HEIGHT)
    chevron_host.add_child(_icon_rect(ICON_CHEVRON_RIGHT, Vector2(17, 17), ui_tokens.text_tertiary))
    frame.add_child(chevron_host)

func _populate_game_card_cover(cover_host: Control, game: Dictionary, target_size: Vector2i) -> void:
    var cover_texture := _load_cover_texture(game, target_size, 0)
    if cover_texture != null:
        var cover := TextureRect.new()
        cover.texture = cover_texture
        cover.mouse_filter = Control.MOUSE_FILTER_IGNORE
        cover.set_anchors_preset(Control.PRESET_FULL_RECT)
        cover.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
        cover.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_COVERED
        cover_host.add_child(cover)
    else:
        var placeholder := PanelContainer.new()
        placeholder.mouse_filter = Control.MOUSE_FILTER_IGNORE
        placeholder.set_anchors_preset(Control.PRESET_FULL_RECT)
        placeholder.add_theme_stylebox_override("panel", ui_tokens.panel(ui_tokens.surface_raised, 8 if not home_compact_layout else 0))
        cover_host.add_child(placeholder)
        var icon_size := Vector2(34, 34) if home_compact_layout else Vector2(28, 28)
        var icon := _centered_icon(ICON_GAMEPAD, icon_size, ui_tokens.accent)
        icon.set_anchors_preset(Control.PRESET_FULL_RECT)
        placeholder.add_child(icon)

func _populate_game_card_metadata(metadata: PanelContainer, game: Dictionary, compact: bool) -> void:
    var text_margin := MarginContainer.new()
    text_margin.mouse_filter = Control.MOUSE_FILTER_IGNORE
    text_margin.add_theme_constant_override("margin_left", 14 if compact else 2)
    text_margin.add_theme_constant_override("margin_top", 13 if compact else 7)
    text_margin.add_theme_constant_override("margin_right", 8 if compact else 2)
    text_margin.add_theme_constant_override("margin_bottom", 10 if compact else 7)
    metadata.add_child(text_margin)

    var labels := VBoxContainer.new()
    labels.mouse_filter = Control.MOUSE_FILTER_IGNORE
    labels.alignment = BoxContainer.ALIGNMENT_CENTER if compact else BoxContainer.ALIGNMENT_BEGIN
    labels.add_theme_constant_override("separation", 4)
    text_margin.add_child(labels)

    if not compact:
        var kicker := Label.new()
        kicker.text = _game_type_label(String(game.get("type", "Directory"))).to_upper()
        kicker.mouse_filter = Control.MOUSE_FILTER_IGNORE
        kicker.add_theme_font_size_override("font_size", 9)
        kicker.add_theme_color_override("font_color", ui_tokens.text_tertiary)
        labels.add_child(kicker)

    var title := Label.new()
    title.text = _game_display_title(game)
    title.mouse_filter = Control.MOUSE_FILTER_IGNORE
    title.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
    title.max_lines_visible = 2
    title.text_overrun_behavior = TextServer.OVERRUN_TRIM_ELLIPSIS
    title.custom_minimum_size = Vector2(0, 42 if compact else 34)
    title.add_theme_font_override("font", _game_title_font())
    title.add_theme_font_size_override("font_size", 16 if compact else 15)
    title.add_theme_color_override("font_color", ui_tokens.text_primary)
    labels.add_child(title)

    if not compact:
        var spacer := Control.new()
        spacer.mouse_filter = Control.MOUSE_FILTER_IGNORE
        spacer.size_flags_vertical = Control.SIZE_EXPAND_FILL
        labels.add_child(spacer)

    var sub := Label.new()
    sub.text = "%s  /  %s" % [_game_type_label(String(game.get("type", "Directory"))), _game_subtitle(game)] if compact else _game_subtitle(game)
    sub.mouse_filter = Control.MOUSE_FILTER_IGNORE
    sub.clip_text = true
    sub.add_theme_font_size_override("font_size", 12 if compact else 11)
    sub.add_theme_color_override("font_color", ui_tokens.text_secondary)
    labels.add_child(sub)

func _animate_hero_forward(body: Control) -> void:
    if not is_instance_valid(detail_hero_cover) or hero_source_rect.size == Vector2.ZERO:
        ui_motion.reveal(body)
        return
    var destination := detail_hero_cover.get_global_rect()
    if destination.size == Vector2.ZERO:
        ui_motion.reveal(body)
        return
    _finish_hero_overlay()
    detail_hero_cover.modulate.a = 0.0
    hero_hidden_target = detail_hero_cover
    ui_motion.reveal(body, 0.02)
    var overlay := _create_hero_overlay(hero_source_rect, hero_source_texture)
    var transition_id := hero_transition_id
    var overlay_ref: WeakRef = weakref(overlay)
    ui_motion.hero_rect(overlay, _hero_local_rect(destination), func(): _complete_hero_overlay_ref(overlay_ref, transition_id, false))
    _arm_hero_watchdog(overlay, transition_id, false)

func _animate_hero_back(source_rect: Rect2) -> void:
    await get_tree().process_frame
    var target_card := _find_game_card(hero_source_path)
    if target_card == null or not is_instance_valid(target_card) or source_rect.size == Vector2.ZERO:
        _clear_hero_state()
        ui_motion.settle_route(detail_view, false)
        ui_motion.route_in(home_view, -1.0)
        return
    var target_cover: Control = target_card.get_meta("hero_cover", null)
    if target_cover == null or not is_instance_valid(target_cover):
        _clear_hero_state()
        ui_motion.settle_route(detail_view, false)
        ui_motion.route_in(home_view, -1.0)
        return
    _finish_hero_overlay()
    target_cover.modulate.a = 0.0
    hero_hidden_target = target_cover
    var overlay := _create_hero_overlay(source_rect, hero_source_texture)
    var transition_id := hero_transition_id
    var overlay_ref: WeakRef = weakref(overlay)
    ui_motion.hero_rect(overlay, _hero_local_rect(target_cover.get_global_rect()), func(): _complete_hero_overlay_ref(overlay_ref, transition_id, true))
    _arm_hero_watchdog(overlay, transition_id, true)

func _create_hero_overlay(global_rect: Rect2, texture: Texture2D) -> PanelContainer:
    hero_overlay = PanelContainer.new()
    hero_overlay.mouse_filter = Control.MOUSE_FILTER_IGNORE
    hero_overlay.clip_contents = true
    hero_overlay.position = _hero_local_rect(global_rect).position
    hero_overlay.size = global_rect.size
    var style := ui_tokens.detail_outline_style()
    hero_overlay.add_theme_stylebox_override("panel", style)
    if texture != null:
        var image := TextureRect.new()
        image.texture = texture
        image.mouse_filter = Control.MOUSE_FILTER_IGNORE
        image.set_anchors_preset(Control.PRESET_FULL_RECT)
        image.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
        image.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_COVERED
        hero_overlay.add_child(image)
    else:
        hero_overlay.add_child(_centered_icon(ICON_GAMEPAD, Vector2(48, 48), ui_tokens.accent))
    shell_content.add_child(hero_overlay)
    hero_overlay.move_to_front()
    return hero_overlay

func _hero_local_rect(global_rect: Rect2) -> Rect2:
    return Rect2(global_rect.position - shell_content.global_position, global_rect.size)

func _find_game_card(path: String) -> Button:
    for child in game_list.get_children():
        if child is Button and not child.is_queued_for_deletion() and String(child.get_meta("game_path", "")) == path:
            return child as Button
    return null

func _finish_hero_overlay() -> void:
    hero_transition_id += 1
    if is_instance_valid(hero_hidden_target):
        hero_hidden_target.modulate.a = 1.0
    hero_hidden_target = null
    if is_instance_valid(hero_overlay):
        hero_overlay.visible = false
        hero_overlay.queue_free()
    hero_overlay = null

func _complete_hero_overlay(expected_overlay: Control, transition_id: int, clear_state: bool) -> void:
    if transition_id != hero_transition_id:
        if is_instance_valid(expected_overlay):
            expected_overlay.visible = false
            expected_overlay.queue_free()
        return
    if is_instance_valid(hero_hidden_target):
        hero_hidden_target.modulate.a = 1.0
    hero_hidden_target = null
    if is_instance_valid(expected_overlay):
        expected_overlay.visible = false
        expected_overlay.queue_free()
    if hero_overlay == expected_overlay:
        hero_overlay = null
    hero_transition_id += 1
    if clear_state:
        _clear_hero_state()

func _complete_hero_overlay_ref(expected_overlay_ref: WeakRef, transition_id: int, clear_state: bool) -> void:
    var expected_overlay: Variant = expected_overlay_ref.get_ref()
    _complete_hero_overlay(expected_overlay, transition_id, clear_state)

func _arm_hero_watchdog(expected_overlay: Control, transition_id: int, clear_state: bool) -> void:
    var expected_overlay_ref: WeakRef = weakref(expected_overlay)
    get_tree().create_timer(0.55).timeout.connect(
        func(): _complete_hero_overlay_ref(expected_overlay_ref, transition_id, clear_state),
        CONNECT_ONE_SHOT
    )

func _clear_hero_state() -> void:
    hero_source_rect = Rect2()
    hero_source_path = ""
    hero_source_texture = null
    detail_hero_cover = null

func _load_cover_texture(game: Dictionary, target_size: Vector2i = Vector2i.ZERO, radius: int = 0) -> Texture2D:
    var cover_path := _resolve_cover_path(game)
    if cover_path.is_empty() or not FileAccess.file_exists(cover_path):
        return null
    var modified := FileAccess.get_modified_time(cover_path)
    var cache_key := "%s|%d|%dx%d|%d" % [
        cover_path,
        modified,
        target_size.x,
        target_size.y,
        radius,
    ]
    if cover_texture_cache.has(cache_key):
        return cover_texture_cache.get(cache_key)
    var image := Image.new()
    if image.load(cover_path) != OK:
        return null
    if target_size.x > 0 and target_size.y > 0:
        image = _cover_image_for_card(image, target_size, radius)
    else:
        image.convert(Image.FORMAT_RGBA8)
    var texture := ImageTexture.create_from_image(image)
    cover_texture_cache[cache_key] = texture
    return texture

func _cover_image_for_card(image: Image, target_size: Vector2i, radius: int) -> Image:
    image.convert(Image.FORMAT_RGBA8)
    var source_size := Vector2(float(image.get_width()), float(image.get_height()))
    var target := Vector2(float(target_size.x), float(target_size.y))
    var scale := maxf(target.x / source_size.x, target.y / source_size.y)
    var scaled_size := Vector2i(
        maxi(target_size.x, int(ceil(source_size.x * scale))),
        maxi(target_size.y, int(ceil(source_size.y * scale)))
    )
    image.resize(scaled_size.x, scaled_size.y, Image.INTERPOLATE_LANCZOS)
    var crop_x := maxi(0, int((scaled_size.x - target_size.x) / 2))
    var crop_y := maxi(0, int((scaled_size.y - target_size.y) / 2))
    var cropped := image.get_region(Rect2i(crop_x, crop_y, target_size.x, target_size.y))
    cropped.convert(Image.FORMAT_RGBA8)
    _apply_rounded_image_corners(cropped, radius)
    return cropped

func _apply_rounded_image_corners(image: Image, radius: int) -> void:
    var w := image.get_width()
    var h := image.get_height()
    var r := mini(radius, mini(w, h) / 2)
    if r <= 0:
        return
    var rf := float(r)
    for y in range(r):
        for x in range(r):
            _mask_corner_pixel(image, x, y, Vector2(rf, rf), rf)
            _mask_corner_pixel(image, w - 1 - x, y, Vector2(float(w) - rf, rf), rf)
            _mask_corner_pixel(image, x, h - 1 - y, Vector2(rf, float(h) - rf), rf)
            _mask_corner_pixel(image, w - 1 - x, h - 1 - y, Vector2(float(w) - rf, float(h) - rf), rf)

func _mask_corner_pixel(image: Image, x: int, y: int, center: Vector2, radius: float) -> void:
    var coverage := clampf(radius + 0.5 - Vector2(float(x) + 0.5, float(y) + 0.5).distance_to(center), 0.0, 1.0)
    if coverage >= 1.0:
        return
    var color := image.get_pixel(x, y)
    color.a *= coverage
    image.set_pixel(x, y, color)

func _start_selected_game() -> void:
    _android_input_debug_log("_start_selected_game selected=%s" % str(selected_game))
    if not _require_legal_documents_for_media():
        return
    if _begin_iap_checked_launch("game", selected_game):
        return
    _start_selected_game_after_iap()

func _begin_launch_transition() -> void:
    if shell_root == null or not is_instance_valid(shell_root):
        return
    if launch_transition_tween != null and launch_transition_tween.is_valid():
        launch_transition_tween.kill()
    # The loading overlay already owns the transition. Scaling the complete
    # shell after the game background turns black exposes a mixed frame made of
    # the sidebar, detail page, game viewport, and diagnostics overlay.
    _finish_launch_transition()

func _finish_launch_transition() -> void:
    launch_transition_tween = null
    if shell_root == null or not is_instance_valid(shell_root):
        return
    shell_root.visible = false
    shell_root.modulate.a = 1.0
    shell_root.scale = Vector2.ONE
    shell_root.pivot_offset = Vector2.ZERO

func _start_selected_game_after_iap() -> void:
    if player == null:
        return
    var library_path := String(selected_game.get("path", "")).strip_edges()
    if library_path.is_empty():
        return
    var selected_runtime_kind := _game_runtime_kind(library_path)
    # Development artifacts and Android releases do not use the Apple-only
    # beta entitlement flow.
    if not _beta_access_enforcement_enabled():
        if (
            selected_runtime_kind == RUNTIME_KIRIKIRI
            and current_player_runtime_kind != RUNTIME_KIRIKIRI
            and not _switch_runtime_player(RUNTIME_KIRIKIRI)
        ):
            return
        _start_selected_game_after_entitlements()
        return

    # StoreKit lives on the KiriKiri host. Return to that host before probing
    # a provider-backed beta runtime after another runtime exits.
    if (
        current_player_runtime_kind != RUNTIME_KIRIKIRI
        and not _switch_runtime_player(RUNTIME_KIRIKIRI)
    ):
        return
    var requires_beta_access := (
        _runtime_requires_beta_access(selected_runtime_kind)
        or _selected_game_uses_wa2()
    )
    if not requires_beta_access:
        _start_selected_game_after_entitlements()
        return

    iap_pending_beta_game = selected_game.duplicate(true)
    if not _iap_supported_platform() or not player.has_method("iap_refresh_entitlement"):
        _deny_runtime_beta_launch()
        return
    iap_pending_beta_check_id = int(player.iap_refresh_entitlement(
        IAP_COFFEE_PRODUCT_ID
    ))
    if iap_pending_beta_check_id <= 0:
        _deny_runtime_beta_launch()

func _runtime_requires_beta_access(runtime_kind: String) -> bool:
    # ONS and Artemis are generally available. Minori remains gated by an
    # active coffee entitlement in iOS and macOS distribution builds.
    # Provider-backed runtimes such as WA2 are checked separately.
    return runtime_kind == RUNTIME_MINORI

func _beta_access_enforcement_enabled(platform_name: String = "") -> bool:
    var effective_platform := platform_name if not platform_name.is_empty() else OS.get_name()
    return effective_platform in ["iOS", "macOS"] and not OS.is_debug_build()

func _selected_game_uses_wa2() -> bool:
    # WHITE ALBUM2 runs on the KiriKiri host through the compiled Wa2
    # provider and retains the provider beta-access policy.
    if player == null or not player.has_method("probe_runtime"):
        return false
    var library_path := String(selected_game.get("path", "")).strip_edges()
    if library_path.is_empty():
        return false
    return int(player.probe_runtime("wa2", library_path)) > 0

func _complete_runtime_beta_check() -> void:
    iap_pending_beta_check_id = 0
    if iap_pending_beta_game.is_empty():
        return
    var pending_game: Dictionary = iap_pending_beta_game.duplicate(true)
    iap_pending_beta_game.clear()
    if not bool(iap_coffee_state.get("entitled", false)) and not _secret_coffee_active():
        _deny_runtime_beta_launch()
        return
    selected_game = pending_game
    _start_selected_game_after_entitlements()

func _deny_runtime_beta_launch() -> void:
    iap_pending_beta_check_id = 0
    iap_pending_beta_game.clear()
    _show_system_alert(
        _t("iap.beta_runtime_unavailable"),
        _t("alert.warning_title")
    )

func _start_selected_game_after_entitlements() -> void:
    var library_path := String(selected_game.get("path", ""))
    if library_path.is_empty():
        return
    if not _ensure_android_storage_permission_for_path(library_path):
        return
    if not _mount_web_game(selected_game):
        return
    active_runtime_kind = _game_runtime_kind(library_path)
    if not _switch_runtime_player(active_runtime_kind):
        return
    var launch_uses_directory := GameLaunchEntry.runtime_uses_directory(
        active_runtime_kind
    )
    var raw_launch_file := String(selected_game.get(GameLaunchEntry.FIELD, "")).strip_edges()
    if (
        not launch_uses_directory
        and not raw_launch_file.is_empty()
        and not GameLaunchEntry.is_supported_file(raw_launch_file)
    ):
        _show_system_alert(
            _t("message.launch_file_unsupported"),
            _t("alert.warning_title")
        )
        return
    var relative_launch_file := GameLaunchEntry.configured_relative_path(selected_game)
    if (
        not launch_uses_directory
        and not raw_launch_file.is_empty()
        and relative_launch_file.is_empty()
    ):
        _show_system_alert(
            _t("message.launch_file_outside_game"),
            _t("alert.warning_title")
        )
        return
    var launch_path := GameLaunchEntry.resolve_for_runtime(
        selected_game,
        active_runtime_kind
    )
    if (
        not launch_uses_directory
        and not relative_launch_file.is_empty()
        and not FileAccess.file_exists(launch_path)
    ):
        _show_system_alert(
            _t("message.launch_file_missing", [relative_launch_file]),
            _t("alert.warning_title")
        )
        return
    _set_game_runtime_orientation(true)
    var played_game := _mark_game_played(library_path)
    if not played_game.is_empty():
        selected_game = played_game
    active_game_path = library_path
    active_game_started_msec = Time.get_ticks_msec()
    game_path.text = launch_path
    _write_probe_marker("library_launch root=%s entry=%s" % [library_path, launch_path])
    _set_game_background(true)
    viewport.visible = true
    viewport.move_to_front()
    game_view.visible = true
    # Publish a fully composed loading frame before the asynchronous runtime can
    # expose its black/empty first surface. Diagnostics stay hidden until the
    # first successful game frame replaces this overlay.
    _set_perf_visible(false)
    _set_translation_loading_notice(_translation_model_configured())
    _show_loading_overlay(true)
    restart_notice.visible = true
    _on_open_game()
    if game_running:
        _begin_launch_transition()
    else:
        _hide_loading_overlay()
        _set_game_runtime_orientation(false)
        _set_game_background(false)
        viewport.visible = false
        game_view.visible = false

func _finalize_active_game_session() -> void:
    if active_game_path.is_empty() or active_game_started_msec <= 0:
        return
    var elapsed := int((Time.get_ticks_msec() - active_game_started_msec) / 1000)
    _add_play_duration(active_game_path, elapsed)
    active_game_path = ""
    active_game_started_msec = 0

func _is_runtime_exit_error(message: String) -> bool:
    var lower := message.to_lower()
    return lower.contains("runtime requested termination") or lower.contains("runtime has been terminated")

func _return_to_library_after_runtime_exit() -> void:
    if runtime_exit_cleanup_pending:
        return
    runtime_exit_cleanup_pending = true
    _deactivate_game_text_input()
    _clear_game_input_capture()
    _finalize_active_game_session()
    game_running = false
    _sync_debug_console_state()
    app_lifecycle_paused = false
    cached_startup_state = STARTUP_IDLE
    startup_poll_accum = 0.0
    tick_trace_active_serial = 0
    if loading_panel != null:
        _hide_loading_overlay()
    if restart_notice != null:
        restart_notice.text = ""
        restart_notice.visible = false
    _set_perf_visible(false)
    if diagnostic_session != null:
        diagnostic_session.set_game_active(false)
        diagnostic_session.finish()
    if viewport != null:
        viewport.texture = null
        viewport.visible = false
    if game_view != null:
        game_view.visible = false
    if player != null:
        player.release_frame_texture()
        player.destroy_engine()
    last_texture_size = Vector2i.ZERO
    _set_game_runtime_orientation(false)
    _set_game_background(false)
    if shell_root != null:
        shell_root.visible = true
    Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
    _show_home()
    _fit_full_rects()
    runtime_exit_cleanup_pending = false

func _ready() -> void:
    var vndb_resolver := VNDBCoverResolver.new()
    vndb_resolver.name = "VNDBCoverResolver"
    add_child(vndb_resolver)
    vndb_resolver.resolved.connect(_on_vndb_cover_resolved)
    cli_probe_script = _detect_cli_probe_script()
    _apply_ui_font()
    DisplayServer.window_set_flag(DisplayServer.WINDOW_FLAG_TRANSPARENT, false)
    _apply_initial_window_size()
    _apply_global_dpi_scale()
    get_viewport().transparent_bg = false
    RenderingServer.set_default_clear_color(color_bg)
    var ios_diagnostics_enabled := OS.get_name() == "iOS" and _runtime_flag("AETHERKIRI_IOS_DIAGNOSTICS")
    var perf_interval_env := OS.get_environment("AETHERKIRI_PERF_LOG_INTERVAL")
    if not perf_interval_env.is_empty():
        perf_log_interval = maxf(0.05, perf_interval_env.to_float())
    elif ios_diagnostics_enabled:
        perf_log_interval = 0.5
    var frame_spike_env := OS.get_environment("AETHERKIRI_FRAME_SPIKE_MS")
    if not frame_spike_env.is_empty():
        frame_spike_ms = maxf(0.0, frame_spike_env.to_float())
    elif ios_diagnostics_enabled:
        frame_spike_ms = 20.0
    verbose_render_log = OS.get_environment("AETHERKIRI_VERBOSE_RENDER_LOG") == "1"
    device_probe_enabled = _runtime_flag("AETHERKIRI_DEVICE_PROBE")
    render_surface_mode = _default_render_surface_mode()
    render_surface_base_size = _env_vector2i("AETHERKIRI_GAME_SURFACE_SIZE", RENDER_SURFACE_SIZE)
    render_surface_max_size = _env_vector2i("AETHERKIRI_SURFACE_MAX_SIZE", RENDER_SURFACE_MAX_SIZE)
    follow_texture_surface_size = _runtime_flag("AETHERKIRI_FOLLOW_TEXTURE_SURFACE")
    frame_probe_enabled = _runtime_flag("AETHERKIRI_FRAME_PROBE")
    frame_probe_interval = maxf(0.05, _runtime_float("AETHERKIRI_FRAME_PROBE_INTERVAL", 1.0))
    black_frame_guard_enabled = _runtime_flag("AETHERKIRI_BLACK_FRAME_GUARD")
    input_trace_enabled = (
        _runtime_flag("AETHERKIRI_INPUT_TRACE")
        or ios_diagnostics_enabled
        # A CLI probe should measure the game, not force the very verbose
        # LayerIntf input tracer on every click.  The tracer walks and logs a
        # large TJS object graph from onMouseDown/onMouseUp and can itself
        # create 45-70ms host frames.  Keep it opt-in for targeted input
        # investigations while probes retain their normal input delivery.
    )
    device_probe_enabled = device_probe_enabled or frame_probe_enabled
    device_probe_enabled = device_probe_enabled or input_trace_enabled
    var native_auto_start_enabled := _native_auto_start_enabled()
    device_probe_enabled = device_probe_enabled or (
        native_auto_start_enabled and _runtime_flag("AETHERKIRI_AUTO_OPEN")
    )
    device_probe_enabled = device_probe_enabled or (
        native_auto_start_enabled and not _runtime_string("AETHERKIRI_AUTO_START_GAME").is_empty()
    )
    startup_click_stream_enabled = _runtime_flag("AETHERKIRI_STARTUP_CLICK_STREAM")
    device_probe_enabled = device_probe_enabled or startup_click_stream_enabled
    device_probe_enabled = device_probe_enabled or not OS.get_environment("AETHERKIRI_CAPTURE_UI").is_empty()
    device_probe_enabled = device_probe_enabled or not _runtime_string("AETHERKIRI_CAPTURE_AFTER_OPEN").is_empty()
    device_probe_enabled = device_probe_enabled or not _runtime_string("AETHERKIRI_AUTO_PROBE_CLICKS").is_empty()
    device_probe_enabled = device_probe_enabled or not cli_probe_script.is_empty()

    var live_fps_log_path := OS.get_environment("AETHERKIRI_LIVE_FPS_LOG")
    if live_fps_log_path.is_empty() and ios_diagnostics_enabled and _runtime_flag("AETHERKIRI_IOS_FILE_LOG"):
        live_fps_log_path = "user://aetherkiri-ios-live.log"
    if not live_fps_log_path.is_empty():
        perf_log_file = FileAccess.open(live_fps_log_path, FileAccess.WRITE)
        if perf_log_file != null:
            perf_log_file.store_line("live fps log started path=%s input_trace=%s frame_spike_ms=%.2f" % [
                live_fps_log_path,
                str(input_trace_enabled),
                frame_spike_ms,
            ])
            perf_log_file.flush()

    selected_backend = _normalize_backend_name(_runtime_string(
        "AETHERKIRI_BACKEND",
        ProjectSettings.get_setting(SETTINGS_KEY, "Godot Native")
    ))
    _load_shell_settings()
    _apply_shell_runtime_settings()
    _configure_runtime_diagnostics()
    var env_backend := _runtime_string("AETHERKIRI_BACKEND", "")
    if not env_backend.is_empty():
        selected_backend = _normalize_backend_name(env_backend)
    if not selected_backend in BACKENDS:
        selected_backend = "Godot Native"

    if ui_motion.get_parent() == null:
        add_child(ui_motion)
    _build_ui()
    _stage_runtime_fonts()

    if not _create_runtime_player():
        return
    _restore_native_translation_model_access()
    _initialize_iap()

    diagnostic_session = DiagnosticSession.new()
    add_child(diagnostic_session)
    diagnostic_session.configure_preference(diagnostic_profile)
    diagnostic_session.set_translator(func(key: String): return _t(key))
    diagnostic_session.set_marker_context_provider(func(): return _diagnostic_marker_context())
    diagnostic_session.build_overlay(self)
    _setup_debug_console()

    for item in BACKENDS:
        backend.add_item(item)

    var index := BACKENDS.find(selected_backend)
    backend.select(max(index, 0))

    var configured_game_path := OS.get_environment("AETHERKIRI_GAME_PATH")
    if configured_game_path.is_empty():
        configured_game_path = ProjectSettings.get_setting(GAME_PATH_KEY, "")
    configured_game_path = _resolve_game_path(configured_game_path)
    if not configured_game_path.is_empty():
        ProjectSettings.set_setting(GAME_PATH_KEY, configured_game_path)
    game_path.text = configured_game_path

    backend.item_selected.connect(_on_backend_selected)
    viewport.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
    viewport.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
    viewport.texture_filter = CanvasItem.TEXTURE_FILTER_LINEAR
    get_viewport().canvas_item_default_texture_filter = Viewport.DEFAULT_CANVAS_ITEM_TEXTURE_FILTER_LINEAR
    _apply_upscale_algorithm()

    _append_log("AetherKiri shell ready. Initializing engine...")
    call_deferred("_finish_ready_after_first_frame")

func _setup_debug_console() -> void:
    debug_console = DebugConsole.new()
    debug_console.setup(self, func(key: String): return _t(key), func(): return _debug_console_snapshot())
    debug_console.marker_requested.connect(_on_debug_marker_requested)
    debug_console.snapshot_requested.connect(_on_debug_snapshot_requested)
    debug_console.screenshot_requested.connect(_on_debug_screenshot_requested)
    debug_console.export_requested.connect(_on_debug_export_requested)
    debug_console.copy_summary_requested.connect(_on_debug_copy_summary_requested)
    debug_console.self_check_requested.connect(_on_debug_self_check_requested)
    debug_console.capture_slow_frame_requested.connect(_on_debug_slow_frame_requested)
    debug_console.advanced_toggle_requested.connect(_set_advanced_tool)
    debug_console.logs_clear_requested.connect(func():
        log_lines.clear()
        log_view_dirty = true
    )
    debug_console.drawer_visibility_changed.connect(func(open: bool):
        if diagnostic_session != null:
            diagnostic_session.set_game_active(game_running and not open)
    )
    _sync_debug_console_state()

func _sync_debug_console_state() -> void:
    if debug_console == null:
        return
    var available: bool = diagnostic_session != null and bool(diagnostic_session.active)
    debug_console.set_available(available)
    debug_console.set_game_active(game_running and available)
    if diagnostic_session != null:
        diagnostic_session.set_game_active(game_running and available and not debug_console.is_open())

func _runtime_memory_snapshot() -> Dictionary:
    var memory: Dictionary = {}
    if player != null and player.is_initialized() and player.has_method("get_memory_stats"):
        memory = (player.get_memory_stats() as Dictionary).duplicate(true)
    var resident_bytes := int(memory.get(
        "process_resident_bytes",
        int(memory.get("self_used_mb", 0)) * 1024 * 1024
    ))
    var footprint_bytes := int(memory.get(
        "process_physical_footprint_bytes",
        resident_bytes
    ))
    var current_bytes := footprint_bytes if footprint_bytes > 0 else resident_bytes
    memory_observed_peak_bytes = maxi(memory_observed_peak_bytes, current_bytes)
    var native_peak_bytes := int(memory.get(
        "process_peak_physical_footprint_bytes",
        current_bytes
    ))
    memory["current_bytes"] = current_bytes
    memory["peak_bytes"] = maxi(memory_observed_peak_bytes, native_peak_bytes)
    memory["resident_bytes"] = resident_bytes
    memory["available_bytes"] = int(memory.get("process_available_bytes", 0))
    memory["system_free_bytes"] = int(memory.get("system_free_mb", 0)) * 1024 * 1024
    memory["system_total_bytes"] = int(memory.get("system_total_mb", 0)) * 1024 * 1024
    memory["godot_static_bytes"] = int(Performance.get_monitor(Performance.MEMORY_STATIC))
    memory["godot_static_peak_bytes"] = int(Performance.get_monitor(Performance.MEMORY_STATIC_MAX))
    var gpu_texture_bytes := int(memory.get("gpu_texture_bytes", 0))
    var gpu_buffer_bytes := int(memory.get("gpu_buffer_bytes", 0))
    memory["gpu_total_bytes"] = maxi(
        int(memory.get("gpu_total_bytes", 0)),
        gpu_texture_bytes + gpu_buffer_bytes
    )
    memory["cache_bytes"] = (
        int(memory.get("graphic_cache_bytes", 0))
        + int(memory.get("xp3_segment_cache_bytes", 0))
        + int(memory.get("psb_cache_bytes", 0))
    )
    return memory

func _format_monitor_bytes(value: int) -> String:
    if value <= 0:
        return "-"
    if value >= 1024 * 1024 * 1024:
        return "%.2f GiB" % (float(value) / float(1024 * 1024 * 1024))
    if value >= 1024 * 1024:
        return "%.0f MiB" % (float(value) / float(1024 * 1024))
    if value >= 1024:
        return "%.0f KiB" % (float(value) / 1024.0)
    return "%d B" % value

func _diagnostic_marker_context() -> Dictionary:
    var memory := _runtime_memory_snapshot()
    var plugins := {}
    var renderer := selected_backend
    if player != null and player.is_initialized():
        var renderer_info := String(player.get_renderer_info())
        if not renderer_info.is_empty():
            renderer = renderer_info
        if player.has_method("get_plugin_debug_info"):
            var parsed = JSON.parse_string(String(player.get_plugin_debug_info()))
            if parsed is Dictionary:
                plugins = parsed
    return {
        "fps": Engine.get_frames_per_second(),
        "frame_ms": last_frame_ms,
        "tick_ms": last_tick_ms,
        "godot_update_ms": last_update_ms,
        "frame_summary": diagnostic_session.latest_frame_summary.duplicate(true) if diagnostic_session != null else {},
        "renderer": renderer,
        "texture_size": [last_texture_size.x, last_texture_size.y],
        "surface_size": [current_surface_size.x, current_surface_size.y],
        "render_errors": render_errors,
        "input": {
            "last_event": debug_last_input_event,
            "last_target": debug_last_input_target,
            "last_position": [debug_last_input_position.x, debug_last_input_position.y],
            "received": input_trace_received,
            "forwarded": input_trace_forwarded,
            "blocked": input_trace_blocked,
            "throttled": input_trace_throttled,
            "busy": input_trace_busy,
            "outside": input_trace_outside,
            "suppressed": input_trace_move_suppressed,
            "active_pointers": active_touch_points.size(),
        },
        "memory": memory,
        "plugins": plugins,
    }

func _debug_console_snapshot() -> Dictionary:
    var session: Dictionary = diagnostic_session.status_snapshot() if diagnostic_session != null else {}
    var memory: Dictionary = _runtime_memory_snapshot()
    var plugins: Dictionary = {}
    var renderer: String = selected_backend
    var texture_backend: String = "-"
    if player != null and player.is_initialized():
        var renderer_info := String(player.get_renderer_info())
        if not renderer_info.is_empty():
            renderer = renderer_info
        texture_backend = String(player.get_frame_texture_backend())
        if player.has_method("get_plugin_debug_info"):
            var parsed = JSON.parse_string(String(player.get_plugin_debug_info()))
            if parsed is Dictionary:
                plugins = (parsed as Dictionary).duplicate(true)
                plugins["method_call_count"] = int(plugins.get("method_calls", 0))
                plugins["property_call_count"] = int(plugins.get("property_gets", 0)) + int(plugins.get("property_sets", 0))
                plugins["plugin_load_success_count"] = int(plugins.get("load_succeeded", 0))
                plugins["plugin_load_failure_count"] = int(plugins.get("load_failed", 0))
                plugins["plugin_load_fallback_count"] = int(plugins.get("load_fallback", 0))
                plugins["missing_member_count"] = int(plugins.get("missing_members", 0))
    var points: Dictionary = active_touch_points.duplicate()
    if pending_touch_index >= 0:
        points[pending_touch_index] = pending_touch_mapped
    var frame_summary: Dictionary = session.get("frame_summary", {})
    var surface_text: String = "%dx%d" % [current_surface_size.x, current_surface_size.y]
    var texture_text: String = "%dx%d %s" % [last_texture_size.x, last_texture_size.y, texture_backend]
    return {
        "session": session,
        "performance": {
            "fps": Engine.get_frames_per_second(),
            "frame_summary": frame_summary,
            "tick_ms": last_tick_ms,
            "update_ms": last_update_ms,
            "frame_ms": last_frame_ms,
            "renderer": renderer,
            "texture": texture_text,
            "surface": surface_text,
            "fallback": selected_backend == "Debug CPU",
            "errors": render_errors,
        },
        "memory": memory,
        "plugins": plugins,
        "events": diagnostic_session.recent_events(100) if diagnostic_session != null else [],
        "logs": Array(log_lines),
        "input": {
            "last_event": debug_last_input_event,
            "last_target": debug_last_input_target,
            "last_position": debug_last_input_position,
            "received": input_trace_received,
            "forwarded": input_trace_forwarded,
            "blocked": input_trace_blocked,
            "throttled": input_trace_throttled,
            "busy": input_trace_busy,
            "outside": input_trace_outside,
            "suppressed": input_trace_move_suppressed,
            "active_count": points.size(),
            "points": points,
        },
        "advanced": _advanced_snapshot(),
        "overhead": "high" if plugin_trace or trace_log or diagnostic_profile == "full" else ("medium" if console_log_file or export_scripts or diagnostic_profile not in ["off", "baseline"] else "low"),
    }

func _on_debug_marker_requested(label: String) -> void:
    if diagnostic_session == null or not diagnostic_session.active:
        debug_console.show_result(_t("debug.result.unavailable"), true)
        return
    if diagnostic_session.mark_issue(label):
        debug_console.show_result(_t("debug.result.marked", [label]))
    else:
        debug_console.show_result(_t("debug.result.marker_limit", [DiagnosticSession.MAX_MARKERS]), true)

func _on_debug_snapshot_requested() -> void:
    if diagnostic_session == null or not diagnostic_session.active:
        debug_console.show_result(_t("debug.result.unavailable"), true)
        return
    var snapshot := _debug_console_snapshot()
    snapshot.erase("events")
    snapshot.erase("logs")
    var path: String = diagnostic_session.write_state_snapshot(snapshot)
    debug_console.show_result(_t("debug.result.snapshot", [path]), path.is_empty())

func _on_debug_screenshot_requested() -> void:
    if diagnostic_session == null or not diagnostic_session.active:
        debug_console.show_result(_t("debug.result.unavailable"), true)
        return
    var path: String = String(diagnostic_session.session_dir).path_join("screenshot-%d.png" % Time.get_ticks_msec())
    var result: Error = get_viewport().get_texture().get_image().save_png(path)
    if result == OK:
        diagnostic_session.record("godot", "render", "info", "screenshot_saved", 0, {"path": path.get_file()})
        diagnostic_session.flush()
    var global_path: String = ProjectSettings.globalize_path(path) if result == OK else ""
    debug_console.show_result(_t("debug.result.screenshot", [global_path]), result != OK)

func _on_debug_export_requested() -> void:
    if diagnostic_session == null or not diagnostic_session.active:
        debug_console.show_result(_t("debug.result.unavailable"), true)
        return
    var path: String = diagnostic_session.export_zip()
    debug_console.show_result(_t("debug.result.exported", [path]), path.is_empty())

func _on_debug_copy_summary_requested() -> void:
    if diagnostic_session == null or not diagnostic_session.active:
        debug_console.show_result(_t("debug.result.unavailable"), true)
        return
    DisplayServer.clipboard_set(diagnostic_session.summary_text({"renderer": selected_backend, "errors": render_errors}))
    debug_console.show_result(_t("debug.result.copied"))

func _on_debug_slow_frame_requested() -> void:
    if diagnostic_session != null and diagnostic_session.arm_next_slow_frame():
        debug_console.show_result(_t("debug.result.slow_armed"))
    else:
        debug_console.show_result(_t("debug.result.unavailable"), true)

func _on_debug_self_check_requested() -> void:
    var checks := PackedStringArray()
    checks.append("session=ok" if diagnostic_session != null and diagnostic_session.active else "session=failed")
    checks.append("engine=ok" if player != null and player.is_initialized() else "engine=failed")
    checks.append("renderer=ok" if not selected_backend.is_empty() else "renderer=failed")
    var writable := false
    if diagnostic_session != null and diagnostic_session.active:
        var probe_path: String = String(diagnostic_session.session_dir).path_join(".self-check")
        var probe: FileAccess = FileAccess.open(probe_path, FileAccess.WRITE)
        writable = probe != null
        if probe != null:
            probe.store_string("ok")
            probe = null
            DirAccess.remove_absolute(ProjectSettings.globalize_path(probe_path))
        diagnostic_session.record("godot", "system", "info" if writable else "error", "diagnostic_self_check", 0, {"checks": Array(checks), "storage_writable": writable})
        diagnostic_session.flush()
    checks.append("storage=ok" if writable else "storage=failed")
    debug_console.show_result(_t("debug.result.self_check", [", ".join(checks)]), not writable)

func _create_runtime_player(runtime_kind: String = RUNTIME_KIRIKIRI) -> bool:
    if not ClassDB.class_exists(RUNTIME_PLAYER_CLASS):
        var message := "%s runtime extension class is unavailable." % RUNTIME_PLAYER_CLASS
        push_error(message)
        _append_log(message)
        _show_system_alert(_t("alert.runtime_class_missing"), _t("alert.error_title"))
        return false
    var instance: Object = ClassDB.instantiate(RUNTIME_PLAYER_CLASS)
    if instance == null or not (instance is Node):
        var create_message := "Aether runtime extension could not create %s." % RUNTIME_PLAYER_CLASS
        push_error(create_message)
        _append_log(create_message)
        _show_system_alert(_t("alert.runtime_create_failed"), _t("alert.error_title"))
        return false
    player = instance
    current_player_runtime_kind = runtime_kind
    if instance.has_signal("platform_request"):
        instance.connect(
            "platform_request",
            Callable(self, "_on_runtime_platform_request")
        )
    add_child(instance as Node)
    return true

func _switch_runtime_player(runtime_kind: String) -> bool:
    var normalized := runtime_kind
    if normalized != RUNTIME_ONSCRIPTER and normalized != RUNTIME_MINORI:
        normalized = RUNTIME_KIRIKIRI
    if player != null and current_player_runtime_kind == normalized:
        return true
    if game_running:
        _append_log("Cannot switch visual-novel runtimes while a game is running.")
        return false

    if player == null:
        if not _create_runtime_player(normalized):
            return false
    else:
        # Runtime implementations live behind one stable Godot-facing player.
        # Recreate only its engine handle so UI signals, frame effects, and
        # platform services do not need one Node implementation per backend.
        player.destroy_engine()
        current_player_runtime_kind = normalized
    if not _ensure_player_initialized():
        return false
    _apply_backend(false)
    _apply_engine_options()
    _apply_frame_enhancement_settings()
    if diagnostic_session != null:
        if diagnostic_session.active:
            diagnostic_session.finish()
        diagnostic_session.start(player, selected_backend)
    _append_log("Runtime selected: %s" % (
        "OnscripterYuri"
        if normalized == RUNTIME_ONSCRIPTER
        else "MinoriRust"
        if normalized == RUNTIME_MINORI
        else "KiriKiri"
    ))
    return true

func _parse_platform_form(argument: String) -> Dictionary:
    var values := {}
    for field in argument.split("&", false):
        var separator := field.find("=")
        var encoded_key := field if separator < 0 else field.left(separator)
        var encoded_value := "" if separator < 0 else field.substr(separator + 1)
        var key := encoded_key.replace("+", " ").uri_decode()
        values[key] = encoded_value.replace("+", " ").uri_decode()
    return values

func _on_runtime_platform_request(operation: String, argument: String) -> void:
    if player == null:
        return
    if operation == "minori_select":
        _show_minori_select(argument)
        return
    if operation == "dialog":
        _show_runtime_dialog(_parse_platform_form(argument))
        return
    if operation == "call_native":
        player.submit_platform_response("call_native", "result=")
        return
    if operation == "purchase":
        player.submit_platform_response(
            "purchase",
            "result=-1&title=&description=&price=&token=" +
            "&error_response=-1&error_message=" +
            "In%20App%20Billing%20is%20unavailable"
        )
        return
    _append_log("Unhandled platform request: %s %s" % [operation, argument])

func _show_minori_select(argument: String) -> void:
    if modal_layer == null or player == null:
        return
    var options := argument.split("\n", false)
    if options.is_empty() or options.size() > 4:
        player.submit_platform_response("minori_select", "index=-1")
        return
    var dialog := _modal_dialog(Vector2(560, 120 + options.size() * 62), 0.56)
    if active_modal_scrim != null:
        for connection in active_modal_scrim.gui_input.get_connections():
            active_modal_scrim.gui_input.disconnect(connection.callable)
    var box := VBoxContainer.new()
    box.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    box.size_flags_vertical = Control.SIZE_EXPAND_FILL
    box.alignment = BoxContainer.ALIGNMENT_CENTER
    box.add_theme_constant_override("separation", 12)
    dialog.add_child(box)
    for index in range(options.size()):
        var selected_index := index
        var button := _pill_button(options[index])
        button.custom_minimum_size = Vector2(0, 50)
        button.size_flags_horizontal = Control.SIZE_EXPAND_FILL
        button.pressed.connect(func():
            _dismiss_modal(func():
                if player != null:
                    player.submit_platform_response(
                        "minori_select", "index=%d" % selected_index
                    )
            )
        )
        box.add_child(button)
    var first_button := box.get_child(0) as Button
    if first_button != null:
        first_button.grab_focus()

func _show_runtime_dialog(values: Dictionary) -> void:
    if modal_layer == null or player == null:
        return
    _deactivate_game_text_input()
    modal_layer.set_meta("runtime_platform_dialog", true)
    modal_layer.visible = true
    modal_layer.move_to_front()
    for child in modal_layer.get_children():
        child.queue_free()

    var dim := ColorRect.new()
    dim.color = Color(0, 0, 0, 0.68)
    dim.mouse_filter = Control.MOUSE_FILTER_STOP
    dim.set_anchors_preset(Control.PRESET_FULL_RECT)
    modal_layer.add_child(dim)

    var dialog := PanelContainer.new()
    dialog.name = "ArtemisRuntimeDialog"
    dialog.clip_contents = true
    _mark_centered_safe_dialog(dialog, Vector2(780, 520))
    _layout_safe_dialog(dialog, _ui_safe_rect(get_viewport_rect().size))
    dialog.mouse_filter = Control.MOUSE_FILTER_STOP
    dialog.add_theme_stylebox_override(
        "panel",
        _panel_style(20, color_card, color_accent, 2)
    )
    modal_layer.add_child(dialog)

    _build_runtime_dialog_content(dialog, values)

func _build_runtime_dialog_content(
    dialog: PanelContainer,
    values: Dictionary
) -> void:

    var margin := MarginContainer.new()
    margin.name = "ArtemisDialogMargin"
    margin.add_theme_constant_override("margin_left", 30)
    margin.add_theme_constant_override("margin_top", 26)
    margin.add_theme_constant_override("margin_right", 30)
    margin.add_theme_constant_override("margin_bottom", 26)
    dialog.add_child(margin)

    var box := VBoxContainer.new()
    box.name = "ArtemisDialogContent"
    box.add_theme_constant_override("separation", 20)
    margin.add_child(box)

    var title := Label.new()
    title.text = String(values.get("title", ""))
    title.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
    title.add_theme_font_size_override("font_size", 30)
    title.add_theme_color_override("font_color", color_text)
    box.add_child(title)

    var message_scroll := ScrollContainer.new()
    message_scroll.name = "ArtemisDialogMessageScroll"
    message_scroll.custom_minimum_size = Vector2(0, 64)
    message_scroll.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    message_scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
    message_scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
    message_scroll.vertical_scroll_mode = ScrollContainer.SCROLL_MODE_AUTO
    message_scroll.scroll_deadzone = 0
    message_scroll.mouse_force_pass_scroll_events = false
    box.add_child(message_scroll)

    var message := Label.new()
    message.name = "ArtemisDialogMessage"
    message.text = String(values.get("message", ""))
    message.custom_minimum_size = Vector2.ZERO
    message.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    message.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
    message.add_theme_font_size_override("font_size", 23)
    message.add_theme_color_override("font_color", color_text)
    message_scroll.add_child(message)

    var text_field := String(values.get("text_field", "0")) == "1"
    var yes_no := String(values.get("yes_no", "0")) == "1"
    runtime_dialog_input = null
    if text_field:
        runtime_dialog_input = LineEdit.new()
        runtime_dialog_input.name = "ArtemisDialogInput"
        runtime_dialog_input.custom_minimum_size = Vector2(0, 68)
        runtime_dialog_input.size_flags_horizontal = Control.SIZE_EXPAND_FILL
        runtime_dialog_input.add_theme_font_size_override("font_size", 25)
        var maximum_characters := int(
            String(values.get("maximum_characters", "0"))
        )
        if maximum_characters > 0:
            runtime_dialog_input.max_length = maximum_characters
        box.add_child(runtime_dialog_input)

    var buttons := HBoxContainer.new()
    buttons.name = "ArtemisDialogButtons"
    buttons.alignment = BoxContainer.ALIGNMENT_END
    buttons.add_theme_constant_override("separation", 14)
    box.add_child(buttons)

    if yes_no:
        var no := _pill_button("No")
        no.custom_minimum_size = Vector2(150, 60)
        no.pressed.connect(
            _complete_runtime_dialog.bind(0, runtime_dialog_input)
        )
        buttons.add_child(no)

    var ok := _pill_button("Yes" if yes_no else "OK")
    ok.custom_minimum_size = Vector2(150, 60)
    ok.pressed.connect(
        _complete_runtime_dialog.bind(1, runtime_dialog_input)
    )
    buttons.add_child(ok)

    if runtime_dialog_input != null:
        runtime_dialog_input.text_submitted.connect(
            func(_text: String):
                _complete_runtime_dialog(1, runtime_dialog_input)
        )
        runtime_dialog_input.call_deferred("grab_focus")
    else:
        ok.call_deferred("grab_focus")

func _complete_runtime_dialog(result: int, input: LineEdit) -> void:
    if modal_layer == null or not bool(
        modal_layer.get_meta("runtime_platform_dialog", false)
    ):
        return
    var text := input.text if input != null and is_instance_valid(input) else ""
    modal_layer.set_meta("runtime_platform_dialog", false)
    modal_layer.visible = false
    runtime_dialog_input = null
    if player == null:
        return
    var response := "result=%d&text=%s" % [result, text.uri_encode()]
    var submit_result: int = int(
        player.submit_platform_response("dialog", response)
    )
    if submit_result != ENGINE_RESULT_OK:
        _append_log(
            "Dialog response failed: %s %s" % [
                player.get_last_result(),
                player.get_last_error(),
            ]
        )

func _ensure_player_initialized() -> bool:
    if player == null:
        return false
    if player.is_initialized():
        return true

    var user_dir := OS.get_user_data_dir()
    var cache_dir := user_dir.path_join("cache")
    DirAccess.make_dir_recursive_absolute(cache_dir)
    if not player.initialize_engine(user_dir, cache_dir):
        render_errors += 1
        var init_error_message := "Engine init failed: %s %s" % [
            player.get_last_result(),
            player.get_last_error(),
        ]
        _append_log(init_error_message)
        return false

    var runtime_id := "auto"
    if current_player_runtime_kind in [RUNTIME_ONSCRIPTER, RUNTIME_MINORI]:
        runtime_id = current_player_runtime_kind
    var runtime_result := int(player.set_engine_option("runtime", runtime_id))
    if runtime_result != ENGINE_RESULT_OK:
        render_errors += 1
        _append_log("Runtime selection failed: %s %s" % [
            player.get_last_result(),
            player.get_last_error(),
        ])
        player.destroy_engine()
        return false

    _append_log("%s engine initialized." % (
        "OnscripterYuri" if current_player_runtime_kind == RUNTIME_ONSCRIPTER else "AetherKiri"
    ))
    return true

func _finish_ready_after_first_frame() -> void:
    await get_tree().process_frame

    var engine_initialized := _ensure_player_initialized()

    if engine_initialized:
        _apply_backend(false)
        _apply_engine_options()
        _apply_frame_enhancement_settings()
        diagnostic_session.start(player, selected_backend)
        _sync_debug_console_state()
    if not cli_probe_script.is_empty():
        if not engine_initialized:
            printerr("initialize_engine failed: %s" % player.get_last_error())
            get_tree().quit(1)
            return
        call_deferred("_run_cli_script_probe")
        return

    _append_log("Debug CPU is a fallback backend and is not part of performance acceptance.")
    _write_probe_marker("ready")
    video_progress_data = _load_video_progress()
    _refresh_games()
    _refresh_videos()
    if _show_next_required_legal_document():
        if not OS.get_environment("AETHERKIRI_CAPTURE_UI").is_empty():
            call_deferred("_capture_ui_after_ready")
        return
    _continue_ready_after_legal_gate()

func _continue_ready_after_legal_gate() -> void:
    if legal_gate_completed:
        return
    if _show_next_required_legal_document():
        return
    legal_gate_completed = true
    call_deferred("_refresh_games_after_web_local_restore")
    call_deferred("_auto_start_web_dev_game")

    capture_after_open_path = _runtime_string("AETHERKIRI_CAPTURE_AFTER_OPEN")
    capture_after_open_delay_sec = maxf(
        0.0,
        _runtime_float("AETHERKIRI_CAPTURE_DELAY_SEC", 0.0)
    )
    auto_probe_clicks = _parse_click_points(_runtime_string("AETHERKIRI_AUTO_PROBE_CLICKS"))
    var auto_start_game_spec := _runtime_string("AETHERKIRI_AUTO_START_GAME")
    var auto_open_requested := _runtime_flag("AETHERKIRI_AUTO_OPEN")
    if _native_auto_start_enabled():
        if not auto_start_game_spec.is_empty():
            call_deferred("_auto_start_configured_game", auto_start_game_spec)
        elif auto_open_requested:
            call_deferred("_on_open_game")
    elif not auto_start_game_spec.is_empty() or auto_open_requested:
        _append_log("Native auto-start ignored. Set AETHERKIRI_ENABLE_AUTO_START=1 for automation runs.")
    if not OS.get_environment("AETHERKIRI_CAPTURE_UI").is_empty():
        call_deferred("_capture_ui_after_ready")

func _request_android_storage_permissions() -> void:
    if OS.get_name() != "Android":
        return
    var before := _android_has_external_storage_permission()
    _android_storage_permission_log("request_begin granted_before=%s" % str(before))
    if player != null and player.has_method("android_request_external_storage_permission"):
        var native_result := bool(player.android_request_external_storage_permission())
        _android_storage_permission_log("request_native dispatched=%s" % str(native_result))
        if native_result:
            _begin_android_storage_permission_probe()
            return
    var godot_result := bool(OS.request_permissions())
    _android_storage_permission_log("request_godot dispatched=%s" % str(godot_result))
    if godot_result:
        _begin_android_storage_permission_probe()
    else:
        _android_storage_permission_log("request_failed dispatch=false")

func _ensure_android_storage_permission_for_import(video_import: bool = false) -> bool:
    var message_key := (
        "message.android_video_storage_permission_required"
        if video_import
        else "message.android_storage_permission_required"
    )
    return _ensure_android_storage_permission_for_path("/storage/emulated/0", message_key)

func _ensure_android_storage_permission_for_path(
    path: String,
    message_key: String = "message.android_storage_permission_required"
) -> bool:
    var needs_permission := _android_path_needs_storage_permission(path)
    if not needs_permission:
        if OS.get_name() == "Android":
            _android_storage_permission_log(
                "check path=%s needs=false granted=not_checked" % path
            )
        return true
    var granted := _android_has_external_storage_permission()
    _android_storage_permission_log(
        "check path=%s needs=true granted=%s" % [path, str(granted)]
    )
    if granted:
        return true
    _android_storage_permission_log(
        "authorization_failed path=%s reason=permission_not_granted" % path
    )
    _show_android_storage_permission_prompt(Callable(), message_key)
    return false

func _android_storage_permission_log(message: String) -> void:
    var line := "android storage permission: %s" % message
    print(line)
    _append_log(line)

func _begin_android_storage_permission_probe() -> void:
    android_storage_permission_request_active = true
    android_storage_permission_request_deadline_msec = Time.get_ticks_msec() + 8000
    android_storage_permission_request_last_probe_msec = 0

func _poll_android_storage_permission_request() -> void:
    if not android_storage_permission_request_active or OS.get_name() != "Android":
        return
    var now := Time.get_ticks_msec()
    if android_storage_permission_request_last_probe_msec != 0 and now - android_storage_permission_request_last_probe_msec < 250:
        return
    android_storage_permission_request_last_probe_msec = now
    var granted := _android_has_external_storage_permission()
    if granted:
        android_storage_permission_request_active = false
        _android_storage_permission_log("authorization_granted after_request=true")
        return
    if now >= android_storage_permission_request_deadline_msec:
        android_storage_permission_request_active = false
        _android_storage_permission_log(
            "authorization_failed after_request=true timeout_ms=8000"
        )

func _show_android_storage_permission_prompt(
    after_acknowledged: Callable = Callable(),
    message_key: String = "message.android_storage_permission_required"
) -> void:
    var dialog := _modal_dialog(Vector2(640, 320), 0.46)
    var box := _modal_stack(dialog, APP_DISPLAY_NAME, ICON_LIBRARY)
    var body := Label.new()
    body.text = _t(message_key)
    body.size_flags_vertical = Control.SIZE_EXPAND_FILL
    body.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
    body.add_theme_font_size_override("font_size", 15)
    body.add_theme_color_override("font_color", ui_tokens.text_secondary)
    box.add_child(body)
    var ok := _pill_button(_t("dialog.ok"))
    ok.custom_minimum_size = Vector2(128, 44)
    ok.size_flags_horizontal = Control.SIZE_SHRINK_END
    ok.pressed.connect(func():
        _dismiss_modal(func():
            if after_acknowledged.is_valid():
                after_acknowledged.call_deferred()
            else:
                call_deferred("_request_android_storage_permissions")
        )
    )
    box.add_child(ok)

func _android_path_needs_storage_permission(path: String) -> bool:
    if OS.get_name() != "Android":
        return false
    var normalized := _android_external_storage_path_from_tree_uri(path).strip_edges()
    if normalized.is_empty():
        return false
    if normalized.begins_with("content://"):
        return true
    if normalized.begins_with("/sdcard"):
        return true
    if not normalized.begins_with("/storage/emulated/"):
        return false
    var package_path := "/storage/emulated/0/Android/data/org.github.krkr2.aetherkiri/"
    return not normalized.begins_with(package_path)

func _android_has_external_storage_permission() -> bool:
    if OS.get_name() != "Android":
        return true
    if player != null and player.has_method("android_has_external_storage_permission"):
        return bool(player.android_has_external_storage_permission())
    for root in ["/storage/emulated/0", "/sdcard"]:
        var dir := DirAccess.open(root)
        if dir != null:
            return true
    return false

func _auto_start_configured_game(spec: String = "") -> void:
    _refresh_known_games_for_auto_start()
    var game := {}
    var selectors: Array[String] = []
    var env_path := _runtime_string("AETHERKIRI_GAME_PATH")
    if not env_path.is_empty():
        selectors.append(env_path)
    if not _auto_start_spec_is_toggle(spec):
        selectors.append(spec)
    var configured_path := game_path.text.strip_edges()
    if not configured_path.is_empty():
        selectors.append(configured_path)
    selectors.append("RIDDLE JOKER")
    for selector in selectors:
        game = _find_known_game_by_query(selector)
        if not game.is_empty():
            break
    if game.is_empty() and not known_games.is_empty():
        game = known_games[0]
    if game.is_empty():
        printerr("auto start failed: no game path")
        return
    selected_game = game
    _start_selected_game()

func _refresh_known_games_for_auto_start() -> void:
    known_games = _load_game_list()
    if OS.get_name() == "iOS":
        known_games = _scan_ios_games_dir(known_games)
        _save_game_list(known_games)
    known_games = _sorted_games(known_games)

func _auto_start_spec_is_toggle(spec: String) -> bool:
    var value := spec.strip_edges().to_lower()
    return value == "1" or value == "true" or value == "yes" or value == "on"

func _find_known_game_by_path(path: String) -> Dictionary:
    if path.is_empty():
        return {}
    for game in known_games:
        if String(game.get("path", "")) == path:
            return game
    return {}

func _find_known_game_by_query(query: String) -> Dictionary:
    var normalized := query.strip_edges()
    if normalized.is_empty():
        return {}
    var resolved_path := _resolve_game_path(normalized)
    var game := _find_known_game_by_path(resolved_path)
    if not game.is_empty():
        return game
    if not resolved_path.is_empty() and _path_exists(resolved_path):
        return _game_info_from_path(resolved_path)
    var needle := normalized.to_lower()
    var partial := {}
    for item in known_games:
        var path := String(item.get("path", ""))
        var title := _game_display_title(item)
        var name := String(item.get("name", ""))
        var file_name := path.get_file()
        if title.to_lower() == needle or name.to_lower() == needle or file_name.to_lower() == needle:
            return item
        if partial.is_empty() and (
            title.to_lower().find(needle) >= 0
            or name.to_lower().find(needle) >= 0
            or file_name.to_lower().find(needle) >= 0
            or path.to_lower().find(needle) >= 0
        ):
            partial = item
    return partial

func _refresh_games_after_web_local_restore() -> void:
    if OS.get_name() != "Web":
        return
    var deadline_msec := Time.get_ticks_msec() + 30 * 1000
    while Time.get_ticks_msec() < deadline_msec:
        var state := _web_local_game_restore_state()
        if bool(state.get("done", true)):
            _refresh_games()
            return
        await get_tree().create_timer(0.25).timeout
    _refresh_games()

func _capture_ui_after_ready() -> void:
    var action := OS.get_environment("AETHERKIRI_CAPTURE_UI_ACTION")
    var capture_language := OS.get_environment("AETHERKIRI_CAPTURE_UI_LANGUAGE").strip_edges()
    if capture_language in [LANG_ZH_HANS, LANG_ZH_HANT, LANG_EN, LANG_JA, LANG_KO]:
        language_mode = capture_language
        active_language = capture_language
        _refresh_language_texts()
    if action == "legal":
        _show_legal_agreement(true)
    elif action == "legal_declined":
        _show_legal_declined_screen()
    elif action in ["settings", "settings_diagnostics", "settings_advanced"]:
        if action == "settings_advanced":
            advanced_tool_expanded = true
        _show_settings()
    elif action == "guide":
        _show_import_guide()
    elif action == "video_guide":
        _select_home_library("video")
        _show_import_guide()
    elif action == "video_remove":
        _select_home_library("video")
        if not known_videos.is_empty():
            _confirm_remove_video(known_videos[0])
    elif action == "video":
        _select_home_library("video")
    elif action in [
        "video_player",
        "video_player_controls",
        "video_player_rate",
        "video_player_subtitles",
    ]:
        var capture_video := OS.get_environment("AETHERKIRI_CAPTURE_UI_VIDEO")
        if not capture_video.is_empty() and FileAccess.file_exists(capture_video):
            _open_video_player(_video_info(capture_video))
        elif not known_videos.is_empty():
            _open_video_player(known_videos[0])
        if action in [
            "video_player_controls",
            "video_player_rate",
            "video_player_subtitles",
        ] and video_playing:
            _set_video_controls_visible(true, false)
    elif action == "legal_review":
        _show_settings()
        _show_legal_agreement(false)
    elif action == "ios_statement":
        _show_settings()
        _show_ios_additional_statement()
    elif action == "loading":
        _show_loading_overlay()
    elif action == "detail" and not known_games.is_empty():
        _show_detail(known_games[0])
    elif action == "debug_console" and diagnostic_session != null and diagnostic_session.active:
        shell_root.visible = false
        game_view.visible = true
        viewport.visible = true
        game_running = true
        diagnostic_session.set_game_active(true)
        _sync_debug_console_state()
        debug_console.open_drawer()
    var mouse := OS.get_environment("AETHERKIRI_CAPTURE_UI_MOUSE")
    if not mouse.is_empty():
        var parts := mouse.split(",", false)
        if parts.size() == 2:
            Input.warp_mouse(Vector2(parts[0].to_float(), parts[1].to_float()))
    await get_tree().process_frame
    await get_tree().process_frame
    if action == "video_player_subtitles" and video_playing:
        video_subtitle_button.show_popup()
        await get_tree().process_frame
    elif action == "video_player_rate" and video_playing:
        video_rate_button.show_popup()
        await get_tree().process_frame
    if action in [
        "video_player",
        "video_player_controls",
        "video_player_rate",
        "video_player_subtitles",
    ]:
        var video_wait_sec := 2.5
        var video_wait_text := OS.get_environment("AETHERKIRI_CAPTURE_UI_VIDEO_WAIT_SEC").strip_edges()
        if not video_wait_text.is_empty():
            video_wait_sec = maxf(0.0, video_wait_text.to_float())
        await get_tree().create_timer(video_wait_sec).timeout
    if action == "settings_diagnostics" and settings_view != null:
        settings_view.scroll_vertical = 1200
    elif action == "settings_advanced" and settings_view != null:
        settings_view.scroll_vertical = int(settings_view.get_v_scroll_bar().max_value)
    elif action == "video_player_subtitles" and is_instance_valid(video_subtitle_button):
        var subtitle_popup := video_subtitle_button.get_popup()
        print("video_subtitle_popup visible=%s position=%s size=%s button_rect=%s" % [
            subtitle_popup.visible,
            subtitle_popup.position,
            subtitle_popup.size,
            video_subtitle_button.get_global_rect(),
        ])
    elif action == "video_player_rate" and is_instance_valid(video_rate_button):
        var rate_popup := video_rate_button.get_popup()
        print("video_rate_popup visible=%s position=%s size=%s button_rect=%s" % [
            rate_popup.visible,
            rate_popup.position,
            rate_popup.size,
            video_rate_button.get_global_rect(),
        ])
    await get_tree().process_frame
    var path := OS.get_environment("AETHERKIRI_CAPTURE_UI")
    var image := get_viewport().get_texture().get_image()
    image.save_png(path)
    print("ui_capture output=%s stats=%s" % [path, JSON.stringify(_image_stats(image))])
    if OS.get_environment("AETHERKIRI_QUIT_AFTER_CAPTURE") == "1":
        get_tree().quit(0)

func _run_cli_script_probe() -> void:
    _write_probe_marker("cli_probe start script=%s" % cli_probe_script)
    var config := ProbeConfig.load()
    var target_game_path: String = ProbeConfig.require_game_path(config)
    var requested_game_path := target_game_path
    if OS.get_name() == "iOS" and not target_game_path.is_empty():
        _refresh_known_games_for_auto_start()
        var game := _find_known_game_by_query(target_game_path)
        if not game.is_empty():
            target_game_path = String(game.get("path", target_game_path))
        else:
            target_game_path = _resolve_game_path(target_game_path)
    _write_probe_marker("cli_probe target requested=%s resolved=%s" % [requested_game_path, target_game_path])
    if target_game_path.is_empty():
        _write_probe_marker("cli_probe missing_game")
        printerr("AETHERKIRI_SMOKE_GAME is not set")
        await _probe_cleanup_and_quit(2)
        return

    _prepare_cli_probe_view(config)
    if cli_probe_script == "res://scripts/smoke_test.gd":
        await _run_cli_smoke_probe(config, target_game_path)
    elif cli_probe_script == "res://scripts/step_render_probe.gd":
        await _run_cli_step_probe(config, target_game_path)
    elif cli_probe_script == "res://scripts/gui_render_probe.gd":
        await _run_cli_gui_probe(config, target_game_path)
    elif cli_probe_script == "res://scripts/perf_input_probe.gd":
        await _run_cli_perf_input_probe(config, target_game_path)
    else:
        _write_probe_marker("cli_probe unsupported script=%s" % cli_probe_script)
        printerr("unsupported probe script: %s" % cli_probe_script)
        await _probe_cleanup_and_quit(2)

func _prepare_cli_probe_view(config: Dictionary) -> void:
    var window_size := ProbeConfig.window_size(config, Vector2i(1280, 720))
    if window_size.x > 0 and window_size.y > 0:
        DisplayServer.window_set_size(window_size)
        size = Vector2(window_size)
    shell_root.visible = false
    home_view.visible = false
    settings_view.visible = false
    detail_view.visible = false
    detail_scroll.visible = false
    modal_layer.visible = false
    bg_rect.color = color_game_bg
    viewport.visible = true
    game_view.visible = true
    loading_panel.visible = false
    _set_perf_visible(false)
    restart_notice.visible = false
    viewport.texture = null
    last_texture_size = Vector2i.ZERO
    last_source_texture_size = Vector2i.ZERO
    game_running = false
    _sync_debug_console_state()
    _fit_full_rects()

func _probe_open_game(config: Dictionary, target_game_path: String, backend_env: String) -> bool:
    var runtime_kind := _game_runtime_kind(target_game_path)
    if not _switch_runtime_player(runtime_kind):
        _write_probe_marker("probe_open_game runtime_switch_failed kind=%s" % runtime_kind)
        return false
    if GameLaunchEntry.runtime_uses_directory(runtime_kind):
        target_game_path = _game_runtime_root(target_game_path)
    selected_backend = ProbeConfig.backend(config, backend_env)
    if not selected_backend in BACKENDS:
        selected_backend = "Godot Native"
    _write_probe_marker("probe_open_game backend=%s target=%s" % [selected_backend, target_game_path])
    _apply_backend(false)
    var fps_limit := ProbeConfig.int_value(config, "fps_limit", _runtime_int("AETHERKIRI_PROBE_FPS_LIMIT", 0))
    player.set_engine_option("fps_limit", str(maxi(0, fps_limit)))
    if config.has("engine_options") and config["engine_options"] is Dictionary:
        var engine_options: Dictionary = config["engine_options"]
        for key in engine_options.keys():
            player.set_engine_option(String(key), String(engine_options[key]))
    var surface_size := ProbeConfig.surface_size(config)
    _write_probe_marker("probe_open_game surface=%dx%d fps_limit=%d" % [surface_size.x, surface_size.y, fps_limit])
    var surface_result: int = int(player.set_surface_size(surface_size.x, surface_size.y))
    if surface_result != ENGINE_RESULT_OK:
        _write_probe_marker("probe_open_game set_surface_failed error=%s" % player.get_last_error())
        printerr("set_surface_size failed: %s" % player.get_last_error())
        return false
    current_surface_size = surface_size
    game_path.text = target_game_path
    var result: int = int(player.open_game(target_game_path, true))
    if result != ENGINE_RESULT_OK:
        _write_probe_marker("probe_open_game failed error=%s" % player.get_last_error())
        printerr("open_game failed: %s" % player.get_last_error())
        return false
    _write_probe_marker("probe_open_game opened")
    return true

func _probe_wait_startup(config: Dictionary, fallback_frames: int = 900) -> bool:
    var timeout_frames := ProbeConfig.int_value(config, "startup_timeout_frames", fallback_frames)
    _write_probe_marker("probe_wait_startup frames=%d" % timeout_frames)
    for i in range(timeout_frames):
        var state: int = int(player.get_startup_state())
        if state == STARTUP_SUCCEEDED:
            _write_probe_marker("probe_wait_startup succeeded frame=%d" % i)
            return true
        if state == STARTUP_FAILED:
            _write_probe_marker("probe_wait_startup failed frame=%d error=%s" % [i, player.get_last_error()])
            printerr("startup failed: %s" % player.get_last_error())
            return false
        await get_tree().process_frame
    _write_probe_marker("probe_wait_startup timed_out")
    printerr("startup timed out")
    return false

func _probe_tick_and_update() -> bool:
    _flush_delayed_touch_releases()
    _flush_pending_touch_press_if_ready()
    var result: int = int(player.tick(1.0 / 60.0))
    if result != ENGINE_RESULT_OK:
        _write_probe_marker("probe_tick failed error=%s" % player.get_last_error())
        printerr("tick failed: %s" % player.get_last_error())
        return false
    if present_hold_frames > 0:
        present_hold_frames -= 1
        return true
    if player.has_method("set_frame_enhancement_target_size"):
        var enhancement_target := _frame_enhancement_target_size()
        player.set_frame_enhancement_target_size(enhancement_target.x, enhancement_target.y)
    var texture: Texture2D = player.update_frame_texture()
    if texture != null:
        viewport.texture = texture
        viewport.queue_redraw()
        last_texture_size = Vector2i(texture.get_width(), texture.get_height())
        if player.has_method("get_frame_source_size"):
            var source_size: Vector2i = player.get_frame_source_size()
            if source_size.x > 0 and source_size.y > 0:
                last_source_texture_size = source_size
        _layout_game_viewport(get_viewport_rect().size)
    return true

func _probe_advance(frames: int) -> bool:
    for i in range(max(0, frames)):
        if not _probe_tick_and_update():
            return false
        await get_tree().process_frame
    return true

func _run_cli_smoke_probe(config: Dictionary, target_game_path: String) -> void:
    var backend_name: String = ProbeConfig.backend(config)
    if not _probe_open_game(config, target_game_path, "AETHERKIRI_RENDER_BACKEND"):
        await _probe_cleanup_and_quit(1)
        return
    if not await _probe_wait_startup(config, 600):
        await _probe_cleanup_and_quit(1)
        return
    if not await _probe_advance(ProbeConfig.int_value(config, "smoke_tick_frames", 5)):
        await _probe_cleanup_and_quit(1)
        return

    var frame: Dictionary = player.read_frame_rgba()
    var bytes: int = frame.get("rgba", PackedByteArray()).size()
    var width: int = int(frame.get("width", 0))
    var height: int = int(frame.get("height", 0))
    if width <= 0 or height <= 0 or bytes <= 0:
        printerr("empty frame backend=%s frame=%dx%d bytes=%d renderer=%s" % [
            backend_name,
            width,
            height,
            bytes,
            player.get_renderer_info(),
        ])
        await _probe_cleanup_and_quit(1)
        return

    var texture: Texture2D = player.update_frame_texture()
    var source_size := Vector2i(width, height)
    if player.has_method("get_frame_source_size"):
        source_size = player.get_frame_source_size()
    if texture == null or source_size != Vector2i(width, height):
        printerr("texture update failed backend=%s frame=%dx%d renderer=%s" % [
            backend_name,
            width,
            height,
            player.get_renderer_info(),
        ])
        await _probe_cleanup_and_quit(1)
        return

    print("smoke ok backend=%s renderer=\"%s\" texture_backend=%s frame=%dx%d texture=%dx%d serial=%d bytes=%d" % [
        backend_name,
        player.get_renderer_info(),
        player.get_frame_texture_backend(),
        width,
        height,
        texture.get_width(),
        texture.get_height(),
        int(frame.get("frame_serial", 0)),
        bytes,
    ])
    await _probe_cleanup_and_quit(0)

func _run_cli_step_probe(config: Dictionary, target_game_path: String) -> void:
    cli_probe_runtime_debug = bool(config.get("runtime_debug", false))
    if not _probe_open_game(config, target_game_path, "AETHERKIRI_PROBE_BACKEND"):
        await _probe_cleanup_and_quit(1)
        return
    if not await _probe_wait_startup(config, 900):
        await _probe_cleanup_and_quit(1)
        return
    if not await _probe_advance(ProbeConfig.int_value(config, "warmup_frames", _runtime_int("AETHERKIRI_PROBE_WARMUP_FRAMES", 180))):
        await _probe_cleanup_and_quit(1)
        return
    # GPU-resident frame probes may deliberately avoid readback. On iOS a
    # startup screenshot otherwise serializes the shared OpenGL/Metal path and
    # can hide the exact transition the probe is intended to measure.
    if bool(config.get("capture_startup", true)):
        await _probe_save_step(0, "startup")

    var step := 1
    if config.has("actions") and config["actions"] is Array:
        step = await _probe_run_actions(config, step)
    else:
        step = await _probe_run_legacy_steps(config, step)
    if step < 0:
        await _probe_cleanup_and_quit(1)
        return

    var measured_frames: int = ProbeConfig.int_value(config, "measure_frames", _runtime_int("AETHERKIRI_PROBE_MEASURE_FRAMES", 120))
    var start_ticks: int = Time.get_ticks_usec()
    if not await _probe_advance(measured_frames):
        await _probe_cleanup_and_quit(1)
        return
    var fps: float = float(measured_frames) / max(0.0001, float(Time.get_ticks_usec() - start_ticks) / 1000000.0)
    var line := "step probe fps=%.2f texture_backend=%s renderer=\"%s\" steps=%d output=%s" % [
        fps,
        player.get_frame_texture_backend(),
        player.get_renderer_info(),
        step,
        _default_output_path("aetherkiri-step-*.png"),
    ]
    print(line)
    _write_probe_marker(line)
    await _probe_cleanup_and_quit(0)

func _probe_run_legacy_steps(config: Dictionary, step: int) -> int:
    for click in ProbeConfig.clicks(config):
        var click_dict: Dictionary = click
        var pos := ProbeConfig.click_position(click_dict)
        _probe_send_mapped_click(pos, config)
        var after_frames := int(click_dict.get("after_frames", ProbeConfig.int_value(config, "after_click_frames", _runtime_int("AETHERKIRI_PROBE_AFTER_CLICK_FRAMES", 180))))
        if not await _probe_advance(after_frames):
            return -1
        await _probe_save_step(step, "click_%d_%d" % [int(pos.x), int(pos.y)])
        step += 1

    for key_event in config.get("keys", []):
        if not key_event is Dictionary:
            continue
        var key_dict: Dictionary = key_event
        var key_code := int(key_dict.get("key_code", 13))
        player.send_key_event(true, key_code, int(key_dict.get("modifiers", 0)), int(key_dict.get("unicode", 0)))
        player.tick(1.0 / 60.0)
        player.send_key_event(false, key_code, int(key_dict.get("modifiers", 0)), 0)
        var key_after_frames := int(key_dict.get("after_frames", ProbeConfig.int_value(config, "after_click_frames", _runtime_int("AETHERKIRI_PROBE_AFTER_CLICK_FRAMES", 180))))
        if not await _probe_advance(key_after_frames):
            return -1
        await _probe_save_step(step, "key_%d" % key_code)
        step += 1

    for click in config.get("clicks_after_keys", []):
        if not click is Dictionary:
            continue
        var click_dict: Dictionary = click
        var pos := ProbeConfig.click_position(click_dict)
        _probe_send_mapped_click(pos, config)
        var after_frames := int(click_dict.get("after_frames", ProbeConfig.int_value(config, "after_click_frames", _runtime_int("AETHERKIRI_PROBE_AFTER_CLICK_FRAMES", 180))))
        if not await _probe_advance(after_frames):
            return -1
        await _probe_save_step(step, "click_%d_%d" % [int(pos.x), int(pos.y)])
        step += 1
    return step

func _probe_run_actions(config: Dictionary, step: int) -> int:
    for raw_action in config["actions"]:
        if not raw_action is Dictionary:
            continue
        var action: Dictionary = raw_action
        var kind := String(action.get("type", "click"))
        var label := String(action.get("label", kind))
        if kind == "click":
            var pos := ProbeConfig.click_position(action)
            _probe_send_mapped_click(pos, config)
            if label.is_empty() or label == "click":
                label = "click_%d_%d" % [int(pos.x), int(pos.y)]
        elif kind == "right_click":
            var pos := ProbeConfig.click_position(action)
            _probe_send_mapped_click(pos, config, 1)
            if label.is_empty() or label == "right_click":
                label = "right_click_%d_%d" % [int(pos.x), int(pos.y)]
        elif kind == "touch_click":
            var pos := ProbeConfig.click_position(action)
            _probe_send_mapped_touch_click(pos, config, int(action.get("touch_index", 0)))
            if label.is_empty() or label == "touch_click":
                label = "touch_click_%d_%d" % [int(pos.x), int(pos.y)]
        elif kind == "two_finger_tap":
            var pos := ProbeConfig.click_position(action)
            if not await _probe_send_mapped_two_finger_tap(
                pos,
                config,
                max(0, int(action.get("first_finger_lead_ms", 0)))
            ):
                return -1
            if label.is_empty() or label == "two_finger_tap":
                label = "two_finger_tap_%d_%d" % [int(pos.x), int(pos.y)]
        elif kind == "move":
            var pos := ProbeConfig.click_position(action)
            _probe_send_mapped_move(pos, config)
            if label.is_empty() or label == "move":
                label = "move_%d_%d" % [int(pos.x), int(pos.y)]
        elif kind == "drag":
            var from := _probe_action_point(action, "from", ProbeConfig.click_position(action))
            var to := _probe_action_point(action, "to", from)
            if not await _probe_send_mapped_drag(
                from,
                to,
                config,
                max(1, int(action.get("steps", 12))),
                max(0, int(action.get("per_step_frames", 1))),
                int(action.get("pointer_id", 0))
            ):
                continue
            if label.is_empty() or label == "drag":
                label = "drag_%d_%d_to_%d_%d" % [int(from.x), int(from.y), int(to.x), int(to.y)]
        elif kind == "key":
            var key_code := int(action.get("key_code", 13))
            player.send_key_event(true, key_code, int(action.get("modifiers", 0)), int(action.get("unicode", 0)))
            player.tick(1.0 / 60.0)
            player.send_key_event(false, key_code, int(action.get("modifiers", 0)), 0)
            if label.is_empty() or label == "key":
                label = "key_%d" % key_code
        elif kind == "scroll" or kind == "repeat_scroll":
            var pos := ProbeConfig.click_position(action)
            var count: int = max(1, int(action.get("count", 1)))
            var delta_y := float(action.get("delta_y", -1.0))
            var per_scroll_frames: int = max(0, int(action.get("per_scroll_frames", 1)))
            for i in range(count):
                _probe_send_mapped_scroll(pos, config, delta_y)
                if per_scroll_frames > 0 and not await _probe_advance(per_scroll_frames):
                    return -1
            if label.is_empty() or label == "scroll" or label == "repeat_scroll":
                label = "scroll_%d_%d_%d" % [count, int(pos.x), int(pos.y)]
        elif kind == "click_stream":
            step = await _probe_run_click_stream(config, step, label, action)
            continue
        elif kind == "wait" or kind == "capture":
            pass
        else:
            print("skip unknown action: %s" % kind)
            continue

        var after_frames := int(action.get("after_frames", ProbeConfig.int_value(config, "after_click_frames", _runtime_int("AETHERKIRI_PROBE_AFTER_CLICK_FRAMES", 180))))
        if not await _probe_advance(after_frames):
            return -1
        if bool(action.get("capture", true)):
            await _probe_save_step(step, label)
            step += 1
    return step

func _probe_run_click_stream(config: Dictionary, step: int, label: String, action: Dictionary) -> int:
    var pos := ProbeConfig.click_position(action)
    var mapped := _probe_map_window_point(pos, config)
    if mapped.x < 0.0 or mapped.y < 0.0:
        print("skip click_stream outside texture window=%s mapped=%s" % [pos, mapped])
        return step

    var frames: int = max(1, int(action.get("frames", 180)))
    var clicks_per_frame: int = max(0, int(action.get("clicks_per_frame", 1)))
    var click_every_frames: int = max(1, int(action.get("click_every_frames", 1)))
    var max_clicks: int = max(0, int(action.get("max_clicks", 0)))
    var capture_every: int = max(0, int(action.get("capture_every", 0)))
    var spike_ms: float = max(0.0, float(action.get("spike_ms", 20.0)))
    var sample_interval_ms: int = max(0, int(action.get("sample_interval_ms", 0)))
    var pointer_id: int = int(action.get("pointer_id", TOUCH_POINTER_ID_OFFSET))
    var input_total := 0.0
    var tick_total := 0.0
    var update_total := 0.0
    var frame_total := 0.0
    var wall_frame_total := 0.0
    var input_max := 0.0
    var tick_max := 0.0
    var update_max := 0.0
    var frame_max := 0.0
    var wall_frame_max := 0.0
    var wait_max := 0.0
    var spikes := 0
    var input_events := 0
    var clicks_sent := 0
    var measured_frames := 0
    var stream_start_ticks := Time.get_ticks_usec()
    var sample_start_ticks := stream_start_ticks
    var sample_frames := 0
    var sample_clicks_start := 0
    var sample_input_total := 0.0
    var sample_tick_total := 0.0
    var sample_update_total := 0.0
    var sample_frame_total := 0.0
    var sample_wall_frame_total := 0.0
    var sample_input_max := 0.0
    var sample_tick_max := 0.0
    var sample_update_max := 0.0
    var sample_frame_max := 0.0
    var sample_wall_frame_max := 0.0
    var sample_wait_max := 0.0
    var sample_spikes := 0
    var sample_index := 0

    if label.is_empty() or label == "click_stream":
        label = "click_stream_%d_%d_%d" % [frames, int(pos.x), int(pos.y)]

    player.send_pointer_event(POINTER_MOVE, pointer_id, mapped.x, mapped.y, 0.0, 0.0, 0)
    input_events += 1
    for frame_index in range(frames):
        var frame_start := Time.get_ticks_usec()
        var input_start := frame_start
        var click_batch := 0
        if clicks_per_frame > 0 and (frame_index % click_every_frames) == 0:
            click_batch = clicks_per_frame
            if max_clicks > 0:
                click_batch = mini(click_batch, max_clicks - clicks_sent)
        for i in range(max(0, click_batch)):
            player.send_pointer_event(POINTER_DOWN, pointer_id, mapped.x, mapped.y, 0.0, 0.0, 0)
            player.send_pointer_event(POINTER_UP, pointer_id, mapped.x, mapped.y, 0.0, 0.0, 0)
            input_events += 2
            clicks_sent += 1

        var after_input := Time.get_ticks_usec()
        var tick_start := after_input
        var tick_result: int = int(player.tick(1.0 / 60.0))
        var after_tick := Time.get_ticks_usec()
        if tick_result != ENGINE_RESULT_OK:
            printerr("click_stream tick failed: %s" % player.get_last_error())
            return -1
        var texture: Texture2D = player.update_frame_texture()
        if texture != null:
            viewport.texture = texture
            last_texture_size = Vector2i(texture.get_width(), texture.get_height())
            if player.has_method("get_frame_source_size"):
                var source_size: Vector2i = player.get_frame_source_size()
                if source_size.x > 0 and source_size.y > 0:
                    last_source_texture_size = source_size
            _layout_game_viewport(viewport.size)
            viewport.queue_redraw()
        var frame_end := Time.get_ticks_usec()

        var input_ms := float(after_input - input_start) / 1000.0
        var tick_ms := float(after_tick - tick_start) / 1000.0
        var update_ms := float(frame_end - after_tick) / 1000.0
        var frame_ms := float(frame_end - frame_start) / 1000.0
        input_total += input_ms
        tick_total += tick_ms
        update_total += update_ms
        frame_total += frame_ms
        measured_frames += 1
        input_max = maxf(input_max, input_ms)
        tick_max = maxf(tick_max, tick_ms)
        update_max = maxf(update_max, update_ms)
        frame_max = maxf(frame_max, frame_ms)
        if spike_ms > 0.0 and frame_ms >= spike_ms:
            spikes += 1
            sample_spikes += 1
        sample_frames += 1
        sample_input_total += input_ms
        sample_tick_total += tick_ms
        sample_update_total += update_ms
        sample_frame_total += frame_ms
        sample_input_max = maxf(sample_input_max, input_ms)
        sample_tick_max = maxf(sample_tick_max, tick_ms)
        sample_update_max = maxf(sample_update_max, update_ms)
        sample_frame_max = maxf(sample_frame_max, frame_ms)

        if capture_every > 0 and (frame_index % capture_every) == 0:
            await _probe_save_step(
                step,
                "%s_f%03d" % [label, frame_index],
                int(action.get("capture_wait_frames", 2))
            )
            step += 1
        await get_tree().process_frame
        var sample_end_ticks := Time.get_ticks_usec()
        # The overlay's Frame value is the host/Godot frame delta, which
        # includes the time spent yielding to the next process_frame. Keep
        # that wall-clock interval separate from the active engine work above
        # so click-stream results can be compared with the floating panel.
        var wall_frame_ms := float(sample_end_ticks - frame_start) / 1000.0
        var wait_ms := maxf(0.0, wall_frame_ms - frame_ms)
        wall_frame_total += wall_frame_ms
        wall_frame_max = maxf(wall_frame_max, wall_frame_ms)
        wait_max = maxf(wait_max, wait_ms)
        sample_wall_frame_total += wall_frame_ms
        sample_wall_frame_max = maxf(sample_wall_frame_max, wall_frame_ms)
        sample_wait_max = maxf(sample_wait_max, wait_ms)
        var sample_elapsed_ms := float(sample_end_ticks - sample_start_ticks) / 1000.0
        var stream_finished := frame_index + 1 >= frames
        if sample_interval_ms > 0 and (sample_elapsed_ms >= sample_interval_ms or stream_finished):
            var sample_divisor := float(max(1, sample_frames))
            var sample_wait_ms := maxf(
                0.0,
                (sample_elapsed_ms - sample_frame_total) / sample_divisor
            )
            var sample_line := "click_stream_sample label=%s index=%d frames=%d clicks=%d elapsed_ms=%.2f fps=%.2f avg_input_ms=%.2f avg_tick_ms=%.2f avg_update_ms=%.2f avg_active_ms=%.2f avg_wait_ms=%.2f avg_wall_frame_ms=%.2f max_input_ms=%.2f max_tick_ms=%.2f max_update_ms=%.2f max_active_ms=%.2f max_wall_frame_ms=%.2f max_wait_ms=%.2f spikes=%d spike_ms=%.2f texture_backend=%s renderer=\"%s\"" % [
                label,
                sample_index,
                sample_frames,
                clicks_sent - sample_clicks_start,
                sample_elapsed_ms,
                float(sample_frames) * 1000.0 / maxf(0.001, sample_elapsed_ms),
                sample_input_total / sample_divisor,
                sample_tick_total / sample_divisor,
                sample_update_total / sample_divisor,
                sample_frame_total / sample_divisor,
                sample_wait_ms,
                sample_wall_frame_total / sample_divisor,
                sample_input_max,
                sample_tick_max,
                sample_update_max,
                sample_frame_max,
                sample_wall_frame_max,
                sample_wait_max,
                sample_spikes,
                spike_ms,
                player.get_frame_texture_backend(),
                player.get_renderer_info(),
            ]
            print(sample_line)
            _write_probe_marker(sample_line)
            sample_index += 1
            sample_start_ticks = sample_end_ticks
            sample_frames = 0
            sample_clicks_start = clicks_sent
            sample_input_total = 0.0
            sample_tick_total = 0.0
            sample_update_total = 0.0
            sample_frame_total = 0.0
            sample_wall_frame_total = 0.0
            sample_input_max = 0.0
            sample_tick_max = 0.0
            sample_update_max = 0.0
            sample_frame_max = 0.0
            sample_wall_frame_max = 0.0
            sample_wait_max = 0.0
            sample_spikes = 0

    var divisor := float(max(1, measured_frames))
    var elapsed_sec: float = maxf(0.0001, float(Time.get_ticks_usec() - stream_start_ticks) / 1000000.0)
    var line := "click_stream label=%s frames=%d measured_frames=%d clicks_per_frame=%d click_every_frames=%d max_clicks=%d clicks_sent=%d input_events=%d fps=%.2f avg_input_ms=%.2f max_input_ms=%.2f avg_tick_ms=%.2f max_tick_ms=%.2f avg_update_ms=%.2f max_update_ms=%.2f avg_frame_ms=%.2f max_frame_ms=%.2f avg_wall_frame_ms=%.2f max_wall_frame_ms=%.2f max_wait_ms=%.2f spikes=%d spike_ms=%.2f texture_backend=%s renderer=\"%s\"" % [
        label,
        frames,
        measured_frames,
        clicks_per_frame,
        click_every_frames,
        max_clicks,
        clicks_sent,
        input_events,
        float(measured_frames) / elapsed_sec,
        input_total / divisor,
        input_max,
        tick_total / divisor,
        tick_max,
        update_total / divisor,
        update_max,
        frame_total / divisor,
        frame_max,
        wall_frame_total / divisor,
        wall_frame_max,
        wait_max,
        spikes,
        spike_ms,
        player.get_frame_texture_backend(),
        player.get_renderer_info(),
    ]
    print(line)
    _write_probe_marker(line)

    if bool(action.get("capture_final", true)):
        await _probe_save_step(step, "%s_final" % label)
        step += 1
    return step

func _probe_save_step(index: int, label: String, wait_frames: int = 2) -> void:
    for ignored in range(max(0, wait_frames)):
        await get_tree().process_frame
    var image := _probe_capture_image()
    var path := _default_output_path("aetherkiri-step-%02d-%s.png" % [index, label])
    image.save_png(path)
    var line := "step %02d label=%s texture_backend=%s renderer=\"%s\" screenshot=%s stats=%s" % [
        index,
        label,
        player.get_frame_texture_backend(),
        player.get_renderer_info(),
        path,
        JSON.stringify(_image_stats(image)),
    ]
    print(line)
    _write_probe_marker(line)
    if cli_probe_runtime_debug:
        var runtime_debug := String(player.get_plugin_debug_info())
        print("step %02d runtime_debug=%s" % [index, runtime_debug])
        var parsed = JSON.parse_string(runtime_debug)
        if parsed is Dictionary:
            var runtime_state := {}
            for key in [
                "scriptState",
                "scriptWaitReason",
                "waitFlag",
                "delayFlag",
                "textClickFlag",
                "clickFlag",
                "exclickFlag",
                "transitionFlag",
                "keycodeFlag",
                "buttonName",
                "buttonClick",
                "buttonEntry",
                "buttonStop",
                "lastInputDebug",
                "localInput2",
                "externalWaitReason",
                "queuedCommands",
                "eventResumeStates",
                "scriptStack",
            ]:
                if parsed.has(key):
                    runtime_state[key] = parsed[key]
            if parsed.has("commandTrace") and parsed["commandTrace"] is Array:
                var command_trace: Array = parsed["commandTrace"]
                runtime_state["commandTraceTail"] = command_trace.slice(
                    maxi(0, command_trace.size() - 8)
                )
            _write_probe_marker("step %02d runtime_state=%s" % [
                index,
                JSON.stringify(runtime_state),
            ])

func _probe_send_mapped_click(window_pos: Vector2, config: Dictionary, button: int = 0) -> void:
    var mapped := _probe_map_window_point(window_pos, config)
    if mapped.x < 0.0 or mapped.y < 0.0:
        print("skip click outside texture window=%s mapped=%s" % [window_pos, mapped])
        return
    player.send_pointer_event(POINTER_MOVE, 0, mapped.x, mapped.y, 0.0, 0.0, 0)
    player.tick(1.0 / 60.0)
    player.send_pointer_event(POINTER_DOWN, 0, mapped.x, mapped.y, 0.0, 0.0, button)
    _hold_next_present_after_input()
    player.tick(1.0 / 60.0)
    player.send_pointer_event(POINTER_UP, 0, mapped.x, mapped.y, 0.0, 0.0, button)
    _hold_next_present_after_input(POST_CLICK_PRESENT_HOLD_FRAMES, true)

func _probe_send_mapped_touch_click(window_pos: Vector2, config: Dictionary, touch_index: int = 0) -> void:
    var mapped := _probe_map_window_point(window_pos, config)
    if mapped.x < 0.0 or mapped.y < 0.0:
        print("skip touch click outside texture window=%s mapped=%s" % [window_pos, mapped])
        return
    # The CLI probe captures a step before its next tick, so flush any
    # release whose hold interval elapsed while the previous screenshot was
    # being written. The real app performs this from _process every frame.
    _flush_delayed_touch_releases()
    _set_pending_touch(touch_index, mapped)
    _send_pending_touch_click(touch_index, mapped)

func _probe_send_mapped_two_finger_tap(
    window_pos: Vector2,
    config: Dictionary,
    first_finger_lead_ms: int = 0
) -> bool:
    var mapped := _probe_map_window_point(window_pos, config)
    if mapped.x < 0.0 or mapped.y < 0.0:
        print("skip two-finger tap outside texture window=%s mapped=%s" % [window_pos, mapped])
        return false
    var first_id := 0
    var second_id := 1
    var separation := Vector2(12.0, 0.0)
    _set_pending_touch(first_id, mapped - separation)
    if first_finger_lead_ms > 0:
        # Match the real mobile path where the first finger outlives the
        # single-tap disambiguation delay before the second finger lands.
        # This produces primary DOWN, then primary UP + secondary DOWN/UP.
        if not _flush_pending_touch_press(true):
            return false
        if not await _probe_advance_for_ms(first_finger_lead_ms):
            return false
    _handle_secondary_touch_press(second_id, mapped + separation)
    for pointer_id in [first_id, second_id]:
        suppressed_touch_points.erase(pointer_id)
        active_touch_points.erase(pointer_id)
        touch_down_points.erase(pointer_id)
        dragging_touch_points.erase(pointer_id)
        last_forwarded_touch_move_msec_by_id.erase(pointer_id)
        _clear_pending_touch_if_matches(pointer_id)
    return true

func _probe_advance_for_ms(duration_ms: int) -> bool:
    if duration_ms <= 0:
        return true
    var deadline_usec := Time.get_ticks_usec() + duration_ms * 1000
    var next_tick_usec := Time.get_ticks_usec()
    while Time.get_ticks_usec() < deadline_usec:
        var now_usec := Time.get_ticks_usec()
        if now_usec >= next_tick_usec:
            if not _probe_tick_and_update():
                return false
            next_tick_usec = now_usec + 16667
        await get_tree().process_frame
    return true

func _probe_send_mapped_move(window_pos: Vector2, config: Dictionary) -> void:
    var mapped := _probe_map_window_point(window_pos, config)
    if mapped.x < 0.0 or mapped.y < 0.0:
        print("skip move outside texture window=%s mapped=%s" % [window_pos, mapped])
        return
    player.send_pointer_event(POINTER_MOVE, 0, mapped.x, mapped.y, 0.0, 0.0, 0)
    player.tick(1.0 / 60.0)

func _probe_action_point(action: Dictionary, key: String, fallback: Vector2) -> Vector2:
    var value: Variant = action.get(key, null)
    if value is Array and value.size() >= 2:
        return Vector2(float(value[0]), float(value[1]))
    if value is Dictionary:
        return Vector2(float(value.get("x", fallback.x)), float(value.get("y", fallback.y)))
    return fallback

func _probe_send_mapped_drag(
    from: Vector2,
    to: Vector2,
    config: Dictionary,
    steps: int,
    per_step_frames: int,
    pointer_id: int = 0
) -> bool:
    var mapped_from := _probe_map_window_point(from, config)
    var mapped_to := _probe_map_window_point(to, config)
    if mapped_from.x < 0.0 or mapped_from.y < 0.0 or mapped_to.x < 0.0 or mapped_to.y < 0.0:
        print("skip drag outside texture window from=%s to=%s mapped_from=%s mapped_to=%s" % [
            from,
            to,
            mapped_from,
            mapped_to,
        ])
        return false

    player.send_pointer_event(POINTER_MOVE, pointer_id, mapped_from.x, mapped_from.y, 0.0, 0.0, 0)
    player.tick(1.0 / 60.0)
    player.send_pointer_event(POINTER_DOWN, pointer_id, mapped_from.x, mapped_from.y, 0.0, 0.0, 0)
    _hold_next_present_after_input()
    player.tick(1.0 / 60.0)

    var previous := mapped_from
    for index in range(1, steps + 1):
        var current := mapped_from.lerp(mapped_to, float(index) / float(steps))
        var delta := current - previous
        player.send_pointer_event(
            POINTER_MOVE,
            pointer_id,
            current.x,
            current.y,
            delta.x,
            delta.y,
            0,
            0
        )
        player.tick(1.0 / 60.0)
        previous = current
        if per_step_frames > 0 and not await _probe_advance(per_step_frames):
            return false

    player.send_pointer_event(POINTER_UP, pointer_id, mapped_to.x, mapped_to.y, 0.0, 0.0, 0)
    _hold_next_present_after_input(POST_CLICK_PRESENT_HOLD_FRAMES, true)
    player.tick(1.0 / 60.0)
    return true

func _probe_send_mapped_scroll(window_pos: Vector2, config: Dictionary, delta_y: float) -> void:
    var mapped := _probe_map_window_point(window_pos, config)
    if mapped.x < 0.0 or mapped.y < 0.0:
        print("skip scroll outside texture window=%s mapped=%s" % [window_pos, mapped])
        return
    player.send_pointer_event(POINTER_MOVE, 0, mapped.x, mapped.y, 0.0, 0.0, 0)
    player.tick(1.0 / 60.0)
    player.send_pointer_event(POINTER_SCROLL, 0, mapped.x, mapped.y, 0.0, delta_y, 0)

func _probe_map_window_point(pos: Vector2, config: Dictionary) -> Vector2:
    var coord := ProbeConfig.coord_size(config, Vector2i(
        _runtime_int("AETHERKIRI_PROBE_COORD_W", 1600),
        _runtime_int("AETHERKIRI_PROBE_COORD_H", 900)
    ))
    var panel_size := Vector2(coord)
    return GameInputMapping.map_point_to_surface(
        pos,
        Rect2(Vector2.ZERO, panel_size),
        _game_input_content_size(),
        _game_input_surface_size()
    )

func _run_cli_gui_probe(config: Dictionary, target_game_path: String) -> void:
    if not _probe_open_game(config, target_game_path, "AETHERKIRI_RENDER_BACKEND"):
        await _probe_cleanup_and_quit(1)
        return
    if not await _probe_wait_startup(config, 600):
        await _probe_cleanup_and_quit(1)
        return

    var min_frames := ProbeConfig.int_value(config, "min_visible_frames", 0)
    var max_frames := ProbeConfig.int_value(config, "gui_probe_frames", 180)
    var frame := {}
    for i in range(max_frames):
        if not _probe_tick_and_update():
            await _probe_cleanup_and_quit(1)
            return
        frame = player.read_frame_rgba()
        if i >= min_frames and int(_frame_stats(frame).get("visible", 0)) > 0:
            break
        await get_tree().process_frame

    await get_tree().process_frame
    await get_tree().process_frame
    var stats := _frame_stats(frame)
    var screenshot := _probe_capture_image()
    var screenshot_stats := _image_stats(screenshot)
    var output_path := OS.get_user_data_dir().path_join("gui_render_probe.png")
    screenshot.save_png(output_path)
    print("gui probe renderer=\"%s\" texture_backend=%s frame=%dx%d serial=%d stats=%s screenshot=%s screenshot_stats=%s" % [
        player.get_renderer_info(),
        player.get_frame_texture_backend(),
        int(frame.get("width", 0)),
        int(frame.get("height", 0)),
        int(frame.get("frame_serial", 0)),
        JSON.stringify(stats),
        output_path,
        JSON.stringify(screenshot_stats),
    ])
    await _probe_cleanup_and_quit(0 if int(screenshot_stats.get("visible", 0)) > 0 else 2)

func _run_cli_perf_input_probe(config: Dictionary, target_game_path: String) -> void:
    if not _probe_open_game(config, target_game_path, "AETHERKIRI_PROBE_BACKEND"):
        await _probe_cleanup_and_quit(1)
        return
    if not await _probe_wait_startup(config, 900):
        await _probe_cleanup_and_quit(1)
        return
    if not await _probe_advance(ProbeConfig.int_value(config, "warmup_frames", 180)):
        await _probe_cleanup_and_quit(1)
        return

    var before := _probe_capture_image()
    var before_path := _default_output_path("aetherkiri-before-click.png")
    before.save_png(before_path)

    var measured_frames: int = ProbeConfig.int_value(config, "measure_frames", _runtime_int("AETHERKIRI_PROBE_MEASURE_FRAMES", 180))
    var start_ticks: int = Time.get_ticks_usec()
    if not await _probe_advance(measured_frames):
        await _probe_cleanup_and_quit(1)
        return
    var fps: float = float(measured_frames) / max(0.0001, float(Time.get_ticks_usec() - start_ticks) / 1000000.0)

    var clicks := ProbeConfig.clicks(config)
    var click_pos: Vector2 = ProbeConfig.click_position(clicks[0]) if not clicks.is_empty() else ProbeConfig.perf_click(config, "click", Vector2(
        _runtime_float("AETHERKIRI_PROBE_CLICK_X", 450.0),
        _runtime_float("AETHERKIRI_PROBE_CLICK_Y", 880.0)
    ))
    _probe_send_direct_click(click_pos)
    var post_click_frames: int = int(clicks[0].get("after_frames", 180)) if not clicks.is_empty() else ProbeConfig.nested_int(config, "perf_input", "post_click_frames", _runtime_int("AETHERKIRI_PROBE_POST_CLICK_FRAMES", 180))
    if not await _probe_advance(post_click_frames):
        await _probe_cleanup_and_quit(1)
        return

    var has_second_click := clicks.size() > 1 or OS.get_environment("AETHERKIRI_PROBE_SECOND_CLICK") == "1"
    if has_second_click:
        var second_click_pos := ProbeConfig.click_position(clicks[1]) if clicks.size() > 1 else ProbeConfig.perf_click(config, "second_click", Vector2(
            _runtime_float("AETHERKIRI_PROBE_SECOND_CLICK_X", 1350.0),
            _runtime_float("AETHERKIRI_PROBE_SECOND_CLICK_Y", 240.0)
        ))
        _probe_send_direct_click(second_click_pos)
        var second_post_click_frames: int = int(clicks[1].get("after_frames", 600)) if clicks.size() > 1 else ProbeConfig.nested_int(config, "perf_input", "second_post_click_frames", _runtime_int("AETHERKIRI_PROBE_SECOND_POST_CLICK_FRAMES", 600))
        if not await _probe_advance(second_post_click_frames):
            await _probe_cleanup_and_quit(1)
            return

    var after := _probe_capture_image()
    var after_path := _default_output_path("aetherkiri-after-click.png")
    after.save_png(after_path)
    var diff: float = _probe_image_diff_score(before, after)
    print("perf_input probe fps=%.2f texture_backend=%s renderer=\"%s\" click_diff=%.5f before=%s after=%s" % [
        fps,
        player.get_frame_texture_backend(),
        player.get_renderer_info(),
        diff,
        before_path,
        after_path,
    ])
    await _probe_cleanup_and_quit(0 if diff > 0.01 else 2)

func _probe_send_direct_click(pos: Vector2) -> void:
    player.send_pointer_event(POINTER_MOVE, 0, pos.x, pos.y, 0.0, 0.0, 0)
    player.tick(1.0 / 60.0)
    player.send_pointer_event(POINTER_DOWN, 0, pos.x, pos.y, 0.0, 0.0, 0)
    player.tick(1.0 / 60.0)
    player.send_pointer_event(POINTER_UP, 0, pos.x, pos.y, 0.0, 0.0, 0)

func _probe_capture_image() -> Image:
    var prefer_engine_frame := OS.get_environment("AETHERKIRI_PROBE_PREFER_ENGINE_FRAME") == "1"
    # In GPU-direct mode this is the texture the user actually sees. The CPU
    # compatibility frame can legitimately lag behind it, so consulting
    # read_frame_rgba() first would hide one-frame crop and layer corruption.
    if not prefer_engine_frame and viewport.texture != null:
        var direct_image := viewport.texture.get_image()
        if direct_image != null and direct_image.get_width() > 0 and direct_image.get_height() > 0:
            if int(_image_stats(direct_image).get("visible", 0)) > 0:
                return direct_image

    # A headless Godot viewport can be an opaque white dummy target. Prefer
    # the engine's composed RGBA frame so CLI regression captures inspect the
    # game output instead of accepting that dummy as a valid screenshot.
    var frame: Dictionary = player.read_frame_rgba()
    var data: PackedByteArray = frame.get("rgba", PackedByteArray())
    var width := int(frame.get("width", 0))
    var height := int(frame.get("height", 0))
    if width > 0 and height > 0 and data.size() >= width * height * 4:
        var frame_image := Image.create_from_data(width, height, false, Image.FORMAT_RGBA8, data)
        if int(_image_stats(frame_image).get("visible", 0)) > 0:
            return frame_image

    var texture := get_viewport().get_texture()
    if texture != null:
        var viewport_image := texture.get_image()
        if viewport_image != null and viewport_image.get_width() > 0 and viewport_image.get_height() > 0:
            if int(_image_stats(viewport_image).get("visible", 0)) > 0:
                return viewport_image

    return Image.create(1, 1, false, Image.FORMAT_RGBA8)

func _probe_image_diff_score(a: Image, b: Image) -> float:
    var width: int = min(a.get_width(), b.get_width())
    var height: int = min(a.get_height(), b.get_height())
    if width <= 0 or height <= 0:
        return 0.0
    var step_x: int = max(1, width / 160)
    var step_y: int = max(1, height / 90)
    var total := 0.0
    var samples := 0
    for y in range(0, height, step_y):
        for x in range(0, width, step_x):
            var ca := a.get_pixel(x, y)
            var cb := b.get_pixel(x, y)
            total += absf(ca.r - cb.r) + absf(ca.g - cb.g) + absf(ca.b - cb.b) + absf(ca.a - cb.a)
            samples += 1
    return total / max(1.0, float(samples))

func _probe_cleanup_and_quit(code: int) -> void:
    _write_probe_marker("probe_cleanup code=%d" % code)
    if FileAccess.file_exists(ProbeConfig.debug_request_path()):
        DirAccess.remove_absolute(ProjectSettings.globalize_path(ProbeConfig.debug_request_path()))
    _deactivate_game_text_input()
    if player != null:
        viewport.texture = null
        await get_tree().process_frame
        player.release_frame_texture()
        player.destroy_engine()
    get_tree().quit(code)

func _apply_initial_window_size() -> void:
    if OS.get_name() == "iOS" or OS.get_name() == "Android":
        return
    var screen_size := DisplayServer.screen_get_size(DisplayServer.window_get_current_screen())
    if screen_size.x <= 0 or screen_size.y <= 0:
        return
    var requested_size := _env_vector2i("AETHERKIRI_WINDOW_SIZE", INITIAL_WINDOW_SIZE)
    var max_window := Vector2(
        float(screen_size.x) * 0.88,
        float(screen_size.y) * 0.82
    )
    var scale := minf(
        max_window.x / float(requested_size.x),
        max_window.y / float(requested_size.y)
    )
    scale = minf(scale, 1.0)
    var target_size := Vector2i(
        int(round(float(requested_size.x) * scale)),
        int(round(float(requested_size.y) * scale))
    )
    DisplayServer.window_set_size(target_size)
    DisplayServer.window_set_position((screen_size - target_size) / 2)

func _apply_global_dpi_scale() -> void:
    var scale_text := OS.get_environment("AETHERKIRI_UI_DPI_SCALE").strip_edges()
    var window_size := DisplayServer.window_get_size()
    var scale := AetherDisplayScale.ui_scale(OS.get_name(), window_size, DEFAULT_UI_DPI_SCALE, scale_text)
    if OS.get_name() == "iOS" and scale_text.is_empty():
        var cfg := ConfigFile.new()
        if cfg.load(SETTINGS_FILE) == OK:
            ios_ui_scale_mode = String(cfg.get_value("interface", "ios_ui_scale_mode", ios_ui_scale_mode))
        scale = AetherDisplayScale.apply_ios_scale_mode(scale, ios_ui_scale_mode)
    var window := get_window()
    window.content_scale_factor = scale

func _process_video_playback(delta: float) -> void:
    if player == null:
        return
    var previous_serial := int(active_video_state.get("frame_serial", -1))
    var state = player.media_get_state()
    if not state is Dictionary:
        return
    active_video_state = state
    var duration := float(state.get("duration", 0.0))
    var position := float(state.get("position", 0.0))
    if duration > 0.0:
        active_video_duration = duration
        video_progress_slider.max_value = duration
    if _apply_pending_video_resume(state):
        position = float(state.get("position", position))
    if not active_video_scrubbing:
        video_progress_slider.value = clampf(position, 0.0, maxf(1.0, active_video_duration))
    if not video_seek_gesture_active:
        video_time_label.text = "%s / %s" % [
            _format_video_time(position),
            _format_video_time(active_video_duration),
        ]
    var serial := int(state.get("frame_serial", 0))
    if bool(state.get("frame_ready", false)) and serial != previous_serial:
        var texture = player.media_update_texture()
        if texture != null:
            video_texture.texture = texture
    _update_video_subtitle(position)
    var status := int(state.get("status", 0))
    _sync_video_play_button(status)
    if status == MEDIA_STATUS_ENDED and not active_video_end_handled:
        active_video_end_handled = true
        _store_active_video_progress(true)
    elif status != MEDIA_STATUS_ENDED:
        active_video_end_handled = false
    video_progress_save_accum += delta
    if status != MEDIA_STATUS_ENDED and video_progress_save_accum >= 5.0:
        video_progress_save_accum = 0.0
        _store_active_video_progress()
    _process_video_controls(delta)

func _apply_pending_video_resume(state: Dictionary) -> bool:
    if video_pending_resume_position <= 2.0 or player == null:
        return false
    var duration := float(state.get("duration", 0.0))
    if duration <= 0.0 or not bool(state.get("seekable", false)):
        return false
    var target := clampf(video_pending_resume_position, 0.0, duration)
    if int(player.media_seek(target)) != ENGINE_RESULT_OK:
        return false
    video_pending_resume_position = 0.0
    state["position"] = target
    active_video_state = state
    if video_progress_slider != null:
        video_progress_slider.value = target
    return true

func _process(delta: float) -> void:
    _poll_android_storage_permission_request()
    _poll_native_launch_file_picker()
    _poll_native_cover_file_picker()
    _poll_native_translation_model_file_picker()
    _fit_full_rects()
    _sync_game_virtual_controls()
    _process_iap(delta)
    _update_advanced_tool_timeouts()
    _flush_log_view_if_needed(delta)
    if video_playing:
        _process_video_playback(delta)
    var startup_state := cached_startup_state
    if game_running:
        _sync_player_surface_size(false)
        # Runtime logs also carry control messages such as [ALERT_DIALOG].
        # Drain them even when developer logging is disabled so those messages
        # cannot remain hidden in the native queue.
        log_drain_accum += delta
        if log_drain_accum >= LOG_DRAIN_INTERVAL:
            log_drain_accum = 0.0
            _drain_logs()

        if app_lifecycle_paused:
            return

        startup_poll_accum += delta
        if cached_startup_state == STARTUP_SUCCEEDED or startup_poll_accum >= STARTUP_POLL_INTERVAL:
            startup_poll_accum = 0.0
            cached_startup_state = player.get_startup_state()
            startup_state = cached_startup_state
            _sync_game_virtual_controls()
        if startup_state == STARTUP_SUCCEEDED:
            if (
                _translation_model_configured()
                and player.has_method("get_text_translation_state")
            ):
                var translation_state := int(player.get_text_translation_state())
                if translation_state == TEXT_TRANSLATION_LOADING:
                    _set_translation_loading_notice(true)
                    return
                # A failed optional model remains fail-open: the runtime keeps
                # running with authored text and its error is available in the
                # startup log instead of trapping the user behind this overlay.
                if translation_state in [
                    TEXT_TRANSLATION_READY,
                    TEXT_TRANSLATION_FAILED,
                    TEXT_TRANSLATION_DISABLED,
                ]:
                    _set_translation_loading_notice(false)
            restart_notice.text = ""
            if loading_panel != null and loading_panel.visible:
                _hide_loading_overlay(func():
                    _set_perf_visible(game_running and show_perf_monitor)
                )
            else:
                _set_perf_visible(show_perf_monitor)
            _flush_delayed_touch_releases()
            _flush_pending_touch_press_if_ready()
            tick_trace_serial += 1
            tick_trace_active_serial = tick_trace_serial
            if _should_log_tick_trace():
                _log_tick_trace("tick_begin serial=%d delta_ms=%.2f pending_touch=%d suppressed=%d busy_left_ms=%d" % [
                    tick_trace_serial,
                    delta * 1000.0,
                    active_touch_points.size(),
                    suppressed_touch_points.size(),
                    maxi(0, touch_input_busy_until_msec - Time.get_ticks_msec()),
                ])
            var tick_start := Time.get_ticks_usec()
            var tick_result: int = int(player.tick(delta))
            # Capture the failing call immediately. Follow-up bridge calls such
            # as text-input synchronization can succeed and overwrite the
            # player's shared last-result/last-error fields.
            var tick_result_name := ""
            var tick_error_message := ""
            if tick_result != ENGINE_RESULT_OK:
                tick_result_name = str(player.get_last_result())
                tick_error_message = str(player.get_last_error())
            _sync_game_text_input_state()
            var tick_ms := float(Time.get_ticks_usec() - tick_start) / 1000.0
            last_tick_ms = tick_ms
            last_frame_ms = delta * 1000.0
            tick_trace_active_serial = 0
            if tick_result != ENGINE_RESULT_OK:
                if _is_runtime_exit_error(tick_error_message):
                    var runtime_exit_line := "Game exited: %s %s" % [
                        tick_result_name,
                        tick_error_message,
                    ]
                    _append_log(runtime_exit_line)
                    print(runtime_exit_line)
                    if perf_log_file != null:
                        perf_log_file.store_line(runtime_exit_line)
                        perf_log_file.flush()
                    _return_to_library_after_runtime_exit()
                    return
                render_errors += 1
                var tick_error_line := "Tick failed: %s %s" % [
                    tick_result_name,
                    tick_error_message,
                ]
                _append_log(tick_error_line)
                print(tick_error_line)
                if perf_log_file != null:
                    perf_log_file.store_line(tick_error_line)
                    perf_log_file.flush()
                game_running = false
                _sync_game_virtual_controls()
                _deactivate_game_text_input()
                _sync_debug_console_state()
                if diagnostic_session != null:
                    diagnostic_session.set_game_active(false)
                    diagnostic_session.record("godot", "lifecycle", "error", "game_tick_failed", 0, {
                        "result": tick_result_name,
                        "error": tick_error_message,
                    })
                app_lifecycle_paused = false
            else:
                if tick_ms >= frame_spike_ms and frame_spike_ms > 0.0:
                    _log_tick_trace("tick_end serial=%d tick_ms=%.2f renderer=\"%s\"" % [
                        tick_trace_serial,
                        tick_ms,
                        player.get_renderer_info(),
                    ])
                _sync_game_text_input_state()
                var update_start := Time.get_ticks_usec()
                _update_frame()
                var update_ms := float(Time.get_ticks_usec() - update_start) / 1000.0
                last_update_ms = update_ms
                _flush_artemis_input_trace_samples()
                _update_touch_busy_gate(maxf(delta * 1000.0, tick_ms + update_ms))
                if diagnostic_session != null:
                    diagnostic_session.sample_frame(
                        delta,
                        tick_ms,
                        update_ms,
                        player.get_renderer_info(),
                        player.get_frame_texture_backend()
                    )
                _log_live_perf(delta, tick_ms, update_ms)
                _log_frame_spike(delta, tick_ms, update_ms)
                _log_frame_probe(delta)
                _log_input_trace(delta, tick_ms, update_ms)
        elif startup_state == STARTUP_FAILED:
            var startup_error_message := str(player.get_last_error())
            if _is_runtime_exit_error(startup_error_message):
                _append_log("Game exited during startup: %s" % startup_error_message)
                _return_to_library_after_runtime_exit()
                return
            restart_notice.text = "Game startup failed."
            _set_translation_loading_notice(false)
            _hide_loading_overlay()
            _set_game_background(false)
            shell_root.visible = true
            viewport.visible = false
            game_view.visible = false
            game_running = false
            _sync_game_virtual_controls()
            _deactivate_game_text_input()
            _sync_debug_console_state()
            if diagnostic_session != null:
                diagnostic_session.set_game_active(false)
                diagnostic_session.record("godot", "lifecycle", "error", "game_startup_failed", 0, {
                    "error": player.get_last_error(),
                })
            app_lifecycle_paused = false
            render_errors += 1
            var startup_error := "Startup failed: %s" % startup_error_message
            _append_log(startup_error)

    perf_accum += delta
    state_log_accum += delta
    if game_running and state_log_accum >= 1.0:
        state_log_accum = 0.0
        if _should_emit_runtime_perf_logs():
            var state_line := "main_state startup=%d last_result=%s last_error=\"%s\" texture=%s texture_size=%dx%d surface_mode=%s surface=%dx%d" % [
                startup_state,
                player.get_last_result(),
                player.get_last_error(),
                player.get_frame_texture_backend(),
                last_texture_size.x,
                last_texture_size.y,
                render_surface_mode,
                current_surface_size.x,
                current_surface_size.y,
            ]
            print(state_line)
            _write_probe_marker(state_line)
            if perf_log_file != null:
                perf_log_file.store_line(state_line)
                perf_log_file.flush()
    if perf_accum >= PERF_UPDATE_INTERVAL:
        perf_accum = 0.0
        if (perf_panel == null or not perf_panel.visible) and not verbose_render_log:
            return
        var frame_ms := delta * 1000.0
        var renderer: String = selected_backend
        if game_running and startup_state == STARTUP_SUCCEEDED:
            renderer = String(player.get_renderer_info())
        var renderer_summary := _renderer_summary(renderer)
        if verbose_render_log and game_running and not renderer.is_empty() and renderer_summary != last_renderer_info_logged:
            last_renderer_info_logged = renderer_summary
            _append_log("Renderer info: %s" % renderer)
        if perf_panel == null or not perf_panel.visible:
            return
        var fallback := _renderer_fallback(renderer)
        var texture_backend: String = String(player.get_frame_texture_backend()) if game_running else "none"
        var memory := _runtime_memory_snapshot()
        var summary_text := "Backend: %s | FPS: %d | Frame: %.2f ms | Texture: %s | Size: %dx%d | Surface: %s %dx%d | Fallback: %s | Errors: %d" % [
            renderer_summary,
            Engine.get_frames_per_second(),
            frame_ms,
            texture_backend,
            last_texture_size.x,
            last_texture_size.y,
            render_surface_mode,
            current_surface_size.x,
            current_surface_size.y,
            fallback,
            render_errors,
        ]
        if game_running and player.has_method("get_frame_enhancement_status"):
            var effect_status: Dictionary = player.get_frame_enhancement_status()
            var effect_state := "active" if bool(effect_status.get("active", false)) else ("waiting" if bool(effect_status.get("enabled", false)) else "off")
            var effect_label := _t("settings.frame_enhancement_mode.%s" % frame_enhancement_mode)
            if frame_enhancement_kind == "custom":
                effect_label = _t("settings.frame_enhancement_kind.custom")
            summary_text += "\nEnhancement: %s | Effect: %s | Source: %dx%d | Output: %dx%d | Raw: %s" % [
                effect_state,
                effect_label,
                int(effect_status.get("source_width", 0)),
                int(effect_status.get("source_height", 0)),
                last_texture_size.x,
                last_texture_size.y,
                "yes" if bool(effect_status.get("raw_source_output", false)) else "no",
            ]
        summary_text += "\nMemory: App %s | Peak %s | Headroom %s | Godot %s | GPU(est.) %s (Tex %s / Buf %s) | Cache %s" % [
            _format_monitor_bytes(int(memory.get("current_bytes", 0))),
            _format_monitor_bytes(int(memory.get("peak_bytes", 0))),
            _format_monitor_bytes(int(memory.get("available_bytes", 0))),
            _format_monitor_bytes(int(memory.get("godot_static_bytes", 0))),
            _format_monitor_bytes(int(memory.get("gpu_total_bytes", 0))),
            _format_monitor_bytes(int(memory.get("gpu_texture_bytes", 0))),
            _format_monitor_bytes(int(memory.get("gpu_buffer_bytes", 0))),
            _format_monitor_bytes(int(memory.get("cache_bytes", 0))),
        ]
        if player.has_method("get_text_translation_stats"):
            var translation: Dictionary = player.get_text_translation_stats()
            var translation_state := int(translation.get("state", TEXT_TRANSLATION_DISABLED))
            if translation_state != TEXT_TRANSLATION_DISABLED or _translation_model_configured():
                var state_label := String({
                    TEXT_TRANSLATION_DISABLED: "Off",
                    TEXT_TRANSLATION_LOADING: "Loading",
                    TEXT_TRANSLATION_READY: "Ready",
                    TEXT_TRANSLATION_FAILED: "Failed",
                }.get(translation_state, "Unknown"))
                var backend_label := String({
                    0: "-",
                    1: "CPU",
                    2: "GPU",
                }.get(int(translation.get("backend", 0)), "Unknown"))
                var model_bytes := int(translation.get(
                    "model_tensor_bytes",
                    int(translation.get("model_file_bytes", 0))
                ))
                var cache_hits := int(translation.get("cache_hits", 0))
                var cache_misses := int(translation.get("cache_misses", 0))
                var cache_requests := cache_hits + cache_misses
                var hit_percent := (
                    float(cache_hits) * 100.0 / float(cache_requests)
                    if cache_requests > 0 else 0.0
                )
                summary_text += "\nTranslation: %s/%s | Model %s | Context %s | Resident(est.) %s | Work %d+%d/%d | Cache %d (hit %.0f%%) | Wait/Infer %.0f/%.0f ms" % [
                    state_label,
                    backend_label,
                    _format_monitor_bytes(model_bytes),
                    _format_monitor_bytes(int(translation.get("context_state_bytes", 0))),
                    _format_monitor_bytes(int(translation.get("model_resident_bytes_estimate", 0))),
                    int(translation.get("active_jobs", 0)),
                    int(translation.get("priority_queue_entries", 0)),
                    int(translation.get("prefetch_queue_entries", 0)),
                    int(translation.get("cache_entries", 0)),
                    hit_percent,
                    float(translation.get("last_synchronous_wait_us", 0)) / 1000.0,
                    float(translation.get("last_inference_us", 0)) / 1000.0,
                ]
        if debug_overlay_mode == "detail" and diagnostic_session != null:
            var frame_summary: Dictionary = diagnostic_session.latest_frame_summary
            summary_text += "\nTick: %.2f ms | Update: %.2f ms | P50/P95/P99/Max: %.2f / %.2f / %.2f / %.2f ms | Dropped: %d" % [
                last_tick_ms,
                last_update_ms,
                float(frame_summary.get("p50_ms", 0.0)),
                float(frame_summary.get("p95_ms", 0.0)),
                float(frame_summary.get("p99_ms", 0.0)),
                float(frame_summary.get("max_ms", 0.0)),
                diagnostic_session.dropped_events,
            ]
        perf.text = summary_text
func _log_live_perf(delta: float, tick_ms: float, update_ms: float) -> void:
    if not _should_emit_runtime_perf_logs():
        return
    perf_log_accum += delta
    if perf_log_accum < perf_log_interval:
        return
    perf_log_accum = 0.0
    var memory := _runtime_memory_snapshot()
    var line := "live_perf fps=%d frame_ms=%.2f tick_ms=%.2f update_ms=%.2f texture=%s size=%dx%d renderer=\"%s\" errors=%d app_mb=%d resident_mb=%d gpu_mb=%d gpu_tex_mb=%d cache_mb=%d graphic_cache_mb=%d xp3_cache_mb=%d psb_cache_mb=%d psb_entries=%d" % [
        Engine.get_frames_per_second(),
        delta * 1000.0,
        tick_ms,
        update_ms,
        player.get_frame_texture_backend(),
        last_texture_size.x,
        last_texture_size.y,
        player.get_renderer_info(),
        render_errors,
        int(memory.get("current_bytes", 0) / (1024 * 1024)),
        int(memory.get("resident_bytes", 0) / (1024 * 1024)),
        int(memory.get("gpu_total_bytes", 0) / (1024 * 1024)),
        int(memory.get("gpu_texture_bytes", 0) / (1024 * 1024)),
        int(memory.get("cache_bytes", 0) / (1024 * 1024)),
        int(memory.get("graphic_cache_bytes", 0) / (1024 * 1024)),
        int(memory.get("xp3_segment_cache_bytes", 0) / (1024 * 1024)),
        int(memory.get("psb_cache_bytes", 0) / (1024 * 1024)),
        int(memory.get("psb_cache_entries", 0)),
    ]
    print(line)
    if perf_log_file != null:
        perf_log_file.store_line(line)
        perf_log_file.flush()

func _log_frame_spike(delta: float, tick_ms: float, update_ms: float) -> void:
    if frame_spike_ms <= 0.0:
        return
    var frame_ms := delta * 1000.0
    var work_ms := tick_ms + update_ms
    if frame_ms < frame_spike_ms and work_ms < frame_spike_ms:
        return
    var line := "frame_spike fps=%d frame_ms=%.2f tick_ms=%.2f update_ms=%.2f texture=%s size=%dx%d renderer=\"%s\" errors=%d" % [
        Engine.get_frames_per_second(),
        frame_ms,
        tick_ms,
        update_ms,
        player.get_frame_texture_backend(),
        last_texture_size.x,
        last_texture_size.y,
        player.get_renderer_info(),
        render_errors,
    ]
    print(line)
    if perf_log_file != null:
        perf_log_file.store_line(line)
        perf_log_file.flush()

func _should_log_tick_trace() -> bool:
    return input_trace_enabled and Time.get_ticks_msec() < tick_trace_until_msec

func _arm_tick_trace() -> void:
    if input_trace_enabled:
        tick_trace_until_msec = maxi(tick_trace_until_msec, Time.get_ticks_msec() + 1200)

func _log_tick_trace(line: String) -> void:
    if not input_trace_enabled:
        return
    print(line)
    if perf_log_file != null:
        perf_log_file.store_line(line)
        perf_log_file.flush()

func _log_input_diagnostic_line(line: String) -> void:
    if not input_trace_enabled:
        return
    print(line)
    _write_probe_marker(line)
    if perf_log_file != null:
        perf_log_file.store_line(line)
        perf_log_file.flush()

func _trace_touch_route(
    action: String,
    pointer_id: int,
    mapped: Vector2,
    detail: String = ""
) -> void:
    if not input_trace_enabled:
        return
    _log_input_diagnostic_line(
        "touch_route action=%s pid=%d mapped=%.1f,%.1f pending=%d active=%s suppressed=%s delayed=%s detail=%s" % [
            action,
            pointer_id,
            mapped.x,
            mapped.y,
            pending_touch_index,
            JSON.stringify(active_touch_points.keys()),
            JSON.stringify(suppressed_touch_points.keys()),
            JSON.stringify(delayed_touch_releases.keys()),
            detail,
        ]
    )

func _queue_artemis_input_state_trace(label: String) -> void:
    if not input_trace_enabled or active_runtime_kind != RUNTIME_KIRIKIRI:
        return
    artemis_input_trace_sequence += 1
    var now := Time.get_ticks_msec()
    for delay_variant in ARTEMIS_INPUT_TRACE_DELAYS_MS:
        var delay_ms := int(delay_variant)
        artemis_input_trace_samples.append({
            "sequence": artemis_input_trace_sequence,
            "label": label,
            "delay_ms": delay_ms,
            "due_msec": now + delay_ms,
        })
    _flush_artemis_input_trace_samples()

func _renderer_trace_stat(renderer: String, name: String) -> String:
    var marker := "%s=" % name
    var start := renderer.find(marker)
    if start < 0:
        return ""
    start += marker.length()
    var end := renderer.find(" ", start)
    if end < 0:
        end = renderer.length()
    return renderer.substr(start, end - start)

func _flush_artemis_input_trace_samples() -> void:
    if artemis_input_trace_samples.is_empty() or player == null:
        return
    if not game_running or active_runtime_kind != RUNTIME_KIRIKIRI:
        artemis_input_trace_samples.clear()
        return
    var now := Time.get_ticks_msec()
    var remaining: Array[Dictionary] = []
    for sample in artemis_input_trace_samples:
        if now < int(sample.get("due_msec", now)):
            remaining.append(sample)
            continue
        var runtime_debug := String(player.get_plugin_debug_info())
        var parsed = JSON.parse_string(runtime_debug)
        if not parsed is Dictionary:
            _log_input_diagnostic_line(
                "artemis_input_state sequence=%d label=%s after_ms=%d parse_failed=1" % [
                    int(sample.get("sequence", 0)),
                    String(sample.get("label", "")),
                    int(sample.get("delay_ms", 0)),
                ]
            )
            continue
        var debug: Dictionary = parsed
        var state := {
            "runtime": debug.get("runtime", ""),
            "scriptState": debug.get("scriptState", ""),
            "scriptWaitReason": debug.get("scriptWaitReason", ""),
            "waitFlag": debug.get("waitFlag", ""),
            "delayFlag": debug.get("delayFlag", ""),
            "textClickFlag": debug.get("textClickFlag", ""),
            "clickFlag": debug.get("clickFlag", ""),
            "exclickFlag": debug.get("exclickFlag", ""),
            "transitionFlag": debug.get("transitionFlag", ""),
            "keycodeFlag": debug.get("keycodeFlag", ""),
            "buttonName": debug.get("buttonName", ""),
            "buttonClick": debug.get("buttonClick", ""),
            "buttonEntry": debug.get("buttonEntry", ""),
            "buttonStop": debug.get("buttonStop", ""),
            "localInput2": debug.get("localInput2", ""),
            "externalWaitReason": debug.get("externalWaitReason", ""),
            "queuedCommands": debug.get("queuedCommands", 0),
            "eventResumeStates": debug.get("eventResumeStates", 0),
            "lastInputDebug": debug.get("lastInputDebug", ""),
            "inputDispatchDebug": debug.get("inputDispatchDebug", ""),
            "overrideDebug": debug.get("overrideDebug", ""),
            "frameSerial": debug.get("frameSerial", ""),
            "uiEvents": debug.get("uiEvents", ""),
            "scriptStack": debug.get("scriptStack", []),
            "touchPending": pending_touch_index,
            "touchPendingQuarantined": pending_touch_quarantined,
            "touchActive": active_touch_points.keys(),
            "touchSuppressed": suppressed_touch_points.keys(),
            "touchDelayed": delayed_touch_releases.keys(),
            "touchSecondaryQuarantineRemainingMs": maxi(
                0,
                touch_secondary_quarantine_until_msec - Time.get_ticks_msec()
            ),
        }
        if debug.has("commandTrace") and debug["commandTrace"] is Array:
            var command_trace: Array = debug["commandTrace"]
            state["commandTraceTail"] = command_trace.slice(
                maxi(0, command_trace.size() - 6)
            )
        var renderer := String(player.get_renderer_info())
        state["bridgeInputs"] = _renderer_trace_stat(renderer, "inputs")
        state["bridgeCoalescedInputs"] = _renderer_trace_stat(
            renderer,
            "coalesced_inputs"
        )
        _log_input_diagnostic_line(
            "artemis_input_state sequence=%d label=%s after_ms=%d tick=%d state=%s" % [
                int(sample.get("sequence", 0)),
                String(sample.get("label", "")),
                int(sample.get("delay_ms", 0)),
                tick_trace_serial,
                JSON.stringify(state),
            ]
        )
    artemis_input_trace_samples = remaining

func _log_frame_probe(delta: float) -> void:
    if not frame_probe_enabled:
        return
    frame_probe_accum += delta
    if frame_probe_accum < frame_probe_interval:
        return
    frame_probe_accum = 0.0
    var frame: Dictionary = player.read_frame_rgba()
    var line := "frame_probe texture=%s size=%dx%d serial=%d stats=%s renderer=\"%s\" errors=%d" % [
        player.get_frame_texture_backend(),
        int(frame.get("width", 0)),
        int(frame.get("height", 0)),
        int(frame.get("frame_serial", 0)),
        JSON.stringify(_frame_stats(frame)),
        player.get_renderer_info(),
        render_errors,
    ]
    print(line)
    if perf_log_file != null:
        perf_log_file.store_line(line)
        perf_log_file.flush()

func _log_input_trace(delta: float, tick_ms: float, update_ms: float) -> void:
    if not input_trace_enabled:
        return
    input_trace_accum += delta
    if input_trace_accum < 0.5:
        return
    if input_trace_received == 0 and input_trace_blocked == 0 and input_trace_throttled == 0 and input_trace_busy == 0 and input_trace_present_holds == 0 and input_trace_move_suppressed == 0:
        input_trace_accum = 0.0
        return
    var line := "input_probe fps=%d frame_ms=%.2f tick_ms=%.2f update_ms=%.2f recv=%d fwd=%d blocked=%d throttled=%d busy=%d move_suppressed=%d outside=%d send_failed=%d active_touch=%d suppressed=%d present_holds=%d texture=%s renderer=\"%s\"" % [
        Engine.get_frames_per_second(),
        delta * 1000.0,
        tick_ms,
        update_ms,
        input_trace_received,
        input_trace_forwarded,
        input_trace_blocked,
        input_trace_throttled,
        input_trace_busy,
        input_trace_move_suppressed,
        input_trace_outside,
        input_trace_send_failed,
        active_touch_points.size(),
        suppressed_touch_points.size(),
        input_trace_present_holds,
        player.get_frame_texture_backend(),
        player.get_renderer_info(),
    ]
    print(line)
    if perf_log_file != null:
        perf_log_file.store_line(line)
        perf_log_file.flush()
    input_trace_accum = 0.0
    input_trace_received = 0
    input_trace_forwarded = 0
    input_trace_blocked = 0
    input_trace_throttled = 0
    input_trace_busy = 0
    input_trace_move_suppressed = 0
    input_trace_outside = 0
    input_trace_send_failed = 0
    input_trace_present_holds = 0

func _notification(what: int) -> void:
    if what == NOTIFICATION_RESIZED:
        _fit_full_rects()
        _queue_settings_relayout_after_resize()
        _queue_detail_relayout_after_resize()
        return
    if player == null:
        return
    if what == NOTIFICATION_APPLICATION_PAUSED or what == NOTIFICATION_APPLICATION_FOCUS_OUT:
        if diagnostic_session != null:
            diagnostic_session.record("godot", "lifecycle", "info", "application_paused", 0, {"notification": what})
        if video_playing:
            active_video_was_playing = int(active_video_state.get("status", 0)) == MEDIA_STATUS_PLAYING
            _store_active_video_progress()
            player.media_pause()
        _pause_game_for_lifecycle("notification_%d" % what)
        return
    if what == NOTIFICATION_APPLICATION_RESUMED or what == NOTIFICATION_APPLICATION_FOCUS_IN:
        if diagnostic_session != null:
            diagnostic_session.record("godot", "lifecycle", "info", "application_resumed", 0, {"notification": what})
        if video_playing and active_video_was_playing:
            player.media_play()
            active_video_was_playing = false
        _resume_game_for_lifecycle("notification_%d" % what)
        return
    if what == NOTIFICATION_WM_CLOSE_REQUEST:
        _deactivate_game_text_input()
        if video_playing:
            _store_active_video_progress()
            player.media_close()
            video_playing = false
        if app_lifecycle_paused:
            player.resume()
            app_lifecycle_paused = false
        _clear_game_input_capture()
        _finalize_active_game_session()
        if diagnostic_session != null:
            diagnostic_session.finish()
        viewport.texture = null
        player.release_frame_texture()
        player.destroy_engine()

func _pause_game_for_lifecycle(reason: String) -> void:
    game_text_input_suspended = true
    _deactivate_game_text_input()
    if not _is_touch_platform():
        return
    if app_lifecycle_paused or not game_running or cached_startup_state != STARTUP_SUCCEEDED:
        return
    _clear_game_input_capture()
    var result: int = int(player.pause())
    if result != ENGINE_RESULT_OK:
        render_errors += 1
        _log_lifecycle_line("app_pause_failed reason=%s result=%s error=\"%s\"" % [
            reason,
            player.get_last_result(),
            player.get_last_error(),
        ])
        return
    app_lifecycle_paused = true
    _log_lifecycle_line("app_paused reason=%s" % reason)

func _resume_game_for_lifecycle(reason: String) -> void:
    game_text_input_suspended = false
    if not _is_touch_platform():
        return
    if not app_lifecycle_paused:
        return
    var result: int = int(player.resume())
    if result != ENGINE_RESULT_OK:
        render_errors += 1
        _log_lifecycle_line("app_resume_failed reason=%s result=%s error=\"%s\"" % [
            reason,
            player.get_last_result(),
            player.get_last_error(),
        ])
        return
    app_lifecycle_paused = false
    _clear_game_input_capture()
    _log_lifecycle_line("app_resumed reason=%s" % reason)

func _log_lifecycle_line(line: String) -> void:
    print(line)
    _write_probe_marker(line)
    if perf_log_file != null:
        perf_log_file.store_line(line)
        perf_log_file.flush()

func _on_backend_selected(index: int) -> void:
    selected_backend = BACKENDS[index]
    ProjectSettings.set_setting(SETTINGS_KEY, selected_backend)
    if game_running:
        restart_notice.text = "Restart current game session to apply renderer."
        _append_log("Renderer change queued: %s" % selected_backend)
        return
    _apply_backend(true)

func _apply_backend(log_selection: bool) -> void:
    var result: int = int(player.set_render_backend(selected_backend))
    if result != ENGINE_RESULT_OK:
        render_errors += 1
        var backend_error_message := "Renderer selection failed: %s %s" % [
            player.get_last_result(),
            player.get_last_error(),
        ]
        _append_log(backend_error_message)
        return
    restart_notice.text = ""
    if log_selection:
        _append_log("Renderer selected: %s" % selected_backend)
    if selected_backend == "GPU Bridge":
        _append_log("GPU Bridge imports the native GPU render target for display.")
    if selected_backend == "Debug CPU":
        _append_log("Debug CPU fallback enabled by user selection.")

func _renderer_fallback(renderer: String) -> String:
    if renderer.is_empty():
        return "pending" if game_running else "none"
    var marker := "fallback="
    var start := renderer.find(marker)
    if start < 0:
        return "unknown" if game_running else "none"
    start += marker.length()
    var end := renderer.find(" ", start)
    if end < 0:
        end = renderer.length()
    var summary := renderer.substr(start, end - start)
    var fallback_ops := _renderer_value(renderer, "fallback_ops")
    var gpu_ops := _renderer_value(renderer, "gpu_ops")
    if not fallback_ops.is_empty() or not gpu_ops.is_empty():
        summary += " (CPU:%s GPU:%s)" % [
            fallback_ops if not fallback_ops.is_empty() else "?",
            gpu_ops if not gpu_ops.is_empty() else "?",
        ]
    return summary

func _renderer_value(renderer: String, key: String) -> String:
    var marker := key + "="
    var start := renderer.find(marker)
    if start < 0:
        return ""
    start += marker.length()
    var end := renderer.find(" ", start)
    if end < 0:
        end = renderer.length()
    return renderer.substr(start, end - start)

func _renderer_summary(renderer: String) -> String:
    if renderer.is_empty():
        return selected_backend
    var summary := selected_backend
    if renderer.contains("backend=onscripter_yuri"):
        summary = "OnscripterYuri (Godot Texture)"
    elif renderer.contains("backend=godot_native"):
        summary = "Godot Native GPU"
    elif renderer.contains("backend=gpu_bridge"):
        summary = "GPU Bridge"
    elif renderer.contains("backend=debug_cpu"):
        summary = "Debug CPU"
    var driver := _renderer_value(renderer, "godot_driver")
    var method := _renderer_value(renderer, "godot_method")
    if not driver.is_empty() or not method.is_empty():
        summary += " (%s/%s)" % [
            driver if not driver.is_empty() else "unknown",
            method if not method.is_empty() else "unknown",
        ]
    return summary


func _on_open_game() -> void:
    if not _require_legal_documents_for_media():
        return
    var requested_path := game_path.text.strip_edges()
    var path := _resolve_game_path(requested_path)
    if path != requested_path:
        _write_probe_marker("open_game remapped_path=%s requested=%s" % [path, requested_path])
        _append_log("Remapped iOS game path: %s" % path)
        game_path.text = path
    _write_probe_marker("open_game path=%s" % path)
    if path.is_empty():
        render_errors += 1
        _append_log("Game path is empty.")
        return

    var detected_runtime := _game_runtime_kind(path)
    if not _switch_runtime_player(detected_runtime):
        render_errors += 1
        return
    active_runtime_kind = detected_runtime
    if GameLaunchEntry.runtime_uses_directory(detected_runtime):
        path = _game_runtime_root(path)
        game_path.text = path
    _load_button_position_memory(path)
    if (
        detected_runtime == RUNTIME_ONSCRIPTER
        and auto_probe_clicks.is_empty()
        and _runtime_flag("AETHERKIRI_AUTO_PROBE_REMEMBERED_CLICKS")
    ):
        auto_probe_clicks = remembered_button_positions.duplicate()
        device_probe_enabled = device_probe_enabled or not auto_probe_clicks.is_empty()

    if not _ensure_player_initialized():
        return

    ProjectSettings.set_setting(GAME_PATH_KEY, path)
    _apply_backend(false)
    _apply_engine_options()
    if diagnostic_session != null:
        # A natural in-game exit destroys the reusable native engine handle.
        # Start diagnostics again when the next title recreates that handle.
        diagnostic_session.start(player, selected_backend)
    _sync_player_surface_size(true)
    cached_startup_state = STARTUP_RUNNING
    startup_poll_accum = STARTUP_POLL_INTERVAL

    var async_open := OS.get_environment("AETHERKIRI_SYNC_OPEN") != "1"
    var result: int = int(player.open_game(path, async_open))
    if result != ENGINE_RESULT_OK:
        render_errors += 1
        cached_startup_state = STARTUP_FAILED
        _write_probe_marker("open_game_failed result=%s error=%s" % [
            player.get_last_result(),
            player.get_last_error(),
        ])
        var launch_error_message := "Game launch failed: %s %s" % [
            player.get_last_result(),
            player.get_last_error(),
        ]
        _append_log(launch_error_message)
        return

    game_running = true
    if diagnostic_session != null:
        diagnostic_session.set_game_active(true)
        diagnostic_session.record("godot", "lifecycle", "info", "game_open_requested", 0, {
            "path": path,
            "backend": selected_backend,
            "async": async_open,
        })
    _sync_debug_console_state()
    app_lifecycle_paused = false
    log_lines.clear()
    log_view_dirty = false
    log_view_flush_accum = 0.0
    _clear_game_input_capture()
    if log_view != null:
        log_view.text = ""
        log_view.scroll_vertical = 0
    last_texture_size = Vector2i.ZERO
    last_source_texture_size = Vector2i.ZERO
    present_hold_frames = 0
    capture_after_open_done = false
    capture_after_open_ready_usec = 0
    auto_probe_running = false
    auto_probe_done = false
    startup_click_stream_running = false
    startup_click_stream_done = false
    last_renderer_info_logged = ""
    restart_notice.text = "Starting..."
    _append_log("Game launch requested with backend: %s" % selected_backend)
    _append_log("Surface mode: %s target=%dx%d texture=%dx%d" % [
        render_surface_mode,
        current_surface_size.x,
        current_surface_size.y,
        last_texture_size.x,
        last_texture_size.y,
    ])
    _append_log("Path: %s" % path)
    if startup_click_stream_enabled and not startup_click_stream_running and not startup_click_stream_done:
        startup_click_stream_running = true
        call_deferred("_run_startup_click_stream_probe")

func _desired_render_surface_size() -> Vector2i:
    var base_size := _base_render_surface_size()
    if output_resolution != "original":
        return _frame_output_target_size(base_size)
    if render_surface_mode == RENDER_SURFACE_MODE_GAME:
        return base_size
    var window_size := DisplayServer.window_get_size()
    if window_size.x < 1 or window_size.y < 1:
        return base_size
    var pixel_scale := _surface_pixel_scale()
    var target_pixel_size := Vector2(
        float(window_size.x) * pixel_scale,
        float(window_size.y) * pixel_scale
    )
    var scale := minf(
        target_pixel_size.x / float(base_size.x),
        target_pixel_size.y / float(base_size.y)
    )
    if scale <= 0.0:
        return base_size
    scale = minf(
        scale,
        minf(
            float(render_surface_max_size.x) / float(base_size.x),
            float(render_surface_max_size.y) / float(base_size.y)
        )
    )
    return Vector2i(
        maxi(1, int(round(float(base_size.x) * scale))),
        maxi(1, int(round(float(base_size.y) * scale)))
    )

func _frame_output_resolution_limit() -> Vector2i:
    var normalized := _normalize_output_resolution(output_resolution)
    if normalized == "original":
        return Vector2i.ZERO
    var preset: Vector2i = OUTPUT_RESOLUTION_LIMITS.get(
        normalized,
        OUTPUT_RESOLUTION_LIMITS[OUTPUT_RESOLUTION_DEFAULT]
    )
    return Vector2i(
        clampi(preset.x, 1, render_surface_max_size.x),
        clampi(preset.y, 1, render_surface_max_size.y)
    )

func _fit_frame_size_within(source_size: Vector2i, bounds: Vector2i) -> Vector2i:
    if bounds.x <= 0 or bounds.y <= 0:
        return Vector2i.ZERO
    if source_size.x <= 0 or source_size.y <= 0:
        return bounds
    var scale := minf(
        float(bounds.x) / float(source_size.x),
        float(bounds.y) / float(source_size.y)
    )
    return Vector2i(
        maxi(1, int(round(float(source_size.x) * scale))),
        maxi(1, int(round(float(source_size.y) * scale)))
    )

func _frame_output_target_size(source_size: Vector2i) -> Vector2i:
    if _normalize_output_resolution(output_resolution) == "original":
        return source_size
    return _fit_frame_size_within(source_size, _frame_output_resolution_limit())

func _base_render_surface_size() -> Vector2i:
    return Vector2i(
        clampi(render_surface_base_size.x, 1, render_surface_max_size.x),
        clampi(render_surface_base_size.y, 1, render_surface_max_size.y)
    )

func _surface_pixel_scale() -> float:
    var env_scale := OS.get_environment("AETHERKIRI_SURFACE_PIXEL_SCALE").strip_edges()
    if not env_scale.is_empty():
        return clampf(env_scale.to_float(), 0.5, 4.0)
    if OS.get_name() == "macOS":
        var screen := DisplayServer.window_get_current_screen()
        return clampf(DisplayServer.screen_get_scale(screen), 1.0, 4.0)
    return 1.0

func _env_vector2i(key: String, fallback: Vector2i) -> Vector2i:
    var value := OS.get_environment(key).strip_edges().to_lower()
    if value.is_empty():
        return fallback
    value = value.replace("x", ",")
    var parts := value.split(",", false)
    if parts.size() != 2:
        return fallback
    var width := int(parts[0])
    var height := int(parts[1])
    if width <= 0 or height <= 0:
        return fallback
    return Vector2i(width, height)

func _sync_player_surface_size(force: bool) -> void:
    if player == null:
        return
    var target_size := _desired_render_surface_size()
    if not force and target_size == current_surface_size:
        return
    var result: int = int(player.set_surface_size(target_size.x, target_size.y))
    if result != ENGINE_RESULT_OK:
        render_errors += 1
        var surface_error_message := "Surface resize failed: %s %s" % [
            player.get_last_result(),
            player.get_last_error(),
        ]
        _append_log(surface_error_message)
        return
    if current_surface_size != target_size:
        last_texture_size = Vector2i.ZERO
        last_source_texture_size = Vector2i.ZERO
        var window_size := DisplayServer.window_get_size()
        var screen := DisplayServer.window_get_current_screen()
        var base_size := _base_render_surface_size()
        var line := "surface_resize mode=%s window=%dx%d screen_scale=%.2f base=%dx%d target=%dx%d max=%dx%d" % [
            render_surface_mode,
            window_size.x,
            window_size.y,
            DisplayServer.screen_get_scale(screen),
            base_size.x,
            base_size.y,
            target_size.x,
            target_size.y,
            render_surface_max_size.x,
            render_surface_max_size.y,
        ]
        print(line)
        _write_probe_marker(line)
        if perf_log_file != null:
            perf_log_file.store_line(line)
            perf_log_file.flush()
    current_surface_size = target_size

func _sync_game_surface_to_texture(texture_size: Vector2i) -> void:
    if render_surface_mode != RENDER_SURFACE_MODE_GAME:
        return
    if not follow_texture_surface_size:
        return
    if player == null or texture_size.x <= 0 or texture_size.y <= 0:
        return
    var target_size := Vector2i(
        clampi(texture_size.x, 1, render_surface_max_size.x),
        clampi(texture_size.y, 1, render_surface_max_size.y)
    )
    if target_size == current_surface_size:
        return
    render_surface_base_size = target_size
    var result: int = int(player.set_surface_size(target_size.x, target_size.y))
    if result != ENGINE_RESULT_OK:
        render_errors += 1
        _append_log("Surface follow frame failed: %s %s" % [
            player.get_last_result(),
            player.get_last_error(),
        ])
        return
    current_surface_size = target_size
    var line := "surface_follow_frame texture=%dx%d target=%dx%d max=%dx%d" % [
        texture_size.x,
        texture_size.y,
        target_size.x,
        target_size.y,
        render_surface_max_size.x,
        render_surface_max_size.y,
    ]
    print(line)
    _write_probe_marker(line)
    if perf_log_file != null:
        perf_log_file.store_line(line)
        perf_log_file.flush()

func _drain_logs() -> void:
    var logs: String = String(player.drain_startup_logs())
    if logs.is_empty():
        return
    var process_runtime_logs := _should_process_runtime_logs()
    for line in logs.split("\n", false):
        # Always process native control messages. Ordinary engine logs remain
        # gated by the diagnostics/UI settings to avoid unnecessary UI work.
        if process_runtime_logs or line.contains("[ALERT_DIALOG]"):
            _append_log(line)

func _should_process_runtime_logs() -> bool:
    return diagnostics_enabled or ui_log_enabled or error_dialog_logs

func _should_collect_log_lines() -> bool:
    return true

func _should_emit_runtime_perf_logs() -> bool:
    return diagnostics_enabled or perf_log_file != null

func _flush_log_view_if_needed(delta: float) -> void:
    if not ui_log_enabled or not log_view_dirty or log_view == null:
        return
    if game_running and not log_view.is_visible_in_tree():
        return
    log_view_flush_accum += delta
    if game_running and log_view_flush_accum < UI_LOG_FLUSH_INTERVAL:
        return
    _flush_log_view()

func _flush_log_view() -> void:
    if log_view == null:
        return
    log_view_flush_accum = 0.0
    log_view_dirty = false
    log_view.text = "\n".join(log_lines)
    call_deferred("_scroll_log_to_bottom")

func _frame_enhancement_target_size() -> Vector2i:
    # The resolution selector is the single target for both the engine's
    # surface scaler and the private enhancement scaler. Restore still runs
    # once at source size; only EASU/Bicubic/Lanczos sees this target.
    var source_size := last_source_texture_size
    if source_size.x <= 0 or source_size.y <= 0:
        if output_resolution == "original":
            # A zero target asks the host to use the source texture dimensions,
            # avoiding a speculative 1080p allocation for the first frame.
            return Vector2i.ZERO
        source_size = _base_render_surface_size()
    return _frame_output_target_size(source_size)

func _game_input_content_size() -> Vector2:
    # With enhancement enabled the host publishes the raw game frame so it is
    # processed exactly once. This is the aspect ratio actually visible inside
    # GameViewport and therefore the first coordinate space for pointer input.
    if last_source_texture_size.x > 0 and last_source_texture_size.y > 0:
        return Vector2(last_source_texture_size)
    return Vector2(maxi(1, last_texture_size.x), maxi(1, last_texture_size.y))

func _game_input_surface_size() -> Vector2:
    # ONS and Minori consume coordinates in their published content space.
    if active_runtime_kind in [RUNTIME_ONSCRIPTER, RUNTIME_MINORI]:
        return _game_input_content_size()
    if current_surface_size.x > 0 and current_surface_size.y > 0:
        return Vector2(current_surface_size)
    return _game_input_content_size()

func _update_frame() -> void:
    if present_hold_frames > 0:
        present_hold_frames -= 1
        return
    if player.has_method("set_frame_enhancement_target_size"):
        var enhancement_target := _frame_enhancement_target_size()
        player.set_frame_enhancement_target_size(
            enhancement_target.x,
            enhancement_target.y
        )
    var texture: Texture2D = player.update_frame_texture()
    if texture != null:
        if _should_hold_suspect_black_frame():
            return
        viewport.texture = texture
        viewport.queue_redraw()
        last_texture_size = Vector2i(texture.get_width(), texture.get_height())
        if player.has_method("get_frame_source_size"):
            var source_size: Vector2i = player.get_frame_source_size()
            if source_size.x > 0 and source_size.y > 0:
                last_source_texture_size = source_size
        if last_source_texture_size.x <= 0 or last_source_texture_size.y <= 0:
            last_source_texture_size = last_texture_size
        _sync_game_surface_to_texture(last_source_texture_size)
        _layout_game_viewport(get_viewport_rect().size)
        if not auto_probe_clicks.is_empty() and not auto_probe_running and not auto_probe_done:
            auto_probe_running = true
            call_deferred("_run_auto_probe")
        if not capture_after_open_path.is_empty() and not capture_after_open_done:
            if capture_after_open_ready_usec == 0:
                capture_after_open_ready_usec = Time.get_ticks_usec() + int(capture_after_open_delay_sec * 1000000.0)
            if Time.get_ticks_usec() < capture_after_open_ready_usec:
                return
            capture_after_open_done = true
            var frame_stats := {
                "source": "viewport_texture",
                "texture_width": last_texture_size.x,
                "texture_height": last_texture_size.y,
                "texture_backend": player.get_frame_texture_backend(),
            }
            call_deferred("_capture_main_view", frame_stats)

func _capture_main_view(frame_stats: Dictionary) -> void:
    await get_tree().process_frame
    await get_tree().process_frame
    var image := get_viewport().get_texture().get_image()
    var screenshot_stats := _image_stats(image)
    var output_path := capture_after_open_path
    if output_path.is_empty():
        output_path = _default_output_path("main_render_probe.png")
    image.save_png(output_path)
    _write_probe_marker("capture output=%s stats=%s" % [
        output_path,
        JSON.stringify(screenshot_stats),
    ])
    print("main probe renderer=\"%s\" texture_backend=%s texture_width=%d frame_stats=%s screenshot=%s screenshot_stats=%s" % [
        player.get_renderer_info(),
        player.get_frame_texture_backend(),
        last_texture_size.x,
        JSON.stringify(frame_stats),
        output_path,
        JSON.stringify(screenshot_stats),
    ])
    if OS.get_environment("AETHERKIRI_QUIT_AFTER_CAPTURE") == "1":
        var visible := int(screenshot_stats.get("visible", 0))
        get_tree().quit(0 if visible > 0 else 2)

func _clear_game_input_capture() -> void:
    if game_virtual_controls != null:
        game_virtual_controls.set_enabled(false)
    _deactivate_game_text_input()
    active_touch_points.clear()
    active_mouse_buttons.clear()
    suppressed_touch_points.clear()
    touch_down_points.clear()
    dragging_touch_points.clear()
    pending_touch_index = -1
    pending_touch_mapped = Vector2.ZERO
    pending_touch_down_msec = 0
    pending_touch_quarantined = false
    delayed_touch_releases.clear()
    last_forwarded_touch_move_msec_by_id.clear()
    last_forwarded_touch_down_msec = 0
    last_forwarded_touch_up_msec = 0
    touch_secondary_quarantine_until_msec = 0
    suppress_mouse_until_msec = 0
    present_hold_frames = 0
    last_present_hold_msec = 0
    tick_trace_until_msec = 0
    tick_trace_active_serial = 0
    artemis_input_trace_samples.clear()
    input_trace_accum = 0.0
    input_trace_received = 0
    input_trace_forwarded = 0
    input_trace_blocked = 0
    input_trace_throttled = 0
    input_trace_busy = 0
    input_trace_move_suppressed = 0
    input_trace_outside = 0
    input_trace_send_failed = 0
    input_trace_present_holds = 0
    touch_input_busy_until_msec = 0
    black_frame_guard_until_msec = 0
    black_frame_next_sample_msec = 0
    black_frame_consecutive = 0
    black_frame_last_log_msec = 0

func _run_auto_probe() -> void:
    await _auto_probe_wait_frames(_runtime_int("AETHERKIRI_AUTO_PROBE_WARMUP_FRAMES", 180))
    await _save_auto_probe_step(0, "startup")
    var step := 1
    for pos in auto_probe_clicks:
        _send_probe_click(pos)
        await _auto_probe_wait_frames(_runtime_int("AETHERKIRI_AUTO_PROBE_AFTER_CLICK_FRAMES", 180))
        await _save_auto_probe_step(step, "click_%d_%d" % [int(pos.x), int(pos.y)])
        step += 1
    auto_probe_done = true
    auto_probe_running = false
    _write_probe_marker("auto_probe_done steps=%d renderer=%s" % [
        step,
        player.get_renderer_info(),
    ])
    if _runtime_flag("AETHERKIRI_QUIT_AFTER_AUTO_PROBE"):
        get_tree().quit(0)

func _run_startup_click_stream_probe() -> void:
    var warmup_frames: int = max(
        0,
        _runtime_int("AETHERKIRI_STARTUP_CLICK_STREAM_WARMUP_FRAMES", 0)
    )
    var frames: int = max(1, _runtime_int("AETHERKIRI_STARTUP_CLICK_STREAM_FRAMES", 240))
    var post_frames: int = max(
        0,
        _runtime_int("AETHERKIRI_STARTUP_CLICK_STREAM_POST_FRAMES", 0)
    )
    var clicks_per_frame: int = max(1, _runtime_int("AETHERKIRI_STARTUP_CLICK_STREAM_CLICKS_PER_FRAME", 1))
    var capture_every: int = max(0, _runtime_int("AETHERKIRI_STARTUP_CLICK_STREAM_CAPTURE_EVERY", 60))
    var click_pos: Vector2 = Vector2(
        _runtime_float("AETHERKIRI_STARTUP_CLICK_STREAM_X", float(INITIAL_WINDOW_SIZE.x) * 0.5),
        _runtime_float("AETHERKIRI_STARTUP_CLICK_STREAM_Y", float(INITIAL_WINDOW_SIZE.y) * 0.5)
    )
    var attempted: int = 0
    var forwarded: int = 0
    var blocked: int = 0
    var busy: int = 0
    var start_usec: int = Time.get_ticks_usec()
    for frame_index in range(warmup_frames):
        await get_tree().process_frame
    for frame_index in range(frames):
        for i in range(clicks_per_frame):
            attempted += 1
            if _can_forward_game_input():
                if _send_startup_probe_mouse_click(click_pos):
                    forwarded += 1
                else:
                    blocked += 1
            else:
                if _is_game_input_busy():
                    _trace_input_busy()
                    busy += 1
                else:
                    _trace_input_blocked()
                    blocked += 1
        if capture_every > 0 and (frame_index % capture_every) == 0:
            _save_startup_click_stream_capture(frame_index)
        await get_tree().process_frame
    for frame_index in range(post_frames):
        await get_tree().process_frame
    var elapsed_sec := float(Time.get_ticks_usec() - start_usec) / 1000000.0
    var line := "startup_click_stream warmup_frames=%d frames=%d post_frames=%d clicks_per_frame=%d attempted=%d forwarded=%d blocked=%d busy=%d elapsed_sec=%.3f fps=%.2f renderer=\"%s\" texture=%s size=%dx%d" % [
        warmup_frames,
        frames,
        post_frames,
        clicks_per_frame,
        attempted,
        forwarded,
        blocked,
        busy,
        elapsed_sec,
        float(frames) / maxf(0.0001, elapsed_sec),
        player.get_renderer_info(),
        player.get_frame_texture_backend(),
        last_texture_size.x,
        last_texture_size.y,
    ]
    print(line)
    _write_probe_marker(line)
    if perf_log_file != null:
        perf_log_file.store_line(line)
        perf_log_file.flush()
    _save_startup_click_stream_capture(frames)
    startup_click_stream_done = true
    startup_click_stream_running = false
    if _runtime_flag("AETHERKIRI_QUIT_AFTER_STARTUP_CLICK_STREAM"):
        get_tree().quit(0)

func _can_write_probe_files() -> bool:
    if OS.get_name() != "iOS":
        return true
    return not cli_probe_script.is_empty() or _runtime_flag("AETHERKIRI_IOS_FILE_LOG")

func _send_startup_probe_mouse_click(pos: Vector2) -> bool:
    var down := InputEventMouseButton.new()
    down.button_index = MOUSE_BUTTON_LEFT
    down.pressed = true
    down.position = pos
    down.global_position = pos
    _handle_game_pointer_event(down)
    var down_captured := active_mouse_buttons.has(down.button_index)

    var up := InputEventMouseButton.new()
    up.button_index = MOUSE_BUTTON_LEFT
    up.pressed = false
    up.position = pos
    up.global_position = pos
    _handle_game_pointer_event(up)
    return down_captured

func _save_startup_click_stream_capture(frame_index: int) -> void:
    if not _can_write_probe_files():
        return
    var texture := get_viewport().get_texture()
    if texture == null:
        return
    var image := texture.get_image()
    if image == null:
        return
    var prefix := _runtime_string("AETHERKIRI_STARTUP_CLICK_STREAM_PREFIX", "/tmp/aetherkiri-startup-click-stream")
    var path := "%s-%03d.png" % [prefix, frame_index]
    image.save_png(path)
    var line := "startup_click_stream_capture frame=%d output=%s stats=%s" % [
        frame_index,
        path,
        JSON.stringify(_image_stats(image)),
    ]
    print(line)
    if perf_log_file != null:
        perf_log_file.store_line(line)
        perf_log_file.flush()

func _auto_probe_wait_frames(frames: int) -> void:
    for i in range(max(1, frames)):
        await get_tree().process_frame

func _save_auto_probe_step(index: int, label: String) -> void:
    await get_tree().process_frame
    await get_tree().process_frame
    var frame: Dictionary = player.read_frame_rgba()
    var frame_stats := _frame_stats(frame)
    var image := get_viewport().get_texture().get_image()
    var screenshot_stats := _image_stats(image)
    var path := _default_output_path("aetherkiri-auto-step-%02d-%s.png" % [index, label])
    if _can_write_probe_files():
        image.save_png(path)
    else:
        path = "<disabled-on-ios>"
    var line := "auto_step index=%d label=%s output=%s texture=%s frame=%dx%d serial=%d frame_stats=%s screenshot_stats=%s renderer=\"%s\"" % [
        index,
        label,
        path,
        player.get_frame_texture_backend(),
        int(frame.get("width", 0)),
        int(frame.get("height", 0)),
        int(frame.get("frame_serial", 0)),
        JSON.stringify(frame_stats),
        JSON.stringify(screenshot_stats),
        player.get_renderer_info(),
    ]
    _write_probe_marker(line)
    print(line)
    var runtime_debug: String = player.get_plugin_debug_info()
    if not runtime_debug.is_empty():
        var debug_line := "auto_step index=%d runtime_debug=%s" % [
            index,
            runtime_debug,
        ]
        _write_probe_marker(debug_line)
        print(debug_line)
    if perf_log_file != null:
        perf_log_file.store_line(line)
        perf_log_file.flush()

func _send_probe_click(window_pos: Vector2) -> void:
    var mapped := _map_probe_window_point(window_pos)
    if mapped.x < 0.0 or mapped.y < 0.0:
        _write_probe_marker("auto_click_skipped window=%s mapped=%s" % [window_pos, mapped])
        return
    player.send_pointer_event(POINTER_MOVE, 0, mapped.x, mapped.y, 0.0, 0.0, 0)
    player.tick(1.0 / 60.0)
    player.send_pointer_event(POINTER_DOWN, 0, mapped.x, mapped.y, 0.0, 0.0, 0)
    _hold_next_present_after_input()
    player.tick(1.0 / 60.0)
    player.send_pointer_event(POINTER_UP, 0, mapped.x, mapped.y, 0.0, 0.0, 0)
    _hold_next_present_after_input(POST_CLICK_PRESENT_HOLD_FRAMES, true)
    _write_probe_marker("auto_click window=%s mapped=%s" % [window_pos, mapped])

func _map_probe_window_point(pos: Vector2) -> Vector2:
    var panel_size := Vector2(
        float(_runtime_int("AETHERKIRI_AUTO_PROBE_COORD_W", 1600)),
        float(_runtime_int("AETHERKIRI_AUTO_PROBE_COORD_H", 900))
    )
    return GameInputMapping.map_point_to_surface(
        pos,
        Rect2(Vector2.ZERO, panel_size),
        _game_input_content_size(),
        _game_input_surface_size()
    )

func _frame_stats(frame: Dictionary) -> Dictionary:
    var data: PackedByteArray = frame.get("rgba", PackedByteArray())
    var visible := 0
    var sampled := 0
    var step: int = max(4, int(data.size() / 20000) & ~3)
    for i in range(0, data.size() - 3, step):
        sampled += 1
        if data[i + 3] > 0 and (data[i] > 8 or data[i + 1] > 8 or data[i + 2] > 8):
            visible += 1
    return {
        "bytes": data.size(),
        "sampled": sampled,
        "visible": visible,
    }

func _image_stats(image: Image) -> Dictionary:
    var visible := 0
    var sampled := 0
    var width := image.get_width()
    var height := image.get_height()
    var step_x: int = max(1, width / 160)
    var step_y: int = max(1, height / 90)
    for y in range(0, height, step_y):
        for x in range(0, width, step_x):
            sampled += 1
            var color := image.get_pixel(x, y)
            if color.a > 0.01 and (color.r > 0.03 or color.g > 0.03 or color.b > 0.03):
                visible += 1
    return {
        "width": width,
        "height": height,
        "sampled": sampled,
        "visible": visible,
    }

func _arm_black_frame_guard() -> void:
    if not _is_touch_platform() or not black_frame_guard_enabled:
        return
    var now := Time.get_ticks_msec()
    black_frame_guard_until_msec = maxi(black_frame_guard_until_msec, now + BLACK_FRAME_GUARD_MS)
    black_frame_next_sample_msec = 0

func _should_hold_suspect_black_frame() -> bool:
    if not _is_touch_platform() or player == null:
        return false
    if not black_frame_guard_enabled and not frame_probe_enabled:
        black_frame_consecutive = 0
        return false
    var now := Time.get_ticks_msec()
    var guard_active := now < black_frame_guard_until_msec
    if not guard_active and not frame_probe_enabled:
        black_frame_consecutive = 0
        return false
    if black_frame_next_sample_msec > 0 and now < black_frame_next_sample_msec:
        return false

    black_frame_next_sample_msec = now + BLACK_FRAME_SAMPLE_INTERVAL_MS
    var frame: Dictionary = player.read_frame_rgba()
    var stats := _frame_stats(frame)
    var visible := int(stats.get("visible", 0))
    var sampled := int(stats.get("sampled", 0))
    var is_black := sampled > 0 and visible < BLACK_FRAME_VISIBLE_MIN
    if is_black:
        black_frame_consecutive += 1
    else:
        black_frame_consecutive = 0

    if guard_active or is_black or frame_probe_enabled:
        _log_frame_guard_sample(stats, is_black, guard_active)

    if guard_active and is_black and viewport != null and viewport.texture != null:
        return true
    return false

func _log_frame_guard_sample(stats: Dictionary, is_black: bool, guard_active: bool) -> void:
    if not input_trace_enabled and not frame_probe_enabled:
        return
    var now := Time.get_ticks_msec()
    if not is_black and black_frame_last_log_msec > 0 and now - black_frame_last_log_msec < 500:
        return
    black_frame_last_log_msec = now
    var line := "frame_guard black=%d guard=%d consecutive=%d texture=%s stats=%s renderer=\"%s\"" % [
        1 if is_black else 0,
        1 if guard_active else 0,
        black_frame_consecutive,
        player.get_frame_texture_backend(),
        JSON.stringify(stats),
        player.get_renderer_info(),
    ]
    print(line)
    if perf_log_file != null:
        perf_log_file.store_line(line)
        perf_log_file.flush()

func _default_game_path() -> String:
    if OS.get_name() == "iOS":
        return ProjectSettings.globalize_path("user://Games")
    return ""

func _default_output_path(file_name: String) -> String:
    if OS.get_name() == "iOS":
        return "user://".path_join(file_name)
    return "/tmp".path_join(file_name)

func _parse_click_points(spec: String) -> Array[Vector2]:
    var clicks: Array[Vector2] = []
    if spec.is_empty():
        return clicks
    for item in spec.split(";"):
        var parts := item.split(",")
        if parts.size() == 2:
            clicks.push_back(Vector2(float(parts[0]), float(parts[1])))
    return clicks

func _load_button_position_memory(path: String) -> void:
    button_position_memory_key = path.simplify_path()
    remembered_button_positions.clear()
    observed_button_positions.clear()
    if not FileAccess.file_exists(BUTTON_POSITION_MEMORY_PATH):
        return
    var file := FileAccess.open(BUTTON_POSITION_MEMORY_PATH, FileAccess.READ)
    if file == null:
        return
    var parsed = JSON.parse_string(file.get_as_text())
    file.close()
    if not parsed is Dictionary:
        return
    var raw = parsed.get(button_position_memory_key, [])
    if not raw is Array:
        return
    for item in raw:
        if item is Array and item.size() >= 2:
            remembered_button_positions.append(Vector2(float(item[0]), float(item[1])))
    observed_button_positions = remembered_button_positions.duplicate()

func _remember_button_position(position: Vector2) -> void:
    if active_runtime_kind != RUNTIME_ONSCRIPTER or button_position_memory_key.is_empty():
        return
    if observed_button_positions.size() >= 2:
        return
    observed_button_positions.append(position)
    var memory: Dictionary = {}
    if FileAccess.file_exists(BUTTON_POSITION_MEMORY_PATH):
        var existing := FileAccess.open(BUTTON_POSITION_MEMORY_PATH, FileAccess.READ)
        if existing != null:
            var parsed = JSON.parse_string(existing.get_as_text())
            existing.close()
            if parsed is Dictionary:
                memory = parsed
    var saved_positions: Array = []
    for item in observed_button_positions:
        saved_positions.append([item.x, item.y])
    memory[button_position_memory_key] = saved_positions
    var output := FileAccess.open(BUTTON_POSITION_MEMORY_PATH, FileAccess.WRITE)
    if output != null:
        output.store_string(JSON.stringify(memory))
        output.close()
    remembered_button_positions = observed_button_positions.duplicate()

func _runtime_string(name: String, fallback: String = "") -> String:
    var value := OS.get_environment(name)
    if not value.is_empty():
        return value
    if OS.get_name() != "Web":
        return fallback
    var aliases: Array[String] = [name, name.to_lower()]
    if name.begins_with("AETHERKIRI_"):
        aliases.append(name.substr("AETHERKIRI_".length()).to_lower())
    var source := "(function(names){var p=new URLSearchParams(window.location.search);for(var i=0;i<names.length;i++){if(p.has(names[i]))return p.get(names[i])||'';}return '';})(" + JSON.stringify(aliases) + ")"
    value = _web_eval_string(source)
    return fallback if value.is_empty() else value

func _runtime_flag(name: String, fallback: bool = false) -> bool:
    var value := _runtime_string(name)
    if value.is_empty():
        return fallback
    value = value.strip_edges().to_lower()
    return value == "1" or value == "true" or value == "yes" or value == "on"

func _native_auto_start_enabled() -> bool:
    return _runtime_flag("AETHERKIRI_ENABLE_AUTO_START") or _runtime_flag("AETHERKIRI_AUTOMATION")

func _runtime_float(name: String, fallback: float) -> float:
    var value := _runtime_string(name)
    if value.is_empty():
        return fallback
    return value.to_float()

func _runtime_int(name: String, fallback: int) -> int:
    var value := _runtime_string(name)
    if value.is_empty():
        return fallback
    return int(value)

func _write_probe_marker(line: String) -> void:
    if OS.get_name() != "iOS" or not device_probe_enabled or not _can_write_probe_files():
        return
    var marker := FileAccess.open(_default_output_path("aetherkiri-device-probe.log"), FileAccess.READ_WRITE)
    if marker == null:
        marker = FileAccess.open(_default_output_path("aetherkiri-device-probe.log"), FileAccess.WRITE)
    if marker == null:
        return
    marker.seek_end()
    marker.store_line("%d %s" % [Time.get_ticks_msec(), line])
    marker.flush()

func _kirikiri_virtual_key(event: InputEventKey) -> int:
    var key_code := int(event.keycode)
    if key_code == KEY_NONE:
        key_code = int(event.physical_keycode)
    match key_code:
        KEY_BACKSPACE:
            return 0x08
        KEY_TAB, KEY_BACKTAB:
            return 0x09
        KEY_ENTER, KEY_KP_ENTER:
            return 0x0D
        KEY_SHIFT:
            return 0x10
        KEY_CTRL:
            return 0x11
        KEY_ALT:
            return 0x12
        KEY_PAUSE:
            return 0x13
        KEY_CAPSLOCK:
            return 0x14
        KEY_ESCAPE:
            return 0x1B
        KEY_SPACE:
            return 0x20
        KEY_PAGEUP:
            return 0x21
        KEY_PAGEDOWN:
            return 0x22
        KEY_END:
            return 0x23
        KEY_HOME:
            return 0x24
        KEY_LEFT:
            return 0x25
        KEY_UP:
            return 0x26
        KEY_RIGHT:
            return 0x27
        KEY_DOWN:
            return 0x28
        KEY_PRINT:
            return 0x2C
        KEY_INSERT:
            return 0x2D
        KEY_DELETE:
            return 0x2E
        KEY_HELP:
            return 0x2F
    if key_code >= KEY_F1 and key_code <= KEY_F24:
        return 0x70 + key_code - KEY_F1
    if key_code >= 0x61 and key_code <= 0x7A:
        return key_code - 0x20
    return key_code if key_code >= 0 and key_code <= 0xFF else 0

func _kirikiri_key_modifiers(event: InputEventKey) -> int:
    var modifiers := 0
    if event.shift_pressed:
        modifiers |= 0x01
    if event.alt_pressed:
        modifiers |= 0x02
    if event.ctrl_pressed:
        modifiers |= 0x04
    if event.echo:
        modifiers |= 0x80
    return modifiers

func _handle_video_player_input(event: InputEvent) -> bool:
    if event is InputEventKey:
        var media_key := event as InputEventKey
        if not media_key.pressed or media_key.echo:
            return false
        match media_key.keycode:
            KEY_ESCAPE:
                _close_video_player()
                return true
            KEY_SPACE:
                _toggle_video_playback()
                return true
            KEY_LEFT:
                _seek_video_relative(-10.0)
                return true
            KEY_RIGHT:
                _seek_video_relative(10.0)
                return true
        return false
    if event is InputEventMouseMotion:
        var motion := event as InputEventMouseMotion
        if (
            video_seek_mouse_pressed
            and (motion.button_mask & MOUSE_BUTTON_MASK_LEFT) != 0
        ):
            _update_video_seek_gesture(motion.position)
            return true
        if motion.relative.length_squared() > 0.25:
            _set_video_controls_visible(true)
        return false
    if event is InputEventScreenDrag:
        var drag := event as InputEventScreenDrag
        if drag.index == video_seek_touch_index:
            _update_video_seek_gesture(drag.position)
            return true
        return false
    if event is InputEventMouseButton:
        var mouse_button := event as InputEventMouseButton
        if mouse_button.button_index != MOUSE_BUTTON_LEFT:
            return false
        if mouse_button.pressed:
            # Godot may synthesize a mouse click immediately after an iOS
            # touch. The touch owns this gesture, so do not start it twice.
            if Time.get_ticks_msec() <= video_touch_mouse_suppress_until_msec:
                video_touch_mouse_suppress_until_msec = 0
                return true
            if _video_pointer_over_controls(mouse_button.position):
                video_controls_idle_sec = 0.0
                return false
            video_seek_mouse_pressed = true
            video_seek_touch_index = -1
            _begin_video_seek_gesture(mouse_button.position)
            return true
        if not video_seek_mouse_pressed:
            return false
        video_seek_mouse_pressed = false
        _finish_video_seek_gesture(mouse_button.position)
        return true
    if event is InputEventScreenTouch:
        var touch := event as InputEventScreenTouch
        if touch.pressed:
            if _video_pointer_over_controls(touch.position):
                video_controls_idle_sec = 0.0
                return false
            video_touch_mouse_suppress_until_msec = (
                Time.get_ticks_msec() + TOUCH_MOUSE_SUPPRESS_MS
            )
            video_seek_touch_index = touch.index
            video_seek_mouse_pressed = false
            _begin_video_seek_gesture(touch.position)
            return true
        if touch.index != video_seek_touch_index:
            return false
        video_seek_touch_index = -1
        _finish_video_seek_gesture(touch.position)
        return true
    return false

func _reset_mobile_edge_back_gesture() -> void:
    mobile_edge_back_touch_index = -1
    mobile_edge_back_start = Vector2.ZERO
    mobile_edge_back_last = Vector2.ZERO
    mobile_edge_back_cancelled = false

func _mobile_edge_back_available() -> bool:
    if not _mobile_runtime() or game_running or video_playing:
        return false
    if shell_root == null or not shell_root.visible:
        return false
    if modal_layer != null and modal_layer.visible:
        # The mandatory first-use documents cannot be bypassed by a gesture.
        return _next_required_legal_document().is_empty()
    return shell_route in ["detail", "settings"]

func _edge_back_gesture_qualified(
    start: Vector2,
    finish: Vector2,
    available_width: float,
    cancelled: bool = false
) -> bool:
    if cancelled:
        return false
    var delta := finish - start
    var trigger_distance := clampf(
        available_width * 0.18,
        MOBILE_EDGE_BACK_MIN_TRIGGER_DISTANCE,
        110.0
    )
    return delta.x >= trigger_distance and delta.x > absf(delta.y) * 1.25

func _perform_mobile_shell_back() -> bool:
    if modal_layer != null and modal_layer.visible:
        _dismiss_modal()
        return true
    if shell_route == "detail":
        _show_library("game")
        return true
    if shell_route == "settings":
        _show_library(home_library_mode)
        return true
    return false

func _handle_mobile_edge_back_input(event: InputEvent) -> bool:
    if not _mobile_edge_back_available():
        _reset_mobile_edge_back_gesture()
        return false
    var safe_rect := _ui_safe_rect(get_viewport_rect().size)
    var start_width := minf(
        MOBILE_EDGE_BACK_MAX_START_WIDTH,
        maxf(24.0, safe_rect.size.x * 0.075)
    )
    if event is InputEventScreenTouch:
        var touch := event as InputEventScreenTouch
        if touch.pressed:
            if mobile_edge_back_touch_index >= 0:
                return false
            if touch.position.x > safe_rect.position.x + start_width:
                return false
            mobile_edge_back_touch_index = touch.index
            mobile_edge_back_start = touch.position
            mobile_edge_back_last = touch.position
            mobile_edge_back_cancelled = false
            return true
        if touch.index != mobile_edge_back_touch_index:
            return false
        mobile_edge_back_last = touch.position
        var qualified := _edge_back_gesture_qualified(
            mobile_edge_back_start,
            mobile_edge_back_last,
            safe_rect.size.x,
            mobile_edge_back_cancelled
        )
        _reset_mobile_edge_back_gesture()
        if qualified:
            _perform_mobile_shell_back()
        return true
    if event is InputEventScreenDrag:
        var drag := event as InputEventScreenDrag
        if drag.index != mobile_edge_back_touch_index:
            return false
        mobile_edge_back_last = drag.position
        var delta := mobile_edge_back_last - mobile_edge_back_start
        if absf(delta.y) > maxf(48.0, absf(delta.x) * 1.1):
            mobile_edge_back_cancelled = true
        return true
    return false

func _trace_ios_raw_pointer_event(event: InputEvent) -> void:
    if OS.get_name() != "iOS" or not input_trace_enabled or not _is_game_pointer_event(event):
        return
    if event is InputEventMouseMotion:
        var motion := event as InputEventMouseMotion
        if motion.button_mask == 0 and active_mouse_buttons.is_empty():
            return
    var position := Vector2.ZERO
    var phase := "move"
    var pointer_id := -1
    if event is InputEventScreenTouch:
        var touch := event as InputEventScreenTouch
        position = touch.position
        phase = "down" if touch.pressed else "up"
        pointer_id = touch.index
    elif event is InputEventScreenDrag:
        var drag := event as InputEventScreenDrag
        position = drag.position
        pointer_id = drag.index
    elif event is InputEventMouseButton:
        var button := event as InputEventMouseButton
        position = button.position
        phase = "down" if button.pressed else "up"
        pointer_id = int(button.button_index)
    elif event is InputEventMouseMotion:
        position = (event as InputEventMouseMotion).position
    elif event is InputEventPanGesture:
        position = (event as InputEventPanGesture).position
    var line := "ios_raw_input id=%d class=%s phase=%s pointer=%d device=%d pos=%.1f,%.1f game=%s runtime=%s can_forward=%s modal=%s loading=%s" % [
        event.get_instance_id(),
        event.get_class(),
        phase,
        pointer_id,
        event.device,
        position.x,
        position.y,
        str(game_running),
        active_runtime_kind,
        str(_can_forward_game_input()),
        str(modal_layer != null and modal_layer.visible),
        str(loading_panel != null and loading_panel.visible),
    ]
    print(line)
    _write_probe_marker(line)
    if perf_log_file != null:
        perf_log_file.store_line(line)
        perf_log_file.flush()

func _input(event: InputEvent) -> void:
    _trace_ios_raw_pointer_event(event)
    if event is InputEventKey:
        var shell_key := event as InputEventKey
        if shell_key.pressed and not shell_key.echo and shell_key.keycode == KEY_ESCAPE and modal_layer != null and modal_layer.visible:
            _dismiss_modal()
            get_viewport().set_input_as_handled()
            return
    if video_playing and _handle_video_player_input(event):
        get_viewport().set_input_as_handled()
        return
    # _input runs before Control GUI dispatch. Keep pointers that begin on the
    # diagnostic action out of the game bridge so Button can receive them.
    if debug_console != null and debug_console.routes_pointer(event):
        return
    if diagnostic_session != null and diagnostic_session.routes_pointer_to_marker(event):
        return
    if (
        game_virtual_controls != null
        and game_virtual_controls.routes_pointer(event)
    ):
        return
    # Platform dialogs are real Godot Controls, matching CDialog's host-owned
    # modal. Leave their events unhandled so LineEdit/Button GUI dispatch owns
    # them, and never pass the same event through to the game.
    if modal_layer != null and modal_layer.visible:
        return
    # KAG [edit] controls own their focus inside the rendered game; Godot does
    # not mirror that focus onto the TextureRect. Forward keyboard input here,
    # before shell Controls can consume it.
    if event is InputEventKey and _can_forward_game_input():
        var key := event as InputEventKey
        if game_text_input_active and key.pressed and not key.echo and (
            key.meta_pressed or key.ctrl_pressed
        ) and key.keycode == KEY_V:
            player.send_text_input(DisplayServer.clipboard_get())
            get_viewport().set_input_as_handled()
            return
        player.send_key_event(
            key.pressed,
            _kirikiri_virtual_key(key),
            _kirikiri_key_modifiers(key),
            key.unicode
        )
        get_viewport().set_input_as_handled()
        return
    if _is_game_pointer_event(event):
        var debug_pos := Vector2.ZERO
        if event is InputEventMouseButton:
            debug_pos = (event as InputEventMouseButton).position
        elif event is InputEventMouseMotion:
            debug_pos = (event as InputEventMouseMotion).position
        elif event is InputEventScreenTouch:
            debug_pos = (event as InputEventScreenTouch).position
        elif event is InputEventScreenDrag:
            debug_pos = (event as InputEventScreenDrag).position
        elif event is InputEventPanGesture:
            debug_pos = (event as InputEventPanGesture).position
        var debug_control := _control_at_pointer(debug_pos) if shell_root != null and shell_root.visible else null
        var debug_button := _nearest_base_button(debug_control) if debug_control != null else null
        debug_last_input_event = event.get_class()
        debug_last_input_position = debug_pos
        debug_last_input_target = _control_debug_label(debug_button if debug_button != null else debug_control)
        _android_input_debug_log("input event=%s game_running=%s can_forward=%s viewport_visible=%s startup=%d pos=%s control=%s button=%s" % [
            event.get_class(),
            str(game_running),
            str(_can_forward_game_input()),
            str(viewport != null and viewport.visible),
            cached_startup_state,
            str(debug_pos),
            _control_debug_label(debug_control),
            _control_debug_label(debug_button),
        ])
    if game_running and viewport.visible:
        if not _can_forward_game_input():
            if _is_game_pointer_event(event):
                if _is_game_input_busy():
                    _trace_input_busy()
                else:
                    _trace_input_blocked()
                get_viewport().set_input_as_handled()
                return
        elif _handle_game_pointer_event(event):
            get_viewport().set_input_as_handled()
            return

    if _handle_mobile_edge_back_input(event):
        get_viewport().set_input_as_handled()
        return

    if _handle_shell_scroll_input(event):
        get_viewport().set_input_as_handled()
        return

    if detail_view == null or detail_scroll == null or not detail_view.visible:
        return

    if event is InputEventMouseButton:
        var button := event as InputEventMouseButton
        if button.button_index == MOUSE_BUTTON_WHEEL_UP and button.pressed:
            _scroll_detail_by(-72.0)
            get_viewport().set_input_as_handled()
        elif button.button_index == MOUSE_BUTTON_WHEEL_DOWN and button.pressed:
            _scroll_detail_by(72.0)
            get_viewport().set_input_as_handled()
        return

func _scroll_detail_by(delta: float) -> void:
    _scroll_container_by(detail_scroll, delta, true)

func _handle_shell_scroll_input(event: InputEvent) -> bool:
    if shell_root == null or not shell_root.visible:
        return false
    if modal_layer != null and modal_layer.visible:
        return false
    # AetherSelect is rendered in a scene-level overlay, outside the settings
    # ScrollContainer's ancestry. This global shell handler runs before normal
    # Control GUI dispatch, so it must stand down while that overlay owns the
    # pointer; otherwise one wheel gesture moves both scroll containers.
    if get_tree().get_first_node_in_group(AETHER_SELECT_OVERLAY_INPUT_GROUP) != null:
        return false

    if event is InputEventScreenTouch:
        var touch := event as InputEventScreenTouch
        if touch.pressed:
            _start_shell_scroll_drag(touch.index, touch.position)
            return false
        return _finish_shell_scroll_drag(touch.index)

    if event is InputEventScreenDrag:
        var drag := event as InputEventScreenDrag
        return _update_shell_scroll_drag(drag.index, drag.position, drag.relative, drag.velocity.y)

    if event is InputEventPanGesture:
        var pan := event as InputEventPanGesture
        var pan_scroll := _find_shell_scroll_at_position(pan.position)
        if pan_scroll == null:
            return false
        _scroll_container_by(pan_scroll, pan.delta.y * SHELL_SCROLL_TOUCHPAD_SPEED, true)
        return true

    if event is InputEventMouseButton:
        var mouse_button := event as InputEventMouseButton
        var is_wheel := mouse_button.button_index == MOUSE_BUTTON_WHEEL_UP or mouse_button.button_index == MOUSE_BUTTON_WHEEL_DOWN
        if mouse_button.pressed and is_wheel:
            var wheel_scroll := _find_shell_scroll_at_position(mouse_button.position)
            if wheel_scroll == null:
                return false
            var wheel_direction := -1.0 if mouse_button.button_index == MOUSE_BUTTON_WHEEL_UP else 1.0
            var wheel_factor := absf(mouse_button.factor)
            if wheel_factor < 1.0:
                wheel_factor = 1.0
            _scroll_container_by(wheel_scroll, wheel_direction * SHELL_SCROLL_WHEEL_STEP * SHELL_SCROLL_WHEEL_SPEED * wheel_factor, true)
            return true
        if mouse_button.button_index != MOUSE_BUTTON_LEFT:
            return false
        if mouse_button.pressed:
            # Preserve native scrollbar interaction. Starting the shell's
            # click-and-drag scrolling here steals the thumb drag before the
            # ScrollBar can process it, which makes fast navigation impossible.
            var pointer_control := _control_at_pointer(mouse_button.position)
            if pointer_control != null and _is_scroll_bar_control(pointer_control):
                shell_scroll_drag_states.erase(SHELL_SCROLL_MOUSE_KEY)
                var native_scroll := _find_shell_scroll_at_position(mouse_button.position)
                if native_scroll != null:
                    _stop_shell_scroll_tween(native_scroll)
                return false
            _start_shell_scroll_drag(SHELL_SCROLL_MOUSE_KEY, mouse_button.position)
            return false
        return _finish_shell_scroll_drag(SHELL_SCROLL_MOUSE_KEY)

    if event is InputEventMouseMotion and Input.is_mouse_button_pressed(MOUSE_BUTTON_LEFT):
        # A mouse drag must have started as a shell-content drag. In particular,
        # do not lazily create one after a native scrollbar thumb captured the
        # initial press.
        if not shell_scroll_drag_states.has(SHELL_SCROLL_MOUSE_KEY):
            return false
        var motion := event as InputEventMouseMotion
        return _update_shell_scroll_drag(SHELL_SCROLL_MOUSE_KEY, motion.position, motion.relative)

    return false

func _on_viewport_input(event: InputEvent) -> void:
    if (
        game_virtual_controls != null
        and game_virtual_controls.owns_viewport_pointer(event)
    ):
        # Main._input already routed the original full-screen event. The
        # TextureRect receives a localized copy through gui_input; routing it
        # again would mix the two coordinate spaces and apply a second delta.
        get_viewport().set_input_as_handled()
        return
    if not _can_forward_game_input():
        if _is_game_pointer_event(event):
            if _is_game_input_busy():
                _trace_input_busy()
            else:
                _trace_input_blocked()
            get_viewport().set_input_as_handled()
        return
    if _handle_game_pointer_event(event):
        get_viewport().set_input_as_handled()

func _can_forward_game_input() -> bool:
    # ONS marks startup complete before its title transition has necessarily
    # replaced the first black frame. Let the runtime receive taps during the
    # loading fade once it is ready; otherwise an invisible/fading overlay can
    # make the title look unresponsive even though the engine is accepting
    # events. Other runtimes retain the existing loading-overlay gate.
    var loading_blocks_input := (
        active_runtime_kind != RUNTIME_ONSCRIPTER
        and loading_panel != null
        and loading_panel.visible
    )
    return game_running and viewport.visible and cached_startup_state == STARTUP_SUCCEEDED and not loading_blocks_input and (
        modal_layer == null or not modal_layer.visible
    )

func _sync_game_virtual_controls() -> void:
    if game_virtual_controls == null:
        return
    _apply_game_virtual_control_preferences()
    game_virtual_controls.set_enabled(
        _should_enable_game_virtual_controls(
            _is_touch_platform(),
            _can_forward_game_input(),
            app_lifecycle_paused
        )
    )

func _apply_game_virtual_control_preferences() -> void:
    if game_virtual_controls == null:
        return
    game_virtual_controls.set_menu_button_enabled(game_virtual_menu_enabled)
    game_virtual_controls.set_keyboard_controls_opacity(
        game_virtual_keyboard_opacity
    )

func _should_enable_game_virtual_controls(
    touch_platform: bool,
    input_ready: bool,
    lifecycle_paused: bool
) -> bool:
    # Every runtime uses the same EngineApi key and pointer input contract.
    return touch_platform and input_ready and not lifecycle_paused

func _on_game_virtual_key_event(
    pressed: bool,
    key_code: int,
    modifiers: int
) -> void:
    if player == null:
        return
    if pressed and not _can_forward_game_input():
        return
    if not pressed and not game_running:
        return
    var unicode_codepoint := _virtual_key_unicode(
        pressed, key_code, modifiers
    )
    var result := int(player.send_key_event(
        pressed, key_code, modifiers, unicode_codepoint
    ))
    input_trace_forwarded += 1
    if result != ENGINE_RESULT_OK:
        input_trace_send_failed += 1
    if input_trace_enabled:
        _write_probe_marker(
            "game_virtual_key pressed=%s key=0x%02X modifiers=0x%02X result=%d" % [
                str(pressed),
                key_code,
                modifiers,
                result,
            ]
        )

func _virtual_key_unicode(
    pressed: bool,
    key_code: int,
    modifiers: int
) -> int:
    if not pressed or (modifiers & KEY_MOD_CONTROL) != 0:
        return 0
    if key_code >= 0x41 and key_code <= 0x5A:
        return key_code + 0x20
    if key_code >= 0x30 and key_code <= 0x39:
        return key_code
    if key_code == 0x20:
        return key_code
    return 0

func _on_game_virtual_pointer_move(
    screen_position: Vector2,
    screen_delta: Vector2
) -> void:
    if player == null or not _can_forward_virtual_controls_input():
        return
    var mapped := _map_viewport_point(screen_position, true)
    var mapped_delta := _map_viewport_delta(screen_delta)
    _send_game_pointer_event(
        POINTER_MOVE,
        VIRTUAL_CONTROLS_POINTER_ID,
        mapped.x,
        mapped.y,
        mapped_delta.x,
        mapped_delta.y,
        0
    )

func _on_game_virtual_pointer_button(
    pressed: bool,
    button: int,
    modifiers: int,
    screen_position: Vector2
) -> void:
    if player == null:
        return
    if pressed and not _can_forward_virtual_controls_input():
        return
    if not pressed and not game_running:
        return
    var mapped := _map_viewport_point(screen_position, true)
    _send_game_pointer_event(
        POINTER_DOWN if pressed else POINTER_UP,
        VIRTUAL_CONTROLS_POINTER_ID,
        mapped.x,
        mapped.y,
        0.0,
        0.0,
        button,
        modifiers
    )
    if pressed:
        _hold_next_present_after_input()
    else:
        _hold_next_present_after_input(POST_CLICK_PRESENT_HOLD_FRAMES, true)

func _on_game_virtual_pointer_scroll(
    delta_y: float,
    screen_position: Vector2
) -> void:
    if player == null or not _can_forward_virtual_controls_input():
        return
    var mapped := _map_viewport_point(screen_position, true)
    _send_game_pointer_event(
        POINTER_SCROLL,
        VIRTUAL_CONTROLS_POINTER_ID,
        mapped.x,
        mapped.y,
        0.0,
        delta_y,
        0
    )

func _on_game_virtual_keyboard_requested() -> void:
    if not _can_forward_virtual_controls_input():
        return
    if (
        _is_touch_platform()
        and DisplayServer.has_feature(DisplayServer.FEATURE_VIRTUAL_KEYBOARD)
    ):
        game_text_input_forced = true
        var attention := Vector2i(get_viewport().get_visible_rect().get_center())
        _show_game_virtual_keyboard(attention, {})
        game_text_input_attention_position = attention
        game_text_input_active = true
    elif DisplayServer.has_feature(DisplayServer.FEATURE_IME):
        game_text_input_forced = true
        DisplayServer.window_set_ime_active(true)
        game_text_input_active = true

func _on_game_virtual_controls_requested() -> void:
    if game_text_input_active or game_text_input_forced:
        _deactivate_game_text_input()

func _on_game_virtual_input_mode_changed(mode: String) -> void:
    game_virtual_input_mode = _normalize_game_virtual_input_mode(mode)
    _save_game_virtual_input_mode()

func _can_forward_virtual_controls_input() -> bool:
    return (
        _can_forward_game_input()
        and not app_lifecycle_paused
    )

func _is_game_pointer_event(event: InputEvent) -> bool:
    return event is InputEventMouseButton or event is InputEventMouseMotion or event is InputEventScreenTouch or event is InputEventScreenDrag or event is InputEventPanGesture

func _handle_game_pointer_event(event: InputEvent) -> bool:
    # Input callbacks only enqueue events; _process owns engine ticking and frame updates.
    _trace_input_received()
    if event is InputEventMouseButton:
        var mouse_button := event as InputEventMouseButton
        if _is_touch_platform() and mouse_button.device == INPUT_DEVICE_ID_EMULATION:
            # Godot emits the emulated mouse event before the corresponding
            # ScreenTouch on iOS. Waiting for ScreenTouch to set a suppression
            # timestamp is therefore too late and produces two ONS clicks.
            # Hardware mouse events keep their real device id and still work.
            _trace_input_throttled()
            return true
        var is_touch_mouse_duplicate := (
            _is_touch_platform()
            and suppress_mouse_until_msec > 0
            and Time.get_ticks_msec() <= suppress_mouse_until_msec
        )
        if is_touch_mouse_duplicate and mouse_button.button_index != MOUSE_BUTTON_WHEEL_UP and mouse_button.button_index != MOUSE_BUTTON_WHEEL_DOWN:
            _trace_input_throttled()
            return true
        var is_scroll := mouse_button.button_index == MOUSE_BUTTON_WHEEL_UP or mouse_button.button_index == MOUSE_BUTTON_WHEEL_DOWN
        var captured := active_mouse_buttons.has(mouse_button.button_index)
        var mapped := _map_viewport_point(mouse_button.position, captured and not is_scroll)
        if mapped.x < 0.0 or mapped.y < 0.0:
            _trace_input_outside()
            return false
        var event_type := POINTER_DOWN if mouse_button.pressed else POINTER_UP
        if is_scroll:
            event_type = POINTER_SCROLL
        elif mouse_button.pressed:
            active_mouse_buttons[mouse_button.button_index] = mapped
            _remember_button_position(mapped)
        else:
            if not captured:
                _trace_input_throttled()
                return true
            active_mouse_buttons.erase(mouse_button.button_index)
        var button := _map_mouse_button(mouse_button.button_index)
        _send_game_pointer_event(
            event_type,
            0,
            mapped.x,
            mapped.y,
            0.0,
            1.0 if mouse_button.button_index == MOUSE_BUTTON_WHEEL_UP else -1.0,
            button
        )
        if event_type == POINTER_DOWN:
            _hold_next_present_after_input()
        elif event_type == POINTER_UP:
            _hold_next_present_after_input(POST_CLICK_PRESENT_HOLD_FRAMES, true)
        return true
    elif event is InputEventMouseMotion:
        var motion := event as InputEventMouseMotion
        if _is_touch_platform() and motion.device == INPUT_DEVICE_ID_EMULATION:
            return true
        if (
            _is_touch_platform()
            and suppress_mouse_until_msec > 0
            and Time.get_ticks_msec() <= suppress_mouse_until_msec
        ):
            return true
        var captured := not active_mouse_buttons.is_empty()
        var mapped := _map_viewport_point(motion.position, captured)
        if mapped.x < 0.0 or mapped.y < 0.0:
            _trace_input_outside()
            return false
        var rel := _map_viewport_delta(motion.relative)
        var modifiers := 0
        if active_mouse_buttons.has(MOUSE_BUTTON_LEFT) or (motion.button_mask & MOUSE_BUTTON_MASK_LEFT) != 0:
            modifiers |= POINTER_MOD_LEFT
        if active_mouse_buttons.has(MOUSE_BUTTON_RIGHT) or (motion.button_mask & MOUSE_BUTTON_MASK_RIGHT) != 0:
            modifiers |= POINTER_MOD_RIGHT
        if active_mouse_buttons.has(MOUSE_BUTTON_MIDDLE) or (motion.button_mask & MOUSE_BUTTON_MASK_MIDDLE) != 0:
            modifiers |= POINTER_MOD_MIDDLE
        _send_game_pointer_event(
            POINTER_MOVE,
            0,
            mapped.x,
            mapped.y,
            rel.x,
            rel.y,
            0,
            modifiers
        )
        return true
    elif event is InputEventScreenTouch:
        var touch := event as InputEventScreenTouch
        suppress_mouse_until_msec = Time.get_ticks_msec() + TOUCH_MOUSE_SUPPRESS_MS
        var pointer_id := touch.index
        if touch.pressed:
            if _is_touch_input_busy():
                _suppress_touch_pointer(pointer_id)
                _trace_input_busy()
                return true
            var mapped := _map_viewport_point(touch.position)
            if mapped.x < 0.0 or mapped.y < 0.0:
                _trace_input_outside()
                return false
            if _handle_secondary_touch_press(pointer_id, mapped):
                return true
            var quarantine_remaining := touch_secondary_quarantine_until_msec - Time.get_ticks_msec()
            if quarantine_remaining > 0:
                # Keep the first finger pending so a second finger can still
                # form the next two-finger gesture during the quarantine. If
                # no second finger arrives, the pending tap is discarded on
                # release (or when the quarantine expires) and never reaches
                # the engine.
                _set_pending_touch(pointer_id, mapped, true)
                _trace_touch_route(
                    "secondary_quarantine_pending",
                    pointer_id,
                    mapped,
                    "remaining_ms=%d" % quarantine_remaining
                )
                return true
            if not active_touch_points.is_empty():
                _suppress_touch_pointer(pointer_id)
                _trace_input_throttled()
                return true
            if _should_suppress_touch_press():
                _suppress_touch_pointer(pointer_id)
                _trace_input_throttled()
                return true
            _set_pending_touch(pointer_id, mapped)
            return true
        if suppressed_touch_points.has(pointer_id):
            suppressed_touch_points.erase(pointer_id)
            active_touch_points.erase(pointer_id)
            touch_down_points.erase(pointer_id)
            dragging_touch_points.erase(pointer_id)
            _clear_pending_touch_if_matches(pointer_id)
            _trace_input_throttled()
            return true
        if pointer_id == pending_touch_index:
            var pending_up_mapped := _map_viewport_point(touch.position, true)
            if pending_up_mapped.x < 0.0 or pending_up_mapped.y < 0.0:
                pending_up_mapped = pending_touch_mapped
            if pending_touch_quarantined:
                _clear_pending_touch()
                _trace_touch_route(
                    "secondary_quarantine_release",
                    pointer_id,
                    pending_up_mapped
                )
                return true
            _send_pending_touch_click(pointer_id, pending_up_mapped)
            return true
        var captured := active_touch_points.has(pointer_id)
        if not captured:
            _trace_input_throttled()
            return true
        var mapped := _map_viewport_point(touch.position, true)
        if mapped.x < 0.0 or mapped.y < 0.0:
            mapped = active_touch_points.get(pointer_id, Vector2.ZERO)
        var down_mapped: Vector2 = touch_down_points.get(pointer_id, mapped)
        if not dragging_touch_points.has(pointer_id):
            mapped = GameInputMapping.stable_tap_point(
                down_mapped,
                mapped,
                _touch_drag_distance_threshold()
            )
        active_touch_points.erase(pointer_id)
        touch_down_points.erase(pointer_id)
        dragging_touch_points.erase(pointer_id)
        last_forwarded_touch_move_msec_by_id.erase(pointer_id)
        _send_game_pointer_event(POINTER_UP, _touch_engine_pointer_id(pointer_id), mapped.x, mapped.y, 0.0, 0.0, 0)
        last_forwarded_touch_up_msec = Time.get_ticks_msec()
        _apply_touch_action_cooldown()
        _arm_black_frame_guard()
        _hold_next_present_after_input(POST_CLICK_PRESENT_HOLD_FRAMES, true)
        return true
    elif event is InputEventScreenDrag:
        var drag := event as InputEventScreenDrag
        suppress_mouse_until_msec = Time.get_ticks_msec() + TOUCH_MOUSE_SUPPRESS_MS
        var pointer_id := drag.index
        var drag_distance_threshold := _touch_drag_distance_threshold()
        if suppressed_touch_points.has(pointer_id):
            _trace_input_throttled()
            return true
        if pointer_id == pending_touch_index:
            var pending_drag_mapped := _map_viewport_point(drag.position, true)
            if pending_drag_mapped.x < 0.0 or pending_drag_mapped.y < 0.0:
                pending_drag_mapped = pending_touch_mapped
            if pending_drag_mapped.distance_to(pending_touch_mapped) < drag_distance_threshold:
                _trace_input_move_suppressed()
                return true
            _flush_pending_touch_press(true)
        var captured := active_touch_points.has(pointer_id)
        var mapped := _map_viewport_point(drag.position, captured)
        if mapped.x < 0.0 or mapped.y < 0.0:
            if not captured:
                _trace_input_outside()
                return false
            mapped = active_touch_points.get(pointer_id, Vector2.ZERO)
        if captured:
            var down_mapped: Vector2 = touch_down_points.get(pointer_id, mapped)
            if (
                not dragging_touch_points.has(pointer_id)
                and mapped.distance_to(down_mapped) < drag_distance_threshold
            ):
                _trace_input_move_suppressed()
                return true
            dragging_touch_points[pointer_id] = true
            active_touch_points[pointer_id] = mapped
            var rel := _map_viewport_delta(drag.relative)
            var move_modifiers := POINTER_MOD_LEFT
            if _is_touch_platform() and active_runtime_kind == RUNTIME_ONSCRIPTER:
                # Treat touch-drag as cursor positioning for ONS. Keeping the
                # left-button modifier set turns it into a mouse drag, which
                # prevents title-menu hover state from following the finger.
                move_modifiers = 0
            _send_game_pointer_event(
                POINTER_MOVE,
                _touch_engine_pointer_id(pointer_id),
                mapped.x,
                mapped.y,
                rel.x,
                rel.y,
                0,
                move_modifiers
            )
        else:
            _trace_input_throttled()
        return true
    return false

func _suppress_touch_pointer(pointer_id: int) -> void:
    suppressed_touch_points[pointer_id] = true
    active_touch_points.erase(pointer_id)
    touch_down_points.erase(pointer_id)
    dragging_touch_points.erase(pointer_id)
    last_forwarded_touch_move_msec_by_id.erase(pointer_id)
    _clear_pending_touch_if_matches(pointer_id)

func _set_pending_touch(pointer_id: int, mapped: Vector2, quarantined: bool = false) -> void:
    suppressed_touch_points.erase(pointer_id)
    active_touch_points.erase(pointer_id)
    touch_down_points.erase(pointer_id)
    dragging_touch_points.erase(pointer_id)
    last_forwarded_touch_move_msec_by_id.erase(pointer_id)
    pending_touch_index = pointer_id
    pending_touch_mapped = mapped
    pending_touch_down_msec = Time.get_ticks_msec()
    pending_touch_quarantined = quarantined
    _trace_touch_route("pending_primary", pointer_id, mapped)

func _clear_pending_touch() -> void:
    pending_touch_index = -1
    pending_touch_mapped = Vector2.ZERO
    pending_touch_down_msec = 0
    pending_touch_quarantined = false

func _clear_pending_touch_if_matches(pointer_id: int) -> void:
    if pending_touch_index == pointer_id:
        _clear_pending_touch()

func _handle_secondary_touch_press(pointer_id: int, mapped: Vector2) -> bool:
    var now := Time.get_ticks_msec()
    if pending_touch_index >= 0 and pending_touch_index != pointer_id:
        if now - pending_touch_down_msec <= TOUCH_SECONDARY_TAP_WINDOW_MS:
            _trace_touch_route(
                "secondary_matches_pending",
                pointer_id,
                mapped,
                "first_pid=%d age_ms=%d" % [
                    pending_touch_index,
                    now - pending_touch_down_msec,
                ]
            )
            _send_touch_secondary_click(pointer_id, mapped)
            return true
        _flush_pending_touch_press(true)
        return false

    if active_touch_points.size() == 1 and last_forwarded_touch_down_msec > 0:
        if now - last_forwarded_touch_down_msec <= TOUCH_SECONDARY_TAP_WINDOW_MS:
            _trace_touch_route(
                "secondary_matches_active",
                pointer_id,
                mapped,
                "age_ms=%d" % (now - last_forwarded_touch_down_msec)
            )
            _send_touch_secondary_click(pointer_id, mapped)
            return true
    return false

func _flush_pending_touch_press_if_ready() -> void:
    _flush_pending_touch_press(false)

func _flush_pending_touch_press(force: bool = false) -> bool:
    if pending_touch_index < 0:
        return false
    var now := Time.get_ticks_msec()
    if pending_touch_quarantined:
        if not force and now < touch_secondary_quarantine_until_msec:
            return false
        var quarantined_pointer_id := pending_touch_index
        var quarantined_mapped := pending_touch_mapped
        _clear_pending_touch()
        _trace_touch_route(
            "secondary_quarantine_expire",
            quarantined_pointer_id,
            quarantined_mapped
        )
        return false
    if not force and now - pending_touch_down_msec < TOUCH_SINGLE_TAP_DELAY_MS:
        return false

    var pointer_id := pending_touch_index
    var mapped := pending_touch_mapped
    _clear_pending_touch()
    suppressed_touch_points.erase(pointer_id)
    active_touch_points[pointer_id] = mapped
    touch_down_points[pointer_id] = mapped
    dragging_touch_points.erase(pointer_id)
    last_forwarded_touch_down_msec = now
    _remember_button_position(mapped)
    _send_game_pointer_event(POINTER_MOVE, _touch_engine_pointer_id(pointer_id), mapped.x, mapped.y, 0.0, 0.0, 0)
    _send_game_pointer_event(POINTER_DOWN, _touch_engine_pointer_id(pointer_id), mapped.x, mapped.y, 0.0, 0.0, 0)
    _trace_touch_route("primary_hold_down", pointer_id, mapped)
    _queue_artemis_input_state_trace("primary_hold_down")
    _arm_tick_trace()
    _arm_black_frame_guard()
    _hold_next_present_after_input()
    return true

func _send_pending_touch_click(pointer_id: int, up_mapped: Vector2) -> void:
    var down_mapped := pending_touch_mapped
    var click_mapped := GameInputMapping.stable_tap_point(
        down_mapped,
        up_mapped,
        _touch_drag_distance_threshold()
    )
    _clear_pending_touch()
    suppressed_touch_points.erase(pointer_id)
    active_touch_points.erase(pointer_id)
    touch_down_points.erase(pointer_id)
    dragging_touch_points.erase(pointer_id)
    last_forwarded_touch_move_msec_by_id.erase(pointer_id)

    last_forwarded_touch_down_msec = Time.get_ticks_msec()
    _remember_button_position(click_mapped)
    _send_game_pointer_event(POINTER_MOVE, _touch_engine_pointer_id(pointer_id), click_mapped.x, click_mapped.y, 0.0, 0.0, 0)
    _send_game_pointer_event(POINTER_DOWN, _touch_engine_pointer_id(pointer_id), click_mapped.x, click_mapped.y, 0.0, 0.0, 0)
    if _is_touch_platform():
        # A short tap can be released before the 90 ms gesture-disambiguation
        # window expires. Sending DOWN and UP from this callback puts both SDL
        # events in the same frame; Artemis can consume that pulse while a
        # timed transition is changing state. Keep one small, deterministic
        # press pulse for every mobile runtime.
        delayed_touch_releases[pointer_id] = {
            "due_msec": Time.get_ticks_msec() + TOUCH_CLICK_HOLD_MS,
            "mapped": click_mapped,
        }
    else:
        _send_game_pointer_event(POINTER_UP, _touch_engine_pointer_id(pointer_id), click_mapped.x, click_mapped.y, 0.0, 0.0, 0)
        last_forwarded_touch_up_msec = Time.get_ticks_msec()
        _apply_touch_action_cooldown()
    _trace_touch_route("primary_tap_down", pointer_id, click_mapped)
    _queue_artemis_input_state_trace("primary_tap")
    _arm_tick_trace()
    _arm_black_frame_guard()
    _hold_next_present_after_input(POST_CLICK_PRESENT_HOLD_FRAMES, true)

func _flush_delayed_touch_releases() -> void:
    if delayed_touch_releases.is_empty():
        return
    var now := Time.get_ticks_msec()
    for pointer_id_variant in delayed_touch_releases.keys():
        var pointer_id := int(pointer_id_variant)
        var release: Dictionary = delayed_touch_releases.get(pointer_id, {})
        if now < int(release.get("due_msec", now)):
            continue
        delayed_touch_releases.erase(pointer_id)
        if player == null:
            continue
        var mapped: Vector2 = release.get("mapped", Vector2.ZERO)
        _send_game_pointer_event(
            POINTER_UP,
            _touch_engine_pointer_id(pointer_id),
            mapped.x,
            mapped.y,
            0.0,
            0.0,
            0
        )
        last_forwarded_touch_up_msec = now
        _trace_touch_route("primary_delayed_up", pointer_id, mapped)
        _apply_touch_action_cooldown()
        _arm_black_frame_guard()
        _hold_next_present_after_input(POST_CLICK_PRESENT_HOLD_FRAMES, true)

func _send_touch_secondary_click(pointer_id: int, mapped: Vector2) -> void:
    var click_mapped := mapped
    if pending_touch_index >= 0:
        var first_id := pending_touch_index
        click_mapped = (pending_touch_mapped + mapped) * 0.5
        suppressed_touch_points[first_id] = true
        touch_down_points.erase(first_id)
        dragging_touch_points.erase(first_id)
        last_forwarded_touch_move_msec_by_id.erase(first_id)
        _clear_pending_touch()
    elif not active_touch_points.is_empty():
        var first_id := int(active_touch_points.keys()[0])
        var first_mapped: Vector2 = active_touch_points.get(first_id, mapped)
        click_mapped = (first_mapped + mapped) * 0.5
        # The first finger may already have crossed the single-touch delay.
        # Reclassifying it as a two-finger gesture must cancel that press; a
        # normal UP would also dispatch Artemis setonpush/adv_click.
        _send_game_pointer_event(
            POINTER_UP,
            _touch_engine_pointer_id(first_id),
            first_mapped.x,
            first_mapped.y,
            0.0,
            0.0,
            0,
            POINTER_MOD_CANCEL
        )
        active_touch_points.erase(first_id)
        touch_down_points.erase(first_id)
        dragging_touch_points.erase(first_id)
        last_forwarded_touch_move_msec_by_id.erase(first_id)
        suppressed_touch_points[first_id] = true
        last_forwarded_touch_up_msec = Time.get_ticks_msec()

    _suppress_touch_pointer(pointer_id)
    _remember_button_position(click_mapped)
    _send_game_pointer_event(POINTER_MOVE, TOUCH_SECONDARY_POINTER_ID, click_mapped.x, click_mapped.y, 0.0, 0.0, 0)
    last_forwarded_touch_down_msec = Time.get_ticks_msec()
    _send_game_pointer_event(POINTER_DOWN, TOUCH_SECONDARY_POINTER_ID, click_mapped.x, click_mapped.y, 0.0, 0.0, 1)
    _send_game_pointer_event(POINTER_UP, TOUCH_SECONDARY_POINTER_ID, click_mapped.x, click_mapped.y, 0.0, 0.0, 1)
    last_forwarded_touch_up_msec = Time.get_ticks_msec()
    touch_secondary_quarantine_until_msec = maxi(
        touch_secondary_quarantine_until_msec,
        last_forwarded_touch_up_msec + TOUCH_SECONDARY_QUARANTINE_MS
    )
    _trace_touch_route(
        "secondary_click",
        pointer_id,
        click_mapped,
        "button=1"
    )
    _trace_touch_route(
        "secondary_quarantine_arm",
        pointer_id,
        click_mapped,
        "duration_ms=%d" % TOUCH_SECONDARY_QUARANTINE_MS
    )
    _queue_artemis_input_state_trace("secondary_click")
    _apply_touch_action_cooldown()
    _arm_tick_trace()
    _arm_black_frame_guard()
    _hold_next_present_after_input(POST_CLICK_PRESENT_HOLD_FRAMES, true)

func _touch_engine_pointer_id(pointer_id: int) -> int:
    return TOUCH_POINTER_ID_OFFSET + pointer_id

func _send_game_pointer_event(event_type: int, pointer_id: int, x: float, y: float, delta_x: float, delta_y: float, button: int, modifiers: int = 0) -> void:
    if _is_touch_platform() and event_type == POINTER_DOWN and button == 0:
        game_text_input_reopen_requested = true
    var result := int(player.send_pointer_event(event_type, pointer_id, x, y, delta_x, delta_y, button, modifiers))
    if _android_input_debug_enabled():
        print("android_input fwd type=%d pid=%d x=%.1f y=%.1f dx=%.1f dy=%.1f button=%d mods=%d result=%d" % [
            event_type,
            pointer_id,
            x,
            y,
            delta_x,
            delta_y,
            button,
            modifiers,
            result,
        ])
        _android_input_debug_log("fwd type=%d pid=%d x=%.1f y=%.1f dx=%.1f dy=%.1f button=%d mods=%d result=%d" % [
            event_type,
            pointer_id,
            x,
            y,
            delta_x,
            delta_y,
            button,
            modifiers,
            result,
        ])
    input_trace_forwarded += 1
    if result != ENGINE_RESULT_OK:
        input_trace_send_failed += 1
    if input_trace_enabled and event_type in [POINTER_DOWN, POINTER_UP]:
        var trace_line := "input_event type=%d pid=%d x=%.1f y=%.1f button=%d result=%d loading=%s runtime=%s" % [
            event_type,
            pointer_id,
            x,
            y,
            button,
            result,
            str(loading_panel != null and loading_panel.visible),
            active_runtime_kind,
        ]
        # Desktop probes do not have the iOS device marker file, so keep the
        # individual edge visible on stdout as well as in optional file logs.
        print(trace_line)
        _write_probe_marker(trace_line)
        if perf_log_file != null:
            perf_log_file.store_line(trace_line)
            perf_log_file.flush()

func _android_input_debug_enabled() -> bool:
    return OS.get_name() == "Android" and input_trace_enabled

func _android_input_debug_log(line: String) -> void:
    if not _android_input_debug_enabled():
        return
    var file := FileAccess.open("user://android-input.log", FileAccess.READ_WRITE)
    if file == null:
        file = FileAccess.open("user://android-input.log", FileAccess.WRITE)
    if file == null:
        return
    file.seek_end()
    file.store_line("%d %s" % [Time.get_ticks_msec(), line])
    file.flush()

func _control_debug_label(control: Control) -> String:
    if control == null:
        return "<null>"
    var rect := control.get_global_rect()
    return "%s name=%s path=%s rect=(%.1f,%.1f %.1fx%.1f) visible=%s filter=%d" % [
        control.get_class(),
        control.name,
        control.get_path(),
        rect.position.x,
        rect.position.y,
        rect.size.x,
        rect.size.y,
        str(control.is_visible_in_tree()),
        int(control.mouse_filter),
    ]

func _trace_input_received() -> void:
    input_trace_received += 1

func _trace_input_blocked() -> void:
    input_trace_blocked += 1

func _trace_input_throttled() -> void:
    input_trace_throttled += 1

func _trace_input_busy() -> void:
    input_trace_busy += 1

func _trace_input_move_suppressed() -> void:
    input_trace_move_suppressed += 1

func _trace_input_outside() -> void:
    input_trace_outside += 1

func _apply_touch_action_cooldown() -> void:
    if not _is_touch_platform() or TOUCH_ACTION_COOLDOWN_MS <= 0:
        return
    touch_input_busy_until_msec = maxi(
        touch_input_busy_until_msec,
        Time.get_ticks_msec() + TOUCH_ACTION_COOLDOWN_MS
    )

func _update_touch_busy_gate(tick_ms: float) -> void:
    if not _is_touch_platform() or TOUCH_BUSY_SUPPRESS_MS <= 0:
        return
    if tick_ms < TOUCH_BUSY_TICK_MS:
        return
    touch_input_busy_until_msec = maxi(
        touch_input_busy_until_msec,
        Time.get_ticks_msec() + TOUCH_BUSY_SUPPRESS_MS
    )

func _is_game_input_busy() -> bool:
    return _is_touch_input_busy()

func _is_touch_input_busy() -> bool:
    return _is_touch_platform() and TOUCH_BUSY_SUPPRESS_MS > 0 and Time.get_ticks_msec() < touch_input_busy_until_msec

func _should_suppress_touch_press() -> bool:
    if not _is_touch_platform() or TOUCH_TAP_MIN_INTERVAL_MS <= 0:
        return false
    var now := Time.get_ticks_msec()
    if last_forwarded_touch_down_msec > 0 and now - last_forwarded_touch_down_msec < TOUCH_TAP_MIN_INTERVAL_MS:
        return true
    if last_forwarded_touch_up_msec > 0 and now - last_forwarded_touch_up_msec < TOUCH_TAP_MIN_INTERVAL_MS:
        return true
    return false

func _should_suppress_touch_drag(pointer_id: int) -> bool:
    if not _is_touch_platform():
        return false
    var now := Time.get_ticks_msec()
    var last_move := int(last_forwarded_touch_move_msec_by_id.get(pointer_id, 0))
    if last_move > 0 and now - last_move < TOUCH_DRAG_MIN_INTERVAL_MS:
        return true
    last_forwarded_touch_move_msec_by_id[pointer_id] = now
    return false

func _hold_next_present_after_input(frames: int = POST_INPUT_PRESENT_HOLD_FRAMES, force: bool = false) -> void:
    if frames <= 0:
        return
    var now := Time.get_ticks_msec()
    if present_hold_frames > 0 and not force:
        return
    if not force and last_present_hold_msec > 0 and now - last_present_hold_msec < POST_INPUT_PRESENT_HOLD_MIN_INTERVAL_MS:
        return
    present_hold_frames = maxi(present_hold_frames, frames)
    last_present_hold_msec = now
    if input_trace_enabled:
        input_trace_present_holds += 1

func _is_touch_platform() -> bool:
    var platform := OS.get_name()
    return platform == "iOS" or platform == "Android"

func _touch_drag_distance_threshold() -> float:
    if not _is_touch_platform():
        return TOUCH_DRAG_DISTANCE_THRESHOLD
    if active_runtime_kind == RUNTIME_ONSCRIPTER:
        # ONS menus are controlled by moving a cursor and activating the
        # item on release. Small touch movement should not move that cursor.
        return ONS_TOUCH_CURSOR_DISTANCE_THRESHOLD
    return TOUCH_DRAG_DISTANCE_THRESHOLD

func _deactivate_game_text_input() -> void:
    if game_text_input_active:
        if (
            _is_touch_platform()
            and DisplayServer.has_feature(DisplayServer.FEATURE_VIRTUAL_KEYBOARD)
        ):
            DisplayServer.virtual_keyboard_hide()
        elif DisplayServer.has_feature(DisplayServer.FEATURE_IME):
            DisplayServer.window_set_ime_active(false)
    game_text_input_active = false
    game_text_input_forced = false
    game_text_input_attention_position = Vector2i(-1, -1)
    game_text_input_reopen_requested = false

func _show_game_virtual_keyboard(attention_position: Vector2i, state: Dictionary) -> void:
    var existing_text := ""
    var cursor_start := -1
    var cursor_end := -1
    if bool(state.get("text_available", false)):
        existing_text = String(state.get("text", ""))
        var text_length := existing_text.length()
        var selection_start := clampi(
            int(state.get("selection_start", text_length)), 0, text_length
        )
        var selection_end := clampi(
            int(state.get("selection_end", selection_start)), 0, text_length
        )
        cursor_start = mini(selection_start, selection_end)
        var ordered_end := maxi(selection_start, selection_end)
        # Godot's mobile backends use -1 to distinguish a caret from a real
        # selection; Android treats equal start/end as a selection session.
        if ordered_end != cursor_start:
            cursor_end = ordered_end
    DisplayServer.virtual_keyboard_show(
        existing_text,
        Rect2(Vector2(attention_position), Vector2.ONE),
        DisplayServer.KEYBOARD_TYPE_DEFAULT,
        -1,
        cursor_start,
        cursor_end
    )
    game_text_input_last_show_msec = Time.get_ticks_msec()

func _sync_game_text_input_state() -> void:
    if game_text_input_forced:
        if game_text_input_suspended or not _can_forward_game_input():
            _deactivate_game_text_input()
        return
    if game_text_input_suspended or not _can_forward_game_input():
        _deactivate_game_text_input()
        return
    if not player.has_method("get_text_input_state"):
        _deactivate_game_text_input()
        return
    var state = player.get_text_input_state()
    if not state is Dictionary or not bool(state.get("available", false)):
        _deactivate_game_text_input()
        return

    var attention_valid := bool(state.get("attention_point_valid", false))
    var ime_active := bool(state.get("ime_active", false))
    var virtual_keyboard_available := (
        _is_touch_platform()
        and DisplayServer.has_feature(DisplayServer.FEATURE_VIRTUAL_KEYBOARD)
    )
    var desktop_ime_available := (
        not virtual_keyboard_available
        and DisplayServer.has_feature(DisplayServer.FEATURE_IME)
    )
    # Attention marks an editable KiriKiri layer. imClose/imDisable still need
    # a mobile soft keyboard for direct (non-composed) text entry.
    var should_activate := attention_valid and (
        virtual_keyboard_available or (desktop_ime_available and ime_active)
    )
    if not should_activate:
        _deactivate_game_text_input()
        return

    var attention_position := game_text_input_attention_position
    if attention_valid:
        var surface_point := Vector2(
            float(state.get("attention_x", 0)),
            float(state.get("attention_y", 0))
        )
        var screen_point := _map_surface_point_to_screen(surface_point)
        attention_position = Vector2i(roundi(screen_point.x), roundi(screen_point.y))
    var attention_position_changed := (
        attention_position != game_text_input_attention_position
    )

    if not game_text_input_active:
        if virtual_keyboard_available:
            _show_game_virtual_keyboard(attention_position, state)
        elif desktop_ime_available:
            DisplayServer.window_set_ime_active(true)
            DisplayServer.window_set_ime_position(attention_position)
        game_text_input_active = true
    elif (
        virtual_keyboard_available
        and game_text_input_reopen_requested
        and not DisplayServer.has_hardware_keyboard()
        and (
            Time.get_ticks_msec() - game_text_input_last_show_msec
            >= VIRTUAL_KEYBOARD_REOPEN_DELAY_MS
        )
    ):
        # A game-surface tap is also a caret/selection resync boundary while
        # the keyboard is already visible.
        _show_game_virtual_keyboard(attention_position, state)
    elif desktop_ime_available and attention_position_changed:
        DisplayServer.window_set_ime_position(attention_position)

    game_text_input_attention_position = attention_position
    game_text_input_reopen_requested = false

func _map_surface_point_to_screen(point: Vector2) -> Vector2:
    if viewport == null:
        return point
    var local_point := point
    if viewport.texture != null:
        var texture_size := Vector2(
            max(1.0, float(viewport.texture.get_width())),
            max(1.0, float(viewport.texture.get_height()))
        )
        var surface_size := _game_input_surface_size()
        var texture_point := Vector2(
            point.x * texture_size.x / surface_size.x,
            point.y * texture_size.y / surface_size.y
        )
        var panel_size := viewport.size
        var scale := minf(
            panel_size.x / texture_size.x,
            panel_size.y / texture_size.y
        )
        var drawn_size := texture_size * scale
        var offset := (panel_size - drawn_size) * 0.5
        local_point = offset + texture_point * scale
    return viewport.get_screen_transform() * local_point

func _map_viewport_point(pos: Vector2, clamp_to_bounds: bool = false) -> Vector2:
    if viewport.texture == null:
        return pos
    return GameInputMapping.map_point_to_surface(
        pos,
        viewport.get_global_rect(),
        _game_input_content_size(),
        _game_input_surface_size(),
        clamp_to_bounds
    )

func _map_viewport_delta(delta: Vector2) -> Vector2:
    if viewport.texture == null:
        return delta
    return GameInputMapping.map_delta_to_surface(
        delta,
        viewport.size,
        _game_input_content_size(),
        _game_input_surface_size()
    )

func _map_mouse_button(button_index: MouseButton) -> int:
    if button_index == MOUSE_BUTTON_RIGHT:
        return 1
    if button_index == MOUSE_BUTTON_MIDDLE:
        return 2
    return 0

func _append_log(line: String) -> void:
    if device_probe_enabled:
        _write_probe_marker("log %s" % line)
    _maybe_show_log_alert(line)
    log_lines.append(line)
    while log_lines.size() > MAX_LOG_LINES:
        log_lines.remove_at(0)
    if ui_log_enabled and log_view != null:
        log_view_dirty = true

func _scroll_log_to_bottom() -> void:
    if log_view == null:
        return
    log_view.scroll_vertical = max(0, log_view.get_line_count())
