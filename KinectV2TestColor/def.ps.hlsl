Texture2D< float4 > tex;
SamplerState ss;

struct PS_IN
{
	float4 pos : SV_POSITION;
	float2 uv : TEXCOORD;
};

float4 main( PS_IN ps ) : SV_TARGET
{
	return tex.SampleLevel( ss, ps.uv, 0 );
}