// ---- always-on cheats (run every step while the game is running)
var _hastp = variable_global_exists("tension") && variable_global_exists("maxtension");
if (!global.drcheat_open)
{
    if (global.drcheat_lockhp)
    {
        for (var i = 0; i < 3; i++)
        {
            var c = global.char[i];
            if (c > 0 && global.hp[c] < global.maxhp[c])
                global.hp[c] = global.maxhp[c];
        }
    }
    if (global.drcheat_nodmg && variable_global_exists("inv") && global.inv < 30)
        global.inv = 30;
    if (global.drcheat_inftp && _hastp && global.tension < global.maxtension)
        global.tension = global.maxtension;
    if (global.drcheat_automercy && drc_in_battle())
    {
        for (var i = 0; i < 3; i++)
        {
            if (drc_en_alive(i) && !drc_en_nospare(i) && global.mercymod[i] < drc_en_mercymax(i))
                drc_en_spareable(i);
        }
    }
}

// ---- open / close
if (global.drcheat_open && global.drcheat_page != 0 && keyboard_check_pressed(ord("X")))
{
    if (global.drcheat_page == 1)
        drc_inv_compact();
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


// ---- inventory page
if (global.drcheat_page == 1)
{
    if (!drc_inv_ready)
        drc_inv_build();
    var cat = clamp(global.drcheat_invcat, 0, 3);
    var ns = drc_inv_slots(cat);
    var r = clamp(global.drcheat_invsel, -1, ns - 1);
    if (keyboard_check_pressed(vk_up))
    {
        r = (r <= -1) ? (ns - 1) : (r - 1);
        snd_play(snd_menumove);
    }
    if (keyboard_check_pressed(vk_down))
    {
        r = (r >= ns - 1) ? -1 : (r + 1);
        snd_play(snd_menumove);
    }
    if (r == -1 && step != 0)
    {
        cat = (cat + 4 + step) mod 4;
        snd_play(snd_menumove);
    }
    else if (r >= 0 && step != 0)
    {
        // cycle through the ids the game has names for (0 = empty)
        var ids = drc_inv_ids[cat];
        var cnt = array_length(ids);
        var cur = drc_inv_get(cat, r);
        var pos = 0;
        for (var k = 0; k < cnt; k++)
        {
            if (ids[k] == cur) { pos = k; break; }
        }
        pos = (pos + cnt + step * (big ? 5 : 1)) mod cnt;
        if (pos < 0) pos += cnt;
        drc_inv_set(cat, r, ids[pos]);
        snd_play(snd_menumove);
    }
    if (r >= 0 && (keyboard_check_pressed(vk_delete) || keyboard_check_pressed(vk_backspace) || keyboard_check_pressed(ord("C"))))
    {
        drc_inv_set(cat, r, 0);
        snd_play(snd_smallswing);
    }
    global.drcheat_invcat = cat;
    global.drcheat_invsel = r;
    if (drc_msgtime > 0)
        drc_msgtime--;
    exit;
}


// ---- enemy page
if (global.drcheat_page == 2)
{
    var rows = drc_en_rows();
    var nr = array_length(rows);
    var r = clamp(global.drcheat_ensel, 0, nr - 1);
    if (keyboard_check_pressed(vk_up)) { r = (r + nr - 1) mod nr; snd_play(snd_menumove); }
    if (keyboard_check_pressed(vk_down)) { r = (r + 1) mod nr; snd_play(snd_menumove); }
    var kind = rows[r][0];
    var e = rows[r][1];
    var fight = drc_in_battle();
    switch (kind)
    {
        case 0:
            if (ok || step != 0) { global.drcheat_automercy = !global.drcheat_automercy; snd_play(snd_select); }
            break;
        case 1:
            if (ok && fight)
            {
                var done = 0;
                var skipped = 0;
                for (var i = 0; i < 3; i++)
                {
                    if (!drc_en_alive(i)) continue;
                    if (drc_en_nospare(i)) { skipped++; continue; }
                    drc_en_spareable(i);
                    done++;
                }
                if (done == 0 && skipped > 0) { drc_msg = "Can't be spared"; snd_play(snd_smallswing); }
                else { drc_msg = (skipped > 0) ? "Spareable (some can't be)" : "All can be spared"; snd_play(snd_select); }
                drc_msgtime = 60;
            }
            break;
        case 2:
            if (ok && fight)
            {
                for (var i = 0; i < 3; i++)
                    if (drc_en_alive(i)) global.monsterhp[i] = 1;
                drc_msg = "Enemy HP set to 1"; drc_msgtime = 60; snd_play(snd_select);
            }
            break;
        case 10:
            if (step != 0)
            {
                global.monsterhp[e] = clamp(global.monsterhp[e] + step * (big ? 100 : 10), 1, max(1, global.monstermaxhp[e]));
                snd_play(snd_menumove);
            }
            break;
        case 11:
            if (step != 0)
            {
                global.mercymod[e] = clamp(global.mercymod[e] + step * (big ? 100 : 10), 0, drc_en_mercymax(e));
                snd_play(snd_menumove);
            }
            break;
        case 12:
            if (ok || step != 0)
            {
                global.monsterstatus[e] = (global.monsterstatus[e] == 1) ? 0 : 1;
                snd_play(snd_select);
            }
            break;
    }
    global.drcheat_ensel = r;
    if (drc_msgtime > 0)
        drc_msgtime--;
    exit;
}

// selected party member (skip empty slots)
var slot = clamp(global.drcheat_slot, 0, 2);
if (global.char[slot] <= 0)
    slot = 0;
var ch = global.char[slot];

switch (global.drcheat_sel)
{
    case 0:
        if (step != 0) { global.gold = max(0, global.gold + step * (big ? 1000 : 10)); snd_play(snd_menumove); }
        break;
    case 1:
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
    case 2:
        if (ok && _hastp) { global.tension = global.maxtension; drc_msg = "TP filled"; drc_msgtime = 60; snd_play(snd_select); }
        break;
    case 3:
        if (ok || step != 0) { global.drcheat_lockhp = !global.drcheat_lockhp; snd_play(snd_select); }
        break;
    case 4:
        if (ok || step != 0) { global.drcheat_nodmg = !global.drcheat_nodmg; snd_play(snd_select); }
        break;
    case 5:
        if (ok || step != 0) { global.drcheat_inftp = !global.drcheat_inftp; snd_play(snd_select); }
        break;
    case 6:
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
    case 7:
        if (step != 0 && ch > 0) { global.at[ch] = max(0, global.at[ch] + step * (big ? 10 : 1)); snd_play(snd_menumove); }
        break;
    case 8:
        if (step != 0 && ch > 0) { global.df[ch] = max(0, global.df[ch] + step * (big ? 10 : 1)); snd_play(snd_menumove); }
        break;
    case 9:
        if (step != 0 && ch > 0) { global.mag[ch] = max(0, global.mag[ch] + step * (big ? 10 : 1)); snd_play(snd_menumove); }
        break;
    case 10:
        if (step != 0 && ch > 0)
        {
            global.maxhp[ch] = max(1, global.maxhp[ch] + step * (big ? 100 : 10));
            global.hp[ch] = min(global.hp[ch], global.maxhp[ch]);
            snd_play(snd_menumove);
        }
        break;
    case 11:
        if (ok)
        {
            global.drcheat_page = 1;
            snd_play(snd_select);
        }
        break;
    case 12:
        if (ok)
        {
            global.drcheat_page = 2;
            snd_play(snd_select);
        }
        break;
    case 13:
        if (ok)
        {
            drc_close();
        }
        break;
}
if (drc_msgtime > 0)
    drc_msgtime--;
