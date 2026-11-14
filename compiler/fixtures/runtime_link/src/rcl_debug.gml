// show_debug_message with a string that is not in data.win (appended to the runner's table).
global.rcl_debug_runs = variable_global_exists("rcl_debug_runs") ? global.rcl_debug_runs + 1 : 1;
if (global.rcl_debug_runs == 1 || global.rcl_debug_runs == 100)
    show_debug_message("[rcl] linked bytecode says hello, run " + string(global.rcl_debug_runs));
return global.rcl_debug_runs;
