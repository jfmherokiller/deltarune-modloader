// show_debug_message with a string the game does not have (appended to the runner's string table),
// a loop (branches) and locals.
var n = 0;
for (var i = 0; i < 4; i++) n += i;
show_debug_message("gmlo debug message " + string(n));
global.gmlo_last_message = "gmlo debug message " + string(n);
return n;
