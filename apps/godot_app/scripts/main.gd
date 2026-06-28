extends Control

const BACKENDS := ["Godot Native", "GPU Bridge", "Debug CPU"]
const SETTINGS_KEY := "aether_kiri/render_backend"
const GAME_PATH_KEY := "aether_kiri/game_path"
const GAME_LIST_FILE := "user://aetherkiri_games.json"
const SETTINGS_FILE := "user://aetherkiri_settings.cfg"
const UI_FONT := preload("res://assets/fonts/aetherkiri-runtime-cjk.otf")
const UI_SYMBOL_FONT := preload("res://assets/fonts/aetherkiri-runtime-symbols.ttf")
const RUNTIME_CJK_FONT_SOURCE := "res://assets/fonts/aetherkiri-runtime-cjk.otf"
const RUNTIME_SYMBOL_FONT_SOURCE := "res://assets/fonts/aetherkiri-runtime-symbols.ttf"
const RUNTIME_FONT_DIR := "user://runtime_fonts"
const RUNTIME_DEFAULT_FONT_FILE := "default.otf"
const RUNTIME_SYMBOL_FONT_FILE := "symbols.ttf"
const ProbeConfig = preload("res://scripts/probe_config.gd")
const UI_ICON_DIR := "res://assets/ui/icons/"
const ICON_SETTINGS := UI_ICON_DIR + "gear-fill.svg"
const ICON_SAVE := UI_ICON_DIR + "save-fill.svg"
const ICON_REFRESH := UI_ICON_DIR + "arrows-counter-clockwise-fill.svg"
const ICON_ADD := UI_ICON_DIR + "plus-circle.svg"
const ICON_HELP := UI_ICON_DIR + "help.svg"
const ICON_LIBRARY := UI_ICON_DIR + "library.svg"
const ICON_GAMEPAD := UI_ICON_DIR + "gamepad-bold.svg"
const ICON_PLAY := UI_ICON_DIR + "game-controller.svg"
const ICON_PERFORMANCE := UI_ICON_DIR + "performance-fill.svg"
const ICON_HOME := UI_ICON_DIR + "round-home.svg"
const ICON_DELETE := UI_ICON_DIR + "round-delete-forever.svg"
const ICON_PAGE := UI_ICON_DIR + "page-template.svg"
const ICON_RENAME := UI_ICON_DIR + "tab-new-24-filled.svg"
const ICON_PLUGIN := UI_ICON_DIR + "plugin-solid.svg"
const LANG_SYSTEM := "system"
const LANG_ZH_HANS := "zh_hans"
const LANG_ZH_HANT := "zh_hant"
const LANG_EN := "en"
const LANG_JA := "ja"
const LANG_KO := "ko"
const LANGUAGE_MODES := [LANG_SYSTEM, LANG_ZH_HANS, LANG_ZH_HANT, LANG_EN, LANG_JA, LANG_KO]
const UI_TEXT := {
    LANG_ZH_HANS: {
        "home.subtitle": "KiriKiri2 运行时外壳",
        "home.status": "Godot Native  /  游戏库",
        "home.empty_title": "尚未添加任何游戏",
        "home.refresh": "刷新",
        "home.import": "导入",
        "home.import_guide": "导入指南",
        "home.empty_help_ios": "使用「文件」App 将游戏文件夹复制到：\n我的 iPhone / iPad > AetherKiri > Games\n然后点击「刷新」",
        "home.empty_help_web": "点击「导入」选择本地游戏目录或 XP3 文件",
        "home.empty_help_desktop": "点击「导入」选择游戏目录或 XP3 文件",
        "settings.title": "设置",
        "settings.save": "保存",
        "settings.section.interface": "界面",
        "settings.section.render": "渲染",
        "settings.section.developer": "开发者",
        "settings.section.about": "关于",
        "settings.language": "语言",
        "settings.language_desc": "默认跟随系统；也可以固定为简体中文、繁体中文、英语、日语或韩语",
        "language.system": "跟随系统",
        "language.system_with_value": "跟随系统（%s）",
        "language.zh_hans": "简体中文",
        "language.zh_hant": "繁體中文",
        "language.en": "English",
        "language.ja": "日本語",
        "language.ko": "한국어",
        "settings.render_backend": "渲染管线",
        "settings.render_backend_desc": "未运行游戏时立即生效；运行中切换需重启当前游戏",
        "settings.surface_mode": "画布尺寸",
        "settings.surface_mode_desc": "Game Native 按游戏基准画布运行；Display Fit 按设备显示尺寸运行",
        "settings.upscale": "缩放算法",
        "settings.upscale_desc": "外层拉伸画面时使用；Smooth/Linear 会做平滑采样",
        "settings.perf": "性能监控",
        "settings.perf_desc": "显示帧率和图形 API 信息",
        "settings.fps_limit": "帧率限制",
        "settings.fps_limit_desc": "开启后使用下方目标帧率；关闭时交给显示刷新率",
        "settings.landscape": "锁定横屏",
        "settings.landscape_desc": "游戏运行时强制横屏显示（手机推荐开启）",
        "settings.target_fps": "目标帧率",
        "settings.target_fps_desc": "限制 C++ 引擎 tick/render 频率；最低 80 FPS",
        "settings.plugin_load_mode": "插件加载模式",
        "settings.plugin_load_mode_desc": "krkrsdl3 只预加载核心兼容插件；aether_all 保留旧全量注册",
        "settings.plugin_trace": "插件调用追踪",
        "settings.plugin_trace_desc": "将所有插件原生调用记录到 plugin_trace.log 用于调试",
        "settings.mock": "Mock 绕过",
        "settings.mock_desc": "为缺失插件返回 mock 对象以抑制错误。关闭可暴露真实错误用于调试。",
        "settings.console_log": "控制台日志文件",
        "settings.console_log_desc": "将引擎控制台日志写入 krkr.console.log 文件",
        "settings.trace_log": "追踪日志",
        "settings.trace_log_desc": "启用 spdlog trace 级别详细日志，输出最大调试信息",
        "settings.export_tjs": "导出 TJS 脚本",
        "settings.export_tjs_desc": "游戏加载时自动从 XP3 中导出反汇编的 TJS 字节码脚本",
        "settings.log_alerts": "日志级别弹窗",
        "settings.log_alerts_desc": "将 warning/error/fatal 等日志行额外显示为系统提示；默认关闭",
        "settings.error_dialog_logs": "错误弹窗附带日志",
        "settings.error_dialog_logs_desc": "真正异常弹窗中追加最近 20 行引擎日志；默认关闭",
        "settings.version": "版本",
        "settings.author": "作者",
        "settings.email": "邮箱",
        "detail.eyebrow": "游戏详情",
        "detail.runtime_profile": "运行配置 / %s",
        "detail.last_played": "上次游玩：%s",
        "detail.played": "已玩 %s",
        "detail.launch": "启动游戏",
        "detail.set_cover": "设置封面",
        "detail.rename": "重命名",
        "detail.remove": "移除游戏",
        "game.today": "今天",
        "game.days_ago": "%d 天前",
        "game.played_duration": "已玩 %s",
        "game.never_played": "尚未游玩",
        "game.local": "本地游戏",
        "game.type_directory": "目录",
        "game.type_archive": "归档",
        "dialog.import_title": "导入游戏",
        "dialog.import_guide_body": "请使用「文件」App 将游戏文件夹复制到本应用的目录：\n\n1. 打开 iPhone / iPad 上的「文件」App\n2. 前往：我的 iPhone / iPad > AetherKiri > Games\n3. 将游戏文件夹复制到 Games 目录\n4. 返回本应用，点击「刷新」检测新游戏\n\n游戏目录：Games/",
        "dialog.ok": "知道了",
        "dialog.scrape_title": "刮削元数据",
        "dialog.scrape_body": "已添加「%s」。是否现在进入详情页设置封面、名称和元数据？",
        "dialog.later": "稍后",
        "dialog.open_detail": "打开详情",
        "dialog.choose_cover": "选择封面图片",
        "dialog.rename": "重命名",
        "dialog.remove_body": "从列表移除「%s」？不会删除磁盘上的游戏文件。",
        "dialog.remove": "移除",
        "dialog.select_game_dir": "选择游戏目录",
        "dialog.select_local_game_dir": "选择本地游戏目录",
        "dialog.select_xp3": "选择 XP3 文件",
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
        "message.path_missing": "游戏路径不存在",
        "message.game_exists": "游戏已存在：%s",
        "alert.error_title": "AetherKiri 错误",
        "alert.warning_title": "AetherKiri 警告",
        "alert.runtime_class_missing": "运行时扩展加载失败：AetherKiriPlayer 不可用",
        "alert.runtime_create_failed": "运行时扩展加载失败：无法创建 AetherKiriPlayer",
        "loading.title": "正在启动游戏..."
    },
    LANG_ZH_HANT: {
        "home.subtitle": "KiriKiri2 執行時外殼",
        "home.status": "Godot Native  /  遊戲庫",
        "home.empty_title": "尚未加入任何遊戲",
        "home.refresh": "重新整理",
        "home.import": "匯入",
        "home.import_guide": "匯入指南",
        "home.empty_help_ios": "使用「檔案」App 將遊戲資料夾複製到：\n我的 iPhone / iPad > AetherKiri > Games\n然後點選「重新整理」",
        "home.empty_help_web": "點選「匯入」選擇本機遊戲目錄或 XP3 檔案",
        "home.empty_help_desktop": "點選「匯入」選擇遊戲目錄或 XP3 檔案",
        "settings.title": "設定",
        "settings.save": "儲存",
        "settings.section.interface": "介面",
        "settings.section.render": "渲染",
        "settings.section.developer": "開發者",
        "settings.section.about": "關於",
        "settings.language": "語言",
        "settings.language_desc": "預設跟隨系統；也可以固定為簡體中文、繁體中文、英語、日語或韓語",
        "language.system": "跟隨系統",
        "language.system_with_value": "跟隨系統（%s）",
        "language.zh_hans": "简体中文",
        "language.zh_hant": "繁體中文",
        "language.en": "English",
        "language.ja": "日本語",
        "language.ko": "한국어",
        "settings.render_backend": "渲染管線",
        "settings.render_backend_desc": "未執行遊戲時立即生效；執行中切換需重新啟動目前遊戲",
        "settings.surface_mode": "畫布尺寸",
        "settings.surface_mode_desc": "Game Native 依遊戲基準畫布執行；Display Fit 依裝置顯示尺寸執行",
        "settings.upscale": "縮放演算法",
        "settings.upscale_desc": "外層拉伸畫面時使用；Smooth/Linear 會進行平滑取樣",
        "settings.perf": "效能監控",
        "settings.perf_desc": "顯示幀率和圖形 API 資訊",
        "settings.fps_limit": "幀率限制",
        "settings.fps_limit_desc": "開啟後使用下方目標幀率；關閉時交給顯示刷新率",
        "settings.landscape": "鎖定橫向",
        "settings.landscape_desc": "遊戲執行時強制橫向顯示（手機建議開啟）",
        "settings.target_fps": "目標幀率",
        "settings.target_fps_desc": "限制 C++ 引擎 tick/render 頻率；最低 80 FPS",
        "settings.plugin_load_mode": "外掛載入模式",
        "settings.plugin_load_mode_desc": "krkrsdl3 只預載核心相容外掛；aether_all 保留舊版全量註冊",
        "settings.plugin_trace": "外掛呼叫追蹤",
        "settings.plugin_trace_desc": "將所有外掛原生呼叫記錄到 plugin_trace.log 以便除錯",
        "settings.mock": "Mock 繞過",
        "settings.mock_desc": "為缺失外掛返回 mock 物件以抑制錯誤。關閉可暴露真實錯誤用於除錯。",
        "settings.console_log": "主控台日誌檔",
        "settings.console_log_desc": "將引擎主控台日誌寫入 krkr.console.log 檔案",
        "settings.trace_log": "追蹤日誌",
        "settings.trace_log_desc": "啟用 spdlog trace 級別詳細日誌，輸出最大除錯資訊",
        "settings.export_tjs": "匯出 TJS 腳本",
        "settings.export_tjs_desc": "遊戲載入時自動從 XP3 中匯出反組譯的 TJS 位元組碼腳本",
        "settings.log_alerts": "日誌級別彈窗",
        "settings.log_alerts_desc": "將 warning/error/fatal 等日誌行額外顯示為系統提示；預設關閉",
        "settings.error_dialog_logs": "錯誤彈窗附帶日誌",
        "settings.error_dialog_logs_desc": "真正異常彈窗中追加最近 20 行引擎日誌；預設關閉",
        "settings.version": "版本",
        "settings.author": "作者",
        "settings.email": "信箱",
        "detail.eyebrow": "遊戲詳情",
        "detail.runtime_profile": "執行設定 / %s",
        "detail.last_played": "上次遊玩：%s",
        "detail.played": "已玩 %s",
        "detail.launch": "啟動遊戲",
        "detail.set_cover": "設定封面",
        "detail.rename": "重新命名",
        "detail.remove": "移除遊戲",
        "game.today": "今天",
        "game.days_ago": "%d 天前",
        "game.played_duration": "已玩 %s",
        "game.never_played": "尚未遊玩",
        "game.local": "本機遊戲",
        "game.type_directory": "目錄",
        "game.type_archive": "封存",
        "dialog.import_title": "匯入遊戲",
        "dialog.import_guide_body": "請使用「檔案」App 將遊戲資料夾複製到本 App 的目錄：\n\n1. 開啟 iPhone / iPad 上的「檔案」App\n2. 前往：我的 iPhone / iPad > AetherKiri > Games\n3. 將遊戲資料夾複製到 Games 目錄\n4. 返回本 App，點選「重新整理」偵測新遊戲\n\n遊戲目錄：Games/",
        "dialog.ok": "知道了",
        "dialog.scrape_title": "擷取元資料",
        "dialog.scrape_body": "已加入「%s」。是否現在進入詳情頁設定封面、名稱和元資料？",
        "dialog.later": "稍後",
        "dialog.open_detail": "開啟詳情",
        "dialog.choose_cover": "選擇封面圖片",
        "dialog.rename": "重新命名",
        "dialog.remove_body": "要從列表移除「%s」嗎？不會刪除磁碟上的遊戲檔案。",
        "dialog.remove": "移除",
        "dialog.select_game_dir": "選擇遊戲目錄",
        "dialog.select_local_game_dir": "選擇本機遊戲目錄",
        "dialog.select_xp3": "選擇 XP3 檔案",
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
        "message.path_missing": "遊戲路徑不存在",
        "message.game_exists": "遊戲已存在：%s",
        "alert.error_title": "AetherKiri 錯誤",
        "alert.warning_title": "AetherKiri 警告",
        "alert.runtime_class_missing": "執行時擴充載入失敗：AetherKiriPlayer 不可用",
        "alert.runtime_create_failed": "執行時擴充載入失敗：無法建立 AetherKiriPlayer",
        "loading.title": "正在啟動遊戲..."
    },
    LANG_EN: {
        "home.subtitle": "KiriKiri2 runtime shell",
        "home.status": "Godot Native  /  Library",
        "home.empty_title": "No games added yet",
        "home.refresh": "Refresh",
        "home.import": "Import",
        "home.import_guide": "Import Guide",
        "home.empty_help_ios": "Use the Files app to copy your game folder to:\nOn My iPhone / iPad > AetherKiri > Games\nThen tap Refresh",
        "home.empty_help_web": "Tap Import to choose a local game folder or XP3 file",
        "home.empty_help_desktop": "Tap Import to choose a game folder or XP3 file",
        "settings.title": "Settings",
        "settings.save": "Save",
        "settings.section.interface": "Interface",
        "settings.section.render": "Rendering",
        "settings.section.developer": "Developer",
        "settings.section.about": "About",
        "settings.language": "Language",
        "settings.language_desc": "Defaults to the system language; you can pin Simplified Chinese, Traditional Chinese, English, Japanese, or Korean",
        "language.system": "Follow System",
        "language.system_with_value": "Follow System (%s)",
        "language.zh_hans": "简体中文",
        "language.zh_hant": "繁體中文",
        "language.en": "English",
        "language.ja": "日本語",
        "language.ko": "한국어",
        "settings.render_backend": "Render Pipeline",
        "settings.render_backend_desc": "Applies immediately while no game is running; switching during play requires restarting the current game",
        "settings.surface_mode": "Canvas Size",
        "settings.surface_mode_desc": "Game Native uses the game's base canvas; Display Fit uses the device display size",
        "settings.upscale": "Scaling",
        "settings.upscale_desc": "Used when stretching the outer frame; Smooth/Linear apply filtered sampling",
        "settings.perf": "Performance Monitor",
        "settings.perf_desc": "Show FPS and graphics API information",
        "settings.fps_limit": "FPS Limit",
        "settings.fps_limit_desc": "When enabled, use the target FPS below; otherwise follow the display refresh rate",
        "settings.landscape": "Lock Landscape",
        "settings.landscape_desc": "Force landscape while a game is running (recommended on phones)",
        "settings.target_fps": "Target FPS",
        "settings.target_fps_desc": "Limit the C++ engine tick/render rate; minimum 80 FPS",
        "settings.plugin_load_mode": "Plugin Load Mode",
        "settings.plugin_load_mode_desc": "krkrsdl3 only preloads core compatibility plugins; aether_all keeps the legacy full registration path",
        "settings.plugin_trace": "Plugin Call Trace",
        "settings.plugin_trace_desc": "Write all native plugin calls to plugin_trace.log for debugging",
        "settings.mock": "Mock Bypass",
        "settings.mock_desc": "Return mock objects for missing plugins to suppress errors. Disable to expose real errors for debugging.",
        "settings.console_log": "Console Log File",
        "settings.console_log_desc": "Write engine console output to krkr.console.log",
        "settings.trace_log": "Trace Log",
        "settings.trace_log_desc": "Enable spdlog trace-level logs for maximum diagnostic output",
        "settings.export_tjs": "Export TJS Scripts",
        "settings.export_tjs_desc": "Automatically export disassembled TJS bytecode scripts from XP3 files while loading games",
        "settings.log_alerts": "Log Alerts",
        "settings.log_alerts_desc": "Show warning/error/fatal log lines as system alerts; disabled by default",
        "settings.error_dialog_logs": "Attach Logs to Errors",
        "settings.error_dialog_logs_desc": "Append the latest 20 engine log lines to real error dialogs; disabled by default",
        "settings.version": "Version",
        "settings.author": "Author",
        "settings.email": "Email",
        "detail.eyebrow": "Library Detail",
        "detail.runtime_profile": "Runtime profile / %s",
        "detail.last_played": "Last played: %s",
        "detail.played": "Played %s",
        "detail.launch": "Launch Game",
        "detail.set_cover": "Set Cover",
        "detail.rename": "Rename",
        "detail.remove": "Remove Game",
        "game.today": "Today",
        "game.days_ago": "%d days ago",
        "game.played_duration": "Played %s",
        "game.never_played": "Not played yet",
        "game.local": "Local Game",
        "game.type_directory": "Directory",
        "game.type_archive": "Archive",
        "dialog.import_title": "Import Game",
        "dialog.import_guide_body": "Use the Files app to copy your game folder into this app's directory:\n\n1. Open the Files app on your iPhone / iPad\n2. Go to: On My iPhone / iPad > AetherKiri > Games\n3. Copy the game folder into Games\n4. Return to this app and tap Refresh to detect new games\n\nGame directory: Games/",
        "dialog.ok": "Got it",
        "dialog.scrape_title": "Scrape Metadata",
        "dialog.scrape_body": "Added \"%s\". Open the detail page now to set cover art, name, and metadata?",
        "dialog.later": "Later",
        "dialog.open_detail": "Open Detail",
        "dialog.choose_cover": "Choose Cover Image",
        "dialog.rename": "Rename",
        "dialog.remove_body": "Remove \"%s\" from the list? This will not delete game files from disk.",
        "dialog.remove": "Remove",
        "dialog.select_game_dir": "Choose Game Folder",
        "dialog.select_local_game_dir": "Choose Local Game Folder",
        "dialog.select_xp3": "Choose XP3 File",
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
        "message.path_missing": "Game path does not exist",
        "message.game_exists": "Game already exists: %s",
        "alert.error_title": "AetherKiri Error",
        "alert.warning_title": "AetherKiri Warning",
        "alert.runtime_class_missing": "Runtime extension failed to load: AetherKiriPlayer is unavailable",
        "alert.runtime_create_failed": "Runtime extension failed to load: could not create AetherKiriPlayer",
        "loading.title": "Launching game..."
    },
    LANG_JA: {
        "home.subtitle": "KiriKiri2 ランタイムシェル",
        "home.status": "Godot Native  /  ライブラリ",
        "home.empty_title": "ゲームはまだ追加されていません",
        "home.refresh": "更新",
        "home.import": "インポート",
        "home.import_guide": "インポートガイド",
        "home.empty_help_ios": "「ファイル」App でゲームフォルダーをコピーしてください：\nこの iPhone / iPad 内 > AetherKiri > Games\nその後「更新」をタップします",
        "home.empty_help_web": "「インポート」をタップしてローカルゲームフォルダーまたは XP3 ファイルを選択",
        "home.empty_help_desktop": "「インポート」をタップしてゲームフォルダーまたは XP3 ファイルを選択",
        "settings.title": "設定",
        "settings.save": "保存",
        "settings.section.interface": "インターフェイス",
        "settings.section.render": "レンダリング",
        "settings.section.developer": "開発者",
        "settings.section.about": "情報",
        "settings.language": "言語",
        "settings.language_desc": "既定ではシステムに従います。簡体字中国語、繁体字中国語、英語、日本語、韓国語に固定できます",
        "language.system": "システムに従う",
        "language.system_with_value": "システムに従う（%s）",
        "language.zh_hans": "简体中文",
        "language.zh_hant": "繁體中文",
        "language.en": "English",
        "language.ja": "日本語",
        "language.ko": "한국어",
        "settings.render_backend": "レンダリングパイプライン",
        "settings.render_backend_desc": "ゲーム未実行時は即時反映。実行中の切り替えは現在のゲームの再起動が必要です",
        "settings.surface_mode": "キャンバスサイズ",
        "settings.surface_mode_desc": "Game Native はゲーム基準のキャンバス、Display Fit はデバイス表示サイズで実行します",
        "settings.upscale": "スケーリング",
        "settings.upscale_desc": "外側の画面を引き伸ばすときに使用します。Smooth/Linear は平滑化サンプリングを行います",
        "settings.perf": "パフォーマンス監視",
        "settings.perf_desc": "FPS とグラフィックス API 情報を表示します",
        "settings.fps_limit": "FPS 制限",
        "settings.fps_limit_desc": "有効時は下の目標 FPS を使用します。無効時はディスプレイのリフレッシュレートに従います",
        "settings.landscape": "横向き固定",
        "settings.landscape_desc": "ゲーム実行中に横向き表示を強制します（スマートフォン推奨）",
        "settings.target_fps": "目標 FPS",
        "settings.target_fps_desc": "C++ エンジンの tick/render 頻度を制限します。最低 80 FPS",
        "settings.plugin_load_mode": "プラグイン読み込みモード",
        "settings.plugin_load_mode_desc": "krkrsdl3 は互換性用コアプラグインのみプリロードします。aether_all は従来の全登録を維持します",
        "settings.plugin_trace": "プラグイン呼び出し追跡",
        "settings.plugin_trace_desc": "すべてのネイティブプラグイン呼び出しを plugin_trace.log に記録します",
        "settings.mock": "Mock バイパス",
        "settings.mock_desc": "不足プラグインに mock オブジェクトを返してエラーを抑制します。無効にすると実エラーを確認できます。",
        "settings.console_log": "コンソールログファイル",
        "settings.console_log_desc": "エンジンのコンソールログを krkr.console.log に書き込みます",
        "settings.trace_log": "トレースログ",
        "settings.trace_log_desc": "spdlog の trace レベル詳細ログを有効にします",
        "settings.export_tjs": "TJS スクリプトを書き出す",
        "settings.export_tjs_desc": "ゲーム読み込み時に XP3 から逆アセンブル済み TJS バイトコードを自動で書き出します",
        "settings.log_alerts": "ログアラート",
        "settings.log_alerts_desc": "warning/error/fatal などのログ行をシステム通知として表示します。既定はオフ",
        "settings.error_dialog_logs": "エラーにログを添付",
        "settings.error_dialog_logs_desc": "実エラーダイアログに直近 20 行のエンジンログを追加します。既定はオフ",
        "settings.version": "バージョン",
        "settings.author": "作者",
        "settings.email": "メール",
        "detail.eyebrow": "ゲーム詳細",
        "detail.runtime_profile": "ランタイムプロファイル / %s",
        "detail.last_played": "前回プレイ：%s",
        "detail.played": "プレイ時間 %s",
        "detail.launch": "ゲームを起動",
        "detail.set_cover": "カバーを設定",
        "detail.rename": "名前を変更",
        "detail.remove": "ゲームを削除",
        "game.today": "今日",
        "game.days_ago": "%d 日前",
        "game.played_duration": "プレイ時間 %s",
        "game.never_played": "未プレイ",
        "game.local": "ローカルゲーム",
        "game.type_directory": "フォルダー",
        "game.type_archive": "アーカイブ",
        "dialog.import_title": "ゲームをインポート",
        "dialog.import_guide_body": "「ファイル」App でゲームフォルダーをこのアプリのディレクトリにコピーしてください：\n\n1. iPhone / iPad で「ファイル」App を開く\n2. 移動先：この iPhone / iPad 内 > AetherKiri > Games\n3. ゲームフォルダーを Games にコピー\n4. アプリに戻り、「更新」をタップして新しいゲームを検出\n\nゲームディレクトリ：Games/",
        "dialog.ok": "了解",
        "dialog.scrape_title": "メタデータ取得",
        "dialog.scrape_body": "「%s」を追加しました。詳細ページでカバー、名前、メタデータを設定しますか？",
        "dialog.later": "あとで",
        "dialog.open_detail": "詳細を開く",
        "dialog.choose_cover": "カバー画像を選択",
        "dialog.rename": "名前を変更",
        "dialog.remove_body": "「%s」をリストから削除しますか？ディスク上のゲームファイルは削除されません。",
        "dialog.remove": "削除",
        "dialog.select_game_dir": "ゲームフォルダーを選択",
        "dialog.select_local_game_dir": "ローカルゲームフォルダーを選択",
        "dialog.select_xp3": "XP3 ファイルを選択",
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
        "message.path_missing": "ゲームパスが存在しません",
        "message.game_exists": "ゲームは既に存在します：%s",
        "alert.error_title": "AetherKiri エラー",
        "alert.warning_title": "AetherKiri 警告",
        "alert.runtime_class_missing": "ランタイム拡張の読み込みに失敗しました：AetherKiriPlayer は利用できません",
        "alert.runtime_create_failed": "ランタイム拡張の読み込みに失敗しました：AetherKiriPlayer を作成できません",
        "loading.title": "ゲームを起動中..."
    },
    LANG_KO: {
        "home.subtitle": "KiriKiri2 런타임 셸",
        "home.status": "Godot Native  /  라이브러리",
        "home.empty_title": "아직 추가된 게임이 없습니다",
        "home.refresh": "새로고침",
        "home.import": "가져오기",
        "home.import_guide": "가져오기 가이드",
        "home.empty_help_ios": "파일 앱으로 게임 폴더를 다음 위치에 복사하세요:\n나의 iPhone / iPad > AetherKiri > Games\n그런 다음 새로고침을 누르세요",
        "home.empty_help_web": "가져오기를 눌러 로컬 게임 폴더 또는 XP3 파일을 선택하세요",
        "home.empty_help_desktop": "가져오기를 눌러 게임 폴더 또는 XP3 파일을 선택하세요",
        "settings.title": "설정",
        "settings.save": "저장",
        "settings.section.interface": "인터페이스",
        "settings.section.render": "렌더링",
        "settings.section.developer": "개발자",
        "settings.section.about": "정보",
        "settings.language": "언어",
        "settings.language_desc": "기본값은 시스템 언어입니다. 중국어 간체, 중국어 번체, 영어, 일본어, 한국어로 고정할 수 있습니다",
        "language.system": "시스템 따르기",
        "language.system_with_value": "시스템 따르기(%s)",
        "language.zh_hans": "简体中文",
        "language.zh_hant": "繁體中文",
        "language.en": "English",
        "language.ja": "日本語",
        "language.ko": "한국어",
        "settings.render_backend": "렌더링 파이프라인",
        "settings.render_backend_desc": "게임이 실행 중이 아닐 때 즉시 적용됩니다. 실행 중 변경하려면 현재 게임을 다시 시작해야 합니다",
        "settings.surface_mode": "캔버스 크기",
        "settings.surface_mode_desc": "Game Native는 게임 기준 캔버스를 사용하고 Display Fit은 장치 표시 크기를 사용합니다",
        "settings.upscale": "스케일링",
        "settings.upscale_desc": "외부 화면을 늘릴 때 사용합니다. Smooth/Linear는 부드러운 샘플링을 적용합니다",
        "settings.perf": "성능 모니터",
        "settings.perf_desc": "FPS와 그래픽 API 정보를 표시합니다",
        "settings.fps_limit": "FPS 제한",
        "settings.fps_limit_desc": "켜면 아래 목표 FPS를 사용하고, 끄면 디스플레이 주사율을 따릅니다",
        "settings.landscape": "가로 방향 고정",
        "settings.landscape_desc": "게임 실행 중 가로 표시를 강제합니다(휴대폰 권장)",
        "settings.target_fps": "목표 FPS",
        "settings.target_fps_desc": "C++ 엔진 tick/render 빈도를 제한합니다. 최소 80 FPS",
        "settings.plugin_load_mode": "플러그인 로드 모드",
        "settings.plugin_load_mode_desc": "krkrsdl3는 핵심 호환 플러그인만 미리 로드합니다. aether_all은 기존 전체 등록 방식을 유지합니다",
        "settings.plugin_trace": "플러그인 호출 추적",
        "settings.plugin_trace_desc": "모든 네이티브 플러그인 호출을 plugin_trace.log에 기록합니다",
        "settings.mock": "Mock 우회",
        "settings.mock_desc": "누락된 플러그인에 mock 객체를 반환해 오류를 억제합니다. 끄면 실제 오류를 확인할 수 있습니다.",
        "settings.console_log": "콘솔 로그 파일",
        "settings.console_log_desc": "엔진 콘솔 로그를 krkr.console.log 파일에 씁니다",
        "settings.trace_log": "추적 로그",
        "settings.trace_log_desc": "spdlog trace 레벨 상세 로그를 켜서 최대 디버그 정보를 출력합니다",
        "settings.export_tjs": "TJS 스크립트 내보내기",
        "settings.export_tjs_desc": "게임 로드 시 XP3에서 디스어셈블된 TJS 바이트코드 스크립트를 자동으로 내보냅니다",
        "settings.log_alerts": "로그 알림",
        "settings.log_alerts_desc": "warning/error/fatal 로그 줄을 시스템 알림으로 표시합니다. 기본값은 꺼짐입니다",
        "settings.error_dialog_logs": "오류에 로그 첨부",
        "settings.error_dialog_logs_desc": "실제 오류 대화상자에 최근 엔진 로그 20줄을 추가합니다. 기본값은 꺼짐입니다",
        "settings.version": "버전",
        "settings.author": "작성자",
        "settings.email": "이메일",
        "detail.eyebrow": "게임 상세",
        "detail.runtime_profile": "런타임 프로필 / %s",
        "detail.last_played": "마지막 플레이: %s",
        "detail.played": "플레이 %s",
        "detail.launch": "게임 실행",
        "detail.set_cover": "표지 설정",
        "detail.rename": "이름 변경",
        "detail.remove": "게임 제거",
        "game.today": "오늘",
        "game.days_ago": "%d일 전",
        "game.played_duration": "플레이 %s",
        "game.never_played": "아직 플레이하지 않음",
        "game.local": "로컬 게임",
        "game.type_directory": "폴더",
        "game.type_archive": "아카이브",
        "dialog.import_title": "게임 가져오기",
        "dialog.import_guide_body": "파일 앱으로 게임 폴더를 이 앱의 디렉터리에 복사하세요:\n\n1. iPhone / iPad에서 파일 앱을 엽니다\n2. 이동: 나의 iPhone / iPad > AetherKiri > Games\n3. 게임 폴더를 Games에 복사합니다\n4. 앱으로 돌아와 새로고침을 눌러 새 게임을 감지합니다\n\n게임 디렉터리: Games/",
        "dialog.ok": "확인",
        "dialog.scrape_title": "메타데이터 가져오기",
        "dialog.scrape_body": "\"%s\"을(를) 추가했습니다. 지금 상세 페이지에서 표지, 이름, 메타데이터를 설정할까요?",
        "dialog.later": "나중에",
        "dialog.open_detail": "상세 열기",
        "dialog.choose_cover": "표지 이미지 선택",
        "dialog.rename": "이름 변경",
        "dialog.remove_body": "\"%s\"을(를) 목록에서 제거할까요? 디스크의 게임 파일은 삭제되지 않습니다.",
        "dialog.remove": "제거",
        "dialog.select_game_dir": "게임 폴더 선택",
        "dialog.select_local_game_dir": "로컬 게임 폴더 선택",
        "dialog.select_xp3": "XP3 파일 선택",
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
        "message.path_missing": "게임 경로가 존재하지 않습니다",
        "message.game_exists": "게임이 이미 있습니다: %s",
        "alert.error_title": "AetherKiri 오류",
        "alert.warning_title": "AetherKiri 경고",
        "alert.runtime_class_missing": "런타임 확장 로드 실패: AetherKiriPlayer를 사용할 수 없습니다",
        "alert.runtime_create_failed": "런타임 확장 로드 실패: AetherKiriPlayer를 만들 수 없습니다",
        "loading.title": "게임 실행 중..."
    }
}

