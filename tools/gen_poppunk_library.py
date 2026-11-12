import json, os, re, shutil, sys

OUT = '/tmp/popunk/RigBench'   # output folder; copy its contents into Documents\RigBench
if os.path.exists('/tmp/popunk'): shutil.rmtree('/tmp/popunk')

def slug(name):
    out=''; lastdash=True
    for c in name.lower():
        if ('a'<=c<='z') or ('0'<=c<='9'):
            out+=c; lastdash=False
        elif not lastdash:
            out+='-'; lastdash=True
    out=out.rstrip('-')
    return out or 'preset'

M='NAM/Amps/Mesa/'
AMPS = {
 # key: (label, file, node)
 'clean_blues': ('Rect Clean - Clean-Blues 1 (20W)', M+'[AMP] Mes.BADLND-S 020W BOLD CLEAN Clean-Blues #01 - DI.nam', 'ampclean'),
 'clean_pushed':('Rect Clean - Clean-Pushed 2', M+'[AMP] Mes.BADLND-S 100W BOLD CLEAN Clean-Pushed #02 - DI.nam', 'ampclean'),
 'clean_noon':  ('Rect Clean - Noon 1', M+'[AMP] Mes.BADLND-S 100W BOLD CLEAN Noon #01 - DI.nam', 'ampclean'),
 'markv_clean': ('Mark V Clean (Ch1)', 'NAM/Amps/Mesa/Mesa Boogie - Mark V (Ch 1 Clean) CLN [18dBu].nam', 'ampclean'),
 'crunch_rock': ('Rect Crunch - Rock-Crunch 2 (variac)', M+'[AMP] Mes.BADLND-S 100W VARC CRNCH Rock-Crunch #02 - DI.nam', 'ampcrunch'),
 'crunch_lead': ('Rect Crunch - Crunch-Lead 1', M+'[AMP] Mes.BADLND-S 100W BOLD CRNCH Crunch-Lead #01 - DI.nam', 'ampcrunch'),
 'crunch_lead2':('Rect Crunch - Crunch-Lead 2', M+'[AMP] Mes.BADLND-S 100W BOLD CRNCH Crunch-Lead #02 - DI.nam', 'ampcrunch'),
 'crunch_noon': ('Rect Crunch - Noon 3', M+'[AMP] Mes.BADLND-S 100W BOLD CRNCH Noon #03 - DI.nam', 'ampcrunch'),
 'crunch_scoopy':('Rect Crunch - Scoopy Dew 1', M+'[AMP] Mes.BADLND-S 100W BOLD CRNCH Scoopy Dew #01 - DI.nam', 'ampcrunch'),
 'jcm800':      ('Marshall JCM800 2203', 'NAM/Amps/Marshall/JCM800 2203 - P5 B5 M5 T5 MV5 G9 - AZG - 700.nam', 'ampcrunch'),
 'twin':('Twin Reverb N 555 DI','NAM/Amps/Fender/73 Twin Reverb N 555 DI.nam','ampclean'),
 'ac30_bright':('AC30 Top Boost V3 bright','NAM/Amps/AC30/SLAMMIN_VOX_AC30_TB_V3_TC0_B4_T7_BRIGHT_S.nam','ampclean'),
 'ac30_noon':('AC30 Top Boost V7 noon','NAM/Amps/AC30/SLAMMIN_VOX_AC30_TB_V7_TC0_B5_T5_NOON_S.nam','ampcrunch'),
 'acoustic':('Acoustic Sim (Take 1)','NAM/Amps/Acoustic/Acoustic Sim (Take 1).nam','ampclean'),
 'crush_astro': ('Rect Crush - Astro Horsey 1', M+'[AMP] Mes.BADLND-S 100W BOLD CRUSH Astro Horsey #01 - DI.nam', 'amphi'),
 'crush_astro2':('Rect Crush - Astro Horsey 2', M+'[AMP] Mes.BADLND-S 100W BOLD CRUSH Astro Horsey #02 - DI.nam', 'amphi'),
 'crush_astro_ts':('Rect Crush - Astro Horsey 1 TS', M+'[AMP] Mes.BADLND-S 100W BOLD CRUSH Astro Horsey #01 TS - DI.nam', 'amphi'),
 'crush_noon':  ('Rect Crush - Noon 5', M+'[AMP] Mes.BADLND-S 100W BOLD CRUSH Noon #05 - DI.nam', 'amphi'),
 'crush_field': ('Rect Crush - Field of Man 1', M+'[AMP] Mes.BADLND-S 100W BOLD CRUSH Field of Man #01 - DI.nam', 'amphi'),
 'crush_solo':  ('Rect Crush - Heavy-Solo 2', M+'[AMP] Mes.BADLND-S 100W BOLD CRUSH Heavy-Solo #02 - DI.nam', 'amphi'),
 'crush_murder':('Rect Crush - Murder Tones 1', M+'[AMP] Mes.BADLND-S 100W BOLD CRUSH Murder Tones #01 - DI.nam', 'amphi'),
}
PEDALS = {
 'sd1':    ('Boss SD-1 (drive 3)', 'NAM/Pedals/Boss-SD-1-Drive-III.nam'),
 'pickle': ('Way Huge Swollen Pickle (smooth lead)', 'NAM/Pedals/RT Swollen Pickle Smooth Lead.nam'),
 'ds1':    ('Boss DS-1', 'NAM/Pedals/Boss DS-1 - T_D_L - 3_7_5.nam'),
}
IRS = {
 'mesa':   ('Mesa V30 4x12 - 57', 'IR/TF MESA ST 4X12A V30 57 3 - Enhanced.wav'),
 'mesa421':('Mesa V30 4x12 - 421', 'IR/TF MESA ST 4X12A V30 421 U5 3 - Enhanced.wav'),
 'marsh':  ('Marshall G12M 4x12 - M160', 'IR/TF MARSH 4x12 G12M 8ohm M160 2 - Enhanced.wav'),
 'marsh545':('Marshall G12M 4x12 - 545', 'IR/TF MARSH 4x12 G12M 8ohm 545 3 - Enhanced.wav'),
 'orange': ('Orange V30 4x12 - 57', 'IR/TF ORANGE 4X12 V30 57 3 - Enhanced.wav'),
 'jcm900': ('Marshall JCM900 1960 - SM57', 'IR/Marshall JCM900 LEAD-1960 SM57.wav'),
 'ac30': ('AC30 2x12 Blue - 57','IR/TF 64 AC30 2X12 BLUE - 57 R121 70-30.wav'),
 'blue':   ('SM58 BL close (clean)', 'IR/SM58 BL center close.wav'),
}
EQ = {
 'Tight hi-gain':   (-3, 2, 1),
 'Scooped raw':     (1, -3, 3),
 'Mid push':        (-1, 4, 0),
 'Lead bright':     (-3, 1, 3),
 'Upper-mid focus': (-2, 1, 4),
 'Mid-heavy':       (-2, 3, 1),
 'Warm':            (1, 1, -2),
 'Clean sparkle':   (0, -1, 2),
 'Raw punk':        (0, 2, 2),
}
SHELF = {
 'Tame fizz':   (100, -3, 6500, -2),
 'Tighten':     (100, -3, 8000, 0),
}
COMP = {
 'Funk comp':     (-28, 8, 3, 80, 5),
 'Tight master':  (-20, 4, 10, 100, 3),
 'Gentle glue':   (-22, 2.5, 15, 150, 2),
 'Snappy':        (-26, 4, 5, 100, 3),
}
CHORUS = {'Chorus subtle': (0.8, 0.25, 0.25), 'Chorus wide': (0.5, 0.35, 0.3)}
REVERB = {'Room small': (0.2, 0.6, 0.1), 'Room': (0.3, 0.5, 0.15), 'Hall light': (0.45, 0.5, 0.15)}

