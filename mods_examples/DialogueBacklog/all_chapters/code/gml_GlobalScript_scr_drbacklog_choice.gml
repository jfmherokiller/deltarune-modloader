// DialogueBacklog: called after obj_choicer_neo's confirm event (self = the choicer). Logs the picked option.
function scr_drbacklog_choice()
{
    if (!variable_instance_exists(id, "choiced") || choiced != 1 || variable_instance_exists(id, "drbl_done"))
        return;
    drbl_done = 1;
    if (mychoice < 0 || !variable_global_exists("choicemsg") || mychoice >= array_length(global.choicemsg))
        return;
    var _t = scr_drbacklog_clean(global.choicemsg[mychoice]);
    _t = string_replace_all(_t, "\n", " ");
    if (_t != "")
        scr_drbacklog_add("", _t, 2);
}
