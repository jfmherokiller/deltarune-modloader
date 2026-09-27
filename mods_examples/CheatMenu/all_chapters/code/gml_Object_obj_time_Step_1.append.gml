// CheatMenu: keep one obj_drcheat alive in every room (obj_time is persistent).
if (!instance_exists(obj_drcheat))
    instance_create_depth(0, 0, -99999, obj_drcheat);
