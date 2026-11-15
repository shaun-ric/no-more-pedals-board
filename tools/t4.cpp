// Library file handling: a scene file that is not valid JSON is never written over, a valid one keeps
// the fields an edit does not touch, and stored file references compare as Windows would.
#include "Core/Library.h"
#include <cstdio>

int main()
{
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("rigbench_t4");
    dir.deleteRecursively();
    dir.createDirectory();

    auto addMidi = [] (juce::DynamicObject& o) { o.setProperty ("midi", 1); };

    // A typo left while editing by hand: the write is refused and the file is untouched.
    const juce::String brokenText = "{ \"name\": \"Song\", \"pc\": 3, ";
    const auto broken = dir.getChildFile ("broken.scene.json");
    broken.replaceWithText (brokenText);
    const bool brokenWritten = Library::updateJsonFile (broken, addMidi);
    std::printf ("broken: written %d unchanged %d readable %d\n", (int) brokenWritten,
                 (int) (broken.loadFileAsString() == brokenText), (int) Library::isJsonObjectFile (broken));

    // A valid file: the edit is added and the other fields are kept.
    const auto good = dir.getChildFile ("good.scene.json");
    good.replaceWithText ("{\"name\":\"Song\",\"pc\":3}");
    const bool goodWritten = Library::updateJsonFile (good, addMidi);
    const auto v = juce::JSON::parse (good);
    std::printf ("good: written %d kept %d added %d\n", (int) goodWritten,
                 (int) (v.getProperty ("name", {}).toString() == "Song" && (int) v.getProperty ("pc", 0) == 3),
                 (int) ((int) v.getProperty ("midi", 0) == 1));

    // A file that does not exist yet is created.
    const auto fresh = dir.getChildFile ("new.scene.json");
    const bool freshWritten = Library::updateJsonFile (fresh, addMidi);
    std::printf ("new: written %d readable %d\n", (int) freshWritten, (int) Library::isJsonObjectFile (fresh));

    // File references: case and slash direction do not matter; different files and empty vs set do.
    std::printf ("same: %d %d %d %d\n",
                 (int) Library::sameFileRef ("lib:NAM/Amps/a.nam", "lib:nam\\amps\\A.NAM"),
                 (int) Library::sameFileRef ("lib:NAM/a.nam", "lib:NAM/b.nam"),
                 (int) Library::sameFileRef ("", ""),
                 (int) Library::sameFileRef ("", "lib:NAM/a.nam"));

    dir.deleteRecursively();
}
