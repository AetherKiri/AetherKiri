extends SceneTree

const MAIN_SOURCE := "res://scripts/main.gd"

func _initialize() -> void:
    var file := FileAccess.open(MAIN_SOURCE, FileAccess.READ)
    assert(file != null)
    if file == null:
        quit(1)
        return

    var source := file.get_as_text()
    var refresh_start := source.find("func _on_refresh_or_import() -> void:")
    var video_refresh_end := source.find("func _show_import_picker() -> void:", refresh_start)
    var video_refresh := source.substr(refresh_start, video_refresh_end - refresh_start)
    assert(video_refresh.find("_open_video_import_dialog()") >= 0)

    var import_start := source.find("func _open_video_import_dialog() -> void:")
    var import_end := source.find("func _video_card", import_start)
    var import_function := source.substr(import_start, import_end - import_start)
    var permission_offset := import_function.find("_ensure_android_storage_permission_for_import()")
    var dialog_offset := import_function.find("_create_file_dialog(")
    assert(permission_offset >= 0)
    assert(dialog_offset > permission_offset)

    var ready_start := source.find("func _finish_ready_after_first_frame() -> void:")
    var ready_end := source.find("func _continue_ready_after_legal_gate() -> void:", ready_start)
    var ready_function := source.substr(ready_start, ready_end - ready_start)
    assert(ready_function.find("_request_android_storage_permissions") < 0)

    var refresh_function_start := source.find("func _refresh_videos() -> void:")
    var refresh_function_end := source.find("func _load_video_list() ->", refresh_function_start)
    var refresh_function := source.substr(refresh_function_start, refresh_function_end - refresh_function_start)
    assert(refresh_function.find('if OS.get_name() == "Android":') < 0)

    print("VIDEO_IMPORT_PERMISSIONS_OK")
    quit(0)
