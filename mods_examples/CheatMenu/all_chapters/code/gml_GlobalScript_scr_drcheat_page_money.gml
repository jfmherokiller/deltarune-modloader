// CheatMenu page: Money. One row per currency of the current chapter (drc_money table).
// Page interface: see obj_drcheat Create.
function scr_drcheat_page_money(_mode)
{
    switch (_mode)
    {
        case "init":
            if (!variable_global_exists("drcheat_monsel"))
                global.drcheat_monsel = 0;
            // One table entry per currency, so a new currency is one more line.
            // name     row label             ch0/ch1  first/last chapter the row is shown in (hidden elsewhere)
            // store    a string = global variable name, a number = global.flag index
            // lo/hi    clamp range for writes   step/big  </> step, and the step with Shift held
            // note     help line under the rows
            //
            // FLAG 1412 RESTRICTION (Chapter 5): global.flag[1412] is the Flowery Dollar reveal state
            // (0 none taken, 1 shown as D$ = gold + F$ by obj_darkcontroller_Draw_0, 2 shown on its own F$
            // line; obj_dw_garden_enemyrush_Other_14 drives it). It is story state, not money: changing it
            // desyncs the F$ reveal scene and the money box. It must NEVER be written. It is not a table
            // entry, rows pointing at it are dropped below, and drc_money_set refuses it. The balances
            // themselves are F$ = flag[1411] and P$ = flag[1312], which are safe to edit.
            drc_money_noflag = 1412;
            drc_money = [
                { name: "D$",            ch0: 1, ch1: 5, store: "gold",  lo: 0, hi: 99999, step: 10, big: 1000, note: "Dark World shops" },
                { name: "Light World $", ch0: 1, ch1: 5, store: "lgold", lo: 0, hi: 99999, step: 10, big: 1000, note: "Light World shops" },
                // same 0..9999 range as the TV Show page's PT row
                { name: "PT",            ch0: 3, ch1: 4, store: 1044,    lo: 0, hi: 9999,  step: 10, big: 100,  note: "TV points (Ch4: Gerson's table)" },
                { name: "Board points",  ch0: 3, ch1: 3, store: 1117,    lo: 0, hi: 99999, step: 10, big: 100,  note: "Score of the current board" },
                { name: "F$",            ch0: 5, ch1: 5, store: 1411,    lo: 0, hi: 99999, step: 10, big: 1000, note: "Flowery Dollars" },
                { name: "P$",            ch0: 5, ch1: 5, store: 1312,    lo: 0, hi: 99999, step: 10, big: 1000, note: "Pink Dollars" }
            ];
            // table indices valid in this chapter (the others are hidden)
            drc_money_rows = [];
            for (var _m = 0; _m < array_length(drc_money); _m++)
            {
                var _e = drc_money[_m];
                var _s = _e.store;
                if (global.chapter < _e.ch0 || global.chapter > _e.ch1)
                    continue;
                if (is_string(_s) ? !variable_global_exists(_s) : (_s == drc_money_noflag || _s >= array_length(global.flag)))
                    continue;
                array_push(drc_money_rows, _m);
            }
            drc_money_get = function(_m)
            {
                var _s = drc_money[_m].store;
                var _v = is_string(_s) ? variable_global_get(_s) : global.flag[_s];
                return is_real(_v) ? _v : 0;
            };
            drc_money_set = function(_m, _v)
            {
                var _e = drc_money[_m];
                var _s = _e.store;
                // clamp to lo..hi, but never lower a value the game already put above hi (Ch5 saves can hold
                // more than 99999 D$), so pressing right at the cap does not take money away
                var _old = drc_money_get(_m);
                _v = max(_e.lo, round(_v));
                if (_v > _e.hi)
                    _v = max(_e.hi, min(_v, _old));
                if (is_string(_s))
                    variable_global_set(_s, _v);
                else if (_s != drc_money_noflag)   // flag 1412 restriction (see above): never written
                    global.flag[_s] = _v;
            };
            return { label: "Money...", why: "" };

        case "step":
            var nr = array_length(drc_money_rows);
            var r = clamp(global.drcheat_monsel, 0, max(0, nr - 1));
            if (nr > 0)
            {
                if (keyboard_check_pressed(vk_up)) { r = (r + nr - 1) mod nr; snd_play(snd_menumove); }
                if (keyboard_check_pressed(vk_down)) { r = (r + 1) mod nr; snd_play(snd_menumove); }
                if (drc_step != 0)
                {
                    var _m = drc_money_rows[r];
                    drc_money_set(_m, drc_money_get(_m) + drc_step * (drc_big ? drc_money[_m].big : drc_money[_m].step));
                    snd_play(snd_menumove);
                }
            }
            global.drcheat_monsel = r;
            break;

        case "draw":
            var bx = drc_bx;
            var by = drc_by;
            var bh = drc_bh;
            draw_text(bx + 50, by + 12, "MONEY");
            var rx = bx + 50;
            var vx = bx + 250;
            var ry = by + 48;
            var rh = 26;
            var nr = array_length(drc_money_rows);
            var r = clamp(global.drcheat_monsel, 0, max(0, nr - 1));
            for (var i = 0; i < nr; i++)
            {
                var _m = drc_money_rows[i];
                var y0 = ry + i * rh;
                draw_set_color((i == r) ? c_yellow : c_white);
                draw_text(rx, y0, drc_money[_m].name);
                draw_text(vx, y0, "< " + string(drc_money_get(_m)) + " >");
                if (i == r)
                    draw_sprite(spr_heart, 0, rx - 26, y0 + 8);
            }
            draw_set_color(c_gray);
            var ty = ry + nr * rh + 10;
            if (nr > 0)
            {
                draw_text(rx - 20, ty, drc_money[drc_money_rows[r]].note);
                ty += 34;
            }
            // Ch5 before the F$ reveal (flag 1412 == 1, read only): the money box adds F$ to the D$ figure
            if (global.chapter == 5 && array_length(global.flag) > drc_money_noflag && global.flag[drc_money_noflag] == 1)
            {
                draw_text(rx - 20, ty, "The money box shows D$ + F$ for now,");
                draw_text(rx - 20, ty + 24, "but D$ shops only take the D$ row.");
            }
            // footer: the selected row's own steps from the table
            var _st = (nr > 0) ? drc_money[drc_money_rows[r]] : { step: 10, big: 1000 };
            draw_text(bx + 20, by + bh - 34, "</> +/-" + string(_st.step) + "  SHIFT +/-" + string(_st.big) + "  X back");
            draw_set_color(c_white);
            break;
    }
    return undefined;
}