def ms(bpm, mult): return round(60000.0/bpm*mult)

# ---- the songs ---------------------------------------------------------------------------------------
# Each part: dict. Defaults are filled in by P().
def P(name, amp, **kw):
    d = dict(name=name, amp=amp, extra=[], tgt=-18.0, inp=0.0, boost=None, gate=-55, comp=None, eq='Tight hi-gain',
             shelf=None, chorus=None, delay=None, reverb=None, ir='mesa', sy=None, synth=False, sylevel=-2.0, note='')
    d.update(kw); return d

SY_PC = {}
songs = []
def S(title, artist, bpm, guitar, role, parts, note=''):
    songs.append(dict(title=title, artist=artist, bpm=bpm, guitar=guitar, role=role, parts=parts, note=note))

LP='Les Paul (humbuckers)'; TL='Tele (P-90s)'

S('The Anthem','Good Charlotte',178,LP,'Lead',[
  P('Main','crush_astro2',boost='sd1',gate=-50,eq='Tight hi-gain',shelf='Tame fizz',ir='mesa'),
  P('Lead boost','crush_astro2',boost='sd1',gate=-50,tgt=-15,eq='Lead bright',shelf='Tame fizz',ir='mesa'),
])
S('The Rock Show','blink-182',193,TL,'Rhythm',[
  P('Main','crunch_scoopy',gate=-55,eq='Scooped raw',ir='mesa'),
], 'Raw and mid-scooped. The Tele gives the brightness; the Les Paul works if you add treble.')
S("Sugar, We're Goin Down",'Fall Out Boy',162,LP,'Rhythm',[
  P('Main','crush_astro',gate=-52,eq='Mid-heavy',shelf='Tighten',ir='mesa421'),
  P('Clean + chorus','twin',gate=-60,eq='Clean sparkle',chorus='Chorus subtle',ir='blue'),
], 'Drop D tuning is on the guitar, nothing to set in the rig.')
S('Sk8er Boi','Avril Lavigne',150,LP,'Lead (SY-200 most of the song)',[
  P('Guitar + SY','jcm800',gate=-52,eq='Tight hi-gain',ir='marsh',sy=10,sylevel=-3.0),
  P('Guitar only','jcm800',gate=-52,eq='Tight hi-gain',ir='marsh'),
  P('Lead boost','jcm800',gate=-52,tgt=-15,boost='sd1',eq='Lead bright',ir='marsh',sy=10,sylevel=-3.0),
], 'SY-200 patch 10 is a placeholder: set the real patch number on the MIDI output node preset.')
S("The Kids Aren't Alright",'The Offspring',200,LP,'Rhythm',[
  P('Main','crush_astro_ts',gate=-50,comp='Tight master',eq='Mid-heavy',shelf='Tighten',ir='mesa'),
])
S('Umbrella','All Time Low',174,LP,'Lead',[
  P('Main','crunch_lead',gate=-55,eq='Upper-mid focus',delay=(ms(174,1),0.25,0.15),ir='orange'),
  P('Lead boost','crunch_lead',gate=-55,tgt=-15,boost='sd1',eq='Lead bright',delay=(ms(174,1),0.25,0.2),ir='orange'),
])
S('Still into You','Paramore',136,TL,'Lead (SY-200 in pre-chorus and chorus)',[
  P('Verse','ac30_bright',gate=-60,comp='Snappy',eq='Clean sparkle',delay=(110,0.1,0.2),ir='ac30'),
  P('Pre-chorus + chorus + SY','ac30_noon',extra=[('ampcrunch','jcm800',-9.0)],gate=-58,comp='Snappy',eq='Clean sparkle',delay=(110,0.1,0.2),ir='ac30',sy=11,sylevel=-2.0),
  P('Lead boost','ac30_noon',gate=-58,tgt=-15,comp='Snappy',eq='Lead bright',delay=(110,0.1,0.2),ir='ac30'),
], 'AC30 Top Boost, with the JCM800 mixed under it in the pre-chorus and chorus.')
S("I Don't Care",'Fall Out Boy',134,LP,'Lead (SY-200 solo at the end)',[
  P('Main','crush_astro',gate=-52,eq='Tight hi-gain',shelf='Tame fizz',ir='mesa'),
  P('Solo + SY','crush_solo',boost='sd1',gate=-50,tgt=-15,eq='Lead bright',shelf='Tame fizz',ir='mesa',sy=12,sylevel=-1.0),
])
S('A Favor House Atlantic','Coheed and Cambria',182,LP,'Rhythm',[
  P('Main','crush_field',gate=-48,comp='Gentle glue',eq='Upper-mid focus',shelf='Tighten',ir='mesa421'),
])
S('Holiday','Green Day',147,TL,'Rhythm (quiet verse, aggressive chorus)',[
  P('Verse','crunch_rock',inp=-3.0,gate=-58,eq='Warm',reverb='Room small',ir='marsh'),
  P('Chorus','jcm800',boost='sd1',gate=-52,eq='Raw punk',reverb='Room small',ir='marsh'),
])
S('The Boys of Summer','The Ataris',160,LP,'Lead (delayed, boosted breakdown)',[
  P('Main','crush_astro',gate=-52,eq='Tight hi-gain',shelf='Tame fizz',delay=(ms(160,0.75),0.35,0.3),ir='mesa'),
  P('Breakdown boost','crush_astro',boost='sd1',gate=-52,tgt=-14.5,eq='Lead bright',shelf='Tame fizz',delay=(ms(160,0.75),0.2,0.05),ir='mesa'),
])
S('American Idiot','Green Day',186,TL,'Rhythm',[
  P('Main','crunch_rock',gate=-45,eq='Raw punk',ir='marsh545'),
], 'Fast gate so the stops are dead silent. No reverb or delay on purpose.')
S('Misery Business','Paramore',173,LP,'Lead',[
  P('Main','jcm800',boost='sd1',gate=-48,eq='Lead bright',shelf='Tame fizz',ir='mesa'),
  P('Solo','jcm800',boost='sd1',gate=-48,tgt=-15,eq='Lead bright',shelf='Tame fizz',delay=(ms(173,0.5),0.2,0.12),ir='mesa'),
])
S("You're Gonna Go Far, Kid",'The Offspring',126,LP,'Rhythm',[
  P('Main','crush_noon',gate=-52,comp='Tight master',eq='Tight hi-gain',shelf='Tighten',ir='mesa'),
])
S('Just a Girl','No Doubt',108,TL,'Lead (SY-200 except chorus, boosted solo)',[
  P('Verse + SY','ac30_bright',gate=-60,eq='Clean sparkle',ir='ac30',sy=13,sylevel=-2.0),
  P('Chorus (no SY)','crush_noon',gate=-52,eq='Tight hi-gain',ir='mesa'),
  P('Solo boost','ac30_noon',boost='sd1',gate=-55,tgt=-14,eq='Lead bright',ir='ac30'),
], 'The chorus drops the SY-200 completely and switches to the Rectifier.')
S('Dance, Dance','Fall Out Boy',115,LP,'Rhythm',[
  P('Main','crush_astro_ts',gate=-50,eq='Mid-heavy',shelf='Tighten',ir='mesa'),
  P('Chorus wide','crush_astro_ts',gate=-50,eq='Mid-heavy',shelf='Tighten',chorus='Chorus wide',ir='mesa'),
])
S('My Friends Over You','New Found Glory',165,LP,'Lead',[
  P('Main','crush_noon',boost='sd1',gate=-52,eq='Upper-mid focus',shelf='Tighten',ir='mesa'),
  P('Lead boost','crush_noon',boost='sd1',gate=-52,tgt=-15,eq='Lead bright',shelf='Tighten',ir='mesa'),
])
S("I'm Not Okay (I Promise)",'My Chemical Romance',179,LP,'Rhythm',[
  P('Main','jcm800',gate=-48,comp='Gentle glue',eq='Mid push',ir='marsh'),
])
S('The Middle','Jimmy Eat World',162,TL,'Rhythm',[
  P('Main','ac30_noon',inp=-3.0,gate=-60,comp='Gentle glue',eq='Warm',reverb='Room',ir='ac30'),
])
S('Since U Been Gone','Kelly Clarkson',131,LP,'Rhythm (verse notes through the VSTi)',[
  P('Verse - VSTi','crush_astro',synth=True,gate=-52,eq='Tight hi-gain',ir='mesa',note='Guitar amps are off; the instrument goes straight to FOH, skipping the cab IR.'),
  P('Chorus','crush_astro',gate=-50,eq='Tight hi-gain',shelf='Tame fizz',ir='mesa'),
], 'Load your instrument plugin into the "Synth (VSTi)" node and save its sound into the node preset.')
S('My Own Worst Enemy','Lit',104,TL,'Lead (not the solo)',[
  P('Main','crunch_lead2',gate=-55,eq='Upper-mid focus',ir='jcm900'),
  P('Intro (clean + chorus)','twin',gate=-60,eq='Clean sparkle',chorus='Chorus subtle',ir='blue'),
])
S('Dirty Little Secret','The All-American Rejects',144,TL,'Lead',[
  P('Main','ac30_noon',gate=-55,eq='Upper-mid focus',delay=(ms(144,0.5),0.2,0.15),ir='ac30'),
  P('Lead boost','ac30_noon',boost='sd1',gate=-55,tgt=-15,eq='Lead bright',delay=(ms(144,0.5),0.2,0.2),ir='ac30'),
])
S('Teenage Dirtbag','Wheatus',95,TL,'Rhythm (clean verse, heavy chorus)',[
  P('Verse (acoustic sim)','acoustic',gate=-62,comp='Gentle glue',eq='Clean sparkle',reverb='Room',ir='none'),
  P('Chorus (heavy)','jcm800',boost='pickle',gate=-50,eq='Raw punk',ir='marsh'),
], 'The verse uses the Acoustic Sim capture straight to the outputs (no cab IR).')
S("Ain't It Fun",'Paramore',104,TL,'Lead',[
  P('Funk clean','twin',gate=-62,comp='Funk comp',eq='Clean sparkle',delay=(ms(104,0.5),0.25,0.15),ir='ac30'),
  P('Crunch','ac30_noon',boost='sd1',gate=-55,comp='Snappy',eq='Lead bright',ir='ac30'),
  P('Lead boost','twin',gate=-60,tgt=-15,comp='Funk comp',eq='Lead bright',delay=(ms(104,0.5),0.25,0.15),ir='ac30'),
])


