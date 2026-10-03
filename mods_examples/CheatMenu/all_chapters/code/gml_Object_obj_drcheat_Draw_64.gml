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
var bh = 466;
var bx = cx + (cw - bw) / 2;
var by = cy + (chh - bh) / 2;
draw_set_color(c_black);
draw_rectangle(bx, by, bx + bw, by + bh, false);
draw_set_color(c_white);
for (var t = 0; t < 3; t++)
    draw_rectangle(bx + t, by + t, bx + bw - t, by + bh - t, true);

scr_84_set_draw_font("main");
draw_sprite(spr_drcheat_icon, 0, bx + 28, by + 26);

if (global.drcheat_page == 1)
{
    draw_text(bx + 50, by + 12, "INVENTORY");
    var cat = clamp(global.drcheat_invcat, 0, 3);
    var ns = drc_inv_slots(cat);
    var r = global.drcheat_invsel;
    var rx = bx + 50;
    var ry = by + 48;
    var rh = 26;
    draw_set_color((r == -1) ? c_yellow : c_white);
    draw_text(rx, ry, "< " + drc_inv_cats[cat] + " >");
    if (r == -1)
        draw_sprite(spr_heart, 0, rx - 26, ry + 8);
    var vis = 12;
    var top = clamp(r - 5, 0, max(0, ns - vis));
    for (var i = 0; i < vis && top + i < ns; i++)
    {
        var si = top + i;
        var y0 = ry + 34 + i * rh;
        var idv = drc_inv_ready ? drc_inv_get(cat, si) : 0;
        draw_set_color((si == r) ? c_yellow : ((idv == 0) ? c_gray : c_white));
        draw_text(rx, y0, string(si + 1));
        draw_text(rx + 40, y0, drc_inv_ready ? drc_inv_name(cat, idv) : "...");
        if (si == r)
            draw_sprite(spr_heart, 0, rx - 26, y0 + 8);
    }
    draw_set_color(c_gray);
    if (top > 0)
        draw_text(bx + bw - 40, ry + 34, "^");
    if (top + vis < ns)
        draw_text(bx + bw - 40, ry + 34 + (vis - 1) * rh, "v");
    if (variable_global_exists("darkzone") && !global.darkzone)
        draw_text(bx + 20, by + bh - 60, "(Light World items aren't listed)");
    draw_text(bx + 20, by + bh - 34, "</> item  SHIFT x5  DEL clear  X back");
    draw_set_color(c_white);
    exit;
}

if (global.drcheat_page == 2)
{
    draw_text(bx + 50, by + 12, "ENEMIES");
    var rows = drc_en_rows();
    var r = global.drcheat_ensel;
    var rx = bx + 50;
    var vx = bx + 250;
    var ry = by + 48;
    var rh = 26;
    var fight = drc_in_battle();
    var yy = ry;
    for (var i = 0; i < array_length(rows); i++)
    {
        var kind = rows[i][0];
        var e = rows[i][1];
        if (kind == 10)
        {
            // enemy header line before its HP row
            yy += 6;
            draw_set_color(c_aqua);
            draw_text(rx - 10, yy, drc_en_name(e) + (drc_en_nospare(e) ? "  (can't be spared)" : ""));
            yy += rh;
        }
        var lab = "";
        var v = "";
        switch (kind)
        {
            case 0: lab = "Auto mercy"; v = global.drcheat_automercy ? "ON" : "OFF"; break;
            case 1: lab = "All: can spare"; break;
            case 2: lab = "All: HP 1"; break;
            case 10: lab = "HP"; v = "< " + string(global.monsterhp[e]) + " > / " + string(global.monstermaxhp[e]); break;
            case 11: lab = "Mercy"; v = "< " + string(global.mercymod[e]) + "% >"; break;
            case 12: lab = "Tired"; v = (global.monsterstatus[e] == 1) ? "YES" : "NO"; break;
        }
        var off = (kind == 1 || kind == 2) && !fight;
        draw_set_color((i == r) ? c_yellow : (off ? c_gray : c_white));
        draw_text(rx + ((kind >= 10) ? 16 : 0), yy, lab);
        draw_text(vx, yy, v);
        if (i == r)
            draw_sprite(spr_heart, 0, rx - 26, yy + 8);
        yy += rh;
    }
    draw_set_color(c_gray);
    if (!fight)
        draw_text(rx, yy + 10, "(not in a battle)");
    draw_set_color(c_gray);
    draw_text(bx + 20, by + bh - 34, "Z/</> change  SHIFT x10  X back");
    if (drc_msgtime > 0)
    {
        draw_set_color(c_lime);
        draw_text(vx, by + 12, drc_msg);
    }
    draw_set_color(c_white);
    exit;
}

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
        case 11: v = ">"; break;
        case 12: v = ">"; break;
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
