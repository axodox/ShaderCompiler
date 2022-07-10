#pragma target cs_5_0
#pragma option bool BooleanOption
#pragma option enum EnumOption {val1, val2, val3}
#pragma option int IntegerOption {1..4}

[numthreads(1, 1, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{

}