const ENGINE_RESULT_OK := 0
const STARTUP_IDLE := 0
const STARTUP_RUNNING := 1
const STARTUP_SUCCEEDED := 2
const STARTUP_FAILED := 3

const POINTER_DOWN := 1
const POINTER_MOVE := 2
const POINTER_UP := 3
const POINTER_SCROLL := 4
const POINTER_MOD_LEFT := 0x08

var backend: OptionButton
var game_path: LineEdit
var restart_notice: Label
var viewport: TextureRect
var perf: Label
var log_view = null
var shell_root: Control
var home_view: Control
var settings_view: ScrollContainer
var detail_view: Control
var detail_scroll: ScrollContainer
var game_view: Control
var modal_layer: Control
var loading_panel: PanelContainer
var game_scroll: ScrollContainer
var game_list: GridContainer
var home_actions: HBoxContainer
var empty_state: Control
var save_button: Button
var bg_rect: ColorRect
var home_subtitle_label: Label
var home_status_label: Label
var empty_title_label: Label
var empty_help_label: Label
var home_primary_button: Button
var home_guide_button: Button
var loading_title_label: Label
var selected_game := {}
var known_games: Array[Dictionary] = []
var show_perf_monitor := true
var lock_landscape := true
var frame_limit_enabled := false
var target_fps := 80
var plugin_trace := false
var plugin_load_mode := "krkrsdl3"
var mock_enabled := true
var console_log_file := true
var trace_log := false
var export_scripts := false
var log_alerts := false
var error_dialog_logs := false
var language_mode := LANG_SYSTEM
var active_language := LANG_ZH_HANS
var dirty_settings := false
var active_game_path := ""
var active_game_started_msec := 0
var detail_touch_scroll_active := false
var rounded_card_shader: Shader
var opaque_frame_shader: Shader
var shown_system_alerts := {}
var ui_icon_cache := {}
var cover_texture_cache := {}

