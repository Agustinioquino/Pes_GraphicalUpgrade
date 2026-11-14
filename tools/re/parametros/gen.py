import pickle,re,collections,struct,sys
sys.path.insert(0,'.')
import arc,gloss,manual,model,fields
D=pickle.load(open('data.pkl','rb'))
R=pickle.load(open('json_runs.pkl','rb')); B=pickle.load(open('bin_runs.pkl','rb'))
use,calls,wrap=pickle.load(open('consumers.pkl','rb'))
E=arc.all_entries()
strs=[l.rstrip('\n').split('\t',1) for l in open('/home/claude/re/strings_idata.txt') if 'common/match/constant/' in l]
jsonp={}
for a,s in strs:
    m=re.search(r'/([^/]+)\.json$',s)
    if m: jsonp.setdefault(m.group(1),s)
import table
ids={}
for i,p,fa,_c,_o in table.ents:
    ids[p.split('/')[-1][:-5]]=i
def lf(c): return c[0].lower()+c[1:]
cls=[l.split()[0].split('::')[1] for l in open('classes.txt')]
vt={l.split()[0].split('::')[1]:l.split() for l in open('classes.txt')}
rtti={l.split()[2].split('::')[1]:l.split()[1] for l in open('/home/claude/re/rtti.txt') if 'constant::' in l}
def tipo(r):
    t=r['type']
    if t=='float': return 'float32' if r['size']==4 else 'float->%sB'%r['size']
    if t=='int': return 'int32' if r['size']==4 else 'int%d'%(8*r['size'])
    if t=='bool': return 'bool(u8)'
    if t=='string': return 'std::string(0x20)' if r['size']=='str' else 'cadena->%s'%r['size']
def meaning(c,r):
    m=manual.M.get((c,r['pat']))
    g=gloss.gloss(r['path'])
    return (m+' | por nombre: '+g) if m else 'por nombre: '+g
hdr=['clase','clave','offset','tipo','default','significado_inferido','archivo_json','archivo_bin','entrada_o','offset_en_o','offset_en_bin_descomprimido','id_constante','lector_json(vf1)','lector_bin(vf2)','valor_verificado','usado_por(heuristico)','nota']
out=open('/home/claude/re/out/constant_gameplay.tsv','w'); out.write('\t'.join(hdr)+'\n')
summ=open('/home/claude/re/out/constant_gameplay_resumen.tsv','w')
summ.write('\t'.join(['clase','clave_patron','offset_primero','n_elementos','tipo','valores(default)','significado_inferido','archivo_json','archivo_bin/.o'])+'\n')
stats={}
for c in cls:
    if c not in D: continue
    d=D[c]; jn=lf(c); jp=jsonp.get(jn,''); i=ids.get(jn,'')
    o=d['o']; arch=d['arch']
    aoff=arc.OFFS.get((arch,o)) if o else None
    verified = c in B and not B[c]['err']
    u=use.get(i,{}) if i!='' else {}
    for r in d['rows']:
        cons=sorted(u.get(r['off'],[]))
        note=r['note']
        if r['type']=='int' and isinstance(r['val'],int) and r['val'] in (0x3f800000,0x40000000,0x3f000000): note=(note+'; ' if note else '')+'el .bin trae bits de float (%g) en campo int'%struct.unpack('<f',struct.pack('<i',r['val']))[0]
        row=[c,r['key'],'0x%x'%r['off'] if r['off'] is not None else '',tipo(r),r['vs'].replace('\t',' ').replace('\n',' '),meaning(c,r),jp,arch or '',o or '',
             '0x%x'%r['binpos'] if r['binpos'] is not None else '', '0x%x'%(aoff+r['binpos']) if (aoff is not None and r['binpos'] is not None) else '',
             str(i),'0x'+vt[c][2],'0x'+vt[c][3],'emulado' if verified else 'modelo',','.join(cons[:3])+('...' if len(cons)>3 else ''),note]
        out.write('\t'.join(row)+'\n')
    # resumen
    g=collections.OrderedDict()
    for r in d['rows']: g.setdefault(r['pat'],[]).append(r)
    for p,rs in g.items():
        vals=[r['vs'].replace('\t',' ') for r in rs]
        vs=('%s (x%d)'%(vals[0],len(vals))) if len(set(vals))==1 and len(vals)>1 else ' '.join(vals)
        if len(vs)>600: vs=vs[:600]+' ...'
        summ.write('\t'.join([c,p,'0x%x'%rs[0]['off'],str(len(rs)),tipo(rs[0]),vs,meaning(c,rs[0]),jp,'%s/%s'%(arch,o)])+'\n')
    stats[c]=dict(n=len(d['rows']),pats=len(g),json=jp,arch=arch,o=o,id=i,verified=verified,vt=vt[c],vtable=rtti.get(c),aoff=aoff,size=max((r['off'] or 0) for r in d['rows'])+4)
out.close(); summ.close()
pickle.dump(stats,open('stats.pkl','wb'))
# variantes
VAR={'PositionNone':[('constant_positionCK.bin',None),('constant_positionPK.bin',None)],'StudiumPositionData':[('constant_stadium.bin',None)],
     'TutorialNone':[('constant_tutorialConsole.bin',None)],'BlockDebug':[('constant_shootAging.bin',None)]}
vo=open('/home/claude/re/out/constant_gameplay_variantes.tsv','w')
vo.write('clase\tvariante(.o)\tarchivo_bin\tid_constante\tclave\toffset\ttipo\tvalor_variante\tvalor_base\n')
nvar=0
for c,lst in VAR.items():
    fs=fields.fields_of(R[c]); base={r['k']:r for r in D[c]['rows']}
    for arch,_ in lst:
        for name,blob in arc.load('bin/'+arch).items():
            if name==D[c]['o']: continue
            mv=model.model_values(fs,blob); nvar+=1
            for f in fs:
                v=mv.get(f['k'],(None,None))[1]; b=base[f['k']]
                vs='%.6g'%v if isinstance(v,float) else ('' if v is None else str(v))
                if vs!=b['vs']:
                    vo.write('\t'.join([c,name,arch,str(ids.get(name[:-2],'')),b['key'],'0x%x'%b['off'],tipo(b),vs,b['vs']])+'\n')
vo.close(); print('variantes',nvar)
