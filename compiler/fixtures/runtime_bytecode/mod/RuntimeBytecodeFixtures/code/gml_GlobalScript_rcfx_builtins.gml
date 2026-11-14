function rcfx_builtins()
{
    var n = abs(-3) + floor(2.5) + string_length("abc");
    show_debug_message(string(n));
    return max(n, room_speed);
}
