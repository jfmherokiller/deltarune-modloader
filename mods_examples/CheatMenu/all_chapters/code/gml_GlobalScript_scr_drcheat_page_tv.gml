// CheatMenu page: TV Show (Chapter 3 only): points, saved board ranks, minigame assist / top score.
// Page interface: see obj_drcheat Create.
function scr_drcheat_page_tv(_mode)
{
    switch (_mode)
    {
        case "init":
            if (global.chapter != 3)
                return undefined;
            global.drcheat_tvsel = 0;
            // flag 1044 = points (PTs), 1173/1174 = saved rank of board 1/2 (0 Z, 1 C, 2 B, 3 A, 4 S, 5 T).
            // The ranks unlock the ranking-room doors (A>=3, B>=2, C>=1) and the T-rank room (5).
            drc_tv_letters = ["Z", "C", "B", "A", "S", "T"];
            drc_tv_rows = ["Points", "Board 1 rank", "Board 2 rank", "Minigame assist", "Minigame: top score"];
            if (!variable_global_exists("drcheat_mgassist"))
                global.drcheat_mgassist = 0;
            global.drcheat_mgmax = 0;   // pending "top score"; applied after closing (minigame objects are frozen while open)
            return { label: "TV Show...", why: "" };

        // Ch3 TV minigames: cooking show (obj_chefs_game), rhythm game (obj_rhythmgame), Susiezilla.
        // assist keeps you from losing; top score sets each game's own score past its best grade
        // (cooking: 640 = T, and >= the 400 goal ends the show as a win; rhythm: 28000 = the game's
        // own debug top score; Susiezilla: 800+ = ultimate victory on the next defeated enemy).
        case "tick":
            if (global.drcheat_mgassist || global.drcheat_mgmax)
            {
                var _top = global.drcheat_mgmax;
                global.drcheat_mgmax = 0;
                var _o = asset_get_index("obj_chefs_game");
                if (_o >= 0)
                    with (_o)
                    {
                        if (min_pts < 0) min_pts = 0;
                        if (_top && !gameover) scorepoints = max(scorepoints, 640);
                    }
                _o = asset_get_index("obj_rhythmgame");
                if (_o >= 0)
                    with (_o)
                    {
                        fame = max_fame;
                        if (_top) points = max(points, 28000);
                    }
                _o = asset_get_index("obj_susiezilla_statue");
                if (_o >= 0)
                    with (_o)
                        if (hp > 0) hp = maxhp;
                _o = asset_get_index("obj_susiezilla_singlescreen_hud_score");
                if (_o >= 0 && _top)
                    with (_o)
                        my_score = 999;
            }
            break;

        case "step":
            var nr = array_length(drc_tv_rows);
            var r = clamp(global.drcheat_tvsel, 0, nr - 1);
            var step = drc_step;
            if (keyboard_check_pressed(vk_up)) { r = (r + nr - 1) mod nr; snd_play(snd_menumove); }
            if (keyboard_check_pressed(vk_down)) { r = (r + 1) mod nr; snd_play(snd_menumove); }
            if (step != 0)
            {
                if (r == 0)
                    global.flag[1044] = clamp(global.flag[1044] + step * (drc_big ? 1000 : 100), 0, 9999);
                else if (r == 3)
                    global.drcheat_mgassist = !global.drcheat_mgassist;
                else if (r == 4)
                {
                }
                else
                {
                    var f = (r == 1) ? 1173 : 1174;
                    global.flag[f] = clamp(global.flag[f] + step, 0, 5);
                }
                snd_play(snd_menumove);
            }
            if (drc_ok && r == 3)
            {
                global.drcheat_mgassist = !global.drcheat_mgassist;
                snd_play(snd_select);
            }
            if (drc_ok && r == 4)
            {
                global.drcheat_mgmax = 1;
                drc_msg = "Applied on close"; drc_msgtime = 60; snd_play(snd_select);
            }
            global.drcheat_tvsel = r;
            break;

        case "draw":
            var bx = drc_bx;
            var by = drc_by;
            var bh = drc_bh;
            draw_text(bx + 50, by + 12, "TV SHOW");
            var rx = bx + 50;
            var vx = bx + 250;
            var ry = by + 48;
            var rh = 26;
            for (var i = 0; i < array_length(drc_tv_rows); i++)
            {
                var y0 = ry + i * rh;
                var v = "";
                if (i == 0)
                    v = "< " + string(global.flag[1044]) + " >";
                else if (i == 3)
                    v = global.drcheat_mgassist ? "ON" : "OFF";
                else if (i == 4)
                    v = global.drcheat_mgmax ? "on close" : "Z";
                else
                    v = "< " + drc_tv_letters[clamp(global.flag[(i == 1) ? 1173 : 1174], 0, 5)] + " >";
                draw_set_color((i == global.drcheat_tvsel) ? c_yellow : c_white);
                draw_text(rx, y0, drc_tv_rows[i]);
                draw_text(vx, y0, v);
                if (i == global.drcheat_tvsel)
                    draw_sprite(spr_heart, 0, rx - 26, y0 + 8);
            }
            draw_set_color(c_gray);
            var ty = ry + 6 * rh;
            draw_text(rx - 20, ty, "Ranks: saved board result; they open");
            draw_text(rx - 20, ty + 24, "the ranking room doors and T-Rank room.");
            draw_text(rx - 20, ty + 56, "Minigames: cooking show, rhythm game,");
            draw_text(rx - 20, ty + 80, "Susiezilla. Assist = can't lose.");
            draw_text(rx - 20, ty + 104, "Top score: open F7 during the game.");
            if (drc_msgtime > 0)
            {
                draw_set_color(c_lime);
                draw_text(vx, by + 12, drc_msg);
                draw_set_color(c_gray);
            }
            draw_text(bx + 20, by + bh - 34, "</> change  SHIFT x10  X back");
            draw_set_color(c_white);
            break;
    }
    return undefined;
}
