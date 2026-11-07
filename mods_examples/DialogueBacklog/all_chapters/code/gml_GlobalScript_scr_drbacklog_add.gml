// DialogueBacklog: add one entry to the backlog (kind 0 = line, 1 = session divider, 2 = choice).
// Returns the entry struct so a writer can update it if its text keeps growing.
function scr_drbacklog_add(_speaker, _text, _kind)
{
    scr_drbacklog_init();
    var _e = global.drbacklog_entries;
    var _n = array_length(_e);
    // the same line re-shown at once (a writer re-created with the same page, maybe re-wrapped):
    // keep one copy
    if (_n > 0 && _kind != 1)
    {
        var _last = _e[_n - 1];
        if (_last.k == _kind && _last.s == _speaker && (current_time - _last.ms) < 1500
            && scr_drbacklog_key(_last.t) == scr_drbacklog_key(_text))
        {
            _last.ms = current_time;
            return _last;
        }
    }
    scr_drbacklog_flush();
    var _entry = { k: _kind, s: _speaker, t: _text, ms: current_time };
    array_push(global.drbacklog_entries, _entry);
    if (array_length(global.drbacklog_entries) > global.drbacklog_max)
        array_delete(global.drbacklog_entries, 0, array_length(global.drbacklog_entries) - global.drbacklog_max);
    if (_kind != 1)
        global.drbacklog_pending = _entry;   // written to the transcript once it stops changing
    // keep the view anchored on the same entry while the backlog is scrolled up
    if (global.drbacklog_open && global.drbacklog_scroll > 0)
        global.drbacklog_scroll++;
    return _entry;
}
