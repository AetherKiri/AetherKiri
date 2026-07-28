extends SceneTree

const DOCUMENTS := [
    "res://legal/privacy_disclaimer_zh_hans.txt",
    "res://legal/privacy_disclaimer_zh_hant.txt",
    "res://legal/privacy_disclaimer_en.txt",
    "res://legal/privacy_disclaimer_ja.txt",
    "res://legal/privacy_disclaimer_ko.txt",
    "res://legal/ios_app_store_statement_zh_hans.txt",
    "res://legal/ios_app_store_statement_zh_hant.txt",
    "res://legal/ios_app_store_statement_en.txt",
    "res://legal/ios_app_store_statement_ja.txt",
    "res://legal/ios_app_store_statement_ko.txt",
]

func _initialize() -> void:
    if FileAccess.file_exists("res://tests/legal_pack_test.gd"):
        push_error("test scripts must not be included in the release pack")
        quit(1)
        return
    for path in DOCUMENTS:
        if not FileAccess.file_exists(path):
            push_error("legal document missing from pack: %s" % path)
            quit(2)
            return
        var file := FileAccess.open(path, FileAccess.READ)
        var text := file.get_as_text() if file != null else ""
        if not text.contains("Aether") or text.length() < 1000:
            push_error("legal document is incomplete: %s" % path)
            quit(3)
            return
    print("LEGAL_PACK_OK")
    quit(0)