var player = null
var runtime_default_font_path := ""
var runtime_font_dir_path := ""
var selected_backend := "Godot Native"
var upscale_algorithm := "smooth"
var render_surface_mode := "game"
var game_running := false
var app_lifecycle_paused := false
var render_errors := 0
var last_renderer_info_logged := ""
var last_texture_size := Vector2i.ZERO
var capture_after_open_path := ""
var capture_after_open_done := false
var capture_after_open_delay_sec := 0.0
var capture_after_open_ready_usec := 0
var auto_probe_clicks: Array[Vector2] = []
var auto_probe_running := false
var auto_probe_done := false
var startup_click_stream_enabled := false
var startup_click_stream_running := false
var startup_click_stream_done := false
var log_drain_accum := 0.0
var perf_accum := 0.0
var perf_log_accum := 0.0
var state_log_accum := 0.0
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
var black_frame_guard_until_msec := 0
var black_frame_next_sample_msec := 0
var black_frame_consecutive := 0
var black_frame_last_log_msec := 0
var black_frame_guard_enabled := false
var cli_probe_script := ""
var verbose_render_log := false
var diagnostics_enabled := false
var ui_log_enabled := false
var web_auto_start_attempted := false
var perf_log_file: FileAccess
var log_lines: PackedStringArray = []
var log_view_dirty := false
var log_view_flush_accum := 0.0
var suppress_mouse_until_msec := 0
var active_touch_points := {}
var active_mouse_buttons := {}
var suppressed_touch_points := {}
var touch_down_points := {}
var pending_touch_index := -1
var pending_touch_mapped := Vector2.ZERO
var pending_touch_down_msec := 0
var last_forwarded_touch_down_msec := 0
var last_forwarded_touch_up_msec := 0
var last_forwarded_touch_move_msec_by_id := {}
var touch_input_busy_until_msec := 0
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
const RENDER_SURFACE_MAX_SIZE := Vector2i(1920, 1080)
const RENDER_SURFACE_MODE_GAME := "game"
const RENDER_SURFACE_MODE_DISPLAY := "display"
const POST_INPUT_PRESENT_HOLD_FRAMES := 1
const POST_CLICK_PRESENT_HOLD_FRAMES := 1
const POST_INPUT_PRESENT_HOLD_MIN_INTERVAL_MS := 120
const TOUCH_TAP_MIN_INTERVAL_MS := 0
const TOUCH_ACTION_COOLDOWN_MS := 0
const TOUCH_DRAG_MIN_INTERVAL_MS := 80
const TOUCH_DRAG_DISTANCE_THRESHOLD := 18.0
const TOUCH_BUSY_TICK_MS := 120.0
const TOUCH_BUSY_SUPPRESS_MS := 0
const TOUCH_POINTER_ID_OFFSET := 100000
const TOUCH_SECONDARY_POINTER_ID := 0
const TOUCH_SECONDARY_TAP_WINDOW_MS := 180
const TOUCH_SINGLE_TAP_DELAY_MS := 90
const BLACK_FRAME_GUARD_MS := 3200
const BLACK_FRAME_SAMPLE_INTERVAL_MS := 120
const BLACK_FRAME_VISIBLE_MIN := 8
const INITIAL_WINDOW_SIZE := Vector2i(2240, 1260)
const DEFAULT_UI_DPI_SCALE := 1.35
const TOUCH_MOUSE_SUPPRESS_MS := 700
const COLOR_BG := Color(0.095, 0.102, 0.135, 1.0)
const COLOR_GAME_BG := Color(0, 0, 0, 1)
const COLOR_CARD := Color(0.133, 0.139, 0.184, 1.0)
const COLOR_CARD_ALT := Color(0.176, 0.184, 0.239, 1.0)
const COLOR_CARD_HOVER := Color(0.214, 0.224, 0.290, 1.0)
const COLOR_TEXT := Color(0.972, 0.972, 0.949, 1.0)
const COLOR_MUTED := Color(0.620, 0.650, 0.780, 1.0)
const COLOR_ACCENT := Color(0.741, 0.576, 0.976, 1.0)
const COLOR_ACCENT_SOFT := Color(0.545, 0.914, 0.992, 1.0)
const COLOR_ACCENT_DIM := Color(0.280, 0.235, 0.410, 1.0)
const COLOR_WARN := Color(1.000, 0.722, 0.424, 1.0)
const COLOR_DANGER := Color(1.000, 0.333, 0.333, 1.0)
const COLOR_LINE := Color(1, 1, 1, 0.105)
const TOP_ICON_BUTTON_SIZE := Vector2(60, 60)
const TOP_ACTION_BUTTON_SIZE := Vector2(138, 60)
const HOME_CARD_SIZE := Vector2(272, 368)

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
    var value := String(table.get(key, UI_TEXT[LANG_ZH_HANS].get(key, key)))
    return value % args if not args.is_empty() else value

func _language_option_label(mode: String) -> String:
    if mode == LANG_SYSTEM:
        return _t("language.system_with_value", [_language_native_name(_system_language_code())])
    return _language_native_name(mode)

func _apply_language_mode() -> void:
    language_mode = _normalize_language_mode(language_mode)
    active_language = _effective_language_code()

func _detect_cli_probe_script() -> String:
    var env_script := _normalize_cli_probe_script(OS.get_environment("AETHERKIRI_CLI_PROBE_SCRIPT"))
    if not env_script.is_empty():
        return env_script
    var args := OS.get_cmdline_args()
    for i in range(args.size()):
        var arg := String(args[i])
        if arg == "--script" or arg == "-s":
            if i + 1 < args.size():
                return _normalize_cli_probe_script(String(args[i + 1]))
        elif arg.begins_with("--script="):
            return _normalize_cli_probe_script(arg.substr("--script=".length()))
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
    var ui_theme := Theme.new()
    ui_theme.set_default_font(UI_FONT)
    ui_theme.set_color("font_color", "Label", COLOR_TEXT)
    ui_theme.set_color("font_color", "Button", COLOR_TEXT)
    ui_theme.set_color("font_color", "OptionButton", COLOR_TEXT)
    ui_theme.set_color("font_hover_color", "OptionButton", COLOR_TEXT)
    ui_theme.set_color("font_pressed_color", "OptionButton", COLOR_TEXT)
    ui_theme.set_color("font_color", "LineEdit", COLOR_TEXT)
    ui_theme.set_color("font_color", "TextEdit", COLOR_TEXT)
    ui_theme.set_color("font_placeholder_color", "LineEdit", COLOR_MUTED)
    ui_theme.set_stylebox("normal", "OptionButton", _panel_style(8, COLOR_CARD_ALT, COLOR_LINE, 1))
    ui_theme.set_stylebox("hover", "OptionButton", _panel_style(8, COLOR_CARD_HOVER, COLOR_ACCENT, 1))
    ui_theme.set_stylebox("pressed", "OptionButton", _panel_style(8, COLOR_ACCENT_DIM, COLOR_ACCENT, 1))
    ui_theme.set_stylebox("focus", "OptionButton", _focus_outline(8))
    ui_theme.set_stylebox("normal", "LineEdit", _panel_style(8, COLOR_CARD_ALT, COLOR_LINE, 1))
    ui_theme.set_stylebox("focus", "LineEdit", _panel_style(8, COLOR_CARD_HOVER, COLOR_ACCENT, 2))
    ui_theme.set_stylebox("normal", "TextEdit", _panel_style(8, Color(0, 0, 0, 0.18), COLOR_LINE, 1))
    ui_theme.set_stylebox("focus", "TextEdit", _panel_style(8, Color(0, 0, 0, 0.24), COLOR_ACCENT, 1))
    theme = ui_theme

func _copy_runtime_font(source_path: String, target_path: String) -> bool:
    var input := FileAccess.open(source_path, FileAccess.READ)
    if input == null:
        return false
    var data := input.get_buffer(input.get_length())
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

    if _copy_runtime_font(RUNTIME_CJK_FONT_SOURCE, default_target):
        runtime_default_font_path = ProjectSettings.globalize_path(default_target)
        copied_any = true
    else:
        _append_log("Runtime CJK font staging failed.")

    if _copy_runtime_font(RUNTIME_SYMBOL_FONT_SOURCE, symbols_target):
        copied_any = true
    else:
        _append_log("Runtime symbol font staging failed.")

    if copied_any:
        runtime_font_dir_path = native_dir

func _build_ui() -> void:
    bg_rect = ColorRect.new()
    bg_rect.color = COLOR_BG
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
    _apply_upscale_algorithm()

    game_view = Control.new()
    game_view.set_anchors_preset(Control.PRESET_FULL_RECT)
    game_view.mouse_filter = Control.MOUSE_FILTER_IGNORE
    game_view.visible = false
    add_child(game_view)

    shell_root = Control.new()
    shell_root.set_anchors_preset(Control.PRESET_FULL_RECT)
    add_child(shell_root)

    _build_home_view()
    _build_settings_view()
    _build_detail_view()
    _build_modal_layer()

    perf = Label.new()
    perf.position = Vector2(24, 18)
    perf.add_theme_font_size_override("font_size", 13)
    perf.add_theme_color_override("font_color", Color(1, 1, 1, 0.92))
    perf.visible = false
    game_view.add_child(perf)

    restart_notice = Label.new()
    restart_notice.position = Vector2(24, 44)
    restart_notice.add_theme_font_size_override("font_size", 14)
    restart_notice.add_theme_color_override("font_color", Color(1, 0.82, 0.65, 1))
    restart_notice.visible = false
    game_view.add_child(restart_notice)

    _build_loading_panel()
    _fit_full_rects()

func _load_shell_settings() -> void:
    var cfg := ConfigFile.new()
    if cfg.load(SETTINGS_FILE) != OK:
        var env_surface_mode := _runtime_string("AETHERKIRI_SURFACE_MODE", "")
        if not env_surface_mode.is_empty():
            _select_config_surface_mode(env_surface_mode)
        _apply_language_mode()
        return
    language_mode = _normalize_language_mode(String(cfg.get_value("interface", "language", language_mode)))
    _apply_language_mode()
    selected_backend = _normalize_backend_name(String(cfg.get_value("rendering", "backend", selected_backend)))
    upscale_algorithm = String(cfg.get_value("rendering", "upscale_algorithm", upscale_algorithm))
    if upscale_algorithm == "sharp" or upscale_algorithm == "nearest":
        upscale_algorithm = "smooth"
    if not upscale_algorithm in ["smooth", "nearest", "linear"]:
        upscale_algorithm = "smooth"
    render_surface_mode = String(cfg.get_value("rendering", "surface_mode", render_surface_mode))
    _select_config_surface_mode(_runtime_string("AETHERKIRI_SURFACE_MODE", render_surface_mode))
    show_perf_monitor = bool(cfg.get_value("rendering", "perf_overlay", show_perf_monitor))
    frame_limit_enabled = bool(cfg.get_value("rendering", "fps_limit_enabled", frame_limit_enabled))
    target_fps = int(cfg.get_value("rendering", "target_fps", target_fps))
    lock_landscape = bool(cfg.get_value("rendering", "force_landscape", lock_landscape))
    plugin_trace = bool(cfg.get_value("developer", "plugin_trace", plugin_trace))
    plugin_load_mode = String(cfg.get_value("developer", "plugin_load_mode", plugin_load_mode))
    if not plugin_load_mode in ["krkrsdl3", "aether_all"]:
        plugin_load_mode = "krkrsdl3"
    mock_enabled = bool(cfg.get_value("developer", "mock_enabled", mock_enabled))
    console_log_file = bool(cfg.get_value("developer", "console_log_file", console_log_file))
    trace_log = bool(cfg.get_value("developer", "trace_log", trace_log))
    export_scripts = bool(cfg.get_value("developer", "export_scripts", export_scripts))
    log_alerts = bool(cfg.get_value("developer", "log_alerts", log_alerts))
    error_dialog_logs = bool(cfg.get_value("developer", "error_dialog_logs", error_dialog_logs))

func _configure_runtime_diagnostics() -> void:
    diagnostics_enabled = _runtime_flag("AETHERKIRI_DIAGNOSTICS")
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

func _save_shell_settings() -> void:
    var cfg := ConfigFile.new()
    cfg.set_value("interface", "language", language_mode)
    cfg.set_value("rendering", "backend", selected_backend)
    cfg.set_value("rendering", "upscale_algorithm", upscale_algorithm)
    cfg.set_value("rendering", "surface_mode", render_surface_mode)
    cfg.set_value("rendering", "perf_overlay", show_perf_monitor)
    cfg.set_value("rendering", "fps_limit_enabled", frame_limit_enabled)
    cfg.set_value("rendering", "target_fps", target_fps)
    cfg.set_value("rendering", "force_landscape", lock_landscape)
    cfg.set_value("developer", "plugin_trace", plugin_trace)
    cfg.set_value("developer", "plugin_load_mode", plugin_load_mode)
    cfg.set_value("developer", "mock_enabled", mock_enabled)
    cfg.set_value("developer", "console_log_file", console_log_file)
    cfg.set_value("developer", "trace_log", trace_log)
    cfg.set_value("developer", "export_scripts", export_scripts)
    cfg.set_value("developer", "log_alerts", log_alerts)
    cfg.set_value("developer", "error_dialog_logs", error_dialog_logs)
    cfg.save(SETTINGS_FILE)
    ProjectSettings.set_setting(SETTINGS_KEY, selected_backend)
    _apply_engine_options()
    _apply_shell_runtime_settings()
    dirty_settings = false
    if save_button != null:
        save_button.disabled = true

func _mark_settings_dirty() -> void:
    dirty_settings = true
    if save_button != null:
        save_button.disabled = false

func _apply_engine_options() -> void:
    if player == null:
        return
    if not player.is_initialized():
        return
    var effective_plugin_load_mode := _runtime_string("AETHERKIRI_PLUGIN_LOAD_MODE", plugin_load_mode)
    if not effective_plugin_load_mode in ["krkrsdl3", "aether_all"]:
        effective_plugin_load_mode = "krkrsdl3"
    var effective_plugin_trace := plugin_trace or _runtime_flag("AETHERKIRI_PLUGIN_TRACE", false)
    player.set_engine_option("fps_limit", str(target_fps) if frame_limit_enabled else "0")
    player.set_engine_option("plugin_load_mode", effective_plugin_load_mode)
    player.set_engine_option("plugin_trace", "1" if effective_plugin_trace else "0")
    player.set_engine_option("mock_enabled", "1" if mock_enabled else "0")
    player.set_engine_option("console_log_file", "1" if console_log_file else "0")
    player.set_engine_option("trace_log", "1" if trace_log else "0")
    player.set_engine_option("export_scripts", "1" if export_scripts else "0")
    player.set_engine_option("error_dialog_logs", "1" if error_dialog_logs else "0")
    if not runtime_default_font_path.is_empty():
        player.set_engine_option("default_font", runtime_default_font_path)
    if not runtime_font_dir_path.is_empty():
        player.set_engine_option("font_dir", runtime_font_dir_path)
    player.set_engine_option("error_dialog_logs", "1" if error_dialog_logs else "0")

func _apply_shell_runtime_settings() -> void:
    if OS.get_name() == "iOS" or OS.get_name() == "Android":
        var orientation := DisplayServer.SCREEN_LANDSCAPE if lock_landscape else DisplayServer.SCREEN_SENSOR
        DisplayServer.screen_set_orientation(orientation)

func _fit_full_rects() -> void:
    var window_size := get_viewport_rect().size
    anchor_left = 0.0
    anchor_top = 0.0
    anchor_right = 0.0
    anchor_bottom = 0.0
    position = Vector2.ZERO
    size = window_size
    var controls: Array[Control] = [bg_rect, game_view, shell_root, home_view, settings_view, detail_view, detail_scroll, modal_layer]
    for control in controls:
        if control == null:
            continue
        control.set_anchors_preset(Control.PRESET_FULL_RECT)
        control.offset_left = 0.0
        control.offset_top = 0.0
        control.offset_right = 0.0
        control.offset_bottom = 0.0
    _layout_game_viewport(window_size)
    _layout_home_view(window_size)

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

func _apply_upscale_algorithm() -> void:
    if viewport == null:
        return
    match upscale_algorithm:
        "nearest":
            viewport.texture_filter = CanvasItem.TEXTURE_FILTER_NEAREST
            viewport.material = _opaque_frame_material()
        "linear", "smooth":
            viewport.texture_filter = CanvasItem.TEXTURE_FILTER_LINEAR
            viewport.material = _opaque_frame_material()
        _:
            viewport.texture_filter = CanvasItem.TEXTURE_FILTER_LINEAR
            viewport.material = _opaque_frame_material()

func _set_game_background(active: bool) -> void:
    var color := COLOR_GAME_BG if active else COLOR_BG
    if bg_rect != null:
        bg_rect.color = color
    RenderingServer.set_default_clear_color(color)

func _layout_home_view(window_size: Vector2) -> void:
    if game_scroll == null or game_list == null:
        return
    var margin := 40.0
    var list_top := 188.0
    var bottom_reserved := 118.0
    var list_width := maxf(260.0, window_size.x - margin * 2.0)
    var list_height := maxf(160.0, window_size.y - list_top - bottom_reserved)
    game_scroll.position = Vector2(margin, list_top)
    game_scroll.size = Vector2(list_width, list_height)
    game_scroll.custom_minimum_size = game_scroll.size

    var gap := 18.0
    var columns := maxi(1, int(floor((list_width + gap) / (HOME_CARD_SIZE.x + gap))))
    game_list.columns = columns
    game_list.custom_minimum_size = Vector2(list_width, 0)

    if home_actions != null:
        home_actions.anchor_left = 1.0
        home_actions.anchor_top = 1.0
        home_actions.anchor_right = 1.0
        home_actions.anchor_bottom = 1.0
        home_actions.offset_left = -430.0
        home_actions.offset_top = -96.0
        home_actions.offset_right = -40.0
        home_actions.offset_bottom = -36.0
        home_actions.move_to_front()

