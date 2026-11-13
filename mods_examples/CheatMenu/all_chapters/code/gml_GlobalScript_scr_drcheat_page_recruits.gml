// CheatMenu page: Recruits (Chapters 2-5). Recruitment is stored as
// global.flag[600 + enemy ID] = recruited / required, -1 = lost.
// Page interface: see obj_drcheat Create.
function scr_drcheat_page_recruits(_mode)
{
    switch (_mode)
    {
        case "init":
            if (!variable_global_exists("drcheat_recsel")) global.drcheat_recsel = 0;
            // Use the same per-type IDs as the game's chapter recruit lists; do not include bosses.
            drc_rec_ids = [];
            if (global.chapter >= 2)
            {
                drc_rec_ids = [5, 6, 11, 13, 14, 15, 22, 23];
                var _ids = [30, 31, 32, 33, 34, 35, 36, 40, 42, 44];
                for (var _i = 0; _i < array_length(_ids); _i++)
                    array_push(drc_rec_ids, _ids[_i]);
                if (global.chapter >= 3)
                {
                    var _ids = [54, 55, 56, 57, 58, 59, 60, 61];
                    for (var _i = 0; _i < array_length(_ids); _i++)
                        array_push(drc_rec_ids, _ids[_i]);
                }
                if (global.chapter >= 4)
                {
                    var _ids = [62, 63, 64, 65, 66, 67, 68, 69];
                    for (var _i = 0; _i < array_length(_ids); _i++)
                        array_push(drc_rec_ids, _ids[_i]);
                }
                if (global.chapter >= 5)
                {
                    var _ids = [70, 71, 72, 73, 74, 75, 76, 77];
                    for (var _i = 0; _i < array_length(_ids); _i++)
                        array_push(drc_rec_ids, _ids[_i]);
                }
            }
            drc_rec_count = array_length(drc_rec_ids);
            drc_rec_names = [];
            drc_rec_max = [];
            for (var _r = 0; _r < drc_rec_count; _r++)
            {
                var _info = scr_drcheat_recruit_info(drc_rec_ids[_r]);
                drc_rec_names[_r] = _info[0];
                drc_rec_max[_r] = max(1, _info[1]);
            }
            drc_rec_current = function(_r)
            {
                var _v = global.flag[600 + drc_rec_ids[_r]];
                if (_v < 0) return -1; // the game marks lost recruits with -1
                return clamp(round(_v * drc_rec_max[_r]), 0, drc_rec_max[_r]);
            };
            return { label: "Recruits...", why: (drc_rec_count > 0) ? "" : "Recruits begin in Chapter 2" };

        // left/right adjusts the count; C/Del resets a lost type to zero
        case "step":
            var nr = drc_rec_count;
            var r = clamp(global.drcheat_recsel, 0, max(0, nr - 1));
            if (nr > 0)
            {
                if (keyboard_check_pressed(vk_up)) { r = (r + nr - 1) mod nr; snd_play(snd_menumove); }
                if (keyboard_check_pressed(vk_down)) { r = (r + 1) mod nr; snd_play(snd_menumove); }
                var _cur = drc_rec_current(r);
                if (drc_step != 0)
                {
                    var _next = clamp(max(0, _cur) + drc_step * (drc_big ? 5 : 1), 0, drc_rec_max[r]);
                    global.flag[600 + drc_rec_ids[r]] = _next / drc_rec_max[r];
                    snd_play(snd_menumove);
                }
                if (keyboard_check_pressed(vk_delete) || keyboard_check_pressed(vk_backspace) || keyboard_check_pressed(ord("C")))
                {
                    global.flag[600 + drc_rec_ids[r]] = 0;
                    snd_play(snd_smallswing);
                }
            }
            global.drcheat_recsel = r;
            break;

        case "draw":
            var bx = drc_bx;
            var by = drc_by;
            var bh = drc_bh;
            draw_text(bx + 50, by + 12, "RECRUITS");
            var rx = bx + 50;
            var ry = by + 48;
            var rh = 26;
            var vis = 12;
            var top = drc_list_top(global.drcheat_recsel, drc_rec_count, vis);
            for (var i = 0; i < vis && top + i < drc_rec_count; i++)
            {
                var r = top + i;
                var y0 = ry + i * rh;
                var val = drc_rec_current(r);
                draw_set_color(r == global.drcheat_recsel ? c_yellow : (val < 0 ? c_gray : c_white));
                draw_text(rx, y0, drc_rec_names[r]);
                draw_text(bx + 290, y0, val < 0 ? "LOST" : string(val) + "/" + string(drc_rec_max[r]));
                if (r == global.drcheat_recsel)
                    draw_sprite(spr_heart, 0, rx - 26, y0 + 8);
            }
            drc_draw_more(ry, ry + (vis - 1) * rh, top > 0, top + vis < drc_rec_count);
            draw_set_color(c_gray);
            draw_text(bx + 20, by + bh - 60, "LOST: this recruit was defeated.");
            draw_text(bx + 20, by + bh - 34, "</> +/-1  SHIFT x5  C reset  X back");
            draw_set_color(c_white);
            break;
    }
    return undefined;
}
