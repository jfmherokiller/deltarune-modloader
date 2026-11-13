// CheatMenu page: Flags (ROADMAP 31). Views and edits global.plot and global.flag[n].
// Page interface: see obj_drcheat Create.
//
// Names come from our list (data/flags.json -> chapterN_windows/.../scr_drcheat_flagnames.gml,
// generated; the game's own scr_flag_names table is empty in release builds).
//
// Row 0 is global.plot, the other rows are flags of the current filter (All / Named / Non-zero).
// Writing is always two steps: Z picks a new value (typed or </>), then a Yes/No prompt,
// "Set flag[N] (label) from X to Y?", with No preselected. Only Yes writes; X / No / F7 change
// nothing. Every index is checked against array_length(global.flag) (and the name list)
// before it is read or written, so a chapter with a shorter flag array never crashes.
function scr_drcheat_page_flags(_mode)
{
    switch (_mode)
    {
        case "init":
            if (!variable_global_exists("drcheat_flgidx"))
            {
                global.drcheat_flgidx = -1;    // selected row: -1 = global.plot, else a flag index
                global.drcheat_flgfilter = 0;  // 0 all, 1 named, 2 non-zero
            }
            // the name list builds a 300 KB array: build it once per chapter run, not per room
            if (!variable_global_exists("drcheat_flgnames"))
            {
                global.drcheat_flgnames = scr_drcheat_flagnames();
                global.drcheat_plotnames = scr_drcheat_plotnames();
            }
            drc_flg_filters = ["All", "Named", "Non-zero"];
            drc_flg_list = [];   // flag indices shown (ascending), for the current filter
            drc_flg_pos = 0;     // cursor: 0 = plot row, k = drc_flg_list[k - 1]
            drc_flg_dirty = 1;   // rebuild drc_flg_list before the next step / draw
            drc_flg_state = 0;   // 0 browse, 1 edit value, 2 confirm, 3 jump to index
            drc_flg_target = -1; // row being edited: -1 = plot, else a flag index
            drc_flg_old = 0;
            drc_flg_new = 0;
            drc_flg_buf = "";    // typed text (edit / jump)
            drc_flg_touched = 0; // edit: the value was typed or stepped since Z
            drc_flg_yes = 0;     // confirm cursor: 0 = No (default), 1 = Yes
            drc_flg_hold = 0;    // up/down key repeat
            drc_flg_vis = 10;    // list rows that fit (set by draw)

            // flag count of this chapter's array (0 when the game has none yet)
            drc_flg_count = function()
            {
                return (variable_global_exists("flag") && is_array(global.flag)) ? array_length(global.flag) : 0;
            };
            // our list entry [name, label, desc, value notes, conf] or undefined
            drc_flg_info = function(_i)
            {
                var _n = global.drcheat_flgnames;
                if (_i < 0 || _i >= array_length(_n) || !is_array(_n[_i]))
                    return undefined;
                return _n[_i];
            };
            drc_flg_label = function(_i)
            {
                var _e = drc_flg_info(_i);
                return is_undefined(_e) ? "unknown" : _e[1];
            };
            // current value (-1 target = plot); undefined when the index is out of range
            drc_flg_get = function(_i)
            {
                if (_i < 0)
                    return variable_global_exists("plot") ? global.plot : undefined;
                if (_i >= drc_flg_count())
                    return undefined;
                return global.flag[_i];
            };
            drc_flg_str = function(_v)
            {
                if (is_undefined(_v)) return "-";
                if (is_string(_v)) return "\"" + _v + "\"";
                if (is_numeric(_v)) return drc_flg_fmt(_v);
                return "(" + typeof(_v) + ")";
            };
            // last plot milestone at or below _v: [value, label, desc] or undefined
            drc_flg_plotinfo = function(_v)
            {
                var _p = global.drcheat_plotnames;
                var _best = undefined;
                if (!is_numeric(_v))
                    return _best;
                for (var _k = 0; _k < array_length(_p); _k++)
                {
                    if (_p[_k][0] > _v)
                        break;
                    _best = _p[_k];
                }
                return _best;
            };
            drc_flg_plotlabel = function(_v)
            {
                var _m = drc_flg_plotinfo(_v);
                if (is_undefined(_m))
                    return "unknown";
                return (_m[0] == _v) ? _m[1] : ("after " + _m[1]);
            };
            drc_flg_rowtarget = function(_pos)
            {
                if (_pos <= 0 || _pos > array_length(drc_flg_list))
                    return -1;
                return drc_flg_list[_pos - 1];
            };
            // row of flag _i in the list, or of the next listed flag after it
            drc_flg_find = function(_i)
            {
                if (_i < 0)
                    return 0;
                var _lo = 0;
                var _hi = array_length(drc_flg_list);
                while (_lo < _hi)
                {
                    var _mid = (_lo + _hi) div 2;
                    if (drc_flg_list[_mid] < _i) _lo = _mid + 1; else _hi = _mid;
                }
                return min(_lo, array_length(drc_flg_list) - 1) + 1;
            };
            drc_flg_rebuild = function()
            {
                var _n = drc_flg_count();
                var _f = clamp(global.drcheat_flgfilter, 0, 2);
                drc_flg_list = [];
                for (var _i = 0; _i < _n; _i++)
                {
                    if (_f == 1 && is_undefined(drc_flg_info(_i)))
                        continue;
                    if (_f == 2)
                    {
                        var _v = global.flag[_i];
                        if (is_numeric(_v) && _v == 0)
                            continue;
                    }
                    array_push(drc_flg_list, _i);
                }
                drc_flg_pos = drc_flg_find(global.drcheat_flgidx);
                global.drcheat_flgidx = drc_flg_rowtarget(drc_flg_pos);
                drc_flg_dirty = 0;
            };
            // typed text -> number, or undefined if it is not one ("-", "", "1.", "1.2.3")
            drc_flg_parse = function(_s)
            {
                var _len = string_length(_s);
                var _digits = 0;
                var _dot = 0;
                for (var _k = 1; _k <= _len; _k++)
                {
                    var _c = string_char_at(_s, _k);
                    if (_c == "-" && _k == 1) continue;
                    if (_c == "." && !_dot && _digits > 0 && _k < _len) { _dot = 1; continue; }
                    if (ord(_c) < ord("0") || ord(_c) > ord("9")) return undefined;
                    _digits++;
                }
                return (_digits > 0) ? real(_s) : undefined;
            };
            // keep only number characters of keyboard_string ("-" first, one ".")
            drc_flg_typed = function(_allow_neg)
            {
                var _s = keyboard_string;
                var _o = "";
                for (var _k = 1; _k <= string_length(_s); _k++)
                {
                    var _c = string_char_at(_s, _k);
                    if (ord(_c) >= ord("0") && ord(_c) <= ord("9"))
                        _o += _c;
                    else if (_c == "-" && _allow_neg && _o == "")
                        _o = "-";
                    else if (_c == "." && _allow_neg && string_pos(".", _o) == 0 && _o != "" && _o != "-")
                        _o += ".";
                }
                _o = string_copy(_o, 1, 12);
                keyboard_string = _o;
                return _o;
            };
            // number -> text: integers as is, fractions (recruit progress) with up to 4 decimals
            drc_flg_fmt = function(_v)
            {
                if (_v == floor(_v))
                    return string(_v);
                var _s = string_format(_v, 1, 4);
                while (string_char_at(_s, string_length(_s)) == "0")
                    _s = string_copy(_s, 1, string_length(_s) - 1);
                return _s;
            };
            drc_flg_title = function(_i)
            {
                return (_i < 0) ? "plot" : ("flag[" + string(_i) + "]");
            };
            // text helpers (font set by Draw GUI)
            drc_flg_fit = function(_s, _w)
            {
                if (string_width(_s) <= _w)
                    return _s;
                while (string_length(_s) > 1 && string_width(_s + "..") > _w)
                    _s = string_copy(_s, 1, string_length(_s) - 1);
                return _s + "..";
            };
            drc_flg_wrap = function(_s, _w, _max)
            {
                var _lines = [];
                var _line = "";
                var _words = string_split(_s, " ");
                for (var _k = 0; _k < array_length(_words); _k++)
                {
                    var _try = (_line == "") ? _words[_k] : (_line + " " + _words[_k]);
                    if (string_width(_try) <= _w || _line == "")
                        _line = _try;
                    else
                    {
                        array_push(_lines, _line);
                        _line = _words[_k];
                    }
                }
                if (_line != "")
                    array_push(_lines, _line);
                if (array_length(_lines) > _max)
                {
                    _lines[_max - 1] = drc_flg_fit(_lines[_max - 1] + " " + _lines[_max], _w - string_width(".."));
                    array_resize(_lines, _max);
                }
                for (var _k = 0; _k < array_length(_lines); _k++)
                    _lines[_k] = drc_flg_fit(_lines[_k], _w);
                return _lines;
            };
            drc_flg_reset = function()
            {
                drc_flg_state = 0;
                drc_page_busy = 0;
                drc_flg_buf = "";
                keyboard_string = "";
            };
            return { label: "Flags...", why: "", bw: 600 };

        case "opened":
            drc_flg_dirty = 1;   // the game changed flags since the last visit
            break;

        case "leave":
            drc_flg_reset();     // an unconfirmed edit is dropped: nothing is written
            drc_flg_dirty = 1;
            break;

        case "step":
            if (drc_flg_dirty)
                drc_flg_rebuild();
            var n = array_length(drc_flg_list) + 1;
            var x_pressed = keyboard_check_pressed(ord("X"));

            // ---- jump to index: type digits, Z goes to that flag (or the next listed one)
            if (drc_flg_state == 3)
            {
                drc_flg_buf = drc_flg_typed(0);
                if (x_pressed)
                {
                    drc_flg_reset();
                    snd_play(snd_smallswing);
                }
                else if (drc_ok)
                {
                    if (drc_flg_buf != "" && array_length(drc_flg_list) > 0)
                    {
                        var _want = clamp(real(drc_flg_buf), 0, drc_flg_count() - 1);
                        drc_flg_pos = drc_flg_find(_want);
                        global.drcheat_flgidx = drc_flg_rowtarget(drc_flg_pos);
                        if (global.drcheat_flgidx != _want)
                        {
                            drc_msg = "flag[" + string(_want) + "] not in " + drc_flg_filters[global.drcheat_flgfilter];
                            drc_msgtime = 90;
                        }
                    }
                    drc_flg_reset();
                    snd_play(snd_select);
                }
                break;
            }

            // ---- choose the new value
            if (drc_flg_state == 1)
            {
                // the box starts with the current value; the first typed character replaces it
                var _typed = drc_flg_typed(1);
                if ((drc_flg_touched || _typed != "") && _typed != drc_flg_buf)
                {
                    drc_flg_touched = 1;
                    drc_flg_buf = _typed;
                    var _pv = drc_flg_parse(_typed);
                    if (!is_undefined(_pv))
                        drc_flg_new = _pv;
                }
                if (drc_step != 0)
                {
                    drc_flg_new += drc_step * (drc_big ? 10 : 1);
                    drc_flg_buf = drc_flg_fmt(drc_flg_new);
                    keyboard_string = drc_flg_buf;
                    drc_flg_touched = 1;
                    snd_play(snd_menumove);
                }
                if (x_pressed)
                {
                    drc_flg_reset();
                    drc_msg = "Cancelled";
                    drc_msgtime = 60;
                    snd_play(snd_smallswing);
                }
                else if (drc_ok)
                {
                    if (is_undefined(drc_flg_parse(drc_flg_buf)))
                    {
                        drc_msg = "Not a number";
                        drc_msgtime = 60;
                    }
                    else if (drc_flg_new == drc_flg_old)
                    {
                        drc_flg_reset();
                        drc_msg = "Unchanged";
                        drc_msgtime = 60;
                        snd_play(snd_smallswing);
                    }
                    else
                    {
                        drc_flg_state = 2;
                        drc_flg_yes = 0;   // No is preselected
                        snd_play(snd_select);
                    }
                }
                break;
            }

            // ---- confirm: only Z on Yes writes
            if (drc_flg_state == 2)
            {
                if (drc_step != 0 || keyboard_check_pressed(vk_up) || keyboard_check_pressed(vk_down))
                {
                    drc_flg_yes = !drc_flg_yes;
                    snd_play(snd_menumove);
                }
                if (x_pressed || (drc_ok && !drc_flg_yes))
                {
                    drc_flg_reset();
                    drc_msg = "Cancelled";
                    drc_msgtime = 60;
                    snd_play(snd_smallswing);
                }
                else if (drc_ok)
                {
                    var _t = drc_flg_target;
                    var _cur = drc_flg_get(_t);
                    if (is_undefined(_cur) || !is_numeric(_cur) || _cur != drc_flg_old)
                    {
                        drc_msg = "Value changed, not written";   // never write over a value we did not show
                    }
                    else
                    {
                        if (_t < 0)
                            global.plot = drc_flg_new;
                        else
                            global.flag[_t] = drc_flg_new;
                        drc_msg = drc_flg_title(_t) + " = " + drc_flg_str(drc_flg_get(_t));
                        if (global.drcheat_flgfilter == 2)
                            drc_flg_dirty = 1;   // the row may (dis)appear from Non-zero
                    }
                    drc_msgtime = 90;
                    drc_flg_reset();
                    snd_play(snd_select);
                }
                break;
            }

            // ---- browse. Up/down (held: repeat), </> a page, Shift x10, PgUp/PgDn, Home/End
            var _vis = max(1, drc_flg_vis);
            var _d = keyboard_check(vk_down) - keyboard_check(vk_up);
            var _mv = 0;
            if (_d != 0)
            {
                drc_flg_hold++;
                if (drc_flg_hold == 1 || (drc_flg_hold > 15 && (drc_flg_hold mod 2) == 0))
                    _mv = _d * (drc_big ? _vis : 1);
            }
            else
                drc_flg_hold = 0;
            _mv += drc_step * _vis * (drc_big ? 10 : 1);
            if (keyboard_check_pressed(vk_pagedown)) _mv += _vis;
            if (keyboard_check_pressed(vk_pageup)) _mv -= _vis;
            var _p = drc_flg_pos;
            if (_mv != 0)
            {
                // a single step wraps around, bigger jumps stop at the ends
                if (abs(_mv) == 1)
                    _p = (_p + n + _mv) mod n;
                else
                    _p = clamp(_p + _mv, 0, n - 1);
            }
            if (keyboard_check_pressed(vk_home)) _p = 0;
            if (keyboard_check_pressed(vk_end)) _p = n - 1;
            if (_p != drc_flg_pos)
            {
                drc_flg_pos = _p;
                snd_play(snd_menumove);
            }
            drc_flg_pos = clamp(drc_flg_pos, 0, n - 1);
            global.drcheat_flgidx = drc_flg_rowtarget(drc_flg_pos);

            if (keyboard_check_pressed(ord("F")))
            {
                global.drcheat_flgfilter = (global.drcheat_flgfilter + 1) mod 3;
                drc_flg_rebuild();
                snd_play(snd_select);
            }
            else if (keyboard_check_pressed(ord("J")))
            {
                drc_flg_state = 3;
                drc_page_busy = 1;
                drc_flg_buf = "";
                keyboard_string = "";
                snd_play(snd_select);
            }
            else if (drc_ok)
            {
                var _t = global.drcheat_flgidx;
                var _v = drc_flg_get(_t);
                if (is_undefined(_v) || !is_numeric(_v))
                {
                    drc_msg = "Can't edit: not a number";
                    drc_msgtime = 60;
                }
                else
                {
                    drc_flg_target = _t;
                    drc_flg_old = _v;
                    drc_flg_new = _v;
                    drc_flg_buf = drc_flg_fmt(_v);
                    keyboard_string = "";
                    drc_flg_touched = 0;
                    drc_flg_state = 1;
                    drc_page_busy = 1;   // X cancels the edit instead of leaving the page
                    snd_play(snd_select);
                }
            }
            break;

        case "draw":
            if (drc_flg_dirty)
                drc_flg_rebuild();
            var bx = drc_bx;
            var by = drc_by;
            var bw = drc_bw;
            var bh = drc_bh;
            var rx = bx + 50;
            var ry = by + 48;
            var rh = 26;
            var lh = 22;
            var n = array_length(drc_flg_list) + 1;
            draw_set_color(c_white);
            draw_text(bx + 50, by + 12, "FLAGS");
            draw_set_color(c_gray);
            var _hdr = drc_flg_filters[global.drcheat_flgfilter] + " " + string(n - 1) + "/" + string(drc_flg_count());
            draw_text(bx + bw - 24 - string_width(_hdr), by + 12, _hdr);
            if (drc_msgtime > 0)
            {
                draw_set_color(c_lime);
                draw_text(bx + 150, by + 12, drc_flg_fit(drc_msg, bw - 180 - string_width(_hdr)));
            }

            // info / prompt area: 3 lines above the footer
            var iy = by + bh - 34 - 3 * lh - 10;
            var vis = max(1, floor((iy - 6 - ry) / rh));
            drc_flg_vis = vis;
            var top = drc_list_top(drc_flg_pos, n, vis);
            var cx1 = rx + 72;    // value column
            var cx2 = rx + 172;   // label column
            var lw = bx + bw - 44 - cx2;
            for (var j = 0; j < vis && top + j < n; j++)
            {
                var p = top + j;
                var y0 = ry + j * rh;
                var sel = (p == drc_flg_pos);
                var t = drc_flg_rowtarget(p);
                var v = drc_flg_get(t);
                var known = (t < 0) ? true : !is_undefined(drc_flg_info(t));
                draw_set_color(sel ? c_yellow : c_white);
                draw_text(rx, y0, (t < 0) ? "plot" : string(t));
                if (!sel && is_numeric(v) && v == 0)
                    draw_set_color(c_gray);
                draw_text(cx1, y0, drc_flg_fit(drc_flg_str(v), cx2 - cx1 - 10));
                draw_set_color(sel ? c_yellow : (known ? c_white : c_gray));
                draw_text(cx2, y0, drc_flg_fit((t < 0) ? drc_flg_plotlabel(v) : drc_flg_label(t), lw));
                if (sel)
                    draw_sprite(spr_heart, 0, rx - 26, y0 + 8);
            }
            drc_draw_more(ry, ry + (vis - 1) * rh, top > 0, top + vis < n);
            draw_set_color(c_dkgray);
            draw_line(bx + 12, iy - 4, bx + bw - 12, iy - 4);

            var ix = bx + 20;
            var iw = bw - 40;
            var ti = global.drcheat_flgidx;
            var foot = "";
            if (drc_flg_state == 0)
            {
                var _l = [];
                if (ti < 0)
                {
                    var _m = drc_flg_plotinfo(drc_flg_get(-1));
                    _l[0] = "global.plot  milestone: " + (is_undefined(_m) ? "unknown" : (string(_m[0]) + " " + _m[1]));
                    var _d2 = is_undefined(_m) ? "" : _m[2];
                    if (_d2 == "")
                        _d2 = "Story progress. Most checks are plot >= N: raising skips content, lowering replays it.";
                    var _w = drc_flg_wrap(_d2, iw, 2);
                    for (var k = 0; k < array_length(_w); k++)
                        array_push(_l, _w[k]);
                }
                else
                {
                    var _e = drc_flg_info(ti);
                    if (is_undefined(_e))
                        _l[0] = "flag[" + string(ti) + "]  unknown (not in our list)";
                    else
                    {
                        var _conf = ["  (uncertain)", "", "  (verified)"];
                        _l[0] = drc_flg_fit(_e[0] + _conf[clamp(_e[4], 0, 2)], iw);
                        var _txt = _e[2];
                        if (_e[3] != "")
                            _txt += ((_txt == "") ? "" : "  ") + "Values: " + _e[3];
                        if (_txt == "")
                            _txt = _e[1];
                        var _w = drc_flg_wrap(_txt, iw, 2);
                        for (var k = 0; k < array_length(_w); k++)
                            array_push(_l, _w[k]);
                    }
                }
                for (var k = 0; k < array_length(_l); k++)
                {
                    draw_set_color((k == 0) ? c_white : c_gray);
                    draw_text(ix, iy + k * lh, _l[k]);
                }
                foot = "Z edit  F filter  J jump  </> page  X back";
            }
            else
            {
                var _tt = drc_flg_target;
                var _nm = drc_flg_title(_tt) + " (" + ((_tt < 0) ? drc_flg_plotlabel(drc_flg_old) : drc_flg_label(_tt)) + ")";
                draw_set_color(c_white);
                if (drc_flg_state == 3)
                {
                    draw_text(ix, iy, "Jump to flag index (0-" + string(drc_flg_count() - 1) + "):");
                    draw_set_color(c_yellow);
                    draw_text(ix, iy + lh, drc_flg_buf + "_");
                    foot = "type the index  Z go  X cancel";
                }
                else if (drc_flg_state == 1)
                {
                    draw_text(ix, iy, drc_flg_fit("Edit " + _nm, iw));
                    draw_text(ix, iy + lh, "Now: " + drc_flg_str(drc_flg_old));
                    draw_set_color(c_yellow);
                    var _ok = !is_undefined(drc_flg_parse(drc_flg_buf));
                    draw_text(ix + 200, iy + lh, "New: < " + drc_flg_buf + " >" + (_ok ? "" : " ?"));
                    if (_tt < 0)
                    {
                        draw_set_color(c_gray);
                        draw_text(ix, iy + 2 * lh, drc_flg_fit("= " + drc_flg_plotlabel(drc_flg_new), iw));
                    }
                    foot = "type / </> +-1 SHIFT 10  Z next  X cancel";
                }
                else
                {
                    draw_text(ix, iy, drc_flg_fit("Set " + _nm, iw));
                    draw_text(ix, iy + lh, "from " + drc_flg_str(drc_flg_old) + " to " + drc_flg_str(drc_flg_new) + "?");
                    var _yx = ix + 60;
                    draw_set_color(drc_flg_yes ? c_yellow : c_white);
                    draw_text(_yx, iy + 2 * lh, "Yes");
                    draw_set_color(drc_flg_yes ? c_white : c_yellow);
                    draw_text(_yx + 120, iy + 2 * lh, "No");
                    draw_sprite(spr_heart, 0, (drc_flg_yes ? _yx : _yx + 120) - 26, iy + 2 * lh + 8);
                    foot = "</> choose  Z confirm  X cancel";
                }
            }
            draw_set_color(c_gray);
            draw_text(bx + 20, by + bh - 34, foot);
            draw_set_color(c_white);
            break;
    }
    return undefined;
}
