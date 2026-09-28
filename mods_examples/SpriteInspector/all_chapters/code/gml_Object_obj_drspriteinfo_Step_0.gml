if (keyboard_check_pressed(vk_f9))
{
    if (keyboard_check(vk_shift))
    {
        // Shift+F9: copy room + every sprite name currently on screen
        var txt = "room: " + room_get_name(room) + "\n";
        for (var i = 0; i < array_length(drspr_names); i++)
            txt += drspr_names[i] + "\n";
        clipboard_set_text(txt);
        drspr_msg = "copied " + string(array_length(drspr_names)) + " sprite name(s)";
        drspr_msgtime = 90;
        global.drspr_on = 1;
    }
    else
    {
        global.drspr_on = !global.drspr_on;
    }
}
if (drspr_msgtime > 0)
    drspr_msgtime--;
