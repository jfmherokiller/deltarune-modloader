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

// box
var bw = 420;
var bh = 410;
var bx = cx + (cw - bw) / 2;
var by = cy + (chh - bh) / 2;
draw_set_color(c_black);
draw_rectangle(bx, by, bx + bw, by + bh, false);
draw_set_color(c_white);
for (var t = 0; t < 3; t++)
    draw_rectangle(bx + t, by + t, bx + bw - t, by + bh - t, true);

scr_84_set_draw_font("main");
draw_sprite(spr_drcheat_icon, 0, bx + 28, by + 26);
draw_text(bx + 50, by + 12, "CHEATS");

var slot = clamp(global.drcheat_slot, 0, 2);
if (global.char[slot] <= 0) slot = 0;
var ch = global.char[slot];
var nm = (ch >= 0 && ch < array_length(drc_names)) ? drc_names[ch] : ("#" + string(ch));
var hastp = variable_global_exists("tension") && variable_global_exists("maxtension");

var rx = bx + 50;
var vx = bx + 250;
var ry = by + 48;
var rh = 26;
for (var i = 0; i < array_length(drc_rows); i++)
{
    var y0 = ry + i * rh;
    var sel = (i == global.drcheat_sel);
    draw_set_color(sel ? c_yellow : c_white);
    if ((i == 2 || i == 5) && !hastp)
        draw_set_color(c_gray);
    draw_text(rx, y0, drc_rows[i]);
    var v = "";
    switch (i)
    {
        case 0: v = "< " + string(global.gold) + " >"; break;
        case 3: v = global.drcheat_lockhp ? "ON" : "OFF"; break;
        case 4: v = global.drcheat_nodmg ? "ON" : "OFF"; break;
        case 5: v = global.drcheat_inftp ? "ON" : "OFF"; break;
        case 6: v = "< " + nm + " >"; break;
        case 7: v = (ch > 0) ? ("< " + string(global.at[ch]) + " >") : "-"; break;
        case 8: v = (ch > 0) ? ("< " + string(global.df[ch]) + " >") : "-"; break;
        case 9: v = (ch > 0) ? ("< " + string(global.mag[ch]) + " >") : "-"; break;
        case 10: v = (ch > 0) ? ("< " + string(global.maxhp[ch]) + " >  HP " + string(global.hp[ch])) : "-"; break;
    }
    draw_text(vx, y0, v);
    if (sel)
        draw_sprite(spr_heart, 0, rx - 26, y0 + 8);
}
draw_set_color(c_gray);
draw_text(bx + 20, by + bh - 34, "Z select  </> change  SHIFT x10  X/F7 close");
if (drc_msgtime > 0)
{
    draw_set_color(c_lime);
    draw_text(vx, by + 12, drc_msg);
}
draw_set_color(c_white);
