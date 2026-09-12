extends RefCounted

# The browser owns authentication and the final publish action. No OAuth
# credentials or automatic posting are performed by the game runtime.
static func tweet(parent: Node, fields: Dictionary, done: Callable, preview: Image) -> void:
    var dialog := ConfirmationDialog.new()
    dialog.title = "Share on X"
    dialog.ok_button_text = "Open browser composer"
    dialog.exclusive = true
    var box := VBoxContainer.new()
    box.add_theme_constant_override("separation", 12)
    dialog.add_child(box)
    var text := TextEdit.new()
    text.text = String(fields.get("text", ""))
    text.custom_minimum_size = Vector2(580, 140)
    box.add_child(text)
    var notice := Label.new()
    notice.text = "Nothing is posted automatically. Attach a saved screenshot in the browser if wanted."
    notice.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
    box.add_child(notice)
    if preview != null and not preview.is_empty():
        var image := TextureRect.new()
        image.texture = ImageTexture.create_from_image(preview)
        image.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
        image.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
        image.custom_minimum_size = Vector2(580, 240)
        box.add_child(image)
        dialog.add_button("Save screenshot…", false, "capture")
        dialog.custom_action.connect(func(action: StringName):
            if action == &"capture":
                _save_preview(dialog, preview)
        )
    dialog.confirmed.connect(func():
        var error := OS.shell_open("https://x.com/intent/tweet?text=" + text.text.uri_encode())
        if error != OK:
            push_error("Unable to open X composer: %s" % error_string(error))
        done.call()
        dialog.queue_free()
    )
    dialog.canceled.connect(func():
        done.call()
        dialog.queue_free()
    )
    parent.add_child(dialog)
    dialog.popup_centered()
    text.grab_focus()

static func _save_preview(parent: Node, preview: Image) -> void:
    var picker := FileDialog.new()
    picker.title = "Save screenshot"
    picker.file_mode = FileDialog.FILE_MODE_SAVE_FILE
    picker.access = FileDialog.ACCESS_FILESYSTEM
    picker.use_native_dialog = true
    picker.add_filter("*.png ; PNG image")
    picker.current_file = "capture.png"
    picker.file_selected.connect(func(path: String):
        var error := preview.save_png(path)
        if error != OK:
            push_error("Unable to save screenshot: %s" % error_string(error))
        picker.queue_free()
    )
    picker.canceled.connect(picker.queue_free)
    parent.add_child(picker)
    picker.popup_centered(Vector2i(800, 600))

static func joypad(parent: Node, mapping: RefCounted, done: Callable) -> void:
    var dialog := ConfirmationDialog.new()
    dialog.title = "Siglus gamepad bindings"
    dialog.ok_button_text = "Save"
    dialog.exclusive = true
    var box := VBoxContainer.new()
    dialog.add_child(box)
    var devices := Label.new()
    var names := PackedStringArray()
    for device in Input.get_connected_joypads():
        names.append(Input.get_joy_name(device))
    devices.text = "Connected: " + ", ".join(names) if not names.is_empty() else "No gamepad connected. Bindings can still be edited."
    devices.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
    box.add_child(devices)
    var scroll := ScrollContainer.new()
    scroll.custom_minimum_size = Vector2(520, 400)
    box.add_child(scroll)
    var rows := VBoxContainer.new()
    rows.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    scroll.add_child(rows)
    var choices := {0: "Raw only", 13: "Decide / Enter", 27: "Cancel / Escape", 32: "Space", 17: "Skip / Control", 9: "Tab", 33: "Page Up", 34: "Page Down", 37: "Left", 38: "Up", 39: "Right", 40: "Down"}
    var edited: Dictionary = mapping.bindings.duplicate()
    for button in range(32):
        var row := HBoxContainer.new()
        rows.add_child(row)
        var label := Label.new()
        label.text = "Button %d" % (button + 1)
        label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
        row.add_child(label)
        var select := OptionButton.new()
        for vk in choices:
            select.add_item(choices[vk], vk)
        var current := int(edited.get(button, 0))
        if not choices.has(current):
            select.add_item("VK 0x%02X" % current, current)
        select.select(select.get_item_index(current))
        select.item_selected.connect(func(index: int): edited[button] = select.get_item_id(index))
        row.add_child(select)
    dialog.confirmed.connect(func():
        mapping.bindings = edited
        var error: int = mapping.save_config()
        if error != OK:
            push_error("Unable to save gamepad bindings: %s" % error_string(error))
        done.call()
        dialog.queue_free()
    )
    dialog.canceled.connect(func():
        done.call()
        dialog.queue_free()
    )
    parent.add_child(dialog)
    dialog.popup_centered()
