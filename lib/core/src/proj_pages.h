#pragma once
namespace mt {
// First six IDs are persisted in MIDI bindings. PATTERN is a new display page; its
// bindings keep MAIN's old ID, where Length/Resolution/Swing/Groove originally lived.
enum ProjectPageId { kProjectMain, kProjectDelay, kProjectReverb, kProjectMod, kProjectMaster, kProjectPerf,
                     kProjectPattern, kProjectPageCount };
struct ProjectPageRows { int first, count; };
constexpr int projectPageAt(int physical) {
 constexpr int order[]={kProjectMain,kProjectPattern,kProjectDelay,kProjectReverb,kProjectMod,kProjectMaster,kProjectPerf};
 return order[(physical%kProjectPageCount+kProjectPageCount)%kProjectPageCount];
}
constexpr int projectPagePosition(int logical) {
 for(int i=0;i<kProjectPageCount;++i)if(projectPageAt(i)==logical)return i;
 return 0;
}
constexpr int projectBindingPage(int logical) { return logical==kProjectPattern?kProjectMain:logical; }
constexpr ProjectPageRows projectPageRows(int logical,const int (&legacyFirst)[7],int mainSplit,bool binding=false) {
 if(logical<0 || logical>=kProjectPageCount || mainSplit<legacyFirst[0] || mainSplit>legacyFirst[1])return {0,0};
 if(logical==kProjectPattern)return {mainSplit,legacyFirst[1]-mainSplit};
 if(logical==kProjectMain && !binding)return {legacyFirst[0],mainSplit-legacyFirst[0]};
 return {legacyFirst[logical],legacyFirst[logical+1]-legacyFirst[logical]};
}
}
