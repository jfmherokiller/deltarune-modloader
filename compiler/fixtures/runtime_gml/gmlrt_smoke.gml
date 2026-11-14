// gmlrt::Runtime smoke test: the same source is compiled in the game and run on VM and YYC
// runners alike, so it only uses names every GameMaker game has.
// Covers: locals + for loop, built-in calls, a global read-modify-write, an instance variable on
// the running instance, a built-in variable read, a string the game does not have, show_debug_message.
var total = 0;
for (var k = 1; k <= 4; k++) total += abs(-k);
global.gmlrt_smoke_runs = (variable_global_exists("gmlrt_smoke_runs") ? global.gmlrt_smoke_runs : 0) + 1;
self.gmlrt_smoke_sum = total;
global.gmlrt_smoke_room_speed_ok = room_speed > 0;
var msg = "gmlrt smoke says " + string(total);
show_debug_message(msg);
return msg;
