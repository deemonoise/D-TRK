"""Build explicit synthesis recipes from normalized Genre Lab sound characters.

Numbers are our engine-specific interpretation of the guide's sample descriptions.
No factory preset names or factory lookup participate in this conversion.
"""
import copy

# name,type,machine,oscillators,8 engine parameters,amp profile,filter,cut,res,env,level
FAMILIES = {
    "kick": ("ROUND BD",2,0,[0,0],[64,18,1,12,42,24,56,0],"hit",1,108,0,0,100),
    "kick808": ("808 BD",3,0,[0,0],[64,20,12,36,50,0,0,0],"hit",1,105,0,0,100),
    "kick909": ("909 BD",3,9,[0,0],[64,66,36,54,42,0,0,0],"hit",1,112,0,0,96),
    "snare": ("ACOU SD",2,1,[0,0],[57,20,1,38,40,26,14,0],"hit",1,114,0,0,90),
    "snare808": ("808 SD",3,1,[0,0],[55,52,72,36,54,0,0,0],"hit",1,114,0,0,90),
    "snare909": ("909 SD",3,10,[0,0],[55,66,82,38,56,0,0,0],"hit",1,116,0,0,88),
    "clap": ("CLAP",3,3,[0,0],[62,64,68,54,60,0,0,0],"hit",1,115,0,0,85),
    "hat": ("CLOSEDHH",2,9,[0,0],[64,78,16,4,0,0,1,0],"hit",3,48,0,0,78),
    "hat909": ("909 HH",3,14,[0,0],[30,64,48,62,64,0,0,0],"hit",3,46,0,0,78),
    "openhat": ("OPEN HH",2,9,[0,0],[64,72,24,26,0,0,1,0],"hit",3,44,0,0,74),
    "open909": ("909 OH",3,14,[0,0],[85,64,64,62,64,0,0,0],"hit",3,44,0,0,74),
    "tom": ("HANDPERC",3,2,[0,0],[60,38,22,34,52,0,0,0],"hit",1,104,0,0,84),
    "rim": ("WOOD RIM",3,4,[0,0],[55,40,36,64,0,0,0,0],"hit",1,108,0,0,82),
    "metal": ("METAL",2,3,[0,0],[64,32,0,32,78,58,62,0],"hit",1,112,8,0,72),
    # FX rows keep their syncopation. A damped 909 rim has a pitched body
    # and a transient instead of sustaining sample noise through every hit.
    "noise": ("SOFT RIM",3,13,[0,0],[40,36,3,30,0,0,0,0],"hit",1,90,0,0,57),
    # SYNTH slots: SHP1, SHP2, MIX, DET, SENV (64 neutral), envelope destination,
    # modulation decay and oscillator-2 start phase. MIX / RING / FOLD move even
    # with sine, triangle and saw oscillators, whose SHAPE controls are inactive.
    "sub": ("SUB",4,0,[4,4],[0,0,19,64,80,3,20,8],"bass",1,67,0,4,94),
    "bass": ("ANALOGBS",4,0,[0,1],[0,43,31,69,87,3,30,21],"bass",4,55,21,31,82),
    "acid": ("303",4,0,[0,1],[0,31,12,64,77,3,20,9],"pluck",4,57,73,40,72),
    "reese": ("REESE",4,0,[0,0],[0,0,52,86,75,1,42,28],"bass",1,78,17,13,78),
    "wobble": ("WOBBLE",4,0,[1,0],[42,0,41,74,76,1,32,22],"bass",4,57,30,18,72),
    "bass808": ("808 SUB",4,0,[4,2],[0,0,12,64,82,1,22,0],"bass",1,61,0,4,88),
    "pluckbass": ("PLUCK BS",4,0,[2,1],[0,38,26,64,88,3,23,12],"pluck",1,61,14,27,84),
    "stab": ("STAB",4,0,[2,0],[0,0,24,74,91,3,24,19],"stab",4,64,17,12,68),
    "epiano": ("E PIANO",4,0,[4,4],[0,0,14,66,96,2,27,18],"keys",1,84,4,8,76),
    "fmkeys": ("FM KEYS",2,8,[0,0],[64,40,0,30,18,73,46,0],"keys",1,97,0,8,74),
    "organ": ("ORGAN",4,0,[4,2],[0,0,47,66,82,3,17,32],"keys",1,85,2,5,72),
    "pad": ("WARM PAD",4,0,[2,1],[0,49,44,79,84,0,56,37],"pad",1,75,14,12,64),
    "drone": ("DRONE",4,0,[2,0],[0,0,32,75,75,1,61,29],"pad",4,61,14,6,60),
    "lead": ("SAW LEAD",4,0,[0,0],[0,0,35,78,83,3,36,26],"lead",1,78,17,19,74),
    "pulse": ("PULSE",4,0,[1,2],[32,0,27,70,84,0,31,16],"lead",4,76,15,19,74),
    "pluck": ("PLUCK",4,0,[2,4],[0,0,17,66,80,1,19,11],"pluck",1,78,9,18,76),
    "bell": ("FM BELL",2,7,[0,0],[64,77,0,45,36,55,87,0],"keys",1,97,3,7,80),
    "guitar": ("ACOU GTR",8,0,[0,0],[82,43,15,24,4,0,0,0],"string",1,95,4,8,88),
    "mallet": ("MALLET",8,1,[0,0],[80,48,67,32,3,0,0,0],"string",1,94,4,5,82),
    "vox": ("VOX FORM",4,0,[0,1],[0,34,37,70,77,3,47,27],"pad",7,79,32,4,60),
}
CHORDS = {"none":0,"triad":1,"seventh":2,"major7":3,"minor7":4,"major9":5,"minor9":6}


