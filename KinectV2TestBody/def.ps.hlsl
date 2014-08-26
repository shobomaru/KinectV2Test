struct PS_IN
{
	float4 pos : SV_POSITION;
	float4 color : COLOR;
};

float4 main( PS_IN psIn ) : SV_TARGET
{
	return psIn.color;
}