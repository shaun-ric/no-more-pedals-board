// Which connections a scene preset switches on (Core/PresetPlan.h), and the library's in-memory presets.
#include "Core/Library.h"
#include "Core/PresetPlan.h"
#include <cstdio>
#include <set>

static void show (const char* label, const std::vector<bool>& on)
{
    std::printf ("%s:", label);
    for (const bool b : on)
        std::printf (" %d", (int) b);
    std::printf ("\n");
}

int main()
{
    using presetplan::Edge;

    // The preset knows gate, amp1 and eq. Everything it lists does what it says.
    const std::set<std::string> known { "gate", "amp1", "eq" };
    auto listed = [&known] (const std::string& id) { return known.count (id) > 0; };

    // An amp added in parallel afterwards: gate>amp2 and amp2>eq are not in the preset. amp2 is switched off and
    // passes its input through, so amp2>eq must stay off (eq already has amp1), or the dry guitar is added.
    show ("parallel", presetplan::connectionStates ({ { "gate", "amp1", true, true, true },
                                                      { "amp1", "eq", true, true, true },
                                                      { "gate", "amp2", false, false, true },
                                                      { "amp2", "eq", false, false, true } }, listed));

    // A pedal put into the chain afterwards: gate>amp1 is gone, gate>pedal and pedal>amp1 are new. pedal>amp1 is
    // the only way into amp1, so it must come on, or the old preset is silent.
    show ("series", presetplan::connectionStates ({ { "gate", "pedal", false, false, true },
                                                    { "pedal", "amp1", false, false, true },
                                                    { "amp1", "eq", true, true, true } }, listed));

    // A new connection between two nodes the preset knows keeps its default, on or off.
    show ("known", presetplan::connectionStates ({ { "gate", "amp1", true, true, true },
                                                   { "gate", "eq", false, false, true },
                                                   { "amp1", "eq", false, false, false } }, listed));

    // The preset switches the listed way in off: an unknown node's connection is then the only live one, and comes on.
    show ("listed-off", presetplan::connectionStates ({ { "amp1", "eq", true, false, true },
                                                        { "amp2", "eq", false, false, true } }, listed));

    // Two unknown nodes into one: only the first comes on.
    show ("two-new", presetplan::connectionStates ({ { "amp2", "eq", false, false, true },
                                                     { "amp3", "eq", false, false, true } }, listed));

    // ---- Presets are held in memory ---------------------------------------------------------------------
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("rigbench_t7");
    dir.deleteRecursively();
    Library lib (dir);

    NodePreset p;
    p.id = "bright"; p.name = "Bright"; p.nodeType = "eq3"; p.slot = 3;
    p.values = { { "treble", 4.0f } };
    p.file = "lib:NAM/a.nam"; p.state = "c3RhdGU=";
    lib.writeNodePreset (p);

    NodePreset read;
    const bool found = lib.readNodePreset ("eq3", "bright", read);
    std::printf ("cache: found %d fields %d\n", (int) found,
                 (int) (read.name == "Bright" && read.slot == 3 && read.file == p.file && read.state == p.state
                        && read.values.size() == 1 && read.values[0].second == 4.0f));

    // A file changed behind the app's back is not seen until the library is told to look again...
    const auto file = dir.getChildFile ("presets/eq3/bright.preset.json");
    file.replaceWithText (file.loadFileAsString().replace ("Bright", "Edited"));
    lib.readNodePreset ("eq3", "bright", read);
    const bool stale = read.name == "Bright";
    lib.rereadFromDisk();
    lib.readNodePreset ("eq3", "bright", read);
    std::printf ("cache: held %d reread %d\n", (int) stale, (int) (read.name == "Edited"));

    // ...but everything the app writes itself is seen at once, and a deleted preset is gone.
    p.name = "Brighter";
    lib.writeNodePreset (p);
    lib.readNodePreset ("eq3", "bright", read);
    const bool written = read.name == "Brighter";
    lib.deleteNodePreset ("eq3", "bright");
    std::printf ("cache: own-write %d deleted %d by-slot-gone %d\n", (int) written,
                 (int) ! lib.readNodePreset ("eq3", "bright", read), (int) ! lib.readNodePresetBySlot ("eq3", 3, read));

    ScenePreset sp;
    sp.id = "verse"; sp.name = "Verse"; sp.slot = 0;
    sp.nodes.push_back ({ "amp1", "bright", true });
    lib.writeScenePreset ("song", sp);
    ScenePreset spRead;
    std::printf ("cache: scene preset %d by-slot %d missing %d\n", (int) (lib.readScenePreset ("song", "verse", spRead) && spRead.nodes.size() == 1),
                 (int) lib.readScenePresetBySlot ("song", 0, spRead), (int) ! lib.readScenePreset ("song", "chorus", spRead));

    dir.deleteRecursively();
}
