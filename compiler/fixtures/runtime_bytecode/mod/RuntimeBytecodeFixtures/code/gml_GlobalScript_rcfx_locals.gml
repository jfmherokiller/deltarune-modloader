function rcfx_locals(base)
{
    var a = 1, b = 2;
    var c = a + b + base;
    for (var i = 0; i < 3; i++)
        c += i;
    return c;
}
