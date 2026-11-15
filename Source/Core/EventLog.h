#pragma once

#include <juce_core/juce_core.h>

#include <utility>

// A plain-text record of what happened while the rig was running, for working out afterwards what went
// wrong at a gig: the audio device going away and coming back, late audio blocks and driver dropouts,
// warnings, and every song, scene and scene preset change. One file per day in Documents\RigBench\logs,
// named rigbench-YYYYMMDD.log; files older than `keepDays` are deleted at startup.
//
// add() may be called from any thread except the audio thread: it takes a short lock and does no file
// I/O. flush() writes what has been added, and is called a few times a second by the UI timer (and when
// the log is destroyed), so a crash loses at most the last fraction of a second.
class EventLog
{
public:
    explicit EventLog (juce::File folder, int keepDays = 30) : dir (std::move (folder))
    {
        dir.createDirectory();

        const auto cutoff = juce::Time::getCurrentTime() - juce::RelativeTime::days ((double) keepDays);
        for (const auto& f : dir.findChildFiles (juce::File::findFiles, false, "rigbench-*.log"))
            if (f.getLastModificationTime() < cutoff)
                f.deleteFile();
    }

    ~EventLog() { flush(); }

    void add (const juce::String& line)
    {
        const auto now = juce::Time::getCurrentTime();
        const juce::String stamp = now.formatted ("%Y-%m-%d %H:%M:%S") + "."
                                   + juce::String (now.getMilliseconds()).paddedLeft ('0', 3);

        const juce::ScopedLock sl (lock);
        pending.add (stamp + "  " + line);
    }

    // Writes the lines added since the last call (message thread).
    void flush()
    {
        juce::StringArray lines;
        {
            const juce::ScopedLock sl (lock);
            lines.swapWith (pending);
        }

        if (lines.isEmpty())
            return;

        currentFile().appendText (lines.joinIntoString ("\n") + "\n");
    }

    juce::File currentFile() const
    {
        return dir.getChildFile ("rigbench-" + juce::Time::getCurrentTime().formatted ("%Y%m%d") + ".log");
    }

    const juce::File& folder() const noexcept { return dir; }

private:
    juce::File dir;
    juce::CriticalSection lock;
    juce::StringArray pending;

    JUCE_DECLARE_NON_COPYABLE (EventLog)
};
