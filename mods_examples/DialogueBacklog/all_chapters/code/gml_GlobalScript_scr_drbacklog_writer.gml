// DialogueBacklog: called at the top of obj_writer's Draw event (self = the writer).
// Logs each page once, when it first appears. obj_writer later re-wraps mystring by inserting line
// breaks, so pages are compared without whitespace/break characters.
function scr_drbacklog_writer()
{
    if (!variable_instance_exists(id, "drbl_raw"))
    {
        drbl_raw = undefined;
        drbl_key = "";
        drbl_msgno = -1;
        drbl_entry = -1;
    }
    if (!is_string(mystring))
        return;
    var _no = variable_instance_exists(id, "msgno") ? msgno : 0;
    if (mystring == drbl_raw && _no == drbl_msgno)
        return;
    if (image_alpha == 0)
        return;                         // hidden writer: log it once it is shown
    drbl_raw = mystring;
    var _key = scr_drbacklog_key(mystring);
    if (_no == drbl_msgno && _key == drbl_key)
        return;                         // same page, only re-wrapped
    // a page whose text keeps growing (typed in by a cutscene) updates its entry instead of adding more
    var _grow = (_no == drbl_msgno && is_struct(drbl_entry) && drbl_key != "" && string_pos(drbl_key, _key) == 1);
    drbl_msgno = _no;
    drbl_key = _key;
    var _text = scr_drbacklog_clean(mystring);
    if (_text == "")
        return;
    var _who = scr_drbacklog_speaker();
    if (_grow)
    {
        drbl_entry.t = _text;
        drbl_entry.s = _who;
        drbl_entry.ms = current_time;
    }
    else
    {
        drbl_entry = scr_drbacklog_add(_who, _text, 0);
    }
}
