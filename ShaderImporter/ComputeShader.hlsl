#pragma target cs_5_0
#pragma option bool BooleanOption
#pragma option enum EnumOption {val1, val2, val3}
#pragma option int IntegerOption {1..4}

#pragma option enum LightingMode { Conventional, Pbr }
#pragma option bool UseShadows
#pragma option bool UseLineOfSight
#pragma option bool UseEnvironmentMapping
#pragma option bool UseNormalMap
#pragma option bool UseOcclusionMap
#pragma option bool UseEmissiveMap
#pragma option bool UseAdditionalStaticOverlay

[numthreads(1, 1, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{

}