# ---- per-song scenes ------------------------------------------------------------------------------
def write(path, obj):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    json.dump(obj, open(path,'w'), indent=1)

node_presets = {}
slot_counter = {}
def node_preset(ntype, name, values, file=''):
    pid = slug(name); key=(ntype,pid)
    rec = {'version':1,'id':pid,'name':name,'nodeType':ntype,'values':values}
    if file: rec['file']=file
    if key in node_presets:
        old=node_presets[key]
        assert old['values']==values and old.get('file','')==file, f'clash {key}'
        return pid
    rec['slot']=slot_counter.get(ntype,0); slot_counter[ntype]=rec['slot']+1
    assert rec['slot']<=127
    node_presets[key]=rec
    return pid

def fmt(x): return '%g'%x
def amp_preset(key, tgt, inp):
    label,f,_ = AMPS[key]
    name = label + ('' if tgt==-18.0 else f' (target {fmt(tgt)})') + ('' if inp==0.0 else f' (in {inp:+g} dB)')
    return node_preset('amp', name, {'size':1,'match':1,'target':tgt,'input':inp,'calibrate':1,'passthrough':1}, 'lib:'+f)
def pedal_preset(key):
    label,f=PEDALS[key]
    return node_preset('amp', label, {'size':1,'match':0,'target':-18,'input':0,'calibrate':1,'passthrough':1}, 'lib:'+f)


