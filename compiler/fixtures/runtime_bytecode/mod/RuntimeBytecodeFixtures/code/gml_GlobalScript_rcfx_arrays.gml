function rcfx_arrays()
{
    var arr = [1, 2, 3];
    arr[3] = 4;
    global.rcfx_arr[0] = 7;
    rcfx_inst_arr[1] = arr[2];
    return array_length(arr) + arr[1] + global.rcfx_arr[0];
}
