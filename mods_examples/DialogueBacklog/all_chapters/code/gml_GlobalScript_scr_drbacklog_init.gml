// DialogueBacklog: in-memory state. Safe to call any time; only the first call does anything.
function scr_drbacklog_init()
{
    if (variable_global_exists("drbacklog_entries"))
        return;
    global.drbacklog_entries = [];
    global.drbacklog_max = 1000;        // entries kept in memory (oldest dropped first)
    global.drbacklog_pending = -1;      // newest entry, not yet written to the transcript
    global.drbacklog_loaded = 0;        // transcript read back (needs global.chapter)
    global.drbacklog_open = 0;
    global.drbacklog_scroll = 0;        // entries scrolled up from the newest
}