NORM_AMP={'crush_astro2':'crush_astro','crush_astro_ts':'crush_astro','crush_noon':'crush_astro','crush_field':'crush_astro','crush_solo':'crush_astro','crush_murder':'crush_astro',
 'crunch_lead2':'crunch_lead','crunch_noon':'crunch_lead','crunch_scoopy':'crunch_lead','clean_noon':'twin','clean_blues':'twin'}
FAM={}
for t in ['The Anthem',"The Kids Aren't Alright",'A Favor House Atlantic','The Boys of Summer',"You're Gonna Go Far, Kid",'Dance, Dance','My Friends Over You',"Sugar, We're Goin Down"]: FAM[t]='Hi-Gain Rect'
for t in ['The Rock Show','Umbrella','My Own Worst Enemy']: FAM[t]='Crunch Rect'
for t in ['The Middle','Dirty Little Secret',"Ain't It Fun"]: FAM[t]='AC30 Pop'
for t in ['Holiday','American Idiot','Misery Business',"I'm Not Okay (I Promise)"]: FAM[t]='Marshall'
IRFIX={'Hi-Gain Rect':{'mesa421':'mesa'},'Crunch Rect':{'orange':'mesa','jcm900':'mesa','marsh':'mesa'},'Marshall':{'mesa':'marsh','marsh545':'marsh'}}
for s_ in songs:
    s_['scene']=FAM.get(s_['title'],s_['title'])
    for p in s_['parts']:
        for e in range(len(p['extra'])):
            n_,k_,db_=p['extra'][e]; p['extra'][e]=(n_,NORM_AMP.get(k_,k_),db_)
        p['amp']=NORM_AMP.get(p['amp'],p['amp'])
        p['ir']=IRFIX.get(s_['scene'],{}).get(p['ir'],p['ir'])