func _build_home_view() -> void:
    home_view = Control.new()
    home_view.set_anchors_preset(Control.PRESET_FULL_RECT)
    shell_root.add_child(home_view)

    var title := Label.new()
    title.text = "AetherKiri"
    title.position = Vector2(42, 38)
    title.add_theme_font_size_override("font_size", 36)
    title.add_theme_color_override("font_color", COLOR_TEXT)
    home_view.add_child(title)

    home_subtitle_label = Label.new()
    home_subtitle_label.text = _t("home.subtitle")
    home_subtitle_label.position = Vector2(44, 84)
    home_subtitle_label.add_theme_font_size_override("font_size", 17)
    home_subtitle_label.add_theme_color_override("font_color", COLOR_MUTED)
    home_view.add_child(home_subtitle_label)

    var status_pill := PanelContainer.new()
    status_pill.position = Vector2(42, 122)
    status_pill.size = Vector2(284, 42)
    status_pill.add_theme_stylebox_override("panel", _panel_style(8, COLOR_CARD_ALT, COLOR_LINE, 1))
    home_view.add_child(status_pill)
    home_status_label = Label.new()
    home_status_label.text = _t("home.status")
    home_status_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
    home_status_label.add_theme_font_size_override("font_size", 15)
    home_status_label.add_theme_color_override("font_color", COLOR_ACCENT_SOFT)
    status_pill.add_child(home_status_label)

    var settings_button := _icon_button(ICON_SETTINGS)
    settings_button.anchor_left = 1.0
    settings_button.anchor_right = 1.0
    settings_button.position = Vector2(-100, 42)
    settings_button.pressed.connect(_show_settings)
    home_view.add_child(settings_button)

    game_scroll = ScrollContainer.new()
    game_scroll.position = Vector2(32, 164)
    game_scroll.size = Vector2(390, 500)
    game_scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
    game_scroll.vertical_scroll_mode = ScrollContainer.SCROLL_MODE_AUTO
    home_view.add_child(game_scroll)

    game_list = GridContainer.new()
    game_list.columns = 1
    game_list.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    game_list.add_theme_constant_override("h_separation", 18)
    game_list.add_theme_constant_override("v_separation", 18)
    game_scroll.add_child(game_list)

    empty_state = VBoxContainer.new()
    empty_state.anchor_left = 0.5
    empty_state.anchor_top = 0.5
    empty_state.anchor_right = 0.5
    empty_state.anchor_bottom = 0.5
    empty_state.position = Vector2(-260, -120)
    empty_state.size = Vector2(520, 240)
    empty_state.add_theme_constant_override("separation", 18)
    home_view.add_child(empty_state)

    var empty_icon := _centered_icon(ICON_LIBRARY, Vector2(64, 64), COLOR_ACCENT)
    empty_icon.custom_minimum_size = Vector2(0, 72)
    empty_state.add_child(empty_icon)

    empty_title_label = Label.new()
    empty_title_label.text = _t("home.empty_title")
    empty_title_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
    empty_title_label.add_theme_font_size_override("font_size", 28)
    empty_title_label.add_theme_color_override("font_color", COLOR_TEXT)
    empty_state.add_child(empty_title_label)

    empty_help_label = Label.new()
    empty_help_label.text = _empty_help_text()
    empty_help_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
    empty_help_label.add_theme_font_size_override("font_size", 18)
    empty_help_label.add_theme_color_override("font_color", COLOR_MUTED)
    empty_state.add_child(empty_help_label)

    home_actions = HBoxContainer.new()
    home_actions.anchor_left = 1.0
    home_actions.anchor_top = 1.0
    home_actions.anchor_right = 1.0
    home_actions.anchor_bottom = 1.0
    home_actions.position = Vector2(-430, -96)
    home_actions.size = Vector2(390, 60)
    home_actions.add_theme_constant_override("separation", 12)
    home_view.add_child(home_actions)

    home_primary_button = _pill_button(_t("home.refresh") if OS.get_name() == "iOS" else _t("home.import"), ICON_REFRESH if OS.get_name() == "iOS" else ICON_ADD)
    home_primary_button.custom_minimum_size = Vector2(154, 56)
    home_primary_button.pressed.connect(_on_refresh_or_import)
    home_actions.add_child(home_primary_button)

    home_guide_button = _pill_button(_t("home.import_guide"), ICON_HELP)
    home_guide_button.custom_minimum_size = Vector2(198, 56)
    home_guide_button.pressed.connect(_show_import_guide)
    home_actions.add_child(home_guide_button)

func _build_settings_view() -> void:
    settings_view = ScrollContainer.new()
    settings_view.set_anchors_preset(Control.PRESET_FULL_RECT)
    settings_view.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
    settings_view.vertical_scroll_mode = ScrollContainer.SCROLL_MODE_AUTO
    settings_view.visible = false
    shell_root.add_child(settings_view)

func _rebuild_settings_view() -> void:
    for child in settings_view.get_children():
        settings_view.remove_child(child)
        child.queue_free()

    var margin := MarginContainer.new()
    margin.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    margin.add_theme_constant_override("margin_left", 40)
    margin.add_theme_constant_override("margin_top", 24)
    margin.add_theme_constant_override("margin_right", 40)
    margin.add_theme_constant_override("margin_bottom", 40)
    settings_view.add_child(margin)

    var page := VBoxContainer.new()
    page.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    page.add_theme_constant_override("separation", 26)
    margin.add_child(page)

    var top := HBoxContainer.new()
    top.custom_minimum_size = Vector2(0, 96)
    top.add_theme_constant_override("separation", 18)
    page.add_child(top)

    var back := _icon_button(ICON_HOME)
    back.custom_minimum_size = TOP_ICON_BUTTON_SIZE
    back.pressed.connect(_show_home)
    top.add_child(back)

    var title := Label.new()
    title.text = _t("settings.title")
    title.horizontal_alignment = HORIZONTAL_ALIGNMENT_LEFT
    title.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
    title.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    title.add_theme_font_size_override("font_size", 30)
    title.add_theme_color_override("font_color", COLOR_TEXT)
    top.add_child(title)

    save_button = _pill_button(_t("settings.save"), ICON_SAVE)
    save_button.disabled = not dirty_settings
    save_button.custom_minimum_size = TOP_ACTION_BUTTON_SIZE
    save_button.size_flags_vertical = Control.SIZE_SHRINK_CENTER
    save_button.pressed.connect(_save_shell_settings)
    top.add_child(save_button)

    page.add_child(_section_title(_t("settings.section.interface"), ICON_SETTINGS))
    var interface_card := _settings_card()
    page.add_child(interface_card)
    interface_card.add_child(_settings_block(_t("settings.language"), _t("settings.language_desc"), _language_select()))

    page.add_child(_section_title(_t("settings.section.render"), ICON_PERFORMANCE))
    var render_card := _settings_card()
    page.add_child(render_card)
    render_card.add_child(_settings_block(_t("settings.render_backend"), _t("settings.render_backend_desc"), _backend_segment()))
    render_card.add_child(_settings_block(_t("settings.surface_mode"), _t("settings.surface_mode_desc"), _surface_mode_select()))
    render_card.add_child(_settings_block(_t("settings.upscale"), _t("settings.upscale_desc"), _upscale_select()))
    render_card.add_child(_settings_toggle_row(_t("settings.perf"), _t("settings.perf_desc"), show_perf_monitor, "perf"))
    render_card.add_child(_settings_toggle_row(_t("settings.fps_limit"), _t("settings.fps_limit_desc"), frame_limit_enabled, "fps_limit"))
    if frame_limit_enabled:
        render_card.add_child(_settings_fps_row())
    if OS.get_name() == "iOS" or OS.get_name() == "Android":
        render_card.add_child(_settings_toggle_row(_t("settings.landscape"), _t("settings.landscape_desc"), lock_landscape, "landscape"))

    page.add_child(_section_title(_t("settings.section.developer"), ICON_PLUGIN))
    var dev_card := _settings_card()
    page.add_child(dev_card)
    dev_card.add_child(_settings_block(_t("settings.plugin_load_mode"), _t("settings.plugin_load_mode_desc"), _plugin_load_mode_select()))
    dev_card.add_child(_settings_toggle_row(_t("settings.plugin_trace"), _t("settings.plugin_trace_desc"), plugin_trace, "plugin_trace"))
    dev_card.add_child(_settings_toggle_row(_t("settings.mock"), _t("settings.mock_desc"), mock_enabled, "mock"))
    dev_card.add_child(_settings_toggle_row(_t("settings.console_log"), _t("settings.console_log_desc"), console_log_file, "console_log"))
    dev_card.add_child(_settings_toggle_row(_t("settings.trace_log"), _t("settings.trace_log_desc"), trace_log, "trace_log"))
    dev_card.add_child(_settings_toggle_row(_t("settings.export_tjs"), _t("settings.export_tjs_desc"), export_scripts, "export_tjs"))
    dev_card.add_child(_settings_toggle_row(_t("settings.log_alerts"), _t("settings.log_alerts_desc"), log_alerts, "log_alerts"))
    dev_card.add_child(_settings_toggle_row(_t("settings.error_dialog_logs"), _t("settings.error_dialog_logs_desc"), error_dialog_logs, "error_dialog_logs"))

    page.add_child(_section_title(_t("settings.section.about"), ICON_HELP))
    var about_card := _settings_card()
    page.add_child(about_card)
    about_card.add_child(_settings_value_row(_t("settings.version"), "0.2.0-beta.1"))

func _build_detail_view() -> void:
    detail_view = Control.new()
    detail_view.set_anchors_preset(Control.PRESET_FULL_RECT)
    detail_view.visible = false
    shell_root.add_child(detail_view)

    detail_scroll = ScrollContainer.new()
    detail_scroll.set_anchors_preset(Control.PRESET_FULL_RECT)
    detail_scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
    detail_scroll.vertical_scroll_mode = ScrollContainer.SCROLL_MODE_AUTO
    detail_view.add_child(detail_scroll)

func _build_modal_layer() -> void:
    modal_layer = Control.new()
    modal_layer.set_anchors_preset(Control.PRESET_FULL_RECT)
    modal_layer.visible = false
    add_child(modal_layer)

func _build_loading_panel() -> void:
    loading_panel = PanelContainer.new()
    loading_panel.set_anchors_preset(Control.PRESET_FULL_RECT)
    loading_panel.mouse_filter = Control.MOUSE_FILTER_STOP
    loading_panel.visible = false
    loading_panel.add_theme_stylebox_override("panel", _panel_style(0, Color(0.060, 0.064, 0.086, 0.97), Color(0, 0, 0, 0), 0))
    add_child(loading_panel)

    var margin := MarginContainer.new()
    margin.add_theme_constant_override("margin_left", 34)
    margin.add_theme_constant_override("margin_top", 30)
    margin.add_theme_constant_override("margin_right", 34)
    margin.add_theme_constant_override("margin_bottom", 30)
    loading_panel.add_child(margin)

    var box := VBoxContainer.new()
    box.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    box.size_flags_vertical = Control.SIZE_EXPAND_FILL
    box.add_theme_constant_override("separation", 16)
    margin.add_child(box)

    loading_title_label = Label.new()
    loading_title_label.text = _t("loading.title")
    loading_title_label.add_theme_font_size_override("font_size", 28)
    loading_title_label.add_theme_color_override("font_color", COLOR_TEXT)
    box.add_child(loading_title_label)

    if ui_log_enabled and not _mobile_runtime():
        log_view = TextEdit.new()
        log_view.size_flags_horizontal = Control.SIZE_EXPAND_FILL
        log_view.size_flags_vertical = Control.SIZE_EXPAND_FILL
        log_view.mouse_filter = Control.MOUSE_FILTER_IGNORE
        log_view.editable = false
        log_view.wrap_mode = TextEdit.LINE_WRAPPING_BOUNDARY
        log_view.scroll_fit_content_height = false
        log_view.add_theme_font_size_override("font_size", 18)
        log_view.add_theme_color_override("font_color", Color(0.90, 0.92, 0.98, 1))
        log_view.add_theme_color_override("background_color", Color(0, 0, 0, 0))
        box.add_child(log_view)

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

func _empty_style() -> StyleBoxEmpty:
    return StyleBoxEmpty.new()

func _focus_outline(radius: int = 8) -> StyleBoxFlat:
    var style := StyleBoxFlat.new()
    style.bg_color = Color(0, 0, 0, 0)
    style.border_color = COLOR_ACCENT
    style.border_width_left = 3
    style.border_width_top = 3
    style.border_width_right = 3
    style.border_width_bottom = 3
    style.corner_radius_top_left = radius
    style.corner_radius_top_right = radius
    style.corner_radius_bottom_left = radius
    style.corner_radius_bottom_right = radius
    style.expand_margin_left = 3
    style.expand_margin_top = 3
    style.expand_margin_right = 3
    style.expand_margin_bottom = 3
    return style

func _rounded_card_material(rect_size: Vector2 = HOME_CARD_SIZE, radius: float = 8.0) -> ShaderMaterial:
    if rounded_card_shader == null:
        rounded_card_shader = Shader.new()
        rounded_card_shader.code = """
shader_type canvas_item;
uniform float radius = 8.0;
uniform vec2 rect_size = vec2(272.0, 368.0);
void fragment() {
    vec4 color = texture(TEXTURE, UV);
    vec2 p = UV * rect_size;
    vec2 half_size = rect_size * 0.5;
    vec2 q = abs(p - half_size) - (half_size - vec2(radius));
    float d = length(max(q, vec2(0.0))) + min(max(q.x, q.y), 0.0) - radius;
    color.a *= 1.0 - smoothstep(0.0, 1.5, d);
    COLOR = color;
}
"""
    var material := ShaderMaterial.new()
    material.shader = rounded_card_shader
    material.set_shader_parameter("radius", radius)
    material.set_shader_parameter("rect_size", rect_size)
    return material

func _game_card_border_style(active: bool) -> StyleBoxFlat:
    var style := StyleBoxFlat.new()
    style.bg_color = Color(0, 0, 0, 0)
    style.draw_center = false
    style.border_color = COLOR_ACCENT if active else COLOR_LINE
    var width := 3 if active else 1
    style.border_width_left = width
    style.border_width_top = width
    style.border_width_right = width
    style.border_width_bottom = width
    style.corner_radius_top_left = 8
    style.corner_radius_top_right = 8
    style.corner_radius_bottom_left = 8
    style.corner_radius_bottom_right = 8
    return style

func _set_game_card_border(button: Button, border: PanelContainer, active: bool) -> void:
    border.add_theme_stylebox_override("panel", _game_card_border_style(active))
    button.set_meta("card_border_active", active)

func _load_ui_icon(icon_path: String):
    if icon_path.is_empty():
        return null
    if not ui_icon_cache.has(icon_path):
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

func _icon_rect(icon_path: String, size: Vector2, tint: Color = COLOR_TEXT) -> TextureRect:
    var icon := TextureRect.new()
    icon.texture = _load_ui_icon(icon_path)
    icon.mouse_filter = Control.MOUSE_FILTER_IGNORE
    icon.custom_minimum_size = size
    icon.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
    icon.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
    icon.modulate = tint
    return icon

func _centered_icon(icon_path: String, size: Vector2, tint: Color = COLOR_TEXT) -> CenterContainer:
    var holder := CenterContainer.new()
    holder.mouse_filter = Control.MOUSE_FILTER_IGNORE
    holder.add_child(_icon_rect(icon_path, size, tint))
    return holder

func _apply_button_style(button: Button, normal: StyleBox, hover: StyleBox, pressed: StyleBox, disabled: StyleBox = null) -> void:
    button.add_theme_stylebox_override("normal", normal)
    button.add_theme_stylebox_override("hover", hover)
    button.add_theme_stylebox_override("pressed", pressed)
    button.add_theme_stylebox_override("focus", _focus_outline(8))
    if disabled != null:
        button.add_theme_stylebox_override("disabled", disabled)
    button.add_theme_color_override("font_hover_color", button.get_theme_color("font_color"))
    button.add_theme_color_override("font_pressed_color", button.get_theme_color("font_color"))
    button.add_theme_color_override("font_focus_color", button.get_theme_color("font_color"))
    button.add_theme_color_override("font_disabled_color", Color(1, 1, 1, 0.72))

func _pill_button(text: String, icon_path: String = "") -> Button:
    var button := Button.new()
    button.text = text
    button.alignment = HORIZONTAL_ALIGNMENT_CENTER
    button.clip_text = true
    button.focus_mode = Control.FOCUS_ALL
    button.add_theme_font_size_override("font_size", 20)
    button.add_theme_color_override("font_color", Color.WHITE)
    if not icon_path.is_empty():
        button.icon = _load_ui_icon(icon_path)
        button.expand_icon = true
        button.icon_alignment = HORIZONTAL_ALIGNMENT_LEFT
        button.add_theme_constant_override("icon_max_width", 24)
        button.add_theme_constant_override("h_separation", 10)
    _apply_button_style(
        button,
        _panel_style(10, COLOR_ACCENT, COLOR_ACCENT, 0),
        _panel_style(10, COLOR_ACCENT.lightened(0.06), COLOR_ACCENT.lightened(0.08), 0),
        _panel_style(10, COLOR_ACCENT.darkened(0.10), COLOR_ACCENT.darkened(0.10), 0),
        _panel_style(10, Color(0.28, 0.30, 0.38, 1), Color(0.28, 0.30, 0.38, 1), 0)
    )
    return button

func _icon_button(icon_path: String) -> Button:
    var button := Button.new()
    button.text = ""
    button.icon = _load_ui_icon(icon_path)
    button.alignment = HORIZONTAL_ALIGNMENT_CENTER
    button.expand_icon = true
    button.icon_alignment = HORIZONTAL_ALIGNMENT_CENTER
    button.focus_mode = Control.FOCUS_ALL
    button.custom_minimum_size = TOP_ICON_BUTTON_SIZE
    button.size_flags_horizontal = Control.SIZE_SHRINK_CENTER
    button.size_flags_vertical = Control.SIZE_SHRINK_CENTER
    button.add_theme_constant_override("icon_max_width", 28)
    button.add_theme_color_override("font_color", COLOR_TEXT)
    button.add_theme_color_override("font_hover_color", COLOR_TEXT)
    button.add_theme_color_override("font_pressed_color", COLOR_TEXT)
    button.add_theme_color_override("font_focus_color", COLOR_TEXT)
    _apply_button_style(
        button,
        _panel_style(8, COLOR_CARD_ALT, COLOR_LINE, 1),
        _panel_style(8, COLOR_CARD_HOVER, COLOR_ACCENT, 1),
        _panel_style(8, COLOR_ACCENT_DIM, COLOR_ACCENT, 1)
    )
    return button

func _section_title(text: String, icon_path: String) -> HBoxContainer:
    var row := HBoxContainer.new()
    row.custom_minimum_size = Vector2(0, 34)
    row.add_theme_constant_override("separation", 10)
    row.add_child(_icon_rect(icon_path, Vector2(22, 22), COLOR_ACCENT_SOFT))
    var label := Label.new()
    label.text = text
    label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
    label.add_theme_font_size_override("font_size", 19)
    label.add_theme_color_override("font_color", COLOR_ACCENT_SOFT)
    row.add_child(label)
    return row

func _settings_card() -> VBoxContainer:
    var box := VBoxContainer.new()
    box.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    box.add_theme_constant_override("separation", 12)
    return box

