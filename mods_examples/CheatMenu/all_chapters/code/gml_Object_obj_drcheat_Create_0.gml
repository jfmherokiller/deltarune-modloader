// CheatMenu: F7 opens a paused cheat overlay (overworld and battle).
// State lives in globals so it survives room changes (this object is re-spawned by obj_time).
//
// This object holds the page registry, page switching, shared helpers and the main page.
// Every other page is one global script, scr_drcheat_page_<name>(_mode), called with
// self = this instance (so its drc_* variables live here). Modes:
//   "init"    from Create: set up the page's state. Return { label, why } for its row on the main
//             page (why != "" shows the row as "-" and Z prints why instead of opening it), or
//             undefined to leave the page out of this chapter.
//   "tick"    every step while the menu is closed (always-on cheats).
//   "opened"  the step the menu opens, before the game's instances are deactivated.
//   "step"    every step while the page is shown. Input: drc_step (-1/0/1, </> with key repeat),
//             drc_big (Shift held), drc_ok (Z/Enter pressed).
//   "draw"    Draw GUI while the page is shown. Box: drc_bx, drc_by, drc_bw, drc_bh. The page draws
//             its title at (drc_bx + 50, drc_by + 12), its rows from drc_by + 48 and its footer at
//             drc_by + drc_bh - 34. drc_list_top / drc_draw_more scroll long lists.
//             The init struct may add bw: a wider box for this page (default 420).
//   "leave"   leaving the page (X back to the main page, or closing the menu).
// While a page sets drc_page_busy = 1 (e.g. a prompt is open), X does not leave the page: the page
// reads X itself (to cancel the prompt) and clears drc_page_busy. F7 still closes the menu ("leave").
// A new page = one new script + one drc_page_add line below.
depth = -99999;
if (!variable_global_exists("drcheat_open"))
{
    global.drcheat_sel = 0;
    global.drcheat_slot = 0;
    global.drcheat_lockhp = 0;
    global.drcheat_nodmg = 0;
    global.drcheat_inftp = 0;
}
// glow-tile puzzle timers (Ch1/3 obj_glowtilepuzz, Ch5 obj_glowtile_puzzle_new): 0 off, 1 slow x3, 2 frozen
if (!variable_global_exists("drcheat_puzzlet"))
    global.drcheat_puzzlet = 0;
drc_ptick = 0;
global.drcheat_open = 0;   // a fresh instance never starts inside an open menu
global.drcheat_page = 0;   // 0 main page, else an index into drc_pages
drc_shot = -1;
drc_hold = 0;
drc_msg = "";
drc_msgtime = 0;
drc_names = ["?", "Kris", "Susie", "Ralsei", "Noelle"];
// input for the page scripts (set by Step each step while the menu is open)
drc_step = 0;
drc_big = 0;
drc_ok = 0;
drc_page_busy = 0;
// menu box (set by Draw GUI each frame)
drc_bx = 0;
drc_by = 0;
drc_bw = 420;
drc_bh = 466;

// ---- walk speed (ROADMAP 28): applied every step by scr_drcheat_speed_apply
// (scales obj_mainchara.bwspeed, restores it at x1)
if (!variable_global_exists("drcheat_speed"))
    global.drcheat_speed = 0;   // index into drc_speed_mults; 0 = off (vanilla)
drc_speed_mults = [1, 1.5, 2, 3];
drc_speed_names = ["OFF", "x1.5", "x2", "x3"];

// ---- shared helpers
// first visible row of a scrolled list: keeps the cursor about in the middle
drc_list_top = function(_sel, _n, _vis)
{
    return clamp(_sel - ((_vis - 1) div 2), 0, max(0, _n - _vis));
};
// "more above / more below" markers at the right edge of a list (_y0 first row, _y1 last row)
drc_draw_more = function(_y0, _y1, _up, _down)
{
    var _x = drc_bx + drc_bw - 34;
    draw_set_color(c_gray);
    if (_up)
        draw_triangle(_x, _y0 + 16, _x + 12, _y0 + 16, _x + 6, _y0 + 6, false);
    if (_down)
        draw_triangle(_x, _y1 + 6, _x + 12, _y1 + 6, _x + 6, _y1 + 16, false);
};
// the open menu deactivates every instance, so remember whether a battle was running when it opened
drc_battle_open = 0;
drc_in_battle = function()
{
    if (!variable_global_exists("monster") || !variable_global_exists("mercymod"))
        return 0;
    return global.drcheat_open ? drc_battle_open : instance_exists(obj_battlecontroller);
};
drc_frozen = [];
drc_page_call = function(_mode)
{
    if (global.drcheat_page > 0 && global.drcheat_page < array_length(drc_pages))
        script_execute(drc_pages[global.drcheat_page].scr, _mode);
};
drc_close = function()
{
    drc_page_call("leave");
    global.drcheat_page = 0;
    drc_page_busy = 0;
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

// ---- page registry. drc_page_add(script, top): top = 1 puts the page's row at the top of the
// main page, 0 at the bottom (above Close). Rows keep the order of these lines.
drc_pages = [undefined];   // index 0 = the main page
drc_page_add = function(_scr, _top)
{
    var _info = script_execute(_scr, "init");
    if (is_undefined(_info))
        return;
    var _bw = variable_struct_exists(_info, "bw") ? _info.bw : 420;
    array_push(drc_pages, { scr: _scr, top: _top, label: _info.label, why: _info.why, bw: _bw });
};
drc_page_add(scr_drcheat_page_money, 1);
drc_page_add(scr_drcheat_page_lightworld, 0);   // right below the Dark World stat rows
drc_page_add(scr_drcheat_page_inventory, 0);
drc_page_add(scr_drcheat_page_enemies, 0);
drc_page_add(scr_drcheat_page_recruits, 0);
drc_page_add(scr_drcheat_page_tv, 0);   // Chapter 3 only
drc_page_add(scr_drcheat_page_flags, 0);

// ---- main page rows: { label, key }, key = a built-in row (Step/Draw switch on it) or "page"
// (then page = its index in drc_pages)
drc_rows = [];
for (var _p = 1; _p < array_length(drc_pages); _p++)
    if (drc_pages[_p].top)
        array_push(drc_rows, { label: drc_pages[_p].label, key: "page", page: _p });
var _main = [
    ["Heal party", "heal"], ["Fill TP", "filltp"], ["Lock HP", "lockhp"], ["No damage", "nodmg"],
    ["Infinite TP", "inftp"], ["Puzzle timer", "puzzle"], ["Walk speed", "speed"],
    ["Character", "char"], ["AT", "at"], ["DF", "df"], ["MAG", "mag"], ["Max HP", "maxhp"]
];
for (var _i = 0; _i < array_length(_main); _i++)
    array_push(drc_rows, { label: _main[_i][0], key: _main[_i][1], page: 0 });
for (var _p = 1; _p < array_length(drc_pages); _p++)
    if (!drc_pages[_p].top)
        array_push(drc_rows, { label: drc_pages[_p].label, key: "page", page: _p });
array_push(drc_rows, { label: "Close", key: "close", page: 0 });
global.drcheat_sel = clamp(global.drcheat_sel, 0, array_length(drc_rows) - 1);