from collections import OrderedDict
GROUPS=OrderedDict()
for s_ in songs: GROUPS.setdefault(s_['scene'],[]).append(s_)
setlist_entries=[]
summary=[]
for gname,gsongs in GROUPS.items():
    parts=[p for x in gsongs for p in x['parts']]
    sid=slug(gname)
    slotn=0
    s={'title':gname}
    amp_keys=[]
    for p in parts:
        if p['synth']: continue
        for k in [p['amp']]+[e[1] for e in p['extra']]:
            if k not in amp_keys: amp_keys.append(k)
    amp_node={k:f'amp{i+1}' for i,k in enumerate(amp_keys)}
    boosts={p['boost'] for p in parts if p['boost']}; assert len(boosts)<=1
    boost=next(iter(boosts)) if boosts else None
    ir_keys=[]
    for p in parts:
        if not p['synth'] and p['ir']!='none' and p['ir'] not in ir_keys: ir_keys.append(p['ir'])
    ir_node={k:(f'ir{i+1}' if len(ir_keys)>1 else 'ir') for i,k in enumerate(ir_keys)}
    has_sy=any(p['sy'] is not None for p in parts)
    has_synth=any(p['synth'] for p in parts)
    use=lambda f: any(f(p) for p in parts)
    nodes=[]   # (id,type,name,props,file)
    def N(i,t,n,props=None,file=''): nodes.append((i,t,n,props or {},file))
    N('in1','input','Guitar in',{'channel':1,'interface':12})
    if has_sy: N('in2','input','SY-200 in',{'channel':2,'interface':12})
    N('gate','gate','Gate')
    if use(lambda p:p['comp']): N('comp','compressor','Compressor')
    if boost: N('boost','amp','Boost: '+PEDALS[boost][0],{},'lib:'+PEDALS[boost][1])
    for k in amp_keys: N(amp_node[k],'amp',AMPS[k][0],{},'lib:'+AMPS[k][1])
    N('eq','eq3','Tone EQ')
    if use(lambda p:p['shelf']): N('shelf','eqshelf','Shelf EQ')
    if use(lambda p:p['chorus']): N('chorus','chorus','Chorus')
    if use(lambda p:p['delay']): N('delay','delay','Delay')
    if use(lambda p:p['reverb']): N('reverb','reverb','Reverb')
    for k in ir_keys: N(ir_node[k],'ir','Cab: '+IRS[k][0],{},'lib:'+IRS[k][1])
    N('out1','output','FOH out',{'channel':1})
    N('out2','output','Stage out',{'channel':2})
    if has_synth: N('synth','vsti','Synth (VSTi)')
    if has_sy: N('sy','midiout','SY-200 patch')
    ids={n[0] for n in nodes}
    pre=[x for x in ['in1','gate','comp','boost'] if x in ids]
    post=[x for x in ['eq','shelf','chorus','delay','reverb'] if x in ids]
    edges=[]
    for a,b in zip(pre,pre[1:]): edges.append((a,b))
    for k in amp_keys: edges.append((pre[-1],amp_node[k]))
    for k in amp_keys: edges.append((amp_node[k],'eq'))
    for a,b in zip(post,post[1:]): edges.append((a,b))
    for k in ir_keys:
        edges.append((post[-1],ir_node[k])); edges.append((ir_node[k],'out1'))
    if any(p['ir']=='none' and not p['synth'] for p in parts): edges.append((post[-1],'out1'))
    edges.append((post[-1],'out2'))
    if has_sy: edges += [('in2','out1'),('in2','out2')]
    if has_synth: edges.append(('synth','out1'))
    for a,b in edges: assert a in ids and b in ids,(a,b)
    gn=[{'id':i,'type':t,'name':n,'active':False,'props':pr,**({'file':f} if f else {})} for i,t,n,pr,f in nodes]
    write(f'{OUT}/scenes/{sid}.scene.json',{'name':s['title'],'pc':-1,'graph':{'nodes':gn,'edges':[{'from':a,'to':b,'enabled':True} for a,b in edges]}})

    for song in gsongs:
      pids=[]
      for p in song['parts']:
        idx=slotn; slotn+=1; assert idx<128
        pname=f"{song['title']} - {p['name']}"; pid=slug(pname)
        L=[]
        def on(n,pre_=None): L.append({'node':n,'preset':pre_ or '','active':True})
        on('in1'); on('out1',node_preset('output','Output -10.5 dB',{'level':-10.5})); on('out2',node_preset('output','Output -10.5 dB',{'level':-10.5}))
        on('gate',node_preset('gate',f"Gate {p['gate']} dB",{'threshold':p['gate']}))
        if p['comp']: on('comp',node_preset('compressor',p['comp'],dict(zip(['threshold','ratio','attack','release','makeup'],COMP[p['comp']]))))
        if p['boost']: on('boost',pedal_preset(p['boost']))
        used={}
        if not p['synth']:
            used[p['amp']]=0.0
            for n_,k,db in p['extra']: used[k]=db
            for k,db in used.items(): on(amp_node[k],amp_preset(k,p['tgt'] if k==p['amp'] else -18.0,p['inp'] if k==p['amp'] else 0.0))
        on('eq',node_preset('eq3','EQ '+p['eq'],dict(zip(['bass','mid','treble'],EQ[p['eq']]))))
        if p['shelf']: on('shelf',node_preset('eqshelf','Shelf '+p['shelf'],dict(zip(['lowfreq','lowgain','highfreq','highgain'],SHELF[p['shelf']]))))
        if p['chorus']: on('chorus',node_preset('chorus',p['chorus'],dict(zip(['rate','depth','mix'],CHORUS[p['chorus']]))))
        if p['delay']:
            t,fb,mix=p['delay']; on('delay',node_preset('delay',f"Delay {t} ms fb {fb:g} mix {mix:g}",{'time':t,'feedback':fb,'mix':mix}))
        if p['reverb']: on('reverb',node_preset('reverb',p['reverb'],dict(zip(['size','damping','mix'],REVERB[p['reverb']]))))
        if not p['synth'] and p['ir']!='none':
            on(ir_node[p['ir']],node_preset('ir',IRS[p['ir']][0],{},'lib:'+IRS[p['ir']][1]))
        if p['synth']: on('synth',node_preset('vsti','Synth default',{'level':0}))
        if p['sy'] is not None: on('sy',node_preset('midiout',f"SY-200 patch {p['sy']} (placeholder)",{'channel':1,'rows':1,'t1':0,'a1':p['sy'],'v1':0}))
        E=[]
        for a,b in edges:
            en=True; lvl=0.0
            if a==pre[-1] and b in amp_node.values():
                k=[kk for kk,v in amp_node.items() if v==b][0]; en=k in used; lvl=used.get(k,0.0)
            elif a in amp_node.values():
                k=[kk for kk,v in amp_node.items() if v==a][0]; en=k in used
            elif a==post[-1] and b=='out1':
                en=(p['ir']=='none') and not p['synth']
            elif a==post[-1] and b in ir_node.values():
                en=(not p['synth']) and ir_node.get(p['ir'])==b
            elif b=='out1' and a in ir_node.values():
                en=(not p['synth']) and ir_node.get(p['ir'])==a
            elif a=='in2': en=p['sy'] is not None; lvl=p['sylevel']
            elif a=='synth': en=p['synth']
            E.append({'edge':f'{a}>{b}','enabled':en,'level':lvl})
        write(f'{OUT}/scenes/{sid}/presets/{pid}.preset.json',{'version':1,'id':pid,'name':pname,'slot':idx,'nodes':L,'edges':E})
        pids.append(pid)
      setlist_entries.append({'scene':sid,'preset':pids[0],'name':song['title'],'slots':(pids+['']*8)[:8]})

order={x['title']:i for i,x in enumerate(songs)}
setlist_entries.sort(key=lambda e:order[e['name']])
for (t,pid),rec in node_presets.items(): write(f'{OUT}/presets/{t}/{pid}.preset.json',rec)
write(f'{OUT}/setlists/pop-punk-set.setlist.json',{'version':1,'name':'Pop-Punk Set','entries':setlist_entries})
print(len(songs),'scenes',len(node_presets),'node presets')

# --- setlist labels and bottom-row slots
_g={x['title']:('HB' if 'Les Paul' in x['guitar'] else 'P90') for x in songs}
_f=f'{OUT}/setlists/pop-punk-set.setlist.json'
_d=json.load(open(_f))
for e in _d['entries']:
    ps=[x for x in e['slots'] if x]
    e['slots']=(['']*4+ps+['']*4)[:8]
    e['name']=f"{e['name']} [{_g[e['name']]}]"
json.dump(_d,open(_f,'w'),indent=1)