func _settings_block(title: String, subtitle: String, control: Control) -> Control:
    var panel := PanelContainer.new()
    panel.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    panel.add_theme_stylebox_override("panel", _panel_style(8, COLOR_CARD, COLOR_LINE, 1))
    var box := VBoxContainer.new()
    box.custom_minimum_size = Vector2(0, 116)
    box.add_theme_constant_override("separation", 8)
    panel.add_child(box)
    var title_label := Label.new()
    title_label.text = title
    title_label.add_theme_font_size_override("font_size", 20)
    title_label.add_theme_color_override("font_color", COLOR_TEXT)
    box.add_child(title_label)
    if not subtitle.is_empty():
        var sub := Label.new()
        sub.text = subtitle
        sub.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
        sub.add_theme_font_size_override("font_size", 16)
        sub.add_theme_color_override("font_color", COLOR_MUTED)
        box.add_child(sub)
    box.add_child(control)
    return panel

func _settings_toggle_row(title: String, subtitle: String, initial: bool, key: String) -> Control:
    var panel := PanelContainer.new()
    panel.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    panel.add_theme_stylebox_override("panel", _panel_style(8, COLOR_CARD, COLOR_LINE, 1))
    var row := HBoxContainer.new()
    row.custom_minimum_size = Vector2(0, 92)
    row.add_theme_constant_override("separation", 18)
    panel.add_child(row)
    var labels := VBoxContainer.new()
    labels.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    labels.add_theme_constant_override("separation", 6)
    var title_label := Label.new()
    title_label.text = title
    title_label.add_theme_font_size_override("font_size", 20)
    title_label.add_theme_color_override("font_color", COLOR_TEXT)
    labels.add_child(title_label)
    var sub := Label.new()
    sub.text = subtitle
    sub.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
    sub.add_theme_font_size_override("font_size", 16)
    sub.add_theme_color_override("font_color", COLOR_MUTED)
    labels.add_child(sub)
    row.add_child(labels)

    var toggle := CheckButton.new()
    toggle.button_pressed = initial
    toggle.focus_mode = Control.FOCUS_ALL
    toggle.custom_minimum_size = Vector2(92, 54)
    toggle.toggled.connect(func(value: bool): _on_setting_toggle(key, value))
    row.add_child(toggle)
    return panel

func _settings_value_row(title: String, value: String) -> Control:
    var panel := PanelContainer.new()
    panel.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    panel.add_theme_stylebox_override("panel", _panel_style(8, COLOR_CARD, COLOR_LINE, 1))
    var row := HBoxContainer.new()
    row.custom_minimum_size = Vector2(0, 64)
    row.add_theme_constant_override("separation", 18)
    panel.add_child(row)
    var label := Label.new()
    label.text = title
    label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
    label.add_theme_font_size_override("font_size", 19)
    label.add_theme_color_override("font_color", COLOR_TEXT)
    row.add_child(label)
    var value_label := Label.new()
    value_label.text = value
    value_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
    value_label.add_theme_font_size_override("font_size", 17)
    value_label.add_theme_color_override("font_color", COLOR_ACCENT)
    row.add_child(value_label)
    return panel

func _settings_fps_row() -> Control:
    var panel := PanelContainer.new()
    panel.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    panel.add_theme_stylebox_override("panel", _panel_style(8, COLOR_CARD, COLOR_LINE, 1))
    var row := HBoxContainer.new()
    row.custom_minimum_size = Vector2(0, 86)
    row.add_theme_constant_override("separation", 18)
    panel.add_child(row)
    var labels := VBoxContainer.new()
    labels.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    labels.add_theme_constant_override("separation", 6)
    var title_label := Label.new()
    title_label.text = _t("settings.target_fps")
    title_label.add_theme_font_size_override("font_size", 20)
    title_label.add_theme_color_override("font_color", COLOR_TEXT)
    labels.add_child(title_label)
    var sub := Label.new()
    sub.text = _t("settings.target_fps_desc")
    sub.add_theme_font_size_override("font_size", 16)
    sub.add_theme_color_override("font_color", COLOR_MUTED)
    labels.add_child(sub)
    row.add_child(labels)

    var fps_select := OptionButton.new()
    fps_select.custom_minimum_size = Vector2(170, 54)
    var options := [80, 90, 120, 144]
    var selected_index := 0
    for i in range(options.size()):
        fps_select.add_item("%d FPS" % options[i])
        fps_select.set_item_metadata(i, options[i])
        if options[i] == target_fps:
            selected_index = i
    fps_select.select(selected_index)
    fps_select.item_selected.connect(func(index: int):
        target_fps = int(fps_select.get_item_metadata(index))
        _mark_settings_dirty()
        _apply_engine_options()
    )
    row.add_child(fps_select)
    return panel

func _language_select() -> OptionButton:
    var select := OptionButton.new()
    select.custom_minimum_size = Vector2(360, 58)
    var selected_index := 0
    for i in range(LANGUAGE_MODES.size()):
        var mode := String(LANGUAGE_MODES[i])
        select.add_item(_language_option_label(mode))
        select.set_item_metadata(i, mode)
        if mode == language_mode:
            selected_index = i
    select.select(selected_index)
    select.item_selected.connect(func(index: int):
        _select_language_mode(String(select.get_item_metadata(index)))
    )
    return select

func _upscale_select() -> OptionButton:
    var select := OptionButton.new()
    select.custom_minimum_size = Vector2(360, 58)
    var options := [
        {"label": "Smooth", "value": "smooth"},
        {"label": "Linear", "value": "linear"},
        {"label": "Nearest", "value": "nearest"},
    ]
    var selected_index := 0
    for i in range(options.size()):
        select.add_item(String(options[i]["label"]))
        select.set_item_metadata(i, String(options[i]["value"]))
        if String(options[i]["value"]) == upscale_algorithm:
            selected_index = i
    select.select(selected_index)
    select.item_selected.connect(func(index: int):
        _select_upscale_algorithm(String(select.get_item_metadata(index)))
    )
    return select

func _surface_mode_select() -> OptionButton:
    var select := OptionButton.new()
    select.custom_minimum_size = Vector2(360, 58)
    var options := [
        {"label": "Game Native", "value": RENDER_SURFACE_MODE_GAME},
        {"label": "Display Fit", "value": RENDER_SURFACE_MODE_DISPLAY},
    ]
    var selected_index := 0
    for i in range(options.size()):
        select.add_item(String(options[i]["label"]))
        select.set_item_metadata(i, String(options[i]["value"]))
        if String(options[i]["value"]) == render_surface_mode:
            selected_index = i
    select.select(selected_index)
    select.item_selected.connect(func(index: int):
        _select_surface_mode(String(select.get_item_metadata(index)))
    )
    return select

func _plugin_load_mode_select() -> OptionButton:
    var select := OptionButton.new()
    select.custom_minimum_size = Vector2(360, 58)
    var options := [
        {"label": "krkrsdl3", "value": "krkrsdl3"},
        {"label": "aether_all", "value": "aether_all"},
    ]
    var selected_index := 0
    for i in range(options.size()):
        select.add_item(String(options[i]["label"]))
        select.set_item_metadata(i, String(options[i]["value"]))
        if String(options[i]["value"]) == plugin_load_mode:
            selected_index = i
    select.select(selected_index)
    select.item_selected.connect(func(index: int):
        _select_plugin_load_mode(String(select.get_item_metadata(index)))
    )
    return select

func _segment_button(text: String, selected: bool) -> Button:
    var button := Button.new()
    button.text = text
    button.alignment = HORIZONTAL_ALIGNMENT_CENTER
    button.clip_text = true
    button.toggle_mode = true
    button.button_pressed = selected
    button.focus_mode = Control.FOCUS_ALL
    button.custom_minimum_size = Vector2(230, 56)
    button.add_theme_font_size_override("font_size", 18)
    button.add_theme_color_override("font_color", COLOR_TEXT)
    var selected_style := _panel_style(8, COLOR_ACCENT_DIM, COLOR_ACCENT, 1)
    var selected_hover_style := _panel_style(8, COLOR_ACCENT_DIM.lightened(0.06), COLOR_ACCENT_SOFT, 1)
    var normal_style := _panel_style(8, COLOR_CARD_ALT, COLOR_LINE, 1)
    var normal_hover_style := _panel_style(8, COLOR_CARD_HOVER, COLOR_ACCENT, 1)
    _apply_button_style(
        button,
        selected_style if selected else normal_style,
        selected_hover_style if selected else normal_hover_style,
        selected_style
    )
    return button

func _backend_segment() -> HBoxContainer:
    var row := HBoxContainer.new()
    row.add_theme_constant_override("separation", 8)
    var native := _segment_button("Godot Native", selected_backend != "Debug CPU")
    native.pressed.connect(func(): _select_backend("Godot Native"))
    row.add_child(native)
    var cpu := _segment_button("Debug CPU", selected_backend == "Debug CPU")
    cpu.pressed.connect(func(): _select_backend("Debug CPU"))
    row.add_child(cpu)
    return row

func _theme_segment() -> HBoxContainer:
    var row := HBoxContainer.new()
    row.add_theme_constant_override("separation", 8)
    row.add_child(_segment_button(_t("language.system"), true))
    row.add_child(_segment_button("Dark", false))
    row.add_child(_segment_button("Light", false))
    return row

func _on_setting_toggle(key: String, value: bool) -> void:
    if key == "perf":
        show_perf_monitor = value
        perf.visible = game_running and show_perf_monitor
    elif key == "fps_limit":
        frame_limit_enabled = value
    elif key == "landscape":
        lock_landscape = value
    elif key == "plugin_trace":
        plugin_trace = value
    elif key == "mock":
        mock_enabled = value
    elif key == "console_log":
        console_log_file = value
    elif key == "trace_log":
        trace_log = value
    elif key == "export_tjs":
        export_scripts = value
    elif key == "log_alerts":
        log_alerts = value
    elif key == "error_dialog_logs":
        error_dialog_logs = value
    _mark_settings_dirty()
    _apply_engine_options()
    _apply_shell_runtime_settings()
    if key == "fps_limit":
        call_deferred("_rebuild_settings_view")

func _select_backend(value: String) -> void:
    var index := BACKENDS.find(value)
    if index < 0:
        return
    backend.select(index)
    _on_backend_selected(index)
    _mark_settings_dirty()
    call_deferred("_rebuild_settings_view")

func _select_upscale_algorithm(value: String) -> void:
    if not value in ["smooth", "nearest", "linear"]:
        return
    upscale_algorithm = value
    _apply_upscale_algorithm()
    _mark_settings_dirty()

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
    render_surface_mode = value
    _mark_settings_dirty()
    if game_running:
        _sync_player_surface_size(true)

func _select_plugin_load_mode(value: String) -> void:
    if not value in ["krkrsdl3", "aether_all"]:
        return
    plugin_load_mode = value
    _mark_settings_dirty()
    _apply_engine_options()

func _select_language_mode(value: String) -> void:
    var next_language := _normalize_language_mode(value)
    if next_language == language_mode:
        return
    language_mode = next_language
    _apply_language_mode()
    _mark_settings_dirty()
    _refresh_language_texts()
    if settings_view != null and settings_view.visible:
        call_deferred("_rebuild_settings_view")
    if detail_view != null and detail_view.visible and not selected_game.is_empty():
        call_deferred("_show_detail", selected_game)
    _refresh_games()

func _refresh_language_texts() -> void:
    if is_instance_valid(home_subtitle_label):
        home_subtitle_label.text = _t("home.subtitle")
    if is_instance_valid(home_status_label):
        home_status_label.text = _t("home.status")
    if is_instance_valid(empty_title_label):
        empty_title_label.text = _t("home.empty_title")
    if is_instance_valid(empty_help_label):
        empty_help_label.text = _empty_help_text()
    if is_instance_valid(home_primary_button):
        home_primary_button.text = _t("home.refresh") if OS.get_name() == "iOS" else _t("home.import")
    if is_instance_valid(home_guide_button):
        home_guide_button.text = _t("home.import_guide")
    if is_instance_valid(loading_title_label):
        loading_title_label.text = _t("loading.title")

func _empty_help_text() -> String:
    if OS.get_name() == "iOS":
        return _t("home.empty_help_ios")
    if OS.get_name() == "Web":
        return _t("home.empty_help_web")
    return _t("home.empty_help_desktop")

func _show_home() -> void:
    if dirty_settings:
        _save_shell_settings()
    _set_game_background(false)
    home_view.visible = true
    settings_view.visible = false
    detail_view.visible = false
    modal_layer.visible = false
    _refresh_games()

func _show_settings() -> void:
    _set_game_background(false)
    _rebuild_settings_view()
    home_view.visible = false
    settings_view.visible = true
    detail_view.visible = false
    modal_layer.visible = false

func _show_detail(game: Dictionary) -> void:
    _set_game_background(false)
    selected_game = game
    home_view.visible = false
    settings_view.visible = false
    detail_view.visible = true
    modal_layer.visible = false
    for child in detail_scroll.get_children():
        child.queue_free()

    var window_size := get_viewport_rect().size
    var content := Control.new()
    content.custom_minimum_size = Vector2(maxf(1280.0, window_size.x), maxf(840.0, window_size.y))
    content.mouse_filter = Control.MOUSE_FILTER_PASS
    detail_scroll.add_child(content)

    var back := _icon_button(ICON_HOME)
    back.position = Vector2(36, 34)
    back.pressed.connect(_show_home)
    content.add_child(back)

    var eyebrow := Label.new()
    eyebrow.text = _t("detail.eyebrow")
    eyebrow.position = Vector2(116, 44)
    eyebrow.size = Vector2(360, 30)
    eyebrow.add_theme_font_size_override("font_size", 17)
    eyebrow.add_theme_color_override("font_color", COLOR_ACCENT_SOFT)
    content.add_child(eyebrow)

    var cover := PanelContainer.new()
    cover.position = Vector2(62, 126)
    cover.size = Vector2(340, 476)
    cover.add_theme_stylebox_override("panel", _panel_style(8, COLOR_CARD_ALT, COLOR_LINE, 1))
    content.add_child(cover)
    var cover_texture := _load_cover_texture(game)
    if cover_texture != null:
        var image := TextureRect.new()
        image.texture = cover_texture
        image.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
        image.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_COVERED
        cover.add_child(image)
    else:
        var icon := _centered_icon(ICON_GAMEPAD, Vector2(70, 70), COLOR_ACCENT)
        cover.add_child(icon)

    var title := Label.new()
    title.text = _game_display_title(game)
    title.position = Vector2(440, 124)
    title.size = Vector2(760, 72)
    title.horizontal_alignment = HORIZONTAL_ALIGNMENT_LEFT
    title.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
    title.add_theme_font_size_override("font_size", 36)
    title.add_theme_color_override("font_color", COLOR_TEXT)
    content.add_child(title)

    var subtitle := Label.new()
    subtitle.text = _t("detail.runtime_profile", [_game_type_label(String(game.get("type", "Directory")))])
    subtitle.position = Vector2(444, 194)
    subtitle.size = Vector2(760, 28)
    subtitle.add_theme_font_size_override("font_size", 17)
    subtitle.add_theme_color_override("font_color", COLOR_MUTED)
    content.add_child(subtitle)

    var info_panel := PanelContainer.new()
    info_panel.position = Vector2(440, 246)
    info_panel.size = Vector2(760, 206)
    info_panel.add_theme_stylebox_override("panel", _panel_style(8, COLOR_CARD, COLOR_LINE, 1))
    content.add_child(info_panel)
    var info := VBoxContainer.new()
    info.add_theme_constant_override("separation", 12)
    info_panel.add_child(info)
    info.add_child(_detail_line(ICON_PAGE, String(game.get("path", ""))))
    info.add_child(_detail_line(ICON_REFRESH, _t("detail.last_played", [_last_played_label(game)])))
    info.add_child(_detail_line(ICON_PERFORMANCE, _t("detail.played", [_format_play_duration(int(game.get("playDurationSeconds", 0)))])))
    info.add_child(_detail_line(ICON_LIBRARY, _game_type_label(String(game.get("type", "Directory")))))

    var start := _pill_button(_t("detail.launch"), ICON_PLAY)
    start.position = Vector2(440, 480)
    start.size = Vector2(760, 70)
    start.pressed.connect(_start_selected_game)
    content.add_child(start)

    var tools := VBoxContainer.new()
    tools.position = Vector2(440, 582)
    tools.size = Vector2(760, 250)
    tools.add_theme_constant_override("separation", 10)
    content.add_child(tools)
    tools.add_child(_detail_action(ICON_PAGE, _t("detail.set_cover"), func(): _set_cover_for_selected()))
    tools.add_child(_detail_action(ICON_RENAME, _t("detail.rename"), func(): _rename_selected_game()))
    tools.add_child(_detail_action(ICON_DELETE, _t("detail.remove"), func(): _confirm_remove_selected()))

func _detail_line(icon_path: String, text: String) -> HBoxContainer:
    var row := HBoxContainer.new()
    row.add_theme_constant_override("separation", 16)
    row.add_child(_icon_rect(icon_path, Vector2(24, 24), COLOR_ACCENT_SOFT))
    var label := Label.new()
    label.text = text
    label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
    label.add_theme_font_size_override("font_size", 17)
    label.add_theme_color_override("font_color", COLOR_MUTED)
    row.add_child(label)
    return row

func _detail_action(icon_path: String, text: String, callback: Callable = Callable()) -> Button:
    var button := Button.new()
    button.text = text
    button.icon = _load_ui_icon(icon_path)
    button.expand_icon = true
    button.icon_alignment = HORIZONTAL_ALIGNMENT_LEFT
    button.alignment = HORIZONTAL_ALIGNMENT_LEFT
    button.clip_text = true
    button.focus_mode = Control.FOCUS_ALL
    button.custom_minimum_size = Vector2(0, 68)
    button.add_theme_constant_override("icon_max_width", 26)
    button.add_theme_constant_override("h_separation", 14)
    button.add_theme_font_size_override("font_size", 20)
    button.add_theme_color_override("font_color", COLOR_TEXT)
    _apply_button_style(
        button,
        _panel_style(8, COLOR_CARD, COLOR_LINE, 1),
        _panel_style(8, COLOR_CARD_HOVER, COLOR_ACCENT, 1),
        _panel_style(8, COLOR_ACCENT_DIM, COLOR_ACCENT, 1)
    )
    if callback.is_valid():
        button.pressed.connect(callback)
    return button

