if (!global.drcheat_open)
    exit;
// Draw GUI: fixed screen space, independent of the room's camera/view size.
var cx = 0;
var cy = 0;
var cw = display_get_gui_width();
var chh = display_get_gui_height();

// frozen game frame behind the menu
if (sprite_exists(drc_shot))
    draw_sprite_stretched(drc_shot, 0, cx, cy, cw, chh);
draw_set_alpha(0.55);
draw_set_color(c_black);
draw_rectangle(cx, cy, cx + cw, cy + chh, false);
draw_set_alpha(1);

// box: tall enough for the main rows, but never taller than the screen (the main page scrolls
// when its rows don't fit; with a 480-pixel GUI that is 14 rows)
var rh = 26;
var bw = 420;
if (global.drcheat_page > 0 && global.drcheat_page < array_length(drc_pages))
    bw = min(drc_pages[global.drcheat_page].bw, cw - 16);   // a page may ask for a wider box
var bh = min(466 + (array_length(drc_rows) - 14) * rh, chh - 16);
var bx = cx + (cw - bw) / 2;
var by = cy + (chh - bh) / 2;
drc_bx = bx;
drc_by = by;
drc_bw = bw;
drc_bh = bh;
draw_set_color(c_black);
draw_rectangle(bx, by, bx + bw, by + bh, false);
draw_set_color(c_white);
for (var t = 0; t < 3; t++)
    draw_rectangle(bx + t, by + t, bx + bw - t, by + bh - t, true);

scr_84_set_draw_font("main");
draw_sprite(spr_drcheat_icon, 0, bx + 28, by + 26);

// ---- sub page: its own script draws title, rows and footer
if (global.drcheat_page != 0)
{
    drc_page_call("draw");
    exit;
}

// ---- main page
draw_text(bx + 50, by + 12, "CHEATS");

var slot = clamp(global.drcheat_slot, 0, 2);
if (global.char[slot] <= 0) slot = 0;
var ch = global.char[slot];
var nm = (ch >= 0 && ch < array_length(drc_names)) ? drc_names[ch] : ("#" + string(ch));
var hastp = variable_global_exists("tension") && variable_global_exists("maxtension");

var rx = bx + 50;
var vx = bx + 250;
var ry = by + 48;
var n = array_length(drc_rows);
var vis = min(n, floor((bh - 88) / rh));
var top = drc_list_top(global.drcheat_sel, n, vis);
for (var j = 0; j < vis; j++)
{
    var i = top + j;
    var row = drc_rows[i];
    var y0 = ry + j * rh;
    var sel = (i == global.drcheat_sel);
    draw_set_color(sel ? c_yellow : c_white);
    if ((row.key == "filltp" || row.key == "inftp") && !hastp)
        draw_set_color(c_gray);
    draw_text(rx, y0, row.label);
    var v = "";
    switch (row.key)
    {
        case "page": v = (drc_pages[row.page].why == "") ? ">" : "-"; break;
        case "lockhp": v = global.drcheat_lockhp ? "ON" : "OFF"; break;
        case "nodmg": v = global.drcheat_nodmg ? "ON" : "OFF"; break;
        case "inftp": v = global.drcheat_inftp ? "ON" : "OFF"; break;
        case "puzzle": v = (global.drcheat_puzzlet == 2) ? "FROZEN" : ((global.drcheat_puzzlet == 1) ? "SLOW x3" : "OFF"); break;
        case "speed": v = drc_speed_names[clamp(global.drcheat_speed, 0, array_length(drc_speed_names) - 1)]; break;
        case "char": v = "< " + nm + " >"; break;
        case "at": v = (ch > 0) ? ("< " + string(global.at[ch]) + " >") : "-"; break;
        case "df": v = (ch > 0) ? ("< " + string(global.df[ch]) + " >") : "-"; break;
        case "mag": v = (ch > 0) ? ("< " + string(global.mag[ch]) + " >") : "-"; break;
        case "maxhp": v = (ch > 0) ? ("< " + string(global.maxhp[ch]) + " >  HP " + string(global.hp[ch])) : "-"; break;
    }
    draw_text(vx, y0, v);
    if (sel)
        draw_sprite(spr_heart, 0, rx - 26, y0 + 8);
}
drc_draw_more(ry, ry + (vis - 1) * rh, top > 0, top + vis < n);
draw_set_color(c_gray);
draw_text(bx + 20, by + bh - 34, "Z select  </> change  SHIFT x10  X/F7 close");
if (drc_msgtime > 0)
{
    draw_set_color(c_lime);
    draw_text(vx, by + 12, drc_msg);
}
draw_set_color(c_white);
