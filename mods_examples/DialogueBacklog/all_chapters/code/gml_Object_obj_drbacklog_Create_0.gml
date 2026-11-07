// DialogueBacklog: T (configurable) opens an Ace Attorney-style backlog of every line shown this session
// (plus the end of this chapter's saved transcript). The game is frozen while it is open.
// Logging itself happens in obj_writer (see scr_drbacklog_writer); this object is only the viewer,
// re-created in every room by obj_time, so its state lives in globals.
depth = -99997;
scr_drbacklog_init();
global.drbacklog_open = 0;     // a fresh instance never starts inside an open backlog
// hotkey: T by default (DELTARUNE only reads T in dev/debug objects; F6/F8 are Steam Remote Play keys).
// Change it in the save folder's dialogue_backlog.ini, [keys] open=<Windows virtual-key code, decimal>
// (T 84, L 76, Tab 9, F9 120). It is ignored while bound to a game control (Config menu).
if (!variable_global_exists("drbacklog_key"))
{
    ini_open("dialogue_backlog.ini");
    global.drbacklog_key = round(ini_read_real("keys", "open", ord("T")));
    ini_write_string("keys", "open", string(global.drbacklog_key));
    ini_close();
    if (global.drbacklog_key <= 0 || global.drbacklog_key > 255)
        global.drbacklog_key = ord("T");
}
drbl_shot = -1;
drbl_frozen = [];
drbl_hold = 0;
drbl_close = function()
{
    for (var i = 0; i < array_length(drbl_frozen); i++)
        instance_activate_object(drbl_frozen[i]);
    drbl_frozen = [];
    audio_resume_all();
    if (sprite_exists(drbl_shot))
        sprite_delete(drbl_shot);
    drbl_shot = -1;
    global.drbacklog_open = 0;
    snd_play(snd_smallswing);
};
// name plate colour per speaker
drbl_color = function(_s)
{
    switch (_s)
    {
        case "Susie": return make_color_rgb(234, 121, 200);
        case "Ralsei": return make_color_rgb(98, 210, 120);
        case "Noelle": return make_color_rgb(255, 222, 96);
        case "Lancer": return make_color_rgb(96, 150, 255);
        case "King": return make_color_rgb(110, 140, 230);
        case "Berdly": return make_color_rgb(90, 200, 255);
        case "Toriel": return make_color_rgb(210, 160, 255);
        case "Asgore": return make_color_rgb(255, 190, 110);
        case "Queen": return make_color_rgb(150, 230, 255);
        case "Spamton": case "Spamton NEO": return make_color_rgb(255, 230, 60);
        case "Tenna": return make_color_rgb(255, 120, 90);
        case "Jevil": return make_color_rgb(190, 120, 255);
        case "Rouxls Kaard": return make_color_rgb(120, 220, 220);
    }
    return make_color_rgb(200, 200, 200);
};
