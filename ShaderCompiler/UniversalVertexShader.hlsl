//opt: colored | textured, fallback, multi_texture (!colored), flat | lambert (colored)
//opt: colored | textured, ground_clip, transparency, multi_texture (!colored), overlay

float4 main( float4 pos : POSITION ) : SV_POSITION
{
	return pos;
}