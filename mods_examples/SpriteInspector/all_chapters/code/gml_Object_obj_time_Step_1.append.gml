// SpriteInspector: keep one obj_drspriteinfo alive in every room (obj_time is persistent).
if (!instance_exists(obj_drspriteinfo))
    instance_create_depth(0, 0, -99998, obj_drspriteinfo);
