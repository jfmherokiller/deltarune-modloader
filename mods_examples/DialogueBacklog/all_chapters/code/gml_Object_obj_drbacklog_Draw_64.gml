// DialogueBacklog: draw the backlog newest-at-bottom over the frozen frame (GUI space).
if (!global.drbacklog_open)
    exit;
var gw = display_get_gui_width();
var gh = display_get_gui_height();
if (sprite_exists(drbl_shot))
    draw_sprite_stretched(drbl_shot, 0, 0, 0, gw, gh);
draw_set_alpha(0.8);
draw_set_color(c_black);
draw_rectangle(0, 0, gw, gh, false);
draw_set_alpha(1);

scr_84_set_draw_font("main");
draw_set_halign(fa_left);
draw_set_valign(fa_top);
var lh = string_height("M");
var x0 = 48;
var x1 = gw - 48;
var top = 44;
var bottom = gh - 40;
var tw = x1 - x0 - 24;
var E = global.drbacklog_entries;
var n = array_length(E);
var sc = clamp(global.drbacklog_scroll, 0, max(0, n - 1));

draw_set_color(c_yellow);
draw_text(x0, 12, "BACKLOG");
draw_set_color(c_gray);
var cnt = (n == 0) ? "empty" : (string(n - sc) + " / " + string(n));
draw_text(x1 - string_width(cnt), 12, cnt);
draw_set_color(c_white);
draw_line_width(x0, top - 6, x1, top - 6, 2);
draw_line_width(x0, bottom + 4, x1, bottom + 4, 2);

if (n == 0)
{
    draw_set_color(c_gray);
    draw_text(x0, top + 8, "(nothing has been said yet)");
}

var i = n - 1 - sc;
var yb = bottom;
var first = true;
while (i >= 0)
{
    var e = E[i];
    var h;
    if (e.k == 1)
    {
        h = lh + 12;
        if (yb - h < top && !first)
            break;
        yb -= h;
        var lbl = "- " + e.t + " -";
        draw_set_color(c_dkgray);
        draw_line(x0, yb + h / 2, x1, yb + h / 2);
        var lw = string_width(lbl) + 16;
        var lx = x0 + ((x1 - x0) - lw) / 2;
        draw_set_color(c_black);
        draw_rectangle(lx, yb, lx + lw, yb + h - 4, false);
        draw_set_color(c_gray);
        draw_text(lx + 8, yb + 2, lbl);
    }
    else if (e.k == 2)
    {
        var th = string_height_ext(e.t, lh, tw - 24);
        h = th + 12;
        if (yb - h < top && !first)
            break;
        yb -= h;
        draw_sprite_ext(spr_heart, 0, x0 + 18, yb + lh / 2 + 2, 1, 1, 0, c_red, 1);
        draw_set_color(c_yellow);
        draw_text_ext(x0 + 34, yb + 2, e.t, lh, tw - 24);
    }
    else
    {
        var named = e.s != "";
        var nh = named ? (lh + 6) : 0;
        var th = string_height_ext(e.t, lh, tw);
        h = nh + th + 14;
        if (yb - h < top && !first)
            break;
        yb -= h;
        if (named)
        {
            // name plate, like the speaker tag in Ace Attorney's backlog
            var pw = string_width(e.s) + 20;
            var col = drbl_color(e.s);
            draw_set_color(col);
            draw_rectangle(x0, yb, x0 + pw, yb + lh + 2, false);
            draw_set_color(c_black);
            draw_text(x0 + 10, yb + 1, e.s);
        }
        draw_set_color(c_white);
        draw_text_ext(x0 + 12, yb + nh, e.t, lh, tw);
    }
    first = false;
    i--;
}

// scrollbar
if (n > 1)
{
    var bx = x1 + 12;
    draw_set_color(c_dkgray);
    draw_rectangle(bx, top, bx + 4, bottom, false);
    var frac = 1 - sc / (n - 1);
    var ky = top + (bottom - top - 24) * frac;
    draw_set_color(c_white);
    draw_rectangle(bx - 2, ky, bx + 6, ky + 24, false);
}

draw_set_color(c_gray);
var kn = (global.drbacklog_key >= vk_f1 && global.drbacklog_key <= vk_f12) ? ("F" + string(global.drbacklog_key - vk_f1 + 1)) : chr(global.drbacklog_key);
draw_text(x0, gh - 30, "UP/DOWN/WHEEL scroll   PGUP/PGDN   HOME/END   " + kn + "/X close");
draw_set_color(c_white);
