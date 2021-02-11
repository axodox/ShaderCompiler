#pragma target cs_5_0
#pragma option enum EdgeClip {None, RemoveOverflow, BlackOverflow}
#pragma option bool GroundClip
#pragma option bool EnvironmentMapping
#pragma option enum Transparency {Full, Threshold}

[numthreads(1, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{

}