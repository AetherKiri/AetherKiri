extends SceneTree

const MAIN_SCRIPT := preload("res://scripts/main.gd")
const EXPECTED_DOCUMENTS := {
    "zh_hans": ["res://legal/privacy_disclaimer_zh_hans.txt", "隐私政策"],
    "zh_hant": ["res://legal/privacy_disclaimer_zh_hant.txt", "隱私權政策"],
    "en": ["res://legal/privacy_disclaimer_en.txt", "Privacy Policy"],
    "ja": ["res://legal/privacy_disclaimer_ja.txt", "プライバシーポリシー"],
    "ko": ["res://legal/privacy_disclaimer_ko.txt", "개인정보 처리방침"],
}
const EXPECTED_IOS_STATEMENTS := {
    "zh_hans": ["res://legal/ios_app_store_statement_zh_hans.txt", "有限额外许可", "第三方不得援引"],
    "zh_hant": ["res://legal/ios_app_store_statement_zh_hant.txt", "有限額外許可", "第三方不得援引"],
    "en": ["res://legal/ios_app_store_statement_en.txt", "Limited additional permission", "no third-party reliance"],
    "ja": ["res://legal/ios_app_store_statement_ja.txt", "限定的な追加許諾", "第三者による援用を認めない"],
    "ko": ["res://legal/ios_app_store_statement_ko.txt", "제한적인 추가 허가", "제3자는 원용할 수 없음"],
}

func _initialize() -> void:
    var app = MAIN_SCRIPT.new()
    for language in EXPECTED_DOCUMENTS:
        var expected: Array = EXPECTED_DOCUMENTS[language]
        app.active_language = language
        var path := String(app._legal_document_path())
        assert(path == String(expected[0]))
        var text := String(app._load_legal_document())
        assert(text.contains(String(expected[1])))
        assert(text.contains("2026-07-27.4"))
        assert(text.length() > 1000)
        var statement_expected: Array = EXPECTED_IOS_STATEMENTS[language]
        var statement_path := String(app._ios_statement_document_path())
        assert(statement_path == String(statement_expected[0]))
        var statement_text := String(app._load_ios_statement_document())
        assert(statement_text.contains(String(statement_expected[1])))
        assert(statement_text.contains(String(statement_expected[2])))
        assert(statement_text.contains("2026-08-04"))
        assert(statement_text.contains("Apple"))
        assert(statement_text.contains("macOS"))
        assert(statement_text.contains("COPYING.iOS"))
        assert(statement_text.contains("THIRD_PARTY_LICENSES.md"))
        assert(statement_text.length() > 1200)
    app.free()
    print("LEGAL_LOCALIZATION_OK")
    quit(0)
