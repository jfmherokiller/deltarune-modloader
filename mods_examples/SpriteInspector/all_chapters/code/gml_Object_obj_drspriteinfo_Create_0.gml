// SpriteInspector: F9 toggles sprite-name labels. Shift+F9 copies the list to the clipboard.
depth = -99998;
if (!variable_global_exists("drspr_on"))
    global.drspr_on = 0;
drspr_names = [];
drspr_msg = "";
drspr_msgtime = 0;
