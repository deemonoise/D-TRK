#pragma once
#include <stdint.h>
#include "bank_client.h"
#include "link_msg.h"

// Recording on the synth board (Rec* in link_msg.h) from the REC window (src/ui/rec_dialog.*). UI task,
// blocking: the short ones up to 3 x 150 ms, recStop until the take is in the store (Progress
// meanwhile). They work while the song plays. A link failure is RecResult::BadArg with ok false.
namespace audio {

using RecResult = mt::link::RecResult;

// Arms a take; number: the first n free in the project in, the n of the take's file out. q.toSd: REC -> SD
// (only the card's file, no store; ends on its own at 4 GB or the card's free space; CardFull when
// under a second fits).
RecResult recStart(const mt::link::RecStartReq& q, uint16_t& number, bool* link = nullptr);
// REC -> SD from MIX: recStart with toSd and the first free number on the card.
RecResult recStartSd(uint8_t source, int8_t gain, int8_t thresh, bool mon, uint16_t& number, bool* link = nullptr);
// Gain / MON and the source the meter follows; rep (optional) gets the board's state.
RecResult recSet(int8_t gain, bool mon, uint8_t source, mt::link::RecLevelRep* rep = nullptr, bool* link = nullptr);
// The meter since the last poll and the take's state; also the keepalive (2 s without one: the board
// ends the take into its file). rep.left: frames before the take ends on its own (/ mt::kRecRate =
// seconds left: 5 min, the 4 GB or the card's space). State Saving without a recStop: it ended on its
// own (or the card failed): call recStop for the reply. False: no answer.
bool recLevel(mt::link::RecLevelRep& rep);
// Ends the take: sealed, imported into the store (toSd: only sealed; rep.frames / number, bank / crc 0);
// cb gets done / total meanwhile. rep.ended (mt::link::RecEnd): Stop, Length or Space.
RecResult recStop(mt::link::RecStopRep& rep, BankProgressFn cb = nullptr, void* ctx = nullptr, bool* link = nullptr);
// Drops the take and its file.
RecResult recCancel();

}  // namespace audio
