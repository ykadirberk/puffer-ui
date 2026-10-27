// Exact float-source native blend functions. Cache admission is bounded FIFO;
// a miss requests work and leaves the complete native pixel in charge.
#ifndef PUI_FUNCTION_SET
#define PUI_FUNCTION_SET 0
#endif
#ifndef PUI_FUNCTION_BINDING
#define PUI_FUNCTION_BINDING 0
#endif
#ifdef PUI_FUNCTION_READONLY
layout(std430,set=PUI_FUNCTION_SET,binding=PUI_FUNCTION_BINDING) readonly buffer FunctionArena {
#else
layout(std430,set=PUI_FUNCTION_SET,binding=PUI_FUNCTION_BINDING) coherent buffer FunctionArena {
#endif
    uint words[];
} functions;
const uint PUI_FUNCTION_WAYS=8u, PUI_FUNCTION_HEADER=32u;
uint function_rows() { return functions.words[6]; }
uint function_requests() { return functions.words[7]; }
uint function_hashes() { return functions.words[8]; }
uint function_row(uint row) { return PUI_FUNCTION_HEADER+row*8u; }
uint function_request_base() { return function_row(function_rows()); }
uint function_hash_base() { return function_request_base()+function_requests()*8u; }
uint function_list_base() { return function_hash_base()+function_hashes()*8u; }
uint function_hash(uvec4 key) {
    uint h=0x811c9dc5u;
    for(uint i=0u;i<4u;++i) { h=(h^key[i])*0x01000193u; h^=h>>16; }
    return h;
}
bool function_key(uint at,uvec4 key) {
    return functions.words[at]==key.x && functions.words[at+1u]==key.y &&
        functions.words[at+2u]==key.z && functions.words[at+3u]==key.w;
}
bool function_finite(vec4 source) {
    return !any(isnan(source)) && !any(isinf(source)) &&
        all(greaterThanEqual(source,vec4(0.0))) && all(lessThanEqual(source,vec4(1.0)));
}
#ifndef PUI_FUNCTION_READONLY
void function_request(uvec4 key) {
    uint mask=function_hashes()-1u, hash=function_hash(key);
    for(uint probe=0u;probe<8u;++probe) {
        uint at=function_hash_base()+((hash+probe)&mask)*8u;
        uint state=atomicCompSwap(functions.words[at+4u],0u,1u);
        if(state==2u && function_key(at,key)) return;
        // A publisher can be the same key. Never spin on another invocation;
        // dropping this cache request leaves this draw's native result intact.
        if(state==1u) return;
        if(state!=0u) continue;
        for(uint c=0u;c<4u;++c) functions.words[at+c]=key[c];
        memoryBarrierBuffer();
        atomicExchange(functions.words[at+4u],2u);
        uint index=atomicAdd(functions.words[0],1u), capacity=function_requests();
        if(index>=capacity) {
            atomicMin(functions.words[0],capacity);
            atomicAdd(functions.words[1],1u);
            return;
        }
        at=function_request_base()+index*8u;
        for(uint c=0u;c<4u;++c) functions.words[at+c]=key[c];
        return;
    }
    atomicAdd(functions.words[1],1u);
}
#endif
