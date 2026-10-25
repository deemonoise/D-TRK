#pragma once
#include <stdint.h>
#include "bank_client.h"
#include "link_msg.h"

// Recording on the synth board (Rec* in link_msg.h) from the REC window (src/ui/rec_dialog.*). UI task,
// blocking: the short ones up to 3 x 150 ms, recStop until the take is in the store (Progress
// meanwhile). They work while the song plays. A link failure is RecResult::BadArg with ok false.
namespace audio {

using RecResult = mt::link::RecResult;

// Arms a take; number: the first n free in the project in, the n of the take's file out.
RecResult recStart(const mt::link::RecStartReq& q, uint16_t& number, bool* link = nullptr);
// Gain / MON and the source the meter follows; rep (optional) gets the board's state.
RecResult recSet(int8_t gain, bool mon, uint8_t source, mt::link::RecLevelRep* rep = nullptr, bool* link = nullptr);
// The meter since the last poll and the take's state; also the keepalive. False: no answer.
bool recLevel(mt::link::RecLevelRep& rep);
// Ends the take: sealed, imported into the store; cb gets done / total meanwhile.
RecResult recStop(mt::link::RecStopRep& rep, BankProgressFn cb = nullptr, void* ctx = nullptr, bool* link = nullptr);
// Drops the take and its file.
RecResult recCancel();

}  // namespace audio
