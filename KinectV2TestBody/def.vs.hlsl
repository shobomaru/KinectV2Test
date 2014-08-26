struct VS_IN
{
	float3 pos : POSITION;
	float4 color : COLOR;
};

struct PS_IN
{
	float4 pos : SV_POSITION;
	float4 color : COLOR;
};

cbuffer cbModel
{
	float4x4 cbModelWVP;
};

PS_IN main( VS_IN vsIn )
{
	PS_IN psIn;
	psIn.pos = mul( float4( vsIn.pos, 1 ), cbModelWVP );
	psIn.color = vsIn.color;
	return psIn;
}
