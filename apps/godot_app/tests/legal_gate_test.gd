extends SceneTree

const MAIN_SCRIPT := preload("res://scripts/main.gd")

func _initialize() -> void:
    var app = MAIN_SCRIPT.new()

    app.legal_accepted_version = ""
    app.ios_statement_accepted_version = ""
    assert(app._next_required_legal_document("iOS") == "privacy")
    assert(app._next_required_legal_document("macOS") == "privacy")

    app.legal_accepted_version = "2026-07-27.4"
    assert(app._next_required_legal_document("macOS") == "ios_statement")
    assert(app._next_required_legal_document("iOS") == "ios_statement")
    assert(app._ios_statement_required("macOS"))
    assert(not app._ios_statement_required("Windows"))

    app.ios_statement_accepted_version = "2026-08-04"
    assert(app._next_required_legal_document("iOS").is_empty())
    assert(app._next_required_legal_document("macOS").is_empty())

    app.legal_accepted_version = ""
    assert(app._next_required_legal_document("iOS") == "privacy")

    app.free()
    print("LEGAL_GATE_OK")
    quit(0)
