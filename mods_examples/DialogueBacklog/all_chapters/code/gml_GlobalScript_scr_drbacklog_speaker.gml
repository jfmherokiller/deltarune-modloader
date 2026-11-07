// DialogueBacklog: who is speaking in this obj_writer (runs in the writer's scope). "" = narration.
// Order: a \F face code in the page (dialogue boxes), a \T voice code in the page, the current face
// (global.fc), the writer's own voice (textsound), then for battle balloons the nearest enemy.
// Face numbers / letters are the ones obj_writer and scr_speaker use (stable across chapters 1-5).
function scr_drbacklog_speaker()
{
    var _s = mystring;
    var _box = variable_instance_exists(id, "dialoguer") && dialoguer == 1;
    var _nm;
    var _p = string_pos("\\F", _s);
    if (_p > 0 && _box)
    {
        _nm = scr_drbacklog_name(0, string_char_at(_s, _p + 2));
        if (!is_undefined(_nm))
            return _nm;
    }
    _p = string_pos("\\T", _s);
    if (_p > 0)
    {
        _nm = scr_drbacklog_name(1, string_char_at(_s, _p + 2));
        if (!is_undefined(_nm))
            return _nm;
    }
    var _face = asset_get_index("obj_face");
    if ((_box || (_face >= 0 && instance_exists(_face))) && variable_global_exists("fc") && global.fc > 0)
    {
        _nm = scr_drbacklog_name(2, global.fc);
        if (!is_undefined(_nm))
            return _nm;
    }
    if (variable_instance_exists(id, "textsound") && audio_exists(textsound))
    {
        _nm = scr_drbacklog_name(3, audio_get_name(textsound));
        if (!is_undefined(_nm))
            return _nm;
    }
    // battle speech balloon: the writer belongs to an obj_battleblcon; name it after the closest enemy
    var _bl = asset_get_index("obj_battleblcon");
    var _mp = asset_get_index("obj_monsterparent");
    if (_bl >= 0 && _mp >= 0 && instance_exists(_bl) && variable_global_exists("monstername"))
    {
        var _me = id;
        var _mine = 0;
        with (_bl)
        {
            if (variable_instance_exists(id, "mywriter") && mywriter == _me)
                _mine = 1;
        }
        if (_mine)
        {
            var _wx = x;
            var _wy = y;
            var _best = "";
            var _bd = 100000;
            with (_mp)
            {
                if (variable_instance_exists(id, "myself") && is_real(myself) && myself >= 0 && myself < array_length(global.monstername))
                {
                    var _d = point_distance(x, y, _wx, _wy);
                    if (_d < _bd && is_string(global.monstername[myself]))
                    {
                        _bd = _d;
                        _best = global.monstername[myself];
                    }
                }
            }
            return _best;
        }
    }
    return "";
}
