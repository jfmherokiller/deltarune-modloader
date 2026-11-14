// Calls to a compiled (YYC) script, inside a loop: on YYC the linker lowers each call to
// script_execute, which moves the branches around it.
var total = 0;
for (var k = 1; k <= 3; k++) total += __spelunker_minutes_to_seconds(k);
global.gmlo_script_total = total;
var secs = __spelunker_minutes_to_seconds(2);
global.gmlo_script_result = secs;
return secs + 1;
