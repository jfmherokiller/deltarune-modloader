// ---- always-on cheats (run every step while the game is running)
var _hastp = variable_global_exists("tension") && variable_global_exists("maxtension");
if (!global.drcheat_open)
{
    // walk speed: scales obj_mainchara.bwspeed; at OFF it only puts back a value it wrote
    scr_drcheat_speed_apply(drc_speed_mults[clamp(global.drcheat_speed, 0, array_length(drc_speed_mults) - 1)]);
    if (global.drcheat_lockhp)
    {
        for (var i = 0; i < 3; i++)
        {
            var c = global.char[i];
            if (c > 0 && global.hp[c] < global.maxhp[c])
            {
                // a one-hit KO already marked them down (chardead); scr_heal revives them properly
                if (global.hp[c] <= 0 && instance_exists(obj_battlecontroller))
                    scr_heal(i, global.maxhp[c] - global.hp[c]);
                else
                    global.hp[c] = global.maxhp[c];
            }
        }
    }
    if (global.drcheat_nodmg && variable_global_exists("inv") && global.inv < 30)
        global.inv = 30;
    // Ch3 game-show board: party health is obj_mainchara_board.myhealth (hearts), copied into global.hp
    // by scr_board_battlehealth, and hits check the character's own iframes, not global.inv
    var _mb = asset_get_index("obj_mainchara_board");
    if (_mb >= 0 && (global.drcheat_lockhp || global.drcheat_nodmg))
    {
        var _lock = global.drcheat_lockhp;
        var _nod = global.drcheat_nodmg;
        with (_mb)
        {
            if (_lock && myhealth > 0 && myhealth < maxhealth)
                myhealth = maxhealth;
            if (_nod && iframes < 2)
                iframes = 2;
        }
    }
    // Ch5 platformer: Kris has his own hp (30) and an invincible flag; get_hurt() checks it first
    var _pp = asset_get_index("obj_plat_player");
    if (_pp >= 0 && (global.drcheat_lockhp || global.drcheat_nodmg))
    {
        var _lock2 = global.drcheat_lockhp;
        var _nod2 = global.drcheat_nodmg;
        with (_pp)
        {
            if (_lock2 && hp < max_hp)
                hp = max_hp;
            if (_nod2 && !invincible)
            {
                invincible = true;
                invincible_timer = 2;
            }
        }
    }
    // Ch4 Mike minigame: 3 lives, lost on hit
    var _mk = asset_get_index("obj_mike_minigame_controller");
    if (_mk >= 0 && (global.drcheat_lockhp || global.drcheat_nodmg))
    {
        with (_mk)
        {
            if (life > 0 && life < 3)
                life = 3;
        }
    }
    if (global.drcheat_inftp && _hastp && global.tension < global.maxtension)
        global.tension = global.maxtension;
    if (global.drcheat_puzzlet > 0)
    {
        // Give back the frame the puzzle just spent: always when frozen, 2 of every 3 when slowed.
        // Skipped where the game needs the puzzle to fail: Chapter 5's four-switch scene
        // (obj_ch5_DW05 waits for failtimer <= 0, and the timeout must happen before the
        // party's caterpillars it dooms are destroyed, or the fail cleanup reads a dead instance).
        drc_ptick = (drc_ptick + 1) mod 3;
        var _scripted = asset_get_index("obj_ch5_DW05");
        var _safe = !(_scripted >= 0 && instance_exists(_scripted));
        var _mz = asset_get_index("obj_marker_z");
        if (_safe && _mz >= 0)
            with (_mz)
                if (variable_instance_exists(id, "mycaterpillar") && !instance_exists(mycaterpillar))
                    _safe = false;
        if (_safe && (global.drcheat_puzzlet == 2 || drc_ptick != 0))
        {
            var _gp = asset_get_index("obj_glowtile_puzzle_new");   // Ch5: counts failtimer down
            if (_gp >= 0)
                with (_gp)
                    if (state == 1 && cutscene_mode == 0 && bonus_delay <= 0)
                        failtimer += 1;
            _gp = asset_get_index("obj_glowtilepuzz");              // Ch1/3: counts up to failmax
            if (_gp >= 0)
                with (_gp)
                    if (active == 1 && failtimer > 0)
                        failtimer -= 1;
        }
    }
    // the pages' own always-on cheats (enemy auto mercy, TV minigame assist, ...)
    for (var _p = 1; _p < array_length(drc_pages); _p++)
        script_execute(drc_pages[_p].scr, "tick");
}

