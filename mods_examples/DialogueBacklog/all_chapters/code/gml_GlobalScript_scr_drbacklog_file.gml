// DialogueBacklog: transcript file, one per chapter, in the game's save folder
// (%LOCALAPPDATA%\DELTARUNE\dialogue_log_chN.txt). Readable text, one entry per line:
//   # <date>            session start          > text        a choice
//   Speaker<TAB>text    a line ("" speaker = narration); line breaks inside text are written as \n
function scr_drbacklog_file()
{
    if (!variable_global_exists("chapter") || !is_real(global.chapter) || global.chapter < 1)
        return "";
    return "dialogue_log_ch" + string(global.chapter) + ".txt";
}

// Write the pending (newest) entry. Called before each new entry, when the backlog opens,
// and a few seconds after the last change.
function scr_drbacklog_flush()
{
    scr_drbacklog_init();
    var _e = global.drbacklog_pending;
    if (!is_struct(_e))
        return;
    var _fn = scr_drbacklog_file();
    if (_fn == "")
        return;                         // chapter not known yet: keep it pending
    scr_drbacklog_load();
    global.drbacklog_pending = -1;
    var _line;
    if (_e.k == 2)
        _line = "> " + _e.t;
    else
        _line = _e.s + chr(9) + _e.t;
    _line = string_replace_all(_line, "\n", "\\n");
    var _f = file_text_open_append(_fn);
    if (_f < 0)
        return;
    file_text_write_string(_f, _line);
    file_text_writeln(_f);
    file_text_close(_f);
}

// Once per game launch: read the end of this chapter's transcript back into the backlog (so it
// survives quitting and chapter switches), trim the file if it got long, and mark the new session.
function scr_drbacklog_load()
{
    scr_drbacklog_init();
    if (global.drbacklog_loaded)
        return;
    var _fn = scr_drbacklog_file();
    if (_fn == "")
        return;
    global.drbacklog_loaded = 1;
    var _lines = [];
    if (file_exists(_fn))
    {
        var _f = file_text_open_read(_fn);
        if (_f >= 0)
        {
            while (!file_text_eof(_f))
            {
                var _l = file_text_read_string(_f);
                file_text_readln(_f);
                if (_l != "")
                    array_push(_lines, _l);
            }
            file_text_close(_f);
        }
    }
    var _n = array_length(_lines);
    if (_n > 20000)
    {
        // keep the newest 10000 lines
        array_delete(_lines, 0, _n - 10000);
        _n = array_length(_lines);
        var _w = file_text_open_write(_fn);
        if (_w >= 0)
        {
            for (var _i = 0; _i < _n; _i++)
            {
                file_text_write_string(_w, _lines[_i]);
                file_text_writeln(_w);
            }
            file_text_close(_w);
        }
    }
    // older entries go in front of anything logged before the chapter was known
    var _old = [];
    var _from = max(0, _n - floor(global.drbacklog_max / 2));
    for (var _i = _from; _i < _n; _i++)
    {
        var _l = _lines[_i];
        var _c = string_char_at(_l, 1);
        if (_c == "#")
            array_push(_old, { k: 1, s: "", t: string_delete(_l, 1, 2), ms: 0 });
        else if (_c == ">")
            array_push(_old, { k: 2, s: "", t: string_replace_all(string_delete(_l, 1, 2), "\\n", "\n"), ms: 0 });
        else
        {
            var _tab = string_pos(chr(9), _l);
            if (_tab > 0)
                array_push(_old, { k: 0, s: string_copy(_l, 1, _tab - 1), t: string_replace_all(string_delete(_l, 1, _tab), "\\n", "\n"), ms: 0 });
        }
    }
    var _stamp = date_datetime_string(date_current_datetime());
    array_push(_old, { k: 1, s: "", t: _stamp, ms: 0 });
    var _cur = global.drbacklog_entries;
    for (var _i = 0; _i < array_length(_cur); _i++)
        array_push(_old, _cur[_i]);
    global.drbacklog_entries = _old;
    var _a = file_text_open_append(_fn);
    if (_a >= 0)
    {
        file_text_write_string(_a, "# " + _stamp);
        file_text_writeln(_a);
        file_text_close(_a);
    }
    // entries logged before the chapter was known were never written; the pending one still is
}
