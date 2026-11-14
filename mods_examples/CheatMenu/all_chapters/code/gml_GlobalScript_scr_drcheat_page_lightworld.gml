// CheatMenu page: Light World stats (ROADMAP 29). Kris's Light World HP / Max HP / AT / DF / LV / EXP.
// Page interface: see obj_drcheat Create.
//
// What the game does with these (decompiled Ch1-5, see RESEARCH.md "Light World stats"):
// - All six are plain storage: set by scr_gamestart, saved/loaded by scr_saveprocess / scr_load, and
//   shown by the Light World menu (obj_overworldc_Draw_0: HP x/y, LV, AT x (weapon), DF x (armor),
//   EXP, NEXT). Nothing levels up: no code anywhere writes llv/lxp, so changing LV or EXP needs no
//   derived stat recalculated. NEXT is computed from llv/lxp by a fixed table (LV 1..19; LV >= 20
//   shows 0), so LV is clamped to 1..20 here.
// - lat / ldf are only displayed (there are no Light World battles).
// - Dark World <-> Light World: scr_become_dark (Ch2/4/5) sets Kris's Dark World HP to
//   lhp / lmaxhp * maxhp[1], so lmaxhp must never be 0 (division) and is clamped to >= 1.
//   scr_become_light (all chapters) recomputes lhp from Kris's Dark World HP, so an HP edit made in
//   the Dark World is replaced when Kris returns to the Light World. The page says so.
// - lhp is clamped to 1..lmaxhp, like the main page's party HP (no overheal there either);
//   lowering Max HP lowers HP with it.
function scr_drcheat_page_lightworld(_mode)
{
    switch (_mode)
    {
        case "init":
            if (!variable_global_exists("drcheat_lwsel"))
                global.drcheat_lwsel = 0;
            // name    row label   store  global variable name   lo/hi  clamp range   step/big  </> and Shift steps
            // hi = -1: the upper bound is global.lmaxhp (the HP row)
            drc_lw = [
                { name: "HP",     store: "lhp",    lo: 1, hi: -1,    step: 1,  big: 10 },
                { name: "Max HP", store: "lmaxhp", lo: 1, hi: 999,   step: 1,  big: 10 },
                { name: "AT",     store: "lat",    lo: 0, hi: 999,   step: 1,  big: 10 },
                { name: "DF",     store: "ldf",    lo: 0, hi: 999,   step: 1,  big: 10 },
                { name: "LV",     store: "llv",    lo: 1, hi: 20,    step: 1,  big: 5 },
                { name: "EXP",    store: "lxp",    lo: 0, hi: 99999, step: 10, big: 1000 }
            ];
            drc_lw_get = function(_i)
            {
                var _s = drc_lw[_i].store;
                if (!variable_global_exists(_s))
                    return 0;
                var _v = variable_global_get(_s);
                return is_real(_v) ? _v : 0;
            };
            drc_lw_set = function(_i, _v)
            {
                var _e = drc_lw[_i];
                var _hi = (_e.hi < 0) ? max(_e.lo, drc_lw_get(1)) : _e.hi;
                // never lower a value the game already put above hi just by pressing right at the cap
                var _old = drc_lw_get(_i);
                _v = max(_e.lo, round(_v));
                if (_v > _hi)
                    _v = max(_hi, min(_v, _old));
                variable_global_set(_e.store, _v);
                // Max HP went below HP: HP follows (no overheal, same as the main page's Max HP row)
                if (_e.store == "lmaxhp" && drc_lw_get(0) > _v)
                    global.lhp = _v;
            };
            // every chapter's scr_gamestart sets all six; a save from an odd build might lack one
            var _ok = true;
            for (var _i = 0; _i < array_length(drc_lw); _i++)
                if (!variable_global_exists(drc_lw[_i].store))
                    _ok = false;
            return { label: "Light World...", why: _ok ? "" : "No Light World stats" };

        case "step":
            var nr = array_length(drc_lw);
            var r = clamp(global.drcheat_lwsel, 0, nr - 1);
            if (keyboard_check_pressed(vk_up)) { r = (r + nr - 1) mod nr; snd_play(snd_menumove); }
            if (keyboard_check_pressed(vk_down)) { r = (r + 1) mod nr; snd_play(snd_menumove); }
            if (drc_step != 0)
            {
                drc_lw_set(r, drc_lw_get(r) + drc_step * (drc_big ? drc_lw[r].big : drc_lw[r].step));
                snd_play(snd_menumove);
            }
            global.drcheat_lwsel = r;
            break;

        case "draw":
            var bx = drc_bx;
            var by = drc_by;
            var bh = drc_bh;
            draw_text(bx + 50, by + 12, "LIGHT WORLD");
            var rx = bx + 50;
            var vx = bx + 250;
            var ry = by + 48;
            var rh = 26;
            var nr = array_length(drc_lw);
            var r = clamp(global.drcheat_lwsel, 0, nr - 1);
            for (var i = 0; i < nr; i++)
            {
                var y0 = ry + i * rh;
                draw_set_color((i == r) ? c_yellow : c_white);
                draw_text(rx, y0, drc_lw[i].name);
                var v = "< " + string(drc_lw_get(i)) + " >";
                if (i == 0)
                    v += " / " + string(drc_lw_get(1));
                draw_text(vx, y0, v);
                if (i == r)
                    draw_sprite(spr_heart, 0, rx - 26, y0 + 8);
            }
            draw_set_color(c_gray);
            var ty = ry + nr * rh + 10;
            if (drc_lw[r].store == "llv" || drc_lw[r].store == "lxp")
            {
                draw_text(rx - 20, ty, "LV and EXP are separate: EXP never");
                draw_text(rx - 20, ty + 24, "levels you up (no Light World battles).");
                ty += 58;
            }
            if (drc_lw[r].store == "lat" || drc_lw[r].store == "ldf")
            {
                draw_text(rx - 20, ty, "Shown in the Light World menu only.");
                ty += 34;
            }
            // Dark World: lhp is replaced by scr_become_light on the way back
            if (variable_global_exists("darkzone") && global.darkzone)
            {
                draw_text(rx - 20, ty, "Dark World: HP is recalculated from");
                draw_text(rx - 20, ty + 24, "Kris's HP when you return.");
            }
            var _st = drc_lw[r];
            draw_text(bx + 20, by + bh - 34, "</> +/-" + string(_st.step) + "  SHIFT +/-" + string(_st.big) + "  X back");
            draw_set_color(c_white);
            break;
    }
    return undefined;
}
