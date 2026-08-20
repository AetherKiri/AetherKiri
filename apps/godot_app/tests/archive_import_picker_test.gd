extends SceneTree

const MAIN_SOURCE := "res://scripts/main.gd"

func _initialize() -> void:
    var file := FileAccess.open(MAIN_SOURCE, FileAccess.READ)
    assert(file != null)
    if file == null:
        quit(1)
        return

    var source := file.get_as_text()
    var picker_start := source.find("func _show_import_picker() -> void:")
    var picker_end := source.find("func _open_archive_import_dialog() -> void:", picker_start)
    assert(picker_start >= 0)
    assert(picker_end > picker_start)

    var picker := source.substr(picker_start, picker_end - picker_start)
    assert(picker.find('_t("dialog.select_game_dir")') >= 0)
    assert(picker.find('_t("dialog.select_archive_file")') >= 0)
    assert(picker.find("_open_archive_import_dialog()") >= 0)

    assert(source.find("archive_import_begin_probe") >= 0)
    assert(source.find("archive_import_begin_extract") >= 0)
    assert(source.find("archive_import_take_result") >= 0)
    assert(source.find("func _find_importable_game_root") >= 0)
    assert(source.find('"archive.progress_extract"') >= 0)

    print("ARCHIVE_IMPORT_PICKER_OK")
    quit(0)
