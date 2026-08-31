extends Node

signal resolved(game_path: String, cover_path: String, vndb_id: String)

const API_URL := "https://api.vndb.org/kana/vn"

func resolve(game: Dictionary) -> void:
    var candidates: PackedStringArray = game.get("titleCandidates", PackedStringArray())
    if candidates.is_empty():
        _finish(game, "", "")
        return
    var request := HTTPRequest.new()
    add_child(request)
    request.request_completed.connect(func(_result, code, _headers, body):
        var parsed = JSON.parse_string(body.get_string_from_utf8()) if code == 200 else null
        var rows: Array = parsed.get("results", []) if parsed is Dictionary else []
        if rows.is_empty():
            request.queue_free()
            _finish(game, "", "")
            return
        var row: Dictionary = rows[0]
        var image: Dictionary = row.get("image", {})
        var url := String(image.get("url", ""))
        var id := String(row.get("id", ""))
        if url.is_empty():
            request.queue_free()
            _finish(game, "", id)
            return
        var cover_request := HTTPRequest.new()
        add_child(cover_request)
        cover_request.request_completed.connect(func(download_result, download_code, _download_headers, download_body):
            var saved := ""
            if download_result == HTTPRequest.RESULT_SUCCESS and download_code == 200 and not download_body.is_empty():
                var root := String(game.get("path", ""))
                if FileAccess.file_exists(root):
                    root = root.get_base_dir()
                saved = root.path_join("aetherkiri-cover-" + id + ".jpg")
                DirAccess.make_dir_recursive_absolute(root)
                var file := FileAccess.open(saved, FileAccess.WRITE)
                if file != null:
                    file.store_buffer(download_body)
                else:
                    saved = ""
            cover_request.queue_free()
            request.queue_free()
            _finish(game, saved, id)
        )
        cover_request.request(url)
    )
    var payload := {"filters": ["search", "=", String(candidates[0])], "fields": "id,title,image.url,image.thumbnail"}
    request.request(API_URL, PackedStringArray(["Content-Type: application/json"]), HTTPClient.METHOD_POST, JSON.stringify(payload))

func _finish(game: Dictionary, cover_path: String, vndb_id: String) -> void:
    resolved.emit(String(game.get("path", "")), cover_path, vndb_id)
