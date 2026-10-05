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
if (!variable_global_exists("drcheat_automercy"))
{
    global.drcheat_automercy = 0;
    global.drcheat_ensel = 0;
}
if (!variable_global_exists("drcheat_invcat"))
{
    global.drcheat_invcat = 0;
    global.drcheat_invsel = -1;
}
global.drcheat_open = 0;   // a fresh instance never starts inside an open menu
global.drcheat_page = 0;   // 0 = main page, 1 = inventory, 2 = enemies
drc_shot = -1;
drc_hold = 0;
drc_msg = "";
drc_msgtime = 0;
drc_names = ["?", "Kris", "Susie", "Ralsei", "Noelle"];
drc_rows = ["Gold", "Heal party", "Fill TP", "Lock HP", "No damage", "Infinite TP", "Character", "AT", "DF", "MAG", "Max HP", "Inventory...", "Enemies...", "Close"];
// Chapter 3 only: TV show page (points + saved board rankings)
drc_has_tv = global.chapter == 3;
if (drc_has_tv)
    drc_rows = ["Gold", "Heal party", "Fill TP", "Lock HP", "No damage", "Infinite TP", "Character", "AT", "DF", "MAG", "Max HP", "Inventory...", "Enemies...", "TV Show...", "Close"];
global.drcheat_tvsel = 0;
// flag 1044 = points (PTs), 1173/1174 = saved rank of board 1/2 (0 Z, 1 C, 2 B, 3 A, 4 S, 5 T).
// The ranks unlock the ranking-room doors (A>=3, B>=2, C>=1) and the T-rank room (5).
drc_tv_letters = ["Z", "C", "B", "A", "S", "T"];
drc_tv_rows = ["Points", "Board 1 rank", "Board 2 rank", "Minigame assist", "Minigame: top score"];
if (!variable_global_exists("drcheat_mgassist"))
    global.drcheat_mgassist = 0;
global.drcheat_mgmax = 0;   // pending "top score"; applied after closing (minigame objects are frozen while open)

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


// ---- enemy editor (battle only): the 3 enemy slots live in global.monster* arrays
// the open menu deactivates every instance, so remember whether a battle was running when it opened
drc_battle_open = 0;
drc_in_battle = function()
{
    if (!variable_global_exists("monster") || !variable_global_exists("mercymod"))
        return 0;
    return global.drcheat_open ? drc_battle_open : instance_exists(obj_battlecontroller);
};
// enemies the game never lets you spare (per chapter, generated by tools/gen_nospare.csx from the
// game's own code: SPARE event is a no-op, mercy forced back to 0, or no way to gain mercy).
// Forcing their spare state breaks the scripted fight (crash seen sparing Ch2 Queen).
drc_nospare_types = scr_drcheat_nospare();
drc_en_nospare = function(_i)
{
    var _t = global.monstertype[_i];
    for (var _k = 0; _k < array_length(drc_nospare_types); _k++)
    {
        if (drc_nospare_types[_k] == _t)
            return 1;
    }
    return 0;
};
drc_en_alive = function(_i)
{
    return global.monster[_i] == 1;
};
drc_en_mercymax = function(_i)
{
    var _m = 100;
    if (variable_global_exists("mercymax") && is_real(global.mercymax[_i]))
        _m = max(100, global.mercymax[_i]);
    return _m;
};
// the battle's SPARE / Pacify checks mercy >= mercymax and status 1 (Tired)
drc_en_spareable = function(_i)
{
    global.mercymod[_i] = drc_en_mercymax(_i);
    global.monsterstatus[_i] = 1;
};
// rows: [kind, enemy]; 0 auto mercy, 1 all spareable, 2 all HP 1, 10 HP, 11 mercy, 12 tired
drc_en_rows = function()
{
    var _r = [[0, -1], [1, -1], [2, -1]];
    if (drc_in_battle())
    {
        for (var _i = 0; _i < 3; _i++)
        {
            if (drc_en_alive(_i))
            {
                array_push(_r, [10, _i]);
                if (!drc_en_nospare(_i))
                {
                    array_push(_r, [11, _i]);
                    array_push(_r, [12, _i]);
                }
            }
        }
    }
    return _r;
};
drc_en_name = function(_i)
{
    var _n = "";
    if (variable_global_exists("monstername") && is_string(global.monstername[_i]))
        _n = global.monstername[_i];
    if (_n == "")
        _n = "Enemy " + string(_i + 1);
    return _n;
};
