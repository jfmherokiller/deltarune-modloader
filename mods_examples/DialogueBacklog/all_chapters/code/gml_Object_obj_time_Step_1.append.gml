// DialogueBacklog: keep one obj_drbacklog alive in every room (obj_time is persistent).
if (!instance_exists(obj_drbacklog))
    instance_create_depth(0, 0, -99997, obj_drbacklog);
