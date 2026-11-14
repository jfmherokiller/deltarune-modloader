// Counter in a global: every run adds 1.
if (!variable_global_exists("rcl_counter"))
    global.rcl_counter = 0;
global.rcl_counter += 1;
return global.rcl_counter;
