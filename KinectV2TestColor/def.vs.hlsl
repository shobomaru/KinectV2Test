struct PS_IN
{
	float4 pos : SV_POSITION;
	float2 uv : TEXCOORD;
};

PS_IN main( uint id : SV_VERTEXID )
{
	float4 pos = float4( 0, 0, 0, 1 );
	if( id == 0 ) pos.xy = float2( -1, +1 );
	if( id == 1 ) pos.xy = float2( +1, +1 );
	if( id == 2 ) pos.xy = float2( -1, -1 );
	if( id == 3 ) pos.xy = float2( +1, -1 );

	float2 uv = pos.xy * float2( 0.5, 0.5 ) + float2( 0.5, 0.5 );
	uv.y = 1 - uv.y;

	PS_IN ps;
	ps.pos = pos;
	ps.uv = uv;
	return ps;
}