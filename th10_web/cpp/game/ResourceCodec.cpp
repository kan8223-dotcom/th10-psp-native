#include "ResourceCodec.hpp"
namespace th10 {
u8* transform_resource(u8* data,i32 length,u8 key,u8 step,i32 block,i32 limit,bool encrypt,CodecMemory& memory){
    if(block<=0||length<0||limit<0)return data;
    const auto copied=length<limit?length:limit;auto* temporary=memory.allocate_bytes(copied);if(!temporary)return data;std::memcpy(temporary,data,copied);
    const i32 remainder=length%block;auto remaining=length-(length&1)-(remainder<block/4?remainder:0);i32 offset=0;
    while(remaining>0&&limit>0){if(block>remaining)block=remaining;i32 sequential=offset;for(i32 lane=0;lane<2;lane++)for(i32 index=block-1-lane;index>=0;index-=2){if(encrypt)data[sequential]=temporary[offset+index]^key;else data[offset+index]=temporary[sequential]^key;key=static_cast<u8>(key+step);++sequential;}offset+=block;remaining-=block;limit-=block;}
    memory.release_bytes(temporary);return data;
}
struct ResourceBits {
    const u8* input;u32 length,cursor=0,mask=128,byte=0;
    u32 bit() noexcept {if(mask==128)byte=cursor<length?input[cursor++]:0;const u32 value=(byte&mask)!=0;mask>>=1;if(!mask)mask=128;return value;}
    u32 bits(u32 count) noexcept {u32 value=0;while(count--)value=(value<<1)|bit();return value;}
};
// 0x435dc0. Back references may overlap the dictionary entries being written.
template<class Bits> i32 decode_lzss_bits(Bits& reader,u8* destination,u32 capacity,u8* dictionary) noexcept {u32 written=0,head=1;for(;;){if(reader.bit()){if(written>=capacity)return -1;const u8 value=reader.bits(8);destination[written++]=value;dictionary[head]=value;head=(head+1)&8191;}else{const auto offset=reader.bits(13);if(!offset)return static_cast<i32>(written);const auto count=reader.bits(4)+3;if(count>capacity-written)return -1;for(u32 i=0;i<count;i++){const auto value=dictionary[(offset+i)&8191];destination[written++]=value;dictionary[head]=value;head=(head+1)&8191;}}}}
i32 decode_lzss(const u8* source,u32 length,u8* destination,u32 capacity,u8* dictionary) noexcept {ResourceBits reader{source,length};return decode_lzss_bits(reader,destination,capacity,dictionary);}
// th10_port: the same decoder over input pulled in pieces (TH08 src/pbg/Lzss.cpp
// LzssFileBitReader). Past the end the bits are zero, as for ResourceBits.
struct PulledBits {
    LzssInput& input;const u8* window=nullptr;u32 cursor=0,size=0,mask=128,byte=0;
    u32 bit() noexcept {if(mask==128){if(cursor>=size){size=input.pull(window);cursor=0;}byte=cursor<size?window[cursor++]:0;}const u32 value=(byte&mask)!=0;mask>>=1;if(!mask)mask=128;return value;}
    u32 bits(u32 count) noexcept {u32 value=0;while(count--)value=(value<<1)|bit();return value;}
};
i32 decode_lzss(LzssInput& input,u8* destination,u32 capacity,u8* dictionary) noexcept {PulledBits reader{input};return decode_lzss_bits(reader,destination,capacity,dictionary);}
#if defined(TH10_ANM_STREAM_LOAD) && TH10_ANM_STREAM_LOAD
// decode_lzss_bits with its loop state kept between calls (PulledBits inlined).
u32 LzssStream::read(u8* output,u32 count) noexcept {
    auto bit=[this]()noexcept->u32{if(mask==128){if(cursor>=size){size=input->pull(window);cursor=0;}byte=cursor<size?window[cursor++]:0;}const u32 value=(byte&mask)!=0;mask>>=1;if(!mask)mask=128;return value;};
    auto bits=[&bit](u32 n)noexcept->u32{u32 value=0;while(n--)value=(value<<1)|bit();return value;};
    u32 done=0;
    while(done<count){
        if(match_left){const u8 value=dictionary[match_at];match_at=(match_at+1)&8191;--match_left;output[done++]=value;dictionary[head]=value;head=(head+1)&8191;continue;}
        if(ended||failed)break;
        if(bit()){if(written>=capacity){failed=true;break;}const u8 value=static_cast<u8>(bits(8));++written;output[done++]=value;dictionary[head]=value;head=(head+1)&8191;}
        else{const u32 offset=bits(13);if(!offset){ended=true;break;}const u32 n=bits(4)+3;if(n>capacity-written){failed=true;break;}written+=n;match_at=offset;match_left=n;}
    }
    return done;
}
#endif
}
