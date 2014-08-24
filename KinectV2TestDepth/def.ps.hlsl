Texture2D< float > tex;
SamplerState ss;

struct PS_IN
{
	float4 pos : SV_POSITION;
	float2 uv : TEXCOORD;
};

float4 main( PS_IN ps ) : SV_TARGET
{
	float color = tex.SampleLevel( ss, ps.uv, 0 ) * ( 65535.0f / 4500.0f ); // clamp at 4.5 [m]
	color = saturate( color );
	return float4( color, color, color, 1 );
}