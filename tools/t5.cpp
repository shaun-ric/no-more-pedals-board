// Stage hardening helpers: the footswitch debounce (PressFilter) and the event log (EventLog).
#include "Core/EventLog.h"
#include "Core/PressFilter.h"
#include <cstdio>

int main()
{
    // ---- Debounce ---------------------------------------------------------------------------------
    PressFilter f (150);
    const bool first = f.accept ("setlist:next", 1000);
    const bool bounce = f.accept ("setlist:next", 1040);       // 40 ms later: a bounce
    const bool other = f.accept ("slot:5", 1050);              // a different switch is not affected
    const bool later = f.accept ("setlist:next", 1160);        // 160 ms after the first: a real press
    const bool edge = f.accept ("setlist:next", 1160 + 149);   // just inside the window again
    std::printf ("debounce: %d %d %d %d %d\n", (int) first, (int) bounce, (int) other, (int) later, (int) edge);

    // The millisecond counter wraps round every 49 days; a press either side of the wrap still works.
    PressFilter w (150);
    const bool beforeWrap = w.accept ("k", 0xFFFFFFF0u);
    const bool afterWrapBounce = w.accept ("k", 0x00000010u);   // 32 ms later, across the wrap
    const bool afterWrapPress = w.accept ("k", 0x000000C0u);    // 208 ms later
    std::printf ("wrap: %d %d %d\n", (int) beforeWrap, (int) afterWrapBounce, (int) afterWrapPress);

    // ---- Event log ----------------------------------------------------------------------------------
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("rigbench_t5");
    dir.deleteRecursively();
    dir.createDirectory();

    // An old log is deleted when the log starts; a recent one is kept.
    const auto oldLog = dir.getChildFile ("rigbench-20200101.log");
    oldLog.replaceWithText ("old\n");
    oldLog.setLastModificationTime (juce::Time::getCurrentTime() - juce::RelativeTime::days (40));
    const auto recentLog = dir.getChildFile ("rigbench-20990101.log");
    recentLog.replaceWithText ("recent\n");

    {
        EventLog log (dir, 30);
        log.add ("Audio device lost");
        const bool writtenBeforeFlush = log.currentFile().existsAsFile();
        log.flush();
        log.add ("Audio device back");   // written when the log is destroyed
        std::printf ("log: before-flush %d old-deleted %d recent-kept %d\n", (int) writtenBeforeFlush,
                     (int) ! oldLog.existsAsFile(), (int) recentLog.existsAsFile());
    }

    juce::StringArray lines;
    lines.addLines (EventLog (dir, 30).currentFile().loadFileAsString());
    lines.removeEmptyStrings();
    std::printf ("log: lines %d first %d second %d stamped %d\n", lines.size(),
                 (int) lines[0].endsWith ("  Audio device lost"), (int) lines[1].endsWith ("  Audio device back"),
                 (int) (lines[0].length() > 25 && lines[0][4] == '-' && lines[0][19] == '.'));

    dir.deleteRecursively();
}