def bounded(value, lo=0, hi=127):
    return max(lo, min(hi, int(value)))


def architecture(family):
    name, kind, machine, osc, params, envelope, filt, cut, res, fenv, level = FAMILIES[family]
    return dict(name=name,type=kind,machine=machine,osc=copy.copy(osc),params=copy.copy(params),
                profile=envelope,filter=filt,cutoff=cut,resonance=res,fenv=fenv,level=level)


def envelope(profile, decay):
    d = bounded(decay)
    rows = {"hit":[0,127,0,20]}
    rows["bass"] = [4,39+d//6,82,21+d//8]
    rows["pluck"] = [6,28+d//5,9,27+d//8]
    rows["stab"] = [13,35+d//6,13,23+d//10]
    rows["keys"] = [8,44+d//5,41,36+d//8]
    rows["pad"] = [46+d//9,65,86,56+d//8]
    rows["lead"] = [15,43+d//6,70,32+d//8]
    rows["string"] = [0,40,127,42+d//4]
    return [bounded(value) for value in rows[profile]]


def parameters(recipe, character, decay):
    values = recipe["params"]
    kind = recipe["type"]
    tone = bounded(character["tone"],-2,2)
    if kind == 2:
        values[3] = bounded(values[3] + bounded(decay)//5)
        values[5] = bounded(values[5] + tone*5,0,100)
        values[7] = bounded(character["grit"]*6,0,18)
    elif kind == 3:
        values[0] = bounded(values[0] - 12 + bounded(decay)//3)
        values[1] = bounded(values[1] + tone*6)
    elif kind == 8:
        values[0] = bounded(56 + bounded(decay)//3)
        values[1] = bounded(values[1] + tone*6)
    return values


def filtering(recipe, voice):
    cut = bounded(voice["cut"],-64,63)
    bright = bounded(voice["character"]["tone"],-2,2)*4
    highpass = cut > 0 and recipe["profile"] == "hit"
    mode = 3 if highpass else recipe["filter"]
    cutoff = 14+cut+bright if highpass else recipe["cutoff"]+cut//3+bright
    limit = {"acid":90,"stab":24}.get(voice["character"]["family"],56)
    resonance = bounded(max(recipe["resonance"],voice["res"]),0,limit)
    return mode,bounded(cutoff),resonance


def build(voice):
    char = voice["character"]
    recipe = architecture(char["family"])
    recipe["params"] = parameters(recipe,char,voice["decay"])
    recipe["filter"],recipe["cutoff"],recipe["resonance"] = filtering(recipe,voice)
    recipe["env"] = envelope(recipe.pop("profile"),voice["decay"])
    recipe["drive"] = bounded(char["grit"]*6,0,24)
    recipe["sub"] = {"sub":70,"bass":42,"acid":18,"reese":26,"wobble":30,"bass808":75}.get(char["family"],0)
    recipe["noise"] = 0
    recipe["semi"] = {"sub":12,"organ":12,"epiano":19,"pluck":12,"lead":12}.get(char["family"],0)
    recipe["glide"] = 4 if char["family"] in ("acid","bass808") else 0
    recipe["fdec"] = bounded(42+bounded(voice["decay"])//4)
    recipe["chord"] = CHORDS[char.get("chord","none")]
    if recipe["chord"]:
        recipe["level"] = recipe["level"] * (73 if recipe["chord"] == 1 else 64) // 128
    recipe["lfo"] = {"wobble":[7,0,4,18],"reese":[7,0,9,6],"pad":[32,0,10,4]}.get(char["family"],[0,0,0,0])
    return recipe
