Texture2D< uint > tex;
//SamplerState ss;

struct PS_IN
{
	float4 pos : SV_POSITION;
	float2 uv : TEXCOORD;
};

float4 getColor( uint n )
{
	switch( n )
	{
	case 255: return float4( 0, 0, 0, 1 );
	case 0: return float4( 1, 0, 0, 1 );
	case 1: return float4( 0, 1, 0, 1 );
	case 2: return float4( 0, 0, 1, 1 );
	case 3: return float4( 1, 1, 0, 1 );
	case 4: return float4( 0, 1, 1, 1 );
	case 5: return float4( 1, 0, 1, 1 );
	case 6: return float4( 1, 1, 1, 1 );
	default: return float4( 0.5, 0.5, 0.5, 1 );
	}
}

float4 main( PS_IN ps ) : SV_TARGET
{
	float texWidth, texHeight;
	tex.GetDimensions( texWidth, texHeight );
	uint3 pos = uint3( ps.uv * float2( texWidth - 1, texHeight - 2 ), 0 );

	uint n = tex.Load( pos );
	float4 color = getColor( n );
	return color;
}