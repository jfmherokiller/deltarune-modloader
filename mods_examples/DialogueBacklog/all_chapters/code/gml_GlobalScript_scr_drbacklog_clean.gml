// DialogueBacklog: turn an obj_writer string into plain text, using the writer's own control codes:
//   `x   literal x            & \n #  line break         |  spacing (dropped)
//   ^N   pause                 /  %   wait / next page   \XY  two-char command (face, voice, colour...)
//   \*X  button icon -> [X]
function scr_drbacklog_clean(_s)
{
    if (!is_string(_s))
        return "";
    var _out = "";
    var _n = string_length(_s);
    var _i = 1;
    while (_i <= _n)
    {
        var _c = string_char_at(_s, _i);
        if (_c == "`")
        {
            _out += string_char_at(_s, _i + 1);
            _i += 2;
        }
        else if (_c == "&" || _c == "\n" || _c == "#")
        {
            _out += "\n";
            _i += 1;
        }
        else if (_c == "^")
        {
            _i += 2;
        }
        else if (_c == "/" || _c == "%" || _c == "|" || _c == "\r")
        {
            _i += 1;
        }
        else if (_c == "\\")
        {
            if (string_char_at(_s, _i + 1) == "*")
                _out += "[" + string_char_at(_s, _i + 2) + "]";
            _i += 3;
        }
        else
        {
            _out += _c;
            _i += 1;
        }
    }
    // trim surrounding blank lines and spaces
    while (string_length(_out) > 0 && (string_char_at(_out, 1) == "\n" || string_char_at(_out, 1) == " "))
        _out = string_delete(_out, 1, 1);
    while (string_length(_out) > 0 && (string_char_at(_out, string_length(_out)) == "\n" || string_char_at(_out, string_length(_out)) == " "))
        _out = string_delete(_out, string_length(_out), 1);
    return _out;
}

// Comparison key for a page: obj_writer re-wraps long pages after they appear (it replaces spaces
// with "&" breaks and indents continuation lines with "||"), so drop every spacing character.
function scr_drbacklog_key(_s)
{
    if (!is_string(_s))
        return "";
    _s = string_replace_all(_s, "&", "");
    _s = string_replace_all(_s, "|", "");
    _s = string_replace_all(_s, "#", "");
    _s = string_replace_all(_s, "\n", "");
    _s = string_replace_all(_s, "\r", "");
    _s = string_replace_all(_s, " ", "");
    return _s;
}
