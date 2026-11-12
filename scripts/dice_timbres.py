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
    "snare": ("ACOU SD",2,2,[0,0],[64,0,1,36,34,64,60,0],"hit",1,114,0,0,90),
    "snare808": ("808 SD",3,1,[0,0],[55,52,72,36,54,0,0,0],"hit",1,114,0,0,90),
    "snare909": ("909 SD",3,10,[0,0],[55,66,82,38,56,0,0,0],"hit",1,116,0,0,88),
    "clap": ("CLAP",3,3,[0,0],[62,64,68,54,60,0,0,0],"hit",1,115,0,0,85),
    "hat": ("CLOSEDHH",2,9,[0,0],[64,64,24,18,0,0,110,0],"hit",3,48,0,0,78),
    "hat909": ("909 HH",3,14,[0,0],[30,64,48,62,64,0,0,0],"hit",3,46,0,0,78),
    "openhat": ("OPEN HH",2,9,[0,0],[64,64,30,54,0,0,110,0],"hit",3,44,0,0,74),
    "open909": ("909 OH",3,14,[0,0],[85,64,64,62,64,0,0,0],"hit",3,44,0,0,74),
    "tom": ("HANDPERC",3,2,[0,0],[60,38,22,34,52,0,0,0],"hit",1,104,0,0,84),
    "rim": ("WOOD RIM",3,4,[0,0],[55,40,36,64,0,0,0,0],"hit",1,108,0,0,82),
    "metal": ("METAL",2,3,[0,0],[64,32,0,32,78,58,62,0],"hit",1,112,8,0,72),
    "noise": ("TEXTURE",0,0,[3,0],[0,0,0,64,0,0,0,0],"texture",1,100,6,0,55),
    "sub": ("SUB",4,0,[4,4],[0,0,16,64,0,0,0,0],"bass",1,62,0,0,94),
    "bass": ("ANALOGBS",4,0,[0,1],[0,0,36,64,0,0,0,0],"bass",4,66,12,22,82),
    "acid": ("303",4,0,[0,1],[0,0,10,64,0,0,0,0],"pluck",4,67,72,38,72),
    "reese": ("REESE",4,0,[0,0],[0,0,64,76,0,0,0,0],"bass",1,72,16,12,70),
    "wobble": ("WOBBLE",4,0,[1,0],[36,0,44,64,0,0,0,0],"bass",4,64,28,12,72),
    "bass808": ("808 SUB",4,0,[4,2],[0,0,20,64,0,0,0,0],"bass",1,54,0,0,88),
    "pluckbass": ("PLUCK BS",4,0,[2,1],[0,0,24,64,0,0,0,0],"pluck",1,68,12,30,84),
    "stab": ("STAB",4,0,[2,0],[0,0,44,66,0,0,0,0],"stab",1,72,6,10,68),
    "epiano": ("E PIANO",4,0,[4,2],[0,0,28,64,0,0,0,0],"keys",1,98,0,0,76),
    "fmkeys": ("FM KEYS",2,8,[0,0],[64,24,1,48,24,70,50,0],"keys",1,108,0,0,74),
    "organ": ("ORGAN",4,0,[4,4],[0,0,48,64,0,0,0,0],"keys",1,100,0,0,72),
    "pad": ("WARM PAD",4,0,[2,0],[0,0,38,68,0,0,0,0],"pad",1,86,6,8,64),
    "drone": ("DRONE",4,0,[2,4],[0,0,46,67,0,0,0,0],"pad",1,74,10,0,60),
    "lead": ("SAW LEAD",4,0,[0,0],[0,0,42,67,0,0,0,0],"lead",1,102,10,16,74),
    "pulse": ("PULSE",4,0,[1,2],[44,0,32,64,0,0,0,0],"lead",1,96,8,14,74),
    "pluck": ("PLUCK",4,0,[2,4],[0,0,32,64,0,0,0,0],"pluck",1,106,4,12,76),
    "bell": ("FM BELL",2,7,[0,0],[64,40,0,58,68,36,72,0],"keys",1,116,0,0,80),
    "guitar": ("ACOU GTR",8,0,[0,0],[82,50,8,36,12,0,0,0],"string",1,110,0,0,88),
    "mallet": ("MALLET",8,1,[0,0],[80,62,60,24,10,0,0,0],"string",1,114,0,0,82),
    "vox": ("VOX FORM",4,0,[0,1],[0,0,30,65,0,0,0,0],"pad",7,84,40,0,60),
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
    rows["bass"] = [0,70+d//5,92,35+d//6]
    rows["pluck"] = [0,54+d//4,0,35+d//5]
    rows["stab"] = [16,44+d//5,0,24+d//8]
    rows["keys"] = [0,60+d//4,38,40+d//4]
    rows["pad"] = [58+d//8,86,96,66+d//5]
    rows["lead"] = [0,65+d//5,76,38+d//5]
    rows["string"] = [0,40,127,42+d//4]
    rows["texture"] = [8,62+d//4,0,40+d//4]
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
    highpass = cut > 0 and recipe["type"] in (0,2,3)
    mode = 3 if highpass else recipe["filter"]
    cutoff = 14+cut+bright if highpass else recipe["cutoff"]+cut//3+bright
    limit = {"acid":90,"stab":24}.get(voice["character"]["family"],56)
    resonance = bounded(max(recipe["resonance"],voice["res"]),0,limit)
    return mode,bounded(cutoff),resonance


def build(voice):
    char = voice["character"]
    recipe = architecture(char["family"])
    recipe["params"] = parameters(recipe,char,voice["decay"])
    recipe["env"] = envelope(recipe.pop("profile"),voice["decay"])
    recipe["filter"],recipe["cutoff"],recipe["resonance"] = filtering(recipe,voice)
    recipe["drive"] = bounded(char["grit"]*6,0,24)
    recipe["sub"] = {"sub":70,"bass":42,"acid":18,"reese":26,"wobble":30,"bass808":75}.get(char["family"],0)
    recipe["noise"] = 0
    recipe["semi"] = 12 if char["family"] == "organ" else 0
    recipe["glide"] = 4 if char["family"] in ("acid","bass808") else 0
    recipe["fdec"] = bounded(42+bounded(voice["decay"])//4)
    recipe["chord"] = CHORDS[char.get("chord","none")]
    if recipe["chord"]:
        recipe["level"] = recipe["level"] * (73 if recipe["chord"] == 1 else 64) // 128
    recipe["lfo"] = {"wobble":[7,0,4,18],"reese":[7,0,9,6],"pad":[32,0,10,4]}.get(char["family"],[0,0,0,0])
    return recipe