func _show_import_guide() -> void:
    modal_layer.visible = true
    for child in modal_layer.get_children():
        child.queue_free()
    var dim := ColorRect.new()
    dim.color = Color(0, 0, 0, 0.55)
    dim.set_anchors_preset(Control.PRESET_FULL_RECT)
    modal_layer.add_child(dim)

    var dialog := PanelContainer.new()
    dialog.anchor_left = 0.5
    dialog.anchor_top = 0.5
    dialog.anchor_right = 0.5
    dialog.anchor_bottom = 0.5
    dialog.position = Vector2(-320, -250)
    dialog.size = Vector2(640, 500)
    dialog.add_theme_stylebox_override("panel", _panel_style(22, COLOR_CARD, Color(0, 0, 0, 0.04), 1))
    modal_layer.add_child(dialog)

    var box := VBoxContainer.new()
    box.add_theme_constant_override("separation", 22)
    dialog.add_child(box)
    var title := Label.new()
    title.text = _t("dialog.import_title")
    title.add_theme_font_size_override("font_size", 30)
    title.add_theme_color_override("font_color", COLOR_TEXT)
    box.add_child(title)
    var body := Label.new()
    body.text = _t("dialog.import_guide_body")
    body.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
    body.add_theme_font_size_override("font_size", 22)
    body.add_theme_color_override("font_color", COLOR_TEXT)
    box.add_child(body)
    var ok := _pill_button(_t("dialog.ok"))
    ok.custom_minimum_size = Vector2(140, 62)
    ok.pressed.connect(func(): modal_layer.visible = false)
    box.add_child(ok)

func _show_message(message: String) -> void:
    _show_system_alert(message, "AetherKiri")

func _show_system_alert(message: String, title: String = "AetherKiri") -> void:
    if message.strip_edges().is_empty():
        return
    OS.alert(message, title)

func _show_system_alert_once(key: String, message: String, title: String = "AetherKiri") -> void:
    if shown_system_alerts.has(key):
        return
    shown_system_alerts[key] = true
    _show_system_alert(message, title)

func _maybe_show_log_alert(line: String) -> void:
    if not log_alerts:
        return
    var message := line.strip_edges()
    if message.is_empty():
        return
    var lower := message.to_lower()
    var is_warning := lower.contains("warning") or lower.contains("(warning)") or lower.contains("警告")
    var is_error := lower.contains("error") or lower.contains("exception") or lower.contains("fatal") or lower.contains("failed") or lower.contains("错误") or lower.contains("失败")
    if not is_warning and not is_error:
        return
    var title := _t("alert.error_title") if is_error else _t("alert.warning_title")
    _show_system_alert_once("log:%s" % message, message, title)

func _create_file_dialog(title: String, file_mode: int, filters: PackedStringArray = PackedStringArray()) -> FileDialog:
    var dialog := FileDialog.new()
    dialog.file_mode = file_mode
    dialog.access = FileDialog.ACCESS_FILESYSTEM
    dialog.use_native_dialog = true
    dialog.title = title
    for filter in filters:
        dialog.add_filter(filter)
    return dialog

func _offer_scrape_after_add(game: Dictionary) -> void:
    modal_layer.visible = true
    for child in modal_layer.get_children():
        child.queue_free()
    var dim := ColorRect.new()
    dim.color = Color(0, 0, 0, 0.38)
    dim.set_anchors_preset(Control.PRESET_FULL_RECT)
    modal_layer.add_child(dim)
    var dialog := PanelContainer.new()
    dialog.anchor_left = 0.5
    dialog.anchor_top = 0.5
    dialog.anchor_right = 0.5
    dialog.anchor_bottom = 0.5
    dialog.position = Vector2(-280, -150)
    dialog.size = Vector2(560, 300)
    dialog.add_theme_stylebox_override("panel", _panel_style(20, COLOR_CARD, Color(0, 0, 0, 0.06), 1))
    modal_layer.add_child(dialog)
    var box := VBoxContainer.new()
    box.add_theme_constant_override("separation", 18)
    dialog.add_child(box)
    var title := Label.new()
    title.text = _t("dialog.scrape_title")
    title.add_theme_font_size_override("font_size", 28)
    title.add_theme_color_override("font_color", COLOR_TEXT)
    box.add_child(title)
    var body := Label.new()
    body.text = _t("dialog.scrape_body", [_game_display_title(game)])
    body.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
    body.add_theme_font_size_override("font_size", 21)
    body.add_theme_color_override("font_color", COLOR_TEXT)
    box.add_child(body)
    var buttons := HBoxContainer.new()
    buttons.add_theme_constant_override("separation", 12)
    box.add_child(buttons)
    var no := Button.new()
    no.text = _t("dialog.later")
    no.flat = true
    no.pressed.connect(func(): modal_layer.visible = false)
    buttons.add_child(no)
    var yes := _pill_button(_t("dialog.open_detail"))
    yes.pressed.connect(func():
        modal_layer.visible = false
        _show_detail(game)
    )
    buttons.add_child(yes)

func _set_cover_for_selected() -> void:
    var path := String(selected_game.get("path", ""))
    if path.is_empty():
        return
    var dialog := _create_file_dialog(
        _t("dialog.choose_cover"),
        FileDialog.FILE_MODE_OPEN_FILE,
        PackedStringArray(["*.png,*.jpg,*.jpeg,*.webp;Image;image/png,image/jpeg,image/webp"])
    )
    dialog.file_selected.connect(func(cover_path: String):
        _update_game(path, {"coverPath": cover_path})
        _show_detail(selected_game)
    )
    add_child(dialog)
    dialog.popup_centered(Vector2i(900, 640))

func _rename_selected_game() -> void:
    var path := String(selected_game.get("path", ""))
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
    dialog.anchor_left = 0.5
    dialog.anchor_top = 0.5
    dialog.anchor_right = 0.5
    dialog.anchor_bottom = 0.5
    dialog.position = Vector2(-280, -150)
    dialog.size = Vector2(560, 300)
    dialog.add_theme_stylebox_override("panel", _panel_style(20, COLOR_CARD, Color(0, 0, 0, 0.06), 1))
    modal_layer.add_child(dialog)
    var box := VBoxContainer.new()
    box.add_theme_constant_override("separation", 18)
    dialog.add_child(box)
    var title := Label.new()
    title.text = _t("dialog.rename")
    title.add_theme_font_size_override("font_size", 28)
    title.add_theme_color_override("font_color", COLOR_TEXT)
    box.add_child(title)
    var input := LineEdit.new()
    input.text = _game_display_title(selected_game)
    input.custom_minimum_size = Vector2(460, 52)
    box.add_child(input)
    var save := _pill_button(_t("settings.save"))
    save.pressed.connect(func():
        var new_title := input.text.strip_edges()
        if not new_title.is_empty():
            modal_layer.visible = false
            _update_game(path, {"title": new_title})
            _show_detail(selected_game)
    )
    box.add_child(save)

func _confirm_remove_selected() -> void:
    var path := String(selected_game.get("path", ""))
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
    dialog.anchor_left = 0.5
    dialog.anchor_top = 0.5
    dialog.anchor_right = 0.5
    dialog.anchor_bottom = 0.5
    dialog.position = Vector2(-280, -140)
    dialog.size = Vector2(560, 280)
    dialog.add_theme_stylebox_override("panel", _panel_style(20, COLOR_CARD, Color(0, 0, 0, 0.06), 1))
    modal_layer.add_child(dialog)
    var box := VBoxContainer.new()
    box.add_theme_constant_override("separation", 18)
    dialog.add_child(box)
    var label := Label.new()
    label.text = _t("dialog.remove_body", [_game_display_title(selected_game)])
    label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
    label.add_theme_font_size_override("font_size", 22)
    label.add_theme_color_override("font_color", COLOR_TEXT)
    box.add_child(label)
    var remove := _pill_button(_t("dialog.remove"))
    remove.pressed.connect(func():
        modal_layer.visible = false
        _remove_game(path)
    )
    box.add_child(remove)

func _on_refresh_or_import() -> void:
    if OS.get_name() == "iOS":
        _refresh_games()
        return
    if OS.get_name() == "Web":
        _show_web_import_picker()
        return
    _show_import_picker()

func _show_import_picker() -> void:
    modal_layer.visible = true
    for child in modal_layer.get_children():
        child.queue_free()
    var dim := ColorRect.new()
    dim.color = Color(0, 0, 0, 0.45)
    dim.set_anchors_preset(Control.PRESET_FULL_RECT)
    modal_layer.add_child(dim)
    var dialog := PanelContainer.new()
    dialog.anchor_left = 0.5
    dialog.anchor_top = 0.5
    dialog.anchor_right = 0.5
    dialog.anchor_bottom = 0.5
    dialog.position = Vector2(-260, -160)
    dialog.size = Vector2(520, 320)
    dialog.add_theme_stylebox_override("panel", _panel_style(20, COLOR_CARD, Color(0, 0, 0, 0.06), 1))
    modal_layer.add_child(dialog)
    var box := VBoxContainer.new()
    box.add_theme_constant_override("separation", 14)
    dialog.add_child(box)
    var title := Label.new()
    title.text = _t("dialog.import_title")
    title.add_theme_font_size_override("font_size", 28)
    title.add_theme_color_override("font_color", COLOR_TEXT)
    box.add_child(title)
    var dir_button := _pill_button(_t("dialog.select_game_dir"))
    dir_button.pressed.connect(func():
        modal_layer.visible = false
        _open_import_dialog(false)
    )
    box.add_child(dir_button)
    var xp3_button := _pill_button(_t("dialog.select_xp3"))
    xp3_button.pressed.connect(func():
        modal_layer.visible = false
        _open_import_dialog(true)
    )
    box.add_child(xp3_button)
    var cancel := Button.new()
    cancel.text = _t("dialog.cancel")
    cancel.flat = true
    cancel.pressed.connect(func(): modal_layer.visible = false)
    box.add_child(cancel)

func _web_eval_string(source: String) -> String:
    if OS.get_name() != "Web":
        return ""
    var value = JavaScriptBridge.eval(source, true)
    if value == null:
        return ""
    return String(value)

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
    if not bool(support.get("directory", false)) and not bool(support.get("archive", false)) and dev_games.is_empty():
        _show_message(_t("message.web_picker_unsupported_long"))
        return

    modal_layer.visible = true
    for child in modal_layer.get_children():
        child.queue_free()
    var dim := ColorRect.new()
    dim.color = Color(0, 0, 0, 0.45)
    dim.set_anchors_preset(Control.PRESET_FULL_RECT)
    modal_layer.add_child(dim)
    var dialog := PanelContainer.new()
    dialog.anchor_left = 0.5
    dialog.anchor_top = 0.5
    dialog.anchor_right = 0.5
    dialog.anchor_bottom = 0.5
    dialog.position = Vector2(-340, -220)
    dialog.size = Vector2(680, 440)
    dialog.add_theme_stylebox_override("panel", _panel_style(20, COLOR_CARD, Color(0, 0, 0, 0.06), 1))
    modal_layer.add_child(dialog)
    var box := VBoxContainer.new()
    box.add_theme_constant_override("separation", 14)
    dialog.add_child(box)
    var title := Label.new()
    title.text = _t("dialog.import_title")
    title.add_theme_font_size_override("font_size", 28)
    title.add_theme_color_override("font_color", COLOR_TEXT)
    box.add_child(title)

    if bool(support.get("directory", false)):
        var dir_button := _pill_button(_t("dialog.select_local_game_dir"))
        dir_button.pressed.connect(func():
            modal_layer.visible = false
            _pick_web_local_game("directory")
        )
        box.add_child(dir_button)

    if bool(support.get("archive", false)):
        var archive_button := _pill_button(_t("dialog.select_xp3"))
        archive_button.pressed.connect(func():
            modal_layer.visible = false
            _pick_web_local_game("archive")
        )
        box.add_child(archive_button)

    for item in dev_games:
        if not item is Dictionary:
            continue
        var game := _web_game_from_mount_info(item)
        var captured_game := game.duplicate(true)
        var button := _pill_button(_t("dialog.dev_mount", [String(game.get("name", ""))]))
        button.pressed.connect(func():
            modal_layer.visible = false
            if not _mount_web_game(captured_game):
                return
            _add_game_dictionary(captured_game)
        )
        box.add_child(button)
    var cancel := Button.new()
    cancel.text = _t("dialog.cancel")
    cancel.flat = true
    cancel.pressed.connect(func(): modal_layer.visible = false)
    box.add_child(cancel)

func _open_import_dialog(xp3: bool) -> void:
    var filters := PackedStringArray(["*.xp3,*.XP3;KiriKiri XP3 archive"]) if xp3 else PackedStringArray()
    var dialog := _create_file_dialog(
        _t("dialog.select_xp3") if xp3 else _t("dialog.select_game_dir"),
        FileDialog.FILE_MODE_OPEN_FILE if xp3 else FileDialog.FILE_MODE_OPEN_DIR,
        filters
    )
    dialog.dir_selected.connect(func(path: String):
        _add_game_path(path)
    )
    dialog.file_selected.connect(func(path: String):
        _add_game_path(path)
    )
    add_child(dialog)
    dialog.popup_centered(Vector2i(900, 640))

func _refresh_games() -> void:
    known_games = _load_game_list()
    if OS.get_name() == "iOS":
        known_games = _scan_ios_games_dir(known_games)
        _save_game_list(known_games)
    known_games = _sorted_games(known_games)
    for child in game_list.get_children():
        child.queue_free()
    empty_state.visible = known_games.is_empty()
    game_scroll.visible = not known_games.is_empty()
    for game in known_games:
        game_list.add_child(_game_card(game))

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
    var by_name := {}
    var next: Array[Dictionary] = []
    for game in existing:
        var name := _game_display_title(game)
        by_name[name] = game
        if not String(game.get("path", "")).begins_with(root) and _path_exists(String(game.get("path", ""))):
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
                var game: Dictionary = by_name.get(entry, _game_info_from_path(path))
                game["path"] = path
                next.append(game)
        entry = dir.get_next()
    return _dedupe_games(next)

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
    return true

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
    return DirAccess.dir_exists_absolute(path) or FileAccess.file_exists(path)

func _resolve_game_path(path: String) -> String:
    var normalized := path.strip_edges()
    if normalized.is_empty() or _path_exists(normalized) or OS.get_name() != "iOS":
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
    return {
        "name": name,
        "path": path,
        "type": "Archive" if path.to_lower().ends_with(".xp3") else "Directory",
        "lastPlayed": 0,
        "playDurationSeconds": 0,
        "coverPath": "",
        "developer": "",
        "title": "",
    }

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
    var games := _load_game_list()
    var next: Array[Dictionary] = []
    for game in games:
        if String(game.get("path", "")) != path:
            next.append(game)
    _save_game_list(next)
    selected_game = {}
    _show_home()

func _game_card(game: Dictionary) -> Button:
    var button := Button.new()
    button.custom_minimum_size = HOME_CARD_SIZE
    button.clip_text = true
    button.clip_contents = true
    button.focus_mode = Control.FOCUS_ALL
    button.text = ""
    button.add_theme_stylebox_override("normal", _panel_style(8, COLOR_CARD_ALT, COLOR_LINE, 1))
    button.add_theme_stylebox_override("hover", _panel_style(8, COLOR_CARD_HOVER, COLOR_ACCENT, 1))
    button.add_theme_stylebox_override("pressed", _panel_style(8, COLOR_ACCENT_DIM, COLOR_ACCENT, 1))
    button.add_theme_stylebox_override("focus", _focus_outline(8))
    button.pressed.connect(func(): _show_detail(game))

    var frame := Control.new()
    frame.mouse_filter = Control.MOUSE_FILTER_IGNORE
    frame.clip_contents = true
    frame.set_anchors_preset(Control.PRESET_FULL_RECT)
    button.add_child(frame)

    var cover_texture := _load_cover_texture(game, Vector2i(int(HOME_CARD_SIZE.x), int(HOME_CARD_SIZE.y)), 8)
    if cover_texture != null:
        var cover := TextureRect.new()
        cover.texture = cover_texture
        cover.mouse_filter = Control.MOUSE_FILTER_IGNORE
        cover.set_anchors_preset(Control.PRESET_FULL_RECT)
        cover.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
        cover.stretch_mode = TextureRect.STRETCH_SCALE
        frame.add_child(cover)
    else:
        var placeholder := PanelContainer.new()
        placeholder.mouse_filter = Control.MOUSE_FILTER_IGNORE
        placeholder.set_anchors_preset(Control.PRESET_FULL_RECT)
        placeholder.add_theme_stylebox_override("panel", _panel_style(8, COLOR_CARD, COLOR_LINE, 1))
        frame.add_child(placeholder)

        var icon := _centered_icon(ICON_GAMEPAD, Vector2(58, 58), COLOR_ACCENT)
        icon.set_anchors_preset(Control.PRESET_FULL_RECT)
        frame.add_child(icon)

    var shade := PanelContainer.new()
    shade.mouse_filter = Control.MOUSE_FILTER_IGNORE
    shade.anchor_left = 0.0
    shade.anchor_top = 1.0
    shade.anchor_right = 1.0
    shade.anchor_bottom = 1.0
    shade.offset_left = 0.0
    shade.offset_top = -118.0
    shade.offset_right = 0.0
    shade.offset_bottom = 0.0
    shade.add_theme_stylebox_override("panel", _panel_style(8, Color(0.0, 0.0, 0.0, 0.62), Color(0, 0, 0, 0), 0))
    frame.add_child(shade)

    var text_margin := MarginContainer.new()
    text_margin.mouse_filter = Control.MOUSE_FILTER_IGNORE
    text_margin.anchor_left = 0.0
    text_margin.anchor_top = 1.0
    text_margin.anchor_right = 1.0
    text_margin.anchor_bottom = 1.0
    text_margin.offset_left = 0.0
    text_margin.offset_top = -118.0
    text_margin.offset_right = 0.0
    text_margin.offset_bottom = 0.0
    text_margin.add_theme_constant_override("margin_left", 16)
    text_margin.add_theme_constant_override("margin_top", 18)
    text_margin.add_theme_constant_override("margin_right", 16)
    text_margin.add_theme_constant_override("margin_bottom", 16)
    frame.add_child(text_margin)

    var labels := VBoxContainer.new()
    labels.mouse_filter = Control.MOUSE_FILTER_IGNORE
    labels.add_theme_constant_override("separation", 4)
    text_margin.add_child(labels)

    var title := Label.new()
    title.text = _game_display_title(game)
    title.mouse_filter = Control.MOUSE_FILTER_IGNORE
    title.clip_text = true
    title.add_theme_font_size_override("font_size", 21)
    title.add_theme_color_override("font_color", Color.WHITE)
    labels.add_child(title)

    var sub := Label.new()
    sub.text = _game_subtitle(game)
    sub.mouse_filter = Control.MOUSE_FILTER_IGNORE
    sub.clip_text = true
    sub.add_theme_font_size_override("font_size", 15)
    sub.add_theme_color_override("font_color", Color(1, 1, 1, 0.72))
    labels.add_child(sub)

    var border := PanelContainer.new()
    border.mouse_filter = Control.MOUSE_FILTER_IGNORE
    border.set_anchors_preset(Control.PRESET_FULL_RECT)
    _set_game_card_border(button, border, false)
    frame.add_child(border)
    button.add_to_group("game_card_buttons")
    button.set_meta("card_border_path", button.get_path_to(border))
    button.mouse_entered.connect(func(): _set_game_card_border(button, border, true))
    button.mouse_exited.connect(func():
        _set_game_card_border(button, border, button.has_focus())
    )
    button.focus_entered.connect(func(): _set_game_card_border(button, border, true))
    button.focus_exited.connect(func():
        var still_hovered := button.get_global_rect().has_point(button.get_global_mouse_position())
        _set_game_card_border(button, border, still_hovered)
    )
    return button

