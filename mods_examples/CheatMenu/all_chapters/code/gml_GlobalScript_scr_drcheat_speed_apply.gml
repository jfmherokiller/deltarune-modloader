// CheatMenu "Walk speed": scale the overworld player's base walk speed (ROADMAP task 28).
//
// obj_mainchara.bwspeed is the base speed: its Create sets 3 (Light World) or 4 (darkmode).
// Every step obj_mainchara_Step_0 derives wspeed from it (walk = bwspeed, run = bwspeed + 1..3 /
// + 2..5 in the Dark World, climbing = ceil(wspeed * 0.7)), so scaling bwspeed speeds up walking,
// running and the obj_mainchara ladder/climb mode together. wspeed itself is never written.
//
// Called every step while the menu is closed (obj_drcheat Step). Bookkeeping lives on the player
// instance: drc_spd_base = the game's own bwspeed, drc_spd_set = the value we wrote (-1 = none).
// - bwspeed still equals drc_spd_set: it is our value, so the base is unchanged.
// - anything else (a fresh instance after a room change, or the game wrote its own speed): the
//   current bwspeed is the new base. The multiplier is always applied to that base, never to a
//   value we wrote, so it cannot compound and the game's value is never lost.
// - multiplier off: put the base back if bwspeed is still ours, then forget it.
//
// Only the vanilla bases 3 and 4 are scaled. Scripted scenes that drive bwspeed themselves are
// left alone because their values differ: Ch5 obj_ch5_LW20 / LW21 slow Kris to a stop with values
// 0..2 (LW20 reads bwspeed back, clamps it to <= 1 and waits for 0; scaling it would stall that
// cutscene), and Ch3/Ch5 obj_b3bs_stealth rewrites max(2, stealthspeed * t / 6) every End Step.
// Some areas cap wspeed after it is derived (Ch5 side B and plot >= 570 clamp it to 2 walking /
// 3 running); the cap wins there, as in vanilla. Our scaled values (5, 6, 8, 9, 12) never equal
// a vanilla base, so a game write of 3 or 4 is always seen as a new base.
//
// Other player controllers are deliberately NOT scaled (they have their own physics, tuned to
// level layouts and timings, and do not use bwspeed):
// - obj_plat_player (Ch5 platformer: hspeed_max, accel, jump/dash physics)
// - obj_climb_kris (Ch4/5 wall climbing: grid moves at climbspeed)
// - obj_mainchara_dash (Ch5 dash mode), obj_mainchara_board (Ch3 game board),
//   obj_homealone_heart (Ch4, its own bwspeed 2), the battle soul.
// The object_index check keeps any child object of obj_mainchara out as well.
function scr_drcheat_speed_apply(_mult)
{
    with (obj_mainchara)
    {
        if (object_index == obj_mainchara)
        {
            if (!(variable_instance_exists(id, "drc_spd_set") && drc_spd_set >= 0 && bwspeed == drc_spd_set))
            {
                drc_spd_base = bwspeed;
                drc_spd_set = -1;
            }
            if (_mult != 1 && (drc_spd_base == 3 || drc_spd_base == 4))
            {
                var _want = ceil(drc_spd_base * _mult);
                bwspeed = _want;
                drc_spd_set = _want;
            }
            else if (drc_spd_set >= 0)
            {
                bwspeed = drc_spd_base;   // exact vanilla value back
                drc_spd_set = -1;
            }
        }
    }
}
