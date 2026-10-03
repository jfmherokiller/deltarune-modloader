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
if (!variable_global_exists("drcheat_invcat"))
{
    global.drcheat_invcat = 0;
    global.drcheat_invsel = -1;
}
global.drcheat_open = 0;   // a fresh instance never starts inside an open menu
global.drcheat_page = 0;   // 0 = main page, 1 = inventory
drc_shot = -1;
drc_hold = 0;
drc_msg = "";
drc_msgtime = 0;
drc_names = ["?", "Kris", "Susie", "Ralsei", "Noelle"];
drc_rows = ["Gold", "Heal party", "Fill TP", "Lock HP", "No damage", "Infinite TP", "Character", "AT", "DF", "MAG", "Max HP", "Inventory...", "Close"];

// ---- inventory editor (dark world inventory: items, weapons, armor, key items)
drc_inv_cats = ["Items", "Weapons", "Armor", "Key Items"];
drc_inv_ready = 0;
drc_inv_ids = [];   // per category: valid ids, [0, ...] (0 = empty slot)
drc_inv_nm = [];    // per category: name by id
// item name via the game's own info scripts (they set these vars on self)
drc_inv_lookup = function(_cat, _id)
{
    switch (_cat)
    {
        case 0: scr_iteminfo(_id); return itemnameb;
        case 1: scr_weaponinfo(_id); return weaponnametemp;
        case 2: scr_armorinfo(_id); return armornametemp;
        case 3: scr_keyiteminfo(_id); return tempkeyitemname;
    }
    return "";
};
drc_inv_build = function()
{
    drc_inv_ids = [[0], [0], [0], [0]];
    drc_inv_nm = [[], [], [], []];
    for (var _c = 0; _c < 4; _c++)
    {
        for (var _i = 1; _i < 100; _i++)
        {
            var _nm = drc_inv_lookup(_c, _i);
            if (is_string(_nm) && string_replace_all(_nm, " ", "") != "" && _nm != "---")
            {
                array_push(drc_inv_ids[_c], _i);
                drc_inv_nm[_c][_i] = _nm;
            }
        }
    }
    drc_inv_ready = 1;
};
drc_inv_name = function(_cat, _id)
{
    if (_id <= 0)
        return "-";
    var _a = drc_inv_nm[_cat];
    if (_id < array_length(_a) && is_string(_a[_id]))
        return _a[_id];
    return "#" + string(_id);
};
// slot counts: items 12, weapons/armor 12 in Ch1 and 48 after, key items 12
drc_inv_slots = function(_cat)
{
    switch (_cat)
    {
        case 0: return min(12, array_length(global.item));
        case 1: return min((global.chapter <= 1) ? 12 : 48, array_length(global.weapon));
        case 2: return min((global.chapter <= 1) ? 12 : 48, array_length(global.armor));
        case 3: return min(12, array_length(global.keyitem));
    }
    return 0;
};
// direct global writes (a local copy of a global array would copy-on-write)
drc_inv_get = function(_cat, _i)
{
    switch (_cat)
    {
        case 0: return global.item[_i];
        case 1: return global.weapon[_i];
        case 2: return global.armor[_i];
        case 3: return global.keyitem[_i];
    }
    return 0;
};
drc_inv_set = function(_cat, _i, _v)
{
    switch (_cat)
    {
        case 0: global.item[_i] = _v; break;
        case 1: global.weapon[_i] = _v; break;
        case 2: global.armor[_i] = _v; break;
        case 3: global.keyitem[_i] = _v; break;
    }
};
// the game's menus stop at the first empty slot, so close gaps when leaving the page
drc_inv_compact = function()
{
    for (var _c = 0; _c < 4; _c++)
    {
        var _n = drc_inv_slots(_c);
        var _w = 0;
        for (var _i = 0; _i < _n; _i++)
        {
            var _v = drc_inv_get(_c, _i);
            if (_v != 0)
            {
                drc_inv_set(_c, _w, _v);
                _w++;
            }
        }
        for (var _i = _w; _i < _n; _i++)
            drc_inv_set(_c, _i, 0);
    }
};
drc_frozen = [];
drc_close = function()
{
    if (global.drcheat_page == 1)
        drc_inv_compact();
    global.drcheat_page = 0;
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
