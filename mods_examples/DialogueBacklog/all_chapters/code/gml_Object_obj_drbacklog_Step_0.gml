// DialogueBacklog: open/close, scrolling, transcript flushing.
scr_drbacklog_init();
var _p = global.drbacklog_pending;
if (is_struct(_p) && (current_time - _p.ms) > 3000)
    scr_drbacklog_flush();

// a letter key may be rebound to a game control in the Config menu; then leave it to the game
var _key_free = true;
if (!global.drbacklog_open && variable_global_exists("input_k") && is_array(global.input_k))
{
    for (var _k = 0; _k < array_length(global.input_k); _k++)
    {
        if (global.input_k[_k] == global.drbacklog_key)
            _key_free = false;
    }
}
if ((_key_free && keyboard_check_pressed(global.drbacklog_key)) || (global.drbacklog_open && keyboard_check_pressed(ord("X"))))
{
    if (!global.drbacklog_open)
    {
        scr_drbacklog_flush();
        scr_drbacklog_load();
        var sw = surface_get_width(application_surface);
        var sh = surface_get_height(application_surface);
        drbl_shot = sprite_create_from_surface(application_surface, 0, 0, sw, sh, false, false, 0, 0);
        audio_pause_all();
        // remember what was active so closing doesn't wake things the game deactivated itself
        drbl_frozen = [];
        with (all)
        {
            if (id != other.id)
                array_push(other.drbl_frozen, id);
        }
        instance_deactivate_all(true);
        global.drbacklog_open = 1;
        global.drbacklog_scroll = 0;
        snd_play(snd_menumove);
    }
    else
    {
        drbl_close();
    }
    exit;
}
if (!global.drbacklog_open)
    exit;

// read the keyboard directly: the game's input objects are deactivated
var n = array_length(global.drbacklog_entries);
var d = 0;
var up = keyboard_check(vk_up);
var dn = keyboard_check(vk_down);
if (keyboard_check_pressed(vk_up))
{
    d = 1;
    drbl_hold = 0;
}
else if (keyboard_check_pressed(vk_down))
{
    d = -1;
    drbl_hold = 0;
}
else if (up || dn)
{
    // held: repeat after a short delay
    drbl_hold++;
    if (drbl_hold > 14 && (drbl_hold mod 3) == 0)
        d = up ? 1 : -1;
}
if (mouse_wheel_up())
    d = 1;
if (mouse_wheel_down())
    d = -1;
if (keyboard_check_pressed(vk_pageup))
    d = 5;
if (keyboard_check_pressed(vk_pagedown))
    d = -5;
if (keyboard_check_pressed(vk_home))
    d = n;
if (keyboard_check_pressed(vk_end))
    d = -n;
if (d != 0)
    global.drbacklog_scroll = clamp(global.drbacklog_scroll + d, 0, max(0, n - 1));
