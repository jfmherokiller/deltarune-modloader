// CheatMenu page: Inventory (Dark World items, weapons, armor, key items).
// Page interface: see obj_drcheat Create.
function scr_drcheat_page_inventory(_mode)
{
    switch (_mode)
    {
        case "init":
            if (!variable_global_exists("drcheat_invcat"))
            {
                global.drcheat_invcat = 0;
                global.drcheat_invsel = -1;
            }
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
            return { label: "Inventory...", why: "" };

        case "leave":
            drc_inv_compact();
            break;

        case "step":
            if (!drc_inv_ready)
                drc_inv_build();
            var cat = clamp(global.drcheat_invcat, 0, 3);
            var ns = drc_inv_slots(cat);
            var r = clamp(global.drcheat_invsel, -1, ns - 1);
            var step = drc_step;
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
                pos = (pos + cnt + step * (drc_big ? 5 : 1)) mod cnt;
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
            break;

        case "draw":
            var bx = drc_bx;
            var by = drc_by;
            var bh = drc_bh;
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
            var top = drc_list_top(r, ns, vis);
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
            drc_draw_more(ry + 34, ry + 34 + (vis - 1) * rh, top > 0, top + vis < ns);
            draw_set_color(c_gray);
            if (variable_global_exists("darkzone") && !global.darkzone)
                draw_text(bx + 20, by + bh - 60, "(Light World items aren't listed)");
            draw_text(bx + 20, by + bh - 34, "</> item  SHIFT x5  DEL clear  X back");
            draw_set_color(c_white);
            break;
    }
    return undefined;
}