func _load_cover_texture(game: Dictionary, target_size: Vector2i = Vector2i.ZERO, radius: int = 0) -> Texture2D:
    var cover_path := String(game.get("coverPath", ""))
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

func _sync_game_card_hover_states() -> void:
    if home_view == null or not home_view.visible:
        return
    var mouse_pos := get_global_mouse_position()
    for node in get_tree().get_nodes_in_group("game_card_buttons"):
        if not is_instance_valid(node) or not (node is Button):
            continue
        var button := node as Button
        if not button.is_visible_in_tree():
            continue
        var border_path = button.get_meta("card_border_path", NodePath(""))
        var border := button.get_node_or_null(border_path) as PanelContainer
        if border == null:
            continue
        var active := button.has_focus() or button.get_global_rect().has_point(mouse_pos)
        if bool(button.get_meta("card_border_active", false)) != active:
            _set_game_card_border(button, border, active)

func _start_selected_game() -> void:
    var path := String(selected_game.get("path", ""))
    if path.is_empty():
        return
    if not _mount_web_game(selected_game):
        return
    var played_game := _mark_game_played(path)
    if not played_game.is_empty():
        selected_game = played_game
    active_game_path = path
    active_game_started_msec = Time.get_ticks_msec()
    game_path.text = path
    _set_game_background(true)
    shell_root.visible = false
    viewport.visible = true
    viewport.move_to_front()
    game_view.visible = true
    loading_panel.visible = true
    loading_panel.move_to_front()
    perf.visible = show_perf_monitor
    restart_notice.visible = true
    _on_open_game()

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

func _quit_after_runtime_exit() -> void:
    if runtime_exit_cleanup_pending:
        return
    runtime_exit_cleanup_pending = true
    _clear_game_input_capture()
    _finalize_active_game_session()
    game_running = false
    app_lifecycle_paused = false
    cached_startup_state = STARTUP_IDLE
    startup_poll_accum = 0.0
    tick_trace_active_serial = 0
    if loading_panel != null:
        loading_panel.visible = false
    if restart_notice != null:
        restart_notice.text = ""
        restart_notice.visible = false
    if perf != null:
        perf.visible = false
    if viewport != null:
        viewport.texture = null
        viewport.visible = false
    if game_view != null:
        game_view.visible = false
    if player != null:
        player.release_frame_texture()
        player.destroy_engine()
    if _is_touch_platform():
        OS.kill(OS.get_process_id())
        return
    get_tree().quit(0)

func _ready() -> void:
    cli_probe_script = _detect_cli_probe_script()
    _apply_ui_font()
    DisplayServer.window_set_flag(DisplayServer.WINDOW_FLAG_TRANSPARENT, false)
    _apply_initial_window_size()
    _apply_global_dpi_scale()
    get_viewport().transparent_bg = false
    RenderingServer.set_default_clear_color(COLOR_BG)
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
    input_trace_enabled = _runtime_flag("AETHERKIRI_INPUT_TRACE") or ios_diagnostics_enabled
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
    _configure_runtime_diagnostics()
    var env_backend := _runtime_string("AETHERKIRI_BACKEND", "")
    if not env_backend.is_empty():
        selected_backend = _normalize_backend_name(env_backend)
    if not selected_backend in BACKENDS:
        selected_backend = "Godot Native"

    _build_ui()
    _stage_runtime_fonts()

    if not _create_runtime_player():
        return

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

func _create_runtime_player() -> bool:
    if not ClassDB.class_exists("AetherKiriPlayer"):
        var message := "AetherKiri runtime extension class is unavailable."
        push_error(message)
        _append_log(message)
        _show_system_alert(_t("alert.runtime_class_missing"), _t("alert.error_title"))
        return false
    var instance: Object = ClassDB.instantiate("AetherKiriPlayer")
    if instance == null or not (instance is Node):
        var create_message := "AetherKiri runtime extension could not create AetherKiriPlayer."
        push_error(create_message)
        _append_log(create_message)
        _show_system_alert(_t("alert.runtime_create_failed"), _t("alert.error_title"))
        return false
    player = instance
    add_child(instance as Node)
    return true

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

    _append_log("AetherKiri engine initialized.")
    return true

func _finish_ready_after_first_frame() -> void:
    await get_tree().process_frame

    var engine_initialized := _ensure_player_initialized()

    if engine_initialized:
        _apply_backend(false)
        _apply_engine_options()
    _apply_shell_runtime_settings()
    if not cli_probe_script.is_empty():
        if not engine_initialized:
            printerr("initialize_engine failed: %s" % player.get_last_error())
            get_tree().quit(1)
            return
        call_deferred("_run_cli_script_probe")
        return

    _append_log("Debug CPU is a fallback backend and is not part of performance acceptance.")
    _write_probe_marker("ready")
    _refresh_games()
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
    if action == "settings":
        _show_settings()
    elif action == "guide":
        _show_import_guide()
    elif action == "detail" and not known_games.is_empty():
        _show_detail(known_games[0])
    var mouse := OS.get_environment("AETHERKIRI_CAPTURE_UI_MOUSE")
    if not mouse.is_empty():
        var parts := mouse.split(",", false)
        if parts.size() == 2:
            Input.warp_mouse(Vector2(parts[0].to_float(), parts[1].to_float()))
    await get_tree().process_frame
    await get_tree().process_frame
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
    bg_rect.color = COLOR_GAME_BG
    viewport.visible = true
    game_view.visible = true
    loading_panel.visible = false
    perf.visible = false
    restart_notice.visible = false
    viewport.texture = null
    last_texture_size = Vector2i.ZERO
    game_running = false
    _fit_full_rects()

func _probe_open_game(config: Dictionary, target_game_path: String, backend_env: String) -> bool:
    selected_backend = ProbeConfig.backend(config, backend_env)
    if not selected_backend in BACKENDS:
        selected_backend = "Godot Native"
    _write_probe_marker("probe_open_game backend=%s target=%s" % [selected_backend, target_game_path])
    _apply_backend(false)
    var fps_limit := ProbeConfig.int_value(config, "fps_limit", _runtime_int("AETHERKIRI_PROBE_FPS_LIMIT", 0))
    player.set_engine_option("fps_limit", str(maxi(0, fps_limit)))
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
    var result: int = int(player.tick(1.0 / 60.0))
    if result != ENGINE_RESULT_OK:
        _write_probe_marker("probe_tick failed error=%s" % player.get_last_error())
        printerr("tick failed: %s" % player.get_last_error())
        return false
    if present_hold_frames > 0:
        present_hold_frames -= 1
        return true
    var texture: Texture2D = player.update_frame_texture()
    if texture != null:
        viewport.texture = texture
        viewport.queue_redraw()
        last_texture_size = Vector2i(texture.get_width(), texture.get_height())
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
    if texture == null or texture.get_width() != width or texture.get_height() != height:
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
    if not _probe_open_game(config, target_game_path, "AETHERKIRI_PROBE_BACKEND"):
        await _probe_cleanup_and_quit(1)
        return
    if not await _probe_wait_startup(config, 900):
        await _probe_cleanup_and_quit(1)
        return
    if not await _probe_advance(ProbeConfig.int_value(config, "warmup_frames", _runtime_int("AETHERKIRI_PROBE_WARMUP_FRAMES", 180))):
        await _probe_cleanup_and_quit(1)
        return
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
        elif kind == "move":
            var pos := ProbeConfig.click_position(action)
            _probe_send_mapped_move(pos, config)
            if label.is_empty() or label == "move":
                label = "move_%d_%d" % [int(pos.x), int(pos.y)]
        elif kind == "key":
            var key_code := int(action.get("key_code", 13))
            player.send_key_event(true, key_code, int(action.get("modifiers", 0)), int(action.get("unicode", 0)))
            player.tick(1.0 / 60.0)
            player.send_key_event(false, key_code, int(action.get("modifiers", 0)), 0)
            if label.is_empty() or label == "key":
                label = "key_%d" % key_code
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
    var capture_every: int = max(0, int(action.get("capture_every", 0)))
    var spike_ms: float = max(0.0, float(action.get("spike_ms", 20.0)))
    var pointer_id: int = int(action.get("pointer_id", TOUCH_POINTER_ID_OFFSET))
    var input_total := 0.0
    var tick_total := 0.0
    var update_total := 0.0
    var frame_total := 0.0
    var input_max := 0.0
    var tick_max := 0.0
    var update_max := 0.0
    var frame_max := 0.0
    var spikes := 0
    var input_events := 0
    var measured_frames := 0

    if label.is_empty() or label == "click_stream":
        label = "click_stream_%d_%d_%d" % [frames, int(pos.x), int(pos.y)]

    player.send_pointer_event(POINTER_MOVE, pointer_id, mapped.x, mapped.y, 0.0, 0.0, 0)
    input_events += 1
    for frame_index in range(frames):
        var frame_start := Time.get_ticks_usec()
        var input_start := frame_start
        for i in range(clicks_per_frame):
            player.send_pointer_event(POINTER_DOWN, pointer_id, mapped.x, mapped.y, 0.0, 0.0, 0)
            player.send_pointer_event(POINTER_UP, pointer_id, mapped.x, mapped.y, 0.0, 0.0, 0)
            input_events += 2

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

        if capture_every > 0 and (frame_index % capture_every) == 0:
            await _probe_save_step(step, "%s_f%03d" % [label, frame_index])
            step += 1
        await get_tree().process_frame

    var divisor := float(max(1, measured_frames))
    print("click_stream label=%s frames=%d measured_frames=%d clicks_per_frame=%d input_events=%d avg_input_ms=%.2f max_input_ms=%.2f avg_tick_ms=%.2f max_tick_ms=%.2f avg_update_ms=%.2f max_update_ms=%.2f avg_frame_ms=%.2f max_frame_ms=%.2f spikes=%d spike_ms=%.2f texture_backend=%s renderer=\"%s\"" % [
        label,
        frames,
        measured_frames,
        clicks_per_frame,
        input_events,
        input_total / divisor,
        input_max,
        tick_total / divisor,
        tick_max,
        update_total / divisor,
        update_max,
        frame_total / divisor,
        frame_max,
        spikes,
        spike_ms,
        player.get_frame_texture_backend(),
        player.get_renderer_info(),
    ])

    if bool(action.get("capture_final", true)):
        await _probe_save_step(step, "%s_final" % label)
        step += 1
    return step

func _probe_save_step(index: int, label: String) -> void:
    await get_tree().process_frame
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

func _probe_send_mapped_move(window_pos: Vector2, config: Dictionary) -> void:
    var mapped := _probe_map_window_point(window_pos, config)
    if mapped.x < 0.0 or mapped.y < 0.0:
        print("skip move outside texture window=%s mapped=%s" % [window_pos, mapped])
        return
    player.send_pointer_event(POINTER_MOVE, 0, mapped.x, mapped.y, 0.0, 0.0, 0)
    player.tick(1.0 / 60.0)

func _probe_map_window_point(pos: Vector2, config: Dictionary) -> Vector2:
    var tex_size := Vector2(max(1.0, float(last_texture_size.x)), max(1.0, float(last_texture_size.y)))
    var coord := ProbeConfig.coord_size(config, Vector2i(
        _runtime_int("AETHERKIRI_PROBE_COORD_W", 1600),
        _runtime_int("AETHERKIRI_PROBE_COORD_H", 900)
    ))
    var panel_size := Vector2(coord)
    var scale: float = min(panel_size.x / tex_size.x, panel_size.y / tex_size.y)
    if scale <= 0.0:
        return Vector2(-1.0, -1.0)
    var drawn_size := tex_size * scale
    var offset := (panel_size - drawn_size) * 0.5
    var inside := pos - offset
    if inside.x < 0.0 or inside.y < 0.0 or inside.x > drawn_size.x or inside.y > drawn_size.y:
        return Vector2(-1.0, -1.0)
    return inside / scale

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
    var texture := get_viewport().get_texture()
    if texture != null:
        var viewport_image := texture.get_image()
        if viewport_image != null and viewport_image.get_width() > 0 and viewport_image.get_height() > 0:
            if int(_image_stats(viewport_image).get("visible", 0)) > 0:
                return viewport_image

    if viewport.texture != null:
        var viewport_image := viewport.texture.get_image()
        if viewport_image != null and viewport_image.get_width() > 0 and viewport_image.get_height() > 0:
            if int(_image_stats(viewport_image).get("visible", 0)) > 0:
                return viewport_image

    var frame: Dictionary = player.read_frame_rgba()
    var data: PackedByteArray = frame.get("rgba", PackedByteArray())
    var width := int(frame.get("width", 0))
    var height := int(frame.get("height", 0))
    if width > 0 and height > 0 and data.size() >= width * height * 4:
        var frame_image := Image.create_from_data(width, height, false, Image.FORMAT_RGBA8, data)
        if int(_image_stats(frame_image).get("visible", 0)) > 0:
            return frame_image
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
    var scale := DEFAULT_UI_DPI_SCALE
    if not scale_text.is_empty():
        scale = scale_text.to_float()
    scale = clampf(scale, 0.75, 2.0)
    var window := get_window()
    window.content_scale_factor = scale

