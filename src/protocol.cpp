#include "protocol.h"

void Protocol::processLine(const String& line) {
    if (line.startsWith("NOW:")) {
        _received_time = (uint32_t)line.substring(4).toInt();

    } else if (line == "EVENT:CLEAR") {
        _store->clear();
        _complete = false;

    } else if (line.startsWith("EVENT:ADD\t")) {
        // EVENT:ADD\t{start}\t{end}\t{title}\t{location}
        String payload = line.substring(10);

        int t1 = payload.indexOf('\t');
        if (t1 < 0) return;
        int t2 = payload.indexOf('\t', t1 + 1);
        if (t2 < 0) return;
        int t3 = payload.indexOf('\t', t2 + 1);

        Event e;
        e.start_utc = (uint32_t)payload.substring(0, t1).toInt();
        e.end_utc   = (uint32_t)payload.substring(t1 + 1, t2).toInt();
        if (t3 >= 0) {
            e.title    = payload.substring(t2 + 1, t3);
            e.location = payload.substring(t3 + 1);
        } else {
            e.title    = payload.substring(t2 + 1);
            e.location = "";
        }
        e.title.trim();
        e.location.trim();
        _store->add(e);

    } else if (line == "EVENT:FINISH") {
        _complete = true;
    }
}
