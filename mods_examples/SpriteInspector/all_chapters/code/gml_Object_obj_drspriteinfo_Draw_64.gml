if (!global.drspr_on)
    exit;

// room -> GUI coordinates through the active camera
var cam = view_camera[0];
var vx = 0;
var vy = 0;
var vw = room_width;
var vh = room_height;
if (view_enabled && cam != -1)
{
    vx = camera_get_view_x(cam);
    vy = camera_get_view_y(cam);
    vw = camera_get_view_width(cam);
    vh = camera_get_view_height(cam);
}
var gw = display_get_gui_width();
var gh = display_get_gui_height();
var sx = gw / vw;
var sy = gh / vh;

var seen = ds_map_create();
var labels = [];

// instances
with (all)
{
    if (id == other.id || !visible || sprite_index < 0 || !sprite_exists(sprite_index) || image_alpha <= 0)
        continue;
    if (bbox_right < vx || bbox_left > vx + vw || bbox_bottom < vy || bbox_top > vy + vh)
        continue;
    var nm = sprite_get_name(sprite_index);
    array_push(labels, [(x - vx) * sx, (bbox_top - vy) * sy, nm + " [" + string(floor(image_index) mod max(1, sprite_get_number(sprite_index))) + "/" + string(sprite_get_number(sprite_index)) + "]", object_get_name(object_index)]);
    ds_map_set(seen, nm, 1);
}

// sprites placed directly on room asset layers (decorations that are not instances)
var lays = layer_get_all();
for (var i = 0; i < array_length(lays); i++)
{
    if (!layer_get_visible(lays[i]))
        continue;
    var els = layer_get_all_elements(lays[i]);
    for (var j = 0; j < array_length(els); j++)
    {
        if (layer_get_element_type(els[j]) != layerelementtype_sprite)
            continue;
        var spr = layer_sprite_get_sprite(els[j]);
        if (!sprite_exists(spr))
            continue;
        var lx = layer_sprite_get_x(els[j]);
        var ly = layer_sprite_get_y(els[j]);
        if (lx < vx - 64 || lx > vx + vw + 64 || ly < vy - 64 || ly > vy + vh + 64)
            continue;
        var nm2 = sprite_get_name(spr);
        array_push(labels, [(lx - vx) * sx, (ly - vy) * sy, nm2, "layer " + layer_get_name(lays[i])]);
        ds_map_set(seen, nm2, 1);
    }
}

// sorted unique list for the side panel / clipboard
var keys = ds_map_keys_to_array(seen);
ds_map_destroy(seen);
if (!is_array(keys))
    keys = [];
array_sort(keys, true);
drspr_names = keys;

scr_84_set_draw_font("main");
draw_set_halign(fa_left);
draw_set_valign(fa_top);
var s = 0.5;

// labels, outlined so they read on any background
for (var k = 0; k < array_length(labels) && k < 80; k++)
{
    var L = labels[k];
    var tx = clamp(L[0], 2, gw - 200);
    var ty = clamp(L[1] - 12, 2, gh - 12);
    draw_set_color(c_black);
    draw_text_transformed(tx - 1, ty, L[2], s, s, 0);
    draw_text_transformed(tx + 1, ty, L[2], s, s, 0);
    draw_text_transformed(tx, ty - 1, L[2], s, s, 0);
    draw_text_transformed(tx, ty + 1, L[2], s, s, 0);
    draw_set_color(c_yellow);
    draw_text_transformed(tx, ty, L[2], s, s, 0);
}

// panel
var px = 4;
var py = 4;
var ph = 14 + 10 * min(array_length(keys), 30) + 12;
draw_set_alpha(0.7);
draw_set_color(c_black);
draw_rectangle(px, py, px + 230, py + ph, false);
draw_set_alpha(1);
draw_set_color(c_aqua);
draw_text_transformed(px + 4, py + 2, "SPRITES  room: " + room_get_name(room), s, s, 0);
draw_set_color(c_white);
for (var n = 0; n < array_length(keys) && n < 30; n++)
    draw_text_transformed(px + 4, py + 14 + n * 10, keys[n], s, s, 0);
draw_set_color(c_gray);
draw_text_transformed(px + 4, py + ph - 11, "F9 hide  Shift+F9 copy list", s, s, 0);
if (drspr_msgtime > 0)
{
    draw_set_color(c_lime);
    draw_text_transformed(px + 240, py + 2, drspr_msg, s, s, 0);
}
draw_set_color(c_white);