func _process(delta: float) -> void:
    _fit_full_rects()
    _sync_game_card_hover_states()
    _flush_log_view_if_needed(delta)
    var startup_state := cached_startup_state
    if game_running:
        _sync_player_surface_size(false)
        if _should_process_runtime_logs():
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
        if startup_state == STARTUP_SUCCEEDED:
            restart_notice.text = ""
            loading_panel.visible = false
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
            var tick_ms := float(Time.get_ticks_usec() - tick_start) / 1000.0
            tick_trace_active_serial = 0
            if tick_result != ENGINE_RESULT_OK:
                var tick_result_name := str(player.get_last_result())
                var tick_error_message := str(player.get_last_error())
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
                    _quit_after_runtime_exit()
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
                app_lifecycle_paused = false
            else:
                if tick_ms >= frame_spike_ms and frame_spike_ms > 0.0:
                    _log_tick_trace("tick_end serial=%d tick_ms=%.2f renderer=\"%s\"" % [
                        tick_trace_serial,
                        tick_ms,
                        player.get_renderer_info(),
                    ])
                var update_start := Time.get_ticks_usec()
                _update_frame()
                var update_ms := float(Time.get_ticks_usec() - update_start) / 1000.0
                _update_touch_busy_gate(maxf(delta * 1000.0, tick_ms + update_ms))
                _log_live_perf(delta, tick_ms, update_ms)
                _log_frame_spike(delta, tick_ms, update_ms)
                _log_frame_probe(delta)
                _log_input_trace(delta, tick_ms, update_ms)
        elif startup_state == STARTUP_FAILED:
            restart_notice.text = "Game startup failed."
            loading_panel.visible = false
            _set_game_background(false)
            shell_root.visible = true
            viewport.visible = false
            game_view.visible = false
            game_running = false
            app_lifecycle_paused = false
            render_errors += 1
            var startup_error := "Startup failed: %s" % player.get_last_error()
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
        if not perf.visible and not verbose_render_log:
            return
        var frame_ms := delta * 1000.0
        var renderer: String = selected_backend
        if game_running and startup_state == STARTUP_SUCCEEDED:
            renderer = String(player.get_renderer_info())
        var renderer_summary := _renderer_summary(renderer)
        if verbose_render_log and game_running and not renderer.is_empty() and renderer_summary != last_renderer_info_logged:
            last_renderer_info_logged = renderer_summary
            _append_log("Renderer info: %s" % renderer)
        if not perf.visible:
            return
        var fallback := _renderer_fallback(renderer)
        var texture_backend: String = String(player.get_frame_texture_backend()) if game_running else "none"
        perf.text = "Backend: %s | FPS: %d | Frame: %.2f ms | Texture: %s | Size: %dx%d | Surface: %s %dx%d | Fallback: %s | Errors: %d" % [
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
func _log_live_perf(delta: float, tick_ms: float, update_ms: float) -> void:
    if not _should_emit_runtime_perf_logs():
        return
    perf_log_accum += delta
    if perf_log_accum < perf_log_interval:
        return
    perf_log_accum = 0.0
    var line := "live_perf fps=%d frame_ms=%.2f tick_ms=%.2f update_ms=%.2f texture=%s size=%dx%d renderer=\"%s\" errors=%d" % [
        Engine.get_frames_per_second(),
        delta * 1000.0,
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
        return
    if player == null:
        return
    if what == NOTIFICATION_APPLICATION_PAUSED or what == NOTIFICATION_APPLICATION_FOCUS_OUT:
        _pause_game_for_lifecycle("notification_%d" % what)
        return
    if what == NOTIFICATION_APPLICATION_RESUMED or what == NOTIFICATION_APPLICATION_FOCUS_IN:
        _resume_game_for_lifecycle("notification_%d" % what)
        return
    if what == NOTIFICATION_WM_CLOSE_REQUEST:
        if app_lifecycle_paused:
            player.resume()
            app_lifecycle_paused = false
        _clear_game_input_capture()
        _finalize_active_game_session()
        viewport.texture = null
        player.release_frame_texture()
        player.destroy_engine()

func _pause_game_for_lifecycle(reason: String) -> void:
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
    return renderer.substr(start, end - start)

func _renderer_summary(renderer: String) -> String:
    if renderer.is_empty():
        return selected_backend
    if renderer.contains("backend=godot_native"):
        return "Godot Native GPU"
    if renderer.contains("backend=gpu_bridge"):
        return "GPU Bridge"
    if renderer.contains("backend=debug_cpu"):
        return "Debug CPU"
    return selected_backend


func _on_open_game() -> void:
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

    if not _ensure_player_initialized():
        return

    ProjectSettings.set_setting(GAME_PATH_KEY, path)
    _apply_backend(false)
    _apply_engine_options()
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
    app_lifecycle_paused = false
    log_lines.clear()
    log_view_dirty = false
    log_view_flush_accum = 0.0
    _clear_game_input_capture()
    if log_view != null:
        log_view.text = ""
        log_view.scroll_vertical = 0
    last_texture_size = Vector2i.ZERO
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
    if not _should_process_runtime_logs():
        return
    for line in logs.split("\n", false):
        _append_log(line)

func _should_process_runtime_logs() -> bool:
    return diagnostics_enabled or ui_log_enabled or log_alerts or error_dialog_logs

func _should_collect_log_lines() -> bool:
    return ui_log_enabled or error_dialog_logs

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

func _update_frame() -> void:
    if present_hold_frames > 0:
        present_hold_frames -= 1
        return
    var texture: Texture2D = player.update_frame_texture()
    if texture != null:
        if _should_hold_suspect_black_frame():
            return
        viewport.texture = texture
        viewport.queue_redraw()
        last_texture_size = Vector2i(texture.get_width(), texture.get_height())
        _sync_game_surface_to_texture(last_texture_size)
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
    active_touch_points.clear()
    active_mouse_buttons.clear()
    suppressed_touch_points.clear()
    touch_down_points.clear()
    pending_touch_index = -1
    pending_touch_mapped = Vector2.ZERO
    pending_touch_down_msec = 0
    last_forwarded_touch_move_msec_by_id.clear()
    last_forwarded_touch_down_msec = 0
    last_forwarded_touch_up_msec = 0
    suppress_mouse_until_msec = 0
    present_hold_frames = 0
    last_present_hold_msec = 0
    tick_trace_until_msec = 0
    tick_trace_active_serial = 0
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
    var frames: int = max(1, _runtime_int("AETHERKIRI_STARTUP_CLICK_STREAM_FRAMES", 240))
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
    var elapsed_sec := float(Time.get_ticks_usec() - start_usec) / 1000000.0
    var line := "startup_click_stream frames=%d clicks_per_frame=%d attempted=%d forwarded=%d blocked=%d busy=%d elapsed_sec=%.3f fps=%.2f renderer=\"%s\" texture=%s size=%dx%d" % [
        frames,
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
    return _runtime_flag("AETHERKIRI_IOS_FILE_LOG")

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
    var tex_size := Vector2(max(1.0, float(last_texture_size.x)), max(1.0, float(last_texture_size.y)))
    var panel_size := Vector2(
        float(_runtime_int("AETHERKIRI_AUTO_PROBE_COORD_W", 1600)),
        float(_runtime_int("AETHERKIRI_AUTO_PROBE_COORD_H", 900))
    )
    var scale: float = min(panel_size.x / tex_size.x, panel_size.y / tex_size.y)
    if scale <= 0.0:
        return Vector2(-1.0, -1.0)
    var drawn_size := tex_size * scale
    var offset := (panel_size - drawn_size) * 0.5
    var inside := pos - offset
    if inside.x < 0.0 or inside.y < 0.0 or inside.x > drawn_size.x or inside.y > drawn_size.y:
        return Vector2(-1.0, -1.0)
    return inside / scale

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

func _input(event: InputEvent) -> void:
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

    if detail_view == null or detail_scroll == null or not detail_view.visible:
        return

    if event is InputEventScreenTouch:
        var touch := event as InputEventScreenTouch
        detail_touch_scroll_active = touch.pressed
        return

    if event is InputEventScreenDrag:
        var drag := event as InputEventScreenDrag
        _scroll_detail_by(-drag.relative.y)
        get_viewport().set_input_as_handled()
        return

    if event is InputEventPanGesture:
        var pan := event as InputEventPanGesture
        _scroll_detail_by(pan.delta.y)
        get_viewport().set_input_as_handled()
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

    if event is InputEventMouseMotion and Input.is_mouse_button_pressed(MOUSE_BUTTON_LEFT):
        var motion := event as InputEventMouseMotion
        if absf(motion.relative.y) > 1.0:
            _scroll_detail_by(-motion.relative.y)
            get_viewport().set_input_as_handled()

func _scroll_detail_by(delta: float) -> void:
    var bar := detail_scroll.get_v_scroll_bar()
    if bar == null:
        return
    var next := clampf(float(detail_scroll.scroll_vertical) + delta, bar.min_value, bar.max_value)
    detail_scroll.scroll_vertical = int(next)

func _on_viewport_input(event: InputEvent) -> void:
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
    return game_running and viewport.visible and cached_startup_state == STARTUP_SUCCEEDED and (
        loading_panel == null or not loading_panel.visible
    )

func _is_game_pointer_event(event: InputEvent) -> bool:
    return event is InputEventMouseButton or event is InputEventMouseMotion or event is InputEventScreenTouch or event is InputEventScreenDrag or event is InputEventPanGesture

func _handle_game_pointer_event(event: InputEvent) -> bool:
    # Input callbacks only enqueue events; _process owns engine ticking and frame updates.
    _trace_input_received()
    if event is InputEventMouseButton:
        var mouse_button := event as InputEventMouseButton
        if _is_touch_platform() and mouse_button.button_index != MOUSE_BUTTON_WHEEL_UP and mouse_button.button_index != MOUSE_BUTTON_WHEEL_DOWN:
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
            -1.0 if mouse_button.button_index == MOUSE_BUTTON_WHEEL_UP else 1.0,
            button
        )
        if event_type == POINTER_DOWN:
            _hold_next_present_after_input()
        elif event_type == POINTER_UP:
            _hold_next_present_after_input(POST_CLICK_PRESENT_HOLD_FRAMES, true)
        return true
    elif event is InputEventMouseMotion:
        if _is_touch_platform():
            return true
        var motion := event as InputEventMouseMotion
        var mapped := _map_viewport_point(motion.position)
        if mapped.x < 0.0 or mapped.y < 0.0:
            _trace_input_outside()
            return false
        var rel := _map_viewport_delta(motion.relative)
        _send_game_pointer_event(
            POINTER_MOVE,
            0,
            mapped.x,
            mapped.y,
            rel.x,
            rel.y,
            0,
            POINTER_MOD_LEFT if Input.is_mouse_button_pressed(MOUSE_BUTTON_LEFT) else 0
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
            _clear_pending_touch_if_matches(pointer_id)
            _trace_input_throttled()
            return true
        if pointer_id == pending_touch_index:
            var pending_up_mapped := _map_viewport_point(touch.position, true)
            if pending_up_mapped.x < 0.0 or pending_up_mapped.y < 0.0:
                pending_up_mapped = pending_touch_mapped
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
        active_touch_points.erase(pointer_id)
        touch_down_points.erase(pointer_id)
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
        if suppressed_touch_points.has(pointer_id):
            _trace_input_throttled()
            return true
        if pointer_id == pending_touch_index:
            var pending_drag_mapped := _map_viewport_point(drag.position, true)
            if pending_drag_mapped.x < 0.0 or pending_drag_mapped.y < 0.0:
                pending_drag_mapped = pending_touch_mapped
            if pending_drag_mapped.distance_to(pending_touch_mapped) < TOUCH_DRAG_DISTANCE_THRESHOLD:
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
            active_touch_points[pointer_id] = mapped
            var rel := _map_viewport_delta(drag.relative)
            _send_game_pointer_event(
                POINTER_MOVE,
                _touch_engine_pointer_id(pointer_id),
                mapped.x,
                mapped.y,
                rel.x,
                rel.y,
                0,
                POINTER_MOD_LEFT
            )
        else:
            _trace_input_throttled()
        return true
    return false

func _suppress_touch_pointer(pointer_id: int) -> void:
    suppressed_touch_points[pointer_id] = true
    active_touch_points.erase(pointer_id)
    touch_down_points.erase(pointer_id)
    last_forwarded_touch_move_msec_by_id.erase(pointer_id)
    _clear_pending_touch_if_matches(pointer_id)

func _set_pending_touch(pointer_id: int, mapped: Vector2) -> void:
    suppressed_touch_points.erase(pointer_id)
    active_touch_points.erase(pointer_id)
    touch_down_points.erase(pointer_id)
    last_forwarded_touch_move_msec_by_id.erase(pointer_id)
    pending_touch_index = pointer_id
    pending_touch_mapped = mapped
    pending_touch_down_msec = Time.get_ticks_msec()

func _clear_pending_touch() -> void:
    pending_touch_index = -1
    pending_touch_mapped = Vector2.ZERO
    pending_touch_down_msec = 0

func _clear_pending_touch_if_matches(pointer_id: int) -> void:
    if pending_touch_index == pointer_id:
        _clear_pending_touch()

func _handle_secondary_touch_press(pointer_id: int, mapped: Vector2) -> bool:
    var now := Time.get_ticks_msec()
    if pending_touch_index >= 0 and pending_touch_index != pointer_id:
        if now - pending_touch_down_msec <= TOUCH_SECONDARY_TAP_WINDOW_MS:
            _send_touch_secondary_click(pointer_id, mapped)
            return true
        _flush_pending_touch_press(true)
        return false

    if active_touch_points.size() == 1 and last_forwarded_touch_down_msec > 0:
        if now - last_forwarded_touch_down_msec <= TOUCH_SECONDARY_TAP_WINDOW_MS:
            _send_touch_secondary_click(pointer_id, mapped)
            return true
    return false

func _flush_pending_touch_press_if_ready() -> void:
    _flush_pending_touch_press(false)

func _flush_pending_touch_press(force: bool = false) -> bool:
    if pending_touch_index < 0:
        return false
    var now := Time.get_ticks_msec()
    if not force and now - pending_touch_down_msec < TOUCH_SINGLE_TAP_DELAY_MS:
        return false

    var pointer_id := pending_touch_index
    var mapped := pending_touch_mapped
    _clear_pending_touch()
    suppressed_touch_points.erase(pointer_id)
    active_touch_points[pointer_id] = mapped
    touch_down_points[pointer_id] = mapped
    last_forwarded_touch_down_msec = now
    _send_game_pointer_event(POINTER_MOVE, _touch_engine_pointer_id(pointer_id), mapped.x, mapped.y, 0.0, 0.0, 0)
    _send_game_pointer_event(POINTER_DOWN, _touch_engine_pointer_id(pointer_id), mapped.x, mapped.y, 0.0, 0.0, 0)
    _arm_tick_trace()
    _arm_black_frame_guard()
    _hold_next_present_after_input()
    return true

func _send_pending_touch_click(pointer_id: int, up_mapped: Vector2) -> void:
    var down_mapped := pending_touch_mapped
    _clear_pending_touch()
    suppressed_touch_points.erase(pointer_id)
    active_touch_points.erase(pointer_id)
    touch_down_points.erase(pointer_id)
    last_forwarded_touch_move_msec_by_id.erase(pointer_id)

    last_forwarded_touch_down_msec = Time.get_ticks_msec()
    _send_game_pointer_event(POINTER_MOVE, _touch_engine_pointer_id(pointer_id), down_mapped.x, down_mapped.y, 0.0, 0.0, 0)
    _send_game_pointer_event(POINTER_DOWN, _touch_engine_pointer_id(pointer_id), down_mapped.x, down_mapped.y, 0.0, 0.0, 0)
    if up_mapped.distance_to(down_mapped) > 0.5:
        _send_game_pointer_event(
            POINTER_MOVE,
            _touch_engine_pointer_id(pointer_id),
            up_mapped.x,
            up_mapped.y,
            up_mapped.x - down_mapped.x,
            up_mapped.y - down_mapped.y,
            0,
            POINTER_MOD_LEFT
        )
    _send_game_pointer_event(POINTER_UP, _touch_engine_pointer_id(pointer_id), up_mapped.x, up_mapped.y, 0.0, 0.0, 0)
    last_forwarded_touch_up_msec = Time.get_ticks_msec()
    _apply_touch_action_cooldown()
    _arm_tick_trace()
    _arm_black_frame_guard()
    _hold_next_present_after_input(POST_CLICK_PRESENT_HOLD_FRAMES, true)

func _send_touch_secondary_click(pointer_id: int, mapped: Vector2) -> void:
    var click_mapped := mapped
    if pending_touch_index >= 0:
        var first_id := pending_touch_index
        click_mapped = (pending_touch_mapped + mapped) * 0.5
        suppressed_touch_points[first_id] = true
        touch_down_points.erase(first_id)
        last_forwarded_touch_move_msec_by_id.erase(first_id)
        _clear_pending_touch()
    elif not active_touch_points.is_empty():
        var first_id := int(active_touch_points.keys()[0])
        var first_mapped: Vector2 = active_touch_points.get(first_id, mapped)
        click_mapped = (first_mapped + mapped) * 0.5
        _send_game_pointer_event(POINTER_UP, _touch_engine_pointer_id(first_id), first_mapped.x, first_mapped.y, 0.0, 0.0, 0)
        active_touch_points.erase(first_id)
        touch_down_points.erase(first_id)
        last_forwarded_touch_move_msec_by_id.erase(first_id)
        suppressed_touch_points[first_id] = true
        last_forwarded_touch_up_msec = Time.get_ticks_msec()

    _suppress_touch_pointer(pointer_id)
    _send_game_pointer_event(POINTER_MOVE, TOUCH_SECONDARY_POINTER_ID, click_mapped.x, click_mapped.y, 0.0, 0.0, 0)
    last_forwarded_touch_down_msec = Time.get_ticks_msec()
    _send_game_pointer_event(POINTER_DOWN, TOUCH_SECONDARY_POINTER_ID, click_mapped.x, click_mapped.y, 0.0, 0.0, 1)
    _send_game_pointer_event(POINTER_UP, TOUCH_SECONDARY_POINTER_ID, click_mapped.x, click_mapped.y, 0.0, 0.0, 1)
    last_forwarded_touch_up_msec = Time.get_ticks_msec()
    _apply_touch_action_cooldown()
    _arm_tick_trace()
    _arm_black_frame_guard()
    _hold_next_present_after_input(POST_CLICK_PRESENT_HOLD_FRAMES, true)

func _touch_engine_pointer_id(pointer_id: int) -> int:
    return TOUCH_POINTER_ID_OFFSET + pointer_id

func _send_game_pointer_event(event_type: int, pointer_id: int, x: float, y: float, delta_x: float, delta_y: float, button: int, modifiers: int = 0) -> void:
    var result := int(player.send_pointer_event(event_type, pointer_id, x, y, delta_x, delta_y, button, modifiers))
    if not input_trace_enabled:
        return
    input_trace_forwarded += 1
    if result != ENGINE_RESULT_OK:
        input_trace_send_failed += 1

func _trace_input_received() -> void:
    if input_trace_enabled:
        input_trace_received += 1

func _trace_input_blocked() -> void:
    if input_trace_enabled:
        input_trace_blocked += 1

func _trace_input_throttled() -> void:
    if input_trace_enabled:
        input_trace_throttled += 1

func _trace_input_busy() -> void:
    if input_trace_enabled:
        input_trace_busy += 1

func _trace_input_move_suppressed() -> void:
    if input_trace_enabled:
        input_trace_move_suppressed += 1

func _trace_input_outside() -> void:
    if input_trace_enabled:
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

func _map_viewport_point(pos: Vector2, clamp_to_bounds: bool = false) -> Vector2:
    if viewport.texture == null:
        return pos
    var local_pos := pos - viewport.get_global_rect().position
    var tex_size: Vector2 = Vector2(
        max(1.0, float(viewport.texture.get_width())),
        max(1.0, float(viewport.texture.get_height()))
    )
    var panel_size: Vector2 = viewport.size
    var scale: float = min(panel_size.x / tex_size.x, panel_size.y / tex_size.y)
    if scale <= 0.0:
        return Vector2(-1.0, -1.0)
    var drawn_size: Vector2 = tex_size * scale
    var offset: Vector2 = (panel_size - drawn_size) * 0.5
    var inside: Vector2 = local_pos - offset
    if inside.x < 0.0 or inside.y < 0.0 or inside.x > drawn_size.x or inside.y > drawn_size.y:
        if not clamp_to_bounds:
            return Vector2(-1.0, -1.0)
        inside = Vector2(
            clampf(inside.x, 0.0, drawn_size.x),
            clampf(inside.y, 0.0, drawn_size.y)
        )
    return inside / scale

func _map_viewport_delta(delta: Vector2) -> Vector2:
    if viewport.texture == null:
        return delta
    var tex_size: Vector2 = Vector2(
        max(1.0, float(viewport.texture.get_width())),
        max(1.0, float(viewport.texture.get_height()))
    )
    var panel_size: Vector2 = viewport.size
    var scale: float = min(panel_size.x / tex_size.x, panel_size.y / tex_size.y)
    return delta / max(0.0001, scale)

func _map_mouse_button(button_index: MouseButton) -> int:
    if button_index == MOUSE_BUTTON_RIGHT:
        return 1
    if button_index == MOUSE_BUTTON_MIDDLE:
        return 2
    return 0

func _unhandled_input(event: InputEvent) -> void:
    if not game_running:
        return
    if event is InputEventKey:
        var key := event as InputEventKey
        player.send_key_event(key.pressed, key.keycode, key.get_modifiers_mask(), key.unicode)

func _append_log(line: String) -> void:
    if device_probe_enabled:
        _write_probe_marker("log %s" % line)
    _maybe_show_log_alert(line)
    if not _should_collect_log_lines():
        return
    log_lines.append(line)
    while log_lines.size() > MAX_LOG_LINES:
        log_lines.remove_at(0)
    if ui_log_enabled and log_view != null:
        log_view_dirty = true

func _scroll_log_to_bottom() -> void:
    if log_view == null:
        return
    log_view.scroll_vertical = max(0, log_view.get_line_count())
