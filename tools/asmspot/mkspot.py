import os, re, sys
SH=os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'package', 'PES21FX', 'shaders') + os.sep
d0e9=open(SH+'d0e9d81c09b3da66-ps_replace.txt').read()
a=d0e9.index('cbuffer cPSScene'); b=d0e9.index('#include "pes_common.hlsli"')
base=d0e9[a:b]
base=base.replace("SamplerState g_samplerPoint_Wrap_s","""cbuffer cPSMaterial : register(b4)
{
  struct
  {
    float4 m_materials[8];
  } g_psMaterial : packoffset(c0);
}

SamplerState g_samplerPoint_Wrap_s""",1)
sysdecl='''cbuffer cPSSystem : register(b0)
{
  struct
  {
    float4 m_param;
    float4 m_renderInfo;
    float4 m_renderBuffer;
    float4 m_dominantLightDir;
    float4 m_timeParam;
  } g_psSystem : packoffset(c0);
}

'''
shadow_decl=('Texture2D<float4> g_DepthTexture : register(t2);','Texture2D<float4> g_DepthTexture : register(t2);\nTexture2D<float4> g_ShadowTexture : register(t3);')
cmp_decl=('SamplerState g_samplerLinear_Clamp_s : register(s11);','SamplerState g_samplerLinear_Clamp_s : register(s11);\nSamplerComparisonState g_samplerComparisonLess_Linear_Clmap_s : register(s15);')
REP={'cb0[4]':'g_psSystem.m_timeParam',
'cb2[12]':'g_psScene.m_shadowProjection._m00_m10_m20_m30','cb2[13]':'g_psScene.m_shadowProjection._m01_m11_m21_m31',
'cb2[14]':'g_psScene.m_shadowProjection._m02_m12_m22_m32','cb2[15]':'g_psScene.m_shadowProjection._m03_m13_m23_m33',
'cb2[21]':'g_psScene.m_projectionParam','cb2[23]':'g_psScene.m_exposure','cb2[28]':'g_psScene.m_cameraCenterOffset','cb2[29]':'g_psScene.m_shadowMapResolutions',
'cb3[3]':'g_psLight.m_lightSourcePos','cb3[4]':'g_psLight.m_lightSourceColor','cb3[5]':'g_psLight.m_lightSourceDir','cb3[6]':'g_psLight.m_lightSourceParam1',
'cb3[7]':'g_psLight.m_lightSourceParam2','cb3[8]':'g_psLight.m_lightSourceParam3','cb4[6]':'g_psMaterial.m_materials[6]','cb4[7]':'g_psMaterial.m_materials[7]'}
TEX={'t0':'g_NormalTexture','t1':'g_SpecularTexture','t2':'g_DepthTexture','t3':'g_ShadowTexture','t9':'g_lut1','t10':'g_MaterialTexture',
'ss8':'g_samplerPoint_Wrap_s','ss9':'g_samplerPoint_Clamp_s','ss11':'g_samplerLinear_Clamp_s','ss15':'g_samplerComparisonLess_Linear_Clmap_s'}
def build(gen_path, out_name, head, splices):
    gen=open(gen_path).read()
    decl=base
    if 'Texture2D<float4> t3' in gen: decl=decl.replace(*shadow_decl)
    if 'SamplerComparisonState' in gen: decl=decl.replace(*cmp_decl)
    pre = sysdecl if 'cb0[' in gen else ''
    i=gen.index('uint4 PESBfi'); bfi=gen[i:gen.index('\n',i)+1]
    code=gen[gen.index('void main('):]
    for k,v in REP.items(): code=code.replace(k,v)
    assert not re.search(r'\bcb[0-9]\[',code), re.findall(r'\bcb[0-9]\[\d+\]',code)
    code=re.sub(r'\bs(8|9|11|15)\b',lambda m:'ss'+m.group(1),code)
    for k,v in TEX.items(): code=re.sub(r'\b'+k+r'\b',v,code)
    assert not re.search(r'\b(t\d+|ss?\d+)\b',code), re.findall(r'\b(t\d+|ss?\d+)\b',code)[:5]
    for anchor,text in splices:
        assert code.count(anchor)==1, (out_name, anchor, code.count(anchor))
        k=code.index(anchor); k=code.index('\n',k)+1
        code=code[:k]+text+code[k:]
    out=head+pre+decl+bfi+'#include "pes_common.hlsli"\nfloat3 PESSunColor() { return g_psLight.m_lightSourceColor.xyz; }   // para pes_skinlight: el tinte del brillo sigue al color del foco\n#include "pes_skinlight.hlsli"\n\n'+code
    open(SH+out_name,'w',newline='\n').write(out)
    print('ok',out_name,len(out))
