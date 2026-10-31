// Compile/validate only; runtime data retains both transforms and invalid path.
#include "../common/transport_frame.h"
StructuredBuffer<float4> input_data:register(t0);
RWStructuredBuffer<float4> output_data:register(u1);
[numthreads(1,1,1)]
void main(uint3 id:SV_DispatchThreadID){
 uint i=id.x;TfFrame f=tf_frame(input_data[3*i].xyz);
 output_data[2*i]=float4(tf_to_local(f,input_data[3*i+1].xyz),float(f.status));
 output_data[2*i+1]=float4(tf_to_world(f,input_data[3*i+2].xyz),float(f.status));
}
