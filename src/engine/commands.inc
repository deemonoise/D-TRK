// Shared transport and edit commands, driven by the platform clock.
void handle(const Command& c) {
  const uint64_t now = nowUs();
  switch (c.cmd) {
    case Cmd::StartStop:
      if (practice.counting()) { practice.stop(); break; }
      if (seq->playing()) {
        seq->stop(now, midi);
        practice.stop();
      } else if (practice.armed() && practicePrefs.countBars && !seq->paused()) {
        practice.startCount(now, seq->startTempo(), practicePrefs.countBars);
      } else {
        seq->setFollowSalt(freshSeed());
        seq->start(now, midi);
        practice.started(seq->heardPattern(), seq->heardLoop());
      }
      break;
    case Cmd::TogglePlay:
      if (practice.counting()) { practice.stop(); break; }
      if (!seq->playing() && !seq->paused()) seq->setFollowSalt(freshSeed());
      seq->togglePlay(now, midi);
      if (seq->playing()) practice.started(seq->heardPattern(), seq->heardLoop());
      else practice.stop();
      break;
    case Cmd::Stop: practice.stop(); seq->stop(now, midi); break;
    case Cmd::QueuePattern: seq->queuePattern(c.arg); break;
    case Cmd::SelectPattern: seq->selectPattern(c.arg); break;
    case Cmd::SetBpm: seq->setBpm(c.arg); break;
    case Cmd::SetLiveBpm: seq->setLiveBpm(c.arg); break;
    case Cmd::SendProgram: seq->sendProgram(now, c.arg, midi); break;
    case Cmd::ReleaseTies: seq->releaseTies(now, midi); break;
    case Cmd::TrackOut: seq->trackOutChanged(now, c.arg, midi); break;
    case Cmd::SongEdit: seq->songEdited(now, midi, c.arg >> 8, static_cast<mt::SongOp>(c.arg & 0xFF)); break;
    case Cmd::SetSong: seq->setSong(now, midi, c.arg >> 15, c.arg & 0xFF); break;
    case Cmd::Fill: seq->setFill(c.arg != 0); break;
    case Cmd::GenEdit: seq->genEdited(now, midi); break;
    case Cmd::GenReseed: seq->genReseed(now, midi, c.arg); break;
    case Cmd::MutEdit: seq->mutEdited(now, midi); break;
    case Cmd::LiveNote: seq->liveNote(now, c.arg >> 8, c.arg & 0xFF, c.arg2, midi); break;
    case Cmd::LiveAllOff: seq->liveAllOff(now, midi); break;
    case Cmd::MidiPanic: seq->midiPanic(now, midi); break;
    case Cmd::AutomationMute: seq->setAutomationMute(now, c.arg, midi); break;
    case Cmd::PracticeSettings: practicePrefs = mt::practicePrefs(static_cast<uint8_t>(c.arg), c.arg2); break;
    case Cmd::PracticeArm:
      practice.arm(c.arg != 0, seq->playing(), seq->heardPattern(), seq->heardLoop());
      break;
  }
}
