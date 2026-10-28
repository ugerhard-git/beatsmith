#include "Engine/BeatsmithEngine.h"

namespace beatsmith
{

void BeatsmithEngine::process (const BlockContext& context, NoteEventList& output)
{
    // Phase 0: no pattern player yet, so nothing is produced.
    (void) context;
    output.clear();
}

void BeatsmithEngine::releaseAllNotes (NoteEventList& output)
{
    // Phase 0: the engine never starts a note, so there is nothing to end.
    output.clear();
}

} // namespace beatsmith