// ---- open / close
if (global.drcheat_open && global.drcheat_page != 0 && keyboard_check_pressed(ord("X")))
{
    drc_page_call("leave");
    global.drcheat_page = 0;
    snd_play(snd_smallswing);
    exit;
}
if (keyboard_check_pressed(vk_f7) || (global.drcheat_open && keyboard_check_pressed(ord("X"))))
{
    if (!global.drcheat_open)
    {
        var sw = surface_get_width(application_surface);
        var sh = surface_get_height(application_surface);
        drc_shot = sprite_create_from_surface(application_surface, 0, 0, sw, sh, false, false, 0, 0);
        drc_battle_open = instance_exists(obj_battlecontroller);
        for (var _p = 1; _p < array_length(drc_pages); _p++)
            script_execute(drc_pages[_p].scr, "opened");
        audio_pause_all();
        // remember what was active so closing doesn't wake things the game deactivated itself
        drc_frozen = [];
        with (all)
        {
            if (id != other.id)
                array_push(other.drc_frozen, id);
        }
        instance_deactivate_all(true);
        global.drcheat_open = 1;
        snd_play(snd_drcheat_open);
    }
    else
    {
        drc_close();
    }
    exit;
}
if (!global.drcheat_open)
    exit;

// ---- menu input (read the keyboard directly; the game's input objects are deactivated)
var dir = keyboard_check(vk_right) - keyboard_check(vk_left);
var step = 0;
if (dir != 0)
{
    drc_hold++;
    if (drc_hold == 1 || (drc_hold > 15 && (drc_hold mod 3) == 0))
        step = dir;
}
else
{
    drc_hold = 0;
}
var big = keyboard_check(vk_shift);
var ok = keyboard_check_pressed(ord("Z")) || keyboard_check_pressed(vk_enter);
drc_step = step;
drc_big = big;
drc_ok = ok;
if (drc_msgtime > 0)
    drc_msgtime--;

// ---- sub page: its own script handles the input
if (global.drcheat_page != 0)
{
    drc_page_call("step");
    exit;
}

// ---- main page
var n = array_length(drc_rows);
if (keyboard_check_pressed(vk_up))
{
    global.drcheat_sel = (global.drcheat_sel + n - 1) mod n;
    snd_play(snd_menumove);
}
if (keyboard_check_pressed(vk_down))
{
    global.drcheat_sel = (global.drcheat_sel + 1) mod n;
    snd_play(snd_menumove);
}

// selected party member (skip empty slots)
var slot = clamp(global.drcheat_slot, 0, 2);
if (global.char[slot] <= 0)
    slot = 0;
var ch = global.char[slot];

var row = drc_rows[clamp(global.drcheat_sel, 0, n - 1)];
switch (row.key)
{
    case "page":
        if (ok)
        {
            var _pg = drc_pages[row.page];
            if (_pg.why == "")
                global.drcheat_page = row.page;
            else
            {
                drc_msg = _pg.why;
                drc_msgtime = 60;
            }
            snd_play(snd_select);
        }
        break;
    case "heal":
        if (ok)
        {
            for (var i = 0; i < 3; i++)
            {
                var c = global.char[i];
                if (c > 0) global.hp[c] = global.maxhp[c];
            }
            drc_msg = "Party healed"; drc_msgtime = 60; snd_play(snd_select);
        }
        break;
    case "filltp":
        if (ok && _hastp) { global.tension = global.maxtension; drc_msg = "TP filled"; drc_msgtime = 60; snd_play(snd_select); }
        break;
    case "lockhp":
        if (ok || step != 0) { global.drcheat_lockhp = !global.drcheat_lockhp; snd_play(snd_select); }
        break;
    case "nodmg":
        if (ok || step != 0) { global.drcheat_nodmg = !global.drcheat_nodmg; snd_play(snd_select); }
        break;
    case "inftp":
        if (ok || step != 0) { global.drcheat_inftp = !global.drcheat_inftp; snd_play(snd_select); }
        break;
    case "puzzle":
        if (ok || step != 0)
        {
            global.drcheat_puzzlet = (global.drcheat_puzzlet + (step < 0 ? 2 : 1)) mod 3;
            snd_play(snd_select);
        }
        break;
    case "speed":
        if (ok || step != 0)
        {
            var _ns = array_length(drc_speed_mults);
            global.drcheat_speed = (global.drcheat_speed + (step < 0 ? _ns - 1 : 1)) mod _ns;
            snd_play(snd_select);
        }
        break;
    case "char":
        if (step != 0)
        {
            for (var k = 0; k < 3; k++)
            {
                slot = (slot + 3 + step) mod 3;
                if (global.char[slot] > 0) break;
            }
            global.drcheat_slot = slot;
            snd_play(snd_menumove);
        }
        break;
    case "at":
        if (step != 0 && ch > 0) { global.at[ch] = max(0, global.at[ch] + step * (big ? 10 : 1)); snd_play(snd_menumove); }
        break;
    case "df":
        if (step != 0 && ch > 0) { global.df[ch] = max(0, global.df[ch] + step * (big ? 10 : 1)); snd_play(snd_menumove); }
        break;
    case "mag":
        if (step != 0 && ch > 0) { global.mag[ch] = max(0, global.mag[ch] + step * (big ? 10 : 1)); snd_play(snd_menumove); }
        break;
    case "maxhp":
        if (step != 0 && ch > 0)
        {
            global.maxhp[ch] = max(1, global.maxhp[ch] + step * (big ? 100 : 10));
            global.hp[ch] = min(global.hp[ch], global.maxhp[ch]);
            snd_play(snd_menumove);
        }
        break;
    case "close":
        if (ok) drc_close();
        break;
}
