// CheatMenu: F7 opens a paused cheat overlay (overworld and battle).
// State lives in globals so it survives room changes (this object is re-spawned by obj_time).
depth = -99999;
if (!variable_global_exists("drcheat_open"))
{
    global.drcheat_sel = 0;
    global.drcheat_slot = 0;
    global.drcheat_lockhp = 0;
    global.drcheat_nodmg = 0;
    global.drcheat_inftp = 0;
}
global.drcheat_open = 0;   // a fresh instance never starts inside an open menu
drc_shot = -1;
drc_hold = 0;
drc_msg = "";
drc_msgtime = 0;
drc_names = ["?", "Kris", "Susie", "Ralsei", "Noelle"];
drc_rows = ["Gold", "Heal party", "Fill TP", "Lock HP", "No damage", "Infinite TP", "Character", "AT", "DF", "MAG", "Max HP", "Close"];
drc_frozen = [];
drc_close = function()
{
    for (var i = 0; i < array_length(drc_frozen); i++)
        instance_activate_object(drc_frozen[i]);
    drc_frozen = [];
    audio_resume_all();
    if (sprite_exists(drc_shot))
        sprite_delete(drc_shot);
    drc_shot = -1;
    global.drcheat_open = 0;
    snd_play(snd_smallswing);
};
