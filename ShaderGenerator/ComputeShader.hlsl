#pragma target cs_5_0
#pragma entry main
#pragma namespace Holomaps::Graphics::Shaders
#pragma option bool IS_X
#pragma option enum EN {val1, val2, val3}
#pragma option int Y {1..12}
#pragma asd

[numthreads(1, 1, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{

}