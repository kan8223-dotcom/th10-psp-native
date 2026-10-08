#include "AnmResources.hpp"
#if defined(TH10_ANM_STREAM_LOAD) && TH10_ANM_STREAM_LOAD
#include "ResourceArchive.hpp"
#if !TH10_ANM_COMPACT
#error "TH10_ANM_STREAM_LOAD requires TH10_ANM_COMPACT=1 (it writes the compact layout itself)"
#endif
#if !defined(TH10_PRELOAD_TRANSITIONS) || TH10_PRELOAD_TRANSITIONS
#error "TH10_ANM_STREAM_LOAD requires TH10_PRELOAD_TRANSITIONS=0 (the transition preload copies the whole file)"
#endif
#endif
namespace th10 {
#if defined(TH10_ANM_STREAM_LOAD) && TH10_ANM_STREAM_LOAD
// th10_port (TH10_ANM_STREAM_LOAD, PSP-1000 lane): AnmManager::open without
// the whole decoded file in memory (title.anm is 6,389,204 bytes; with its
// 16-bit surfaces it did not fit the 1000's heap). The archive entry is
// decoded piece by piece (ResourceArchive::open_stream). Each chunk keeps only
// [0, texture_offset), the bytes compact() keeps, laid out as compact() lays
// them out; each THTX block goes through a scratch buffer of its own size
// (1 MiB at most in TH10) into create_embedded, the same call materialize
// makes on the same bytes, and '@' chunks get create_empty there too, so
// textures are created in the original order. materialize then fills the
// sprites and scripts and skips the creation. Anything this does not take
// (an external image file, a chunk whose tables reach past its texture, a
// failed read or creation) undoes what it made and takes the whole-file path,
// which then behaves as before.
// The scratch stays within scratch_cap (512 KiB + the header: it must fit the
// PSP-1000's volatile arena next to the sound and the font even in PPSSPP,
// where the effects cannot move to the ME). A bigger THTX block - in TH10 the
// 512x512 A8R8G8B8 images of title.anm and stage02..04.anm, 1 MiB - is read
// twice: once for its alpha bytes (create_embedded's 5551-or-4444 choice),
// then again from a mark in the stream, each band of rows converted to the
// chosen 16-bit format as upload_surface converts it, so the scratch holds the
// 16-bit image; create_embedded's tail then makes the texture from that.
namespace {
AnmStreamStats stream_stats{};bool force_whole=false;u32 fault_after=~0u;
constexpr u32 thtx_bytes_per_pixel[]={4,4,2,2,3,2};   // TexturePlatform.cpp sizes[], by THTX format
constexpr u32 scratch_cap=512u*1024u+16u;
template<class T>T load_value(const u8* bytes){T value;__builtin_memcpy(&value,bytes,sizeof(value));return value;}
struct StreamLoad {
    AnmResourceEnvironment& env;ResourceStream& in;AnmTextureEnvironment& textures;
    u8* kept=nullptr;u32 kept_size=0,kept_capacity=0;
    u8* scratch=nullptr;u32 scratch_capacity=0;
    static constexpr u32 max_textures=128;AnmTexture created[max_textures]{};u32 texture_count=0;
    i32 sprites=0,scripts=0;
    StreamLoad(AnmResourceEnvironment& e,ResourceStream& s,AnmTextureEnvironment& t):env(e),in(s),textures(t){}
    ~StreamLoad(){if(scratch)env.release_scratch(scratch);if(kept)env.release_bytes(kept);}
    bool read(u8* output,u32 count){return in.read(output,count)==count;}
    bool skip(u32 count){u8 sink[256];while(count){const u32 n=count<sizeof(sink)?count:u32(sizeof(sink));if(in.read(sink,n)!=n)return false;count-=n;}return true;}
    bool reserve_kept(u32 need){
        if(need<=kept_capacity)return true;u32 capacity=kept_capacity?kept_capacity:16384u;while(capacity<need)capacity*=2;
        auto* bytes=static_cast<u8*>(env.resize_bytes(kept,capacity));if(!bytes)return false;kept=bytes;kept_capacity=capacity;return true;
    }
    bool reserve_scratch(u32 need){
        if(need<=scratch_capacity)return true;if(scratch)env.release_scratch(scratch);
        scratch=static_cast<u8*>(env.allocate_scratch(need));scratch_capacity=scratch?need:0;return scratch!=nullptr;
    }
    void discard(){for(u32 i=0;i<texture_count;++i)if(created[i].handle){env.release_texture(created[i].handle);created[i].handle=nullptr;}texture_count=0;}
    bool big_texture(AnmTexture& texture,const AnmChunk& chunk,const u8* header,u32 width,u32 height,u32 pixels,bool& done);
    bool run();
};
// A THTX block over scratch_cap (see above). done=false: not an A8R8G8B8 image
// with a 16-bit result; the stream is back at the first pixel byte and the
// caller reads the block whole. Returns false on a read or creation failure.
bool StreamLoad::big_texture(AnmTexture& texture,const AnmChunk& chunk,const u8* header,u32 width,u32 height,u32 pixels,bool& done){
    done=false;
    if(load_value<std::int16_t>(header+6)!=1||!in.mark())return true;
    u8 band[16384];bool binary=true;
    for(u32 left=pixels;left&&binary;){const u32 n=left<sizeof(band)?left:u32(sizeof(band));if(!read(band,n))return false;for(u32 i=3;i<n&&binary;i+=4)binary=band[i]==0||band[i]==255;left-=n;}   // one alpha byte off 0/255 decides
    const u32 index=textures.embedded_index(chunk.format,header,binary);
    if(!in.rewind())return false;
    const u32 row=width*4,rows_per_band=row<=sizeof(band)?u32(sizeof(band))/row:0;
    if(index>=6||thtx_bytes_per_pixel[index]!=2||!rows_per_band||16+width*height*2>scratch_cap)return true;
    if(!reserve_scratch(16+width*height*2))return false;
    __builtin_memcpy(scratch,header,16);const std::int16_t code=std::int16_t(index);__builtin_memcpy(scratch+6,&code,2);
    for(u32 y=0;y<height;){const u32 rows=height-y<rows_per_band?height-y:rows_per_band;
        if(!read(band,rows*row)||textures.convert_embedded_rows(scratch+16+y*width*2,index,band,header,rows))return false;y+=rows;}
    done=true;++stream_stats.two_pass;
    return textures.create_embedded_as(texture,scratch,chunk.width,chunk.height,index)==0;
}
bool StreamLoad::run(){
    const u32 total=in.size();u32 position=0;
    for(;;){
        if(total-position<sizeof(AnmChunk)||texture_count==max_textures||texture_count==fault_after)return false;
        if(!reserve_kept(kept_size+u32(sizeof(AnmChunk)))||!read(kept+kept_size,sizeof(AnmChunk)))return false;
        AnmChunk chunk;__builtin_memcpy(&chunk,kept+kept_size,sizeof(chunk));
        const u32 span=chunk.next_offset?chunk.next_offset:total-position;
        if(chunk.version!=4||span<sizeof(AnmChunk)||span>total-position||chunk.sprite_count<0||chunk.script_count<0)return false;
        const bool embedded=chunk.embedded_texture!=0;const u32 keep=embedded?chunk.texture_offset:span;
        if(keep<sizeof(AnmChunk)||keep>span||(embedded&&span-keep<16))return false;
        const u32 aligned=(keep+3u)&~3u;
        if(!reserve_kept(kept_size+aligned)||!read(kept+kept_size+sizeof(AnmChunk),keep-u32(sizeof(AnmChunk))))return false;
        if(aligned>keep)__builtin_memset(kept+kept_size+keep,0,aligned-keep);
        // compact()'s pass 1: everything materialize reads lies before the texture.
        const u8* bytes=kept+kept_size;
        if(u64(sizeof(AnmChunk))+u64(chunk.sprite_count)*4+u64(chunk.script_count)*8>keep||(!embedded&&chunk.name_offset>=keep))return false;
        for(i32 i=0;i<chunk.sprite_count;++i)if(u64(load_value<u32>(bytes+sizeof(AnmChunk)+i*4))+20>keep)return false;
        for(i32 i=0;i<chunk.script_count;++i)if(load_value<u32>(bytes+sizeof(AnmChunk)+chunk.sprite_count*4+i*8+4)>=keep)return false;
        auto& texture=created[texture_count++];
        if(embedded){
            // THTX: a 16-byte header, then the pixel rows create_embedded reads.
            u8 header[16];if(!read(header,16))return false;
            const i32 format=load_value<std::int16_t>(header+6),width=load_value<std::int16_t>(header+8),height=load_value<std::int16_t>(header+10);
            if(format<0||format>=6)return false;
            const u64 pixels=width>0&&height>0?u64(width)*u64(height)*thtx_bytes_per_pixel[format]:0;
            if(pixels>span-keep-16)return false;
            bool done=false;
            if(16+pixels>scratch_cap&&!big_texture(texture,chunk,header,u32(width),u32(height),u32(pixels),done))return false;
            if(done){if(!skip(span-keep-16-u32(pixels)))return false;}
            else{
                if(!reserve_scratch(16+u32(pixels)))return false;
                __builtin_memcpy(scratch,header,16);
                if(!read(scratch+16,u32(pixels))||!skip(span-keep-16-u32(pixels)))return false;
#if defined(TH10_TEXTURE_LAZY_PADDED) && TH10_TEXTURE_LAZY_PADDED
                if(anm_sprites_past_image(bytes,scratch)?textures.create_embedded_padded(texture,scratch,chunk.width,chunk.height,chunk.format):textures.create_embedded(texture,scratch,chunk.width,chunk.height,chunk.format))return false;
#else
                if(textures.create_embedded(texture,scratch,chunk.width,chunk.height,chunk.format))return false;
#endif
            }
        }else if(reinterpret_cast<const char*>(bytes)[chunk.name_offset]=='@')textures.create_empty(texture,chunk.width,chunk.height,chunk.format);
        else return false;   // an external image file: the whole-file path reads it (prepare_chunk)
        sprites=wrapping_add(sprites,chunk.sprite_count);scripts=wrapping_add(scripts,chunk.script_count);
        if(!chunk.next_offset){kept_size+=aligned;return true;}
        reinterpret_cast<AnmChunk*>(kept+kept_size)->next_offset=aligned;kept_size+=aligned;position+=span;
    }
}
// true: the stream took the file (out is open()'s result); false: whole-file path.
bool stream_open(AnmManager& manager,i32 slot,const char* filename,AnmResourceEnvironment& env,AnmFile*& out){
    auto* textures=env.texture_environment();ResourceStream* in=textures&&!force_whole?env.open_stream(filename):nullptr;
    if(!in){++stream_stats.whole;return false;}
    StreamLoad load{env,*in,*textures};const bool ok=load.run();in->close();
    if(!ok||in->failed()){load.discard();++stream_stats.aborted;++stream_stats.whole;return false;}
    if(load.scratch_capacity>stream_stats.scratch_peak)stream_stats.scratch_peak=load.scratch_capacity;
    if(auto* fitted=static_cast<u8*>(env.resize_bytes(load.kept,load.kept_size)))load.kept=fitted;
    // The file as the whole-file path leaves it after complete_next's compact().
    auto* file=env.allocate_file();__builtin_memset(file,0,sizeof(*file));manager.files[slot]=file;out=file;
    file->loaded_size=load.kept_size;file->file_index=slot;file->loaded=load.kept;load.kept=nullptr;__builtin_memcpy(file->name,filename,std::strlen(filename)+1);
    file->texture_count=load.texture_count;file->textures=static_cast<AnmTexture*>(env.allocate_bytes(load.texture_count*sizeof(AnmTexture)));
    __builtin_memcpy(file->textures,load.created,load.texture_count*sizeof(AnmTexture));load.texture_count=0;
    file->sprites=static_cast<AnmSprite*>(env.allocate_bytes(static_cast<u32>(load.sprites)*sizeof(AnmSprite)));file->scripts=static_cast<AnmInstruction**>(env.allocate_bytes(static_cast<u32>(load.scripts)*4));file->script_count=load.scripts;file->sprite_count=load.sprites;
    ++stream_stats.streamed;
    const auto* chunk=reinterpret_cast<const AnmChunk*>(file->loaded);i32 index=0;for(;;){if(file->prepare_chunk(index,chunk,env)<0){out=nullptr;return true;}if(!chunk->next_offset)return true;chunk=chunk->next();++index;}
}
}
AnmStreamStats anm_stream_stats() noexcept {return stream_stats;}
void anm_stream_force_whole(bool whole) noexcept {force_whole=whole;}
void anm_stream_fault_after(u32 textures) noexcept {fault_after=textures;}
#endif
// 0x4470c0. Keep the allocated slot on read/validation failure, as the original
// resource owner later releases it. Texture bytes are separate allocations.
AnmFile* AnmManager::open(i32 slot,const char* filename,AnmResourceEnvironment& env){
    if(slot<0||slot>=33){env.report(AnmResourceError::InvalidSlot);return nullptr;}
#if defined(TH10_ANM_STREAM_LOAD) && TH10_ANM_STREAM_LOAD
    {AnmFile* streamed=nullptr;if(stream_open(*this,slot,filename,env,streamed))return streamed;}
#endif
#if TH10_ANM_COMPACT
    u32 loaded_size=0;auto* data=env.read_file(filename,false,&loaded_size);auto* file=env.allocate_file();__builtin_memset(file,0,sizeof(*file));files[slot]=file;if(!data)return nullptr;
    file->loaded_size=loaded_size;
#else
    auto* data=env.read_file(filename,false,nullptr);auto* file=env.allocate_file();__builtin_memset(file,0,sizeof(*file));files[slot]=file;if(!data)return nullptr;
#endif
    file->file_index=slot;file->loaded=data;__builtin_memcpy(file->name,filename,std::strlen(filename)+1);
    const auto* chunk=reinterpret_cast<const AnmChunk*>(data);i32 sprites=chunk->sprite_count,scripts=chunk->script_count;u32 textures=1;
    while(chunk->next_offset){chunk=chunk->next();sprites=wrapping_add(sprites,chunk->sprite_count);scripts=wrapping_add(scripts,chunk->script_count);++textures;}
    file->texture_count=textures;file->textures=static_cast<AnmTexture*>(env.allocate_bytes(textures*sizeof(AnmTexture)));__builtin_memset(file->textures,0,textures*sizeof(AnmTexture));
    file->sprites=static_cast<AnmSprite*>(env.allocate_bytes(static_cast<u32>(sprites)*sizeof(AnmSprite)));file->scripts=static_cast<AnmInstruction**>(env.allocate_bytes(static_cast<u32>(scripts)*4));file->script_count=scripts;file->sprite_count=sprites;
    chunk=reinterpret_cast<const AnmChunk*>(data);i32 index=0;for(;;){if(file->prepare_chunk(index,chunk,env)<0)return nullptr;if(!chunk->next_offset)return file;chunk=chunk->next();++index;}
}
// 0x447280. The platform pumps its loader at this blocking resource barrier.
// An already-open slot returns immediately even if its upload is in progress.
AnmFile* AnmManager::load(i32 slot,const char* name,AnmResourceEnvironment& env){
    if(slot<0||slot>=33){env.report(AnmResourceError::InvalidSlot);return nullptr;}if(files[slot])return files[slot];
#ifdef TH_NATIVE_PLATFORM
    if(auto* ready=env.prepared_file(slot,name)){files[slot]=ready;return ready;}
#endif
    auto* file=open(slot,name,env);if(!file)return nullptr;file->unavailable=1;
    do{if(*env.loader_flags&128)return file;env.wait_for_loading(*this,*file);}while(file->unavailable);return file;
}
// 0x4472e0.
i32 AnmFile::prepare_chunk(i32 index,const AnmChunk* chunk,AnmResourceEnvironment& env){
    if(!chunk){env.report(AnmResourceError::MissingHeader);return -1;}if(chunk->version!=4){env.report(AnmResourceError::InvalidVersion);return -1;}
    if(!chunk->embedded_texture&&*chunk->texture_name()!='@'){
        u32 size;auto* bytes=env.read_file(chunk->texture_name(),true,&size);if(!bytes){env.report(AnmResourceError::MissingTexture);return -1;}textures[index].source_size=size;textures[index].source=bytes;
    }
    return 1;
}
// 0x4473c0. Complete one entry at a time, retaining aggregate sprite/script
// offsets while scanning the linked file headers.
bool AnmFile::complete_next(AnmResourceEnvironment& env){
    const auto* chunk=reinterpret_cast<const AnmChunk*>(loaded);i32 texture=0,sprite=0,script=0;bool completed=false;
    for(;;){
        if(static_cast<u32>(texture)==unavailable-1){if(env.materialize(*this,texture,sprite,script,chunk)<0){unavailable=0;return false;}completed=true;}
        sprite=wrapping_add(sprite,chunk->sprite_count);script=wrapping_add(script,chunk->script_count);++texture;
#if TH10_ANM_COMPACT
        if(!chunk->next_offset){unavailable=0;compact(env);return true;}chunk=chunk->next();
#else
        if(!chunk->next_offset){unavailable=0;return true;}chunk=chunk->next();
#endif
        if(static_cast<u32>(texture)==unavailable||completed){++unavailable;return true;}
    }
}
#if TH10_ANM_COMPACT
// th10_port, memory-limited builds (TH08 src/AnmManager.cpp PreparePspAnmCompact
// and FinalizePspAnmCompact, sha256 e8e74a4d): once every texture of the file
// holds its own copy of the pixels, keep only each chunk's [0, texture_offset)
// (header, tables, sprite records, scripts, name) and release the embedded
// pixels, 10.8 MiB at the stage 3 -> 4 load on the Go. Scripts are the only
// pointers into the buffer (materialize). Pass 1 checks every chunk and nothing
// changes if one does not fit; chunks keep 4-byte alignment.
void AnmFile::compact(AnmResourceEnvironment& env){
    if(!loaded||!loaded_size)return;
    u32 offset=0,size=0;
    for(;;){
        if(offset&3u||loaded_size-offset<sizeof(AnmChunk))return;
        const auto* chunk=reinterpret_cast<const AnmChunk*>(loaded+offset);const auto* bytes=loaded+offset;
        const u32 span=chunk->next_offset?chunk->next_offset:loaded_size-offset;
        if(span<sizeof(AnmChunk)||span>loaded_size-offset||chunk->sprite_count<0||chunk->script_count<0)return;
        const u32 keep=chunk->embedded_texture?chunk->texture_offset:span;
        if(keep<sizeof(AnmChunk)||keep>span)return;
        if(u64(sizeof(AnmChunk))+u64(chunk->sprite_count)*4+u64(chunk->script_count)*8>keep)return;
        for(i32 i=0;i<chunk->sprite_count;++i){u32 record;__builtin_memcpy(&record,bytes+sizeof(AnmChunk)+i*4,4);if(u64(record)+20>keep)return;}
        for(i32 i=0;i<chunk->script_count;++i){u32 script;__builtin_memcpy(&script,bytes+sizeof(AnmChunk)+chunk->sprite_count*4+i*8+4,4);if(script>=keep)return;}
        size+=(keep+3u)&~3u;
        if(!chunk->next_offset)break;offset+=chunk->next_offset;
    }
    if(size>=loaded_size)return;
    auto* kept=static_cast<u8*>(env.allocate_bytes(size));if(!kept)return;
    offset=0;u32 out=0;i32 script=0;
    for(;;){
        const auto* chunk=reinterpret_cast<const AnmChunk*>(loaded+offset);
        const u32 span=chunk->next_offset?chunk->next_offset:loaded_size-offset,keep=chunk->embedded_texture?chunk->texture_offset:span,aligned=(keep+3u)&~3u;
        __builtin_memcpy(kept+out,loaded+offset,keep);if(aligned>keep)__builtin_memset(kept+out+keep,0,aligned-keep);
        reinterpret_cast<AnmChunk*>(kept+out)->next_offset=chunk->next_offset?aligned:0;
        for(i32 i=0;i<chunk->script_count;++i,++script)
            scripts[script]=reinterpret_cast<AnmInstruction*>(kept+out+(reinterpret_cast<u8*>(scripts[script])-(loaded+offset)));
        out+=aligned;if(!chunk->next_offset)break;offset+=chunk->next_offset;
    }
    env.release_bytes(loaded);loaded=kept;loaded_size=size;
}
#endif
// 0x4493e0. Mark both registries; actual VM destruction occurs on update.
void AnmRegistry::discard_file(AnmFile* file) const noexcept {
    ListNode<AnmVm>* heads[]={world_head,ui_head};for(auto* head:heads)for(auto* node=head;node;node=node->next)if(node->value->animation_file==file)node->value->flags|=0x04000000;
}
#if defined(TH10_TRANSITION_LOWMEM) && TH10_TRANSITION_LOWMEM
// th10_port (TH10_TRANSITION_LOWMEM): at a stage transition the old stage
// stays alive through the new stage's load and its 30-frame fade, because its
// update (STD script, VM scripts with their random numbers, the world camera
// that camera-relative enemy spawns read) and the fade timer that its draw
// advances are game state. Only its pixels are not: the texture surfaces go
// before the new stage loads, the sprites lose their handles, and the old
// file's VMs are no longer drawn. Scripts, sprites and the file stay until
// the stage is deleted (StageResources::release) as before.
AnmFile* anm_released_textures=nullptr;
void AnmFile::release_textures(AnmResourceEnvironment& env){
    for(i32 index=0;index<texture_count;++index){auto& texture=textures[index];
        if(texture.handle){for(i32 i=0;i<sprite_count;++i)if(sprites[i].texture==texture.handle)sprites[i].texture=nullptr;env.release_texture(texture.handle);texture.handle=nullptr;}
        if(texture.source){env.release_bytes(texture.source);texture.source=nullptr;}}
    anm_released_textures=this;
}
#endif
// 0x447810. The loaded-file pointer gates the entire destruction sequence.
void AnmFile::release(AnmResourceEnvironment& env){
#if defined(TH10_TRANSITION_LOWMEM) && TH10_TRANSITION_LOWMEM
    if(anm_released_textures==this)anm_released_textures=nullptr;   // the next AnmFile may get this address
#endif
    if(!loaded)return;env.registry->discard_file(this);
    for(i32 index=0;index<texture_count;++index){auto& texture=textures[index];if(texture.handle){env.release_texture(texture.handle);texture.handle=nullptr;}if(texture.source){env.release_bytes(texture.source);texture.source=nullptr;}}
    if(textures){env.release_bytes(textures);textures=nullptr;}if(sprites){env.release_bytes(sprites);sprites=nullptr;}if(scripts){env.release_bytes(scripts);scripts=nullptr;}if(extra_data){env.release_bytes(extra_data);extra_data=nullptr;}if(loaded){env.release_bytes(loaded);loaded=nullptr;}
}
// 0x4477d0 / 0x447790.
void AnmManager::unload(i32 slot,AnmResourceEnvironment& env){if(slot<0||slot>=33||!files[slot])return;files[slot]->release(env);env.release_file(files[slot]);files[slot]=nullptr;}
bool AnmManager::resources_ready() const noexcept {for(auto* file:files)if(file&&(file->discard_request||file->unavailable))return false;return true;}
// 0x447700. A stale deletion marker does not dereference the released slot;
// the executable does so on that invalid path, which would fault on Windows.
i32 AnmManager::process_loading(AnmResourceEnvironment& env){
    for(i32 slot=0;slot<33;++slot)if(auto* file=files[slot]){if(file->discard_request)unload(slot,env);else if(file->unavailable)return file->complete_next(env)?0:-1;}return 0;
}
// 0x447940. Preserve input aliasing and each float storage rounding boundary.
void AnmFile::set_sprite(i32 index,const AnmSprite& source) noexcept {
    sprites[index]=source;auto& sprite=sprites[index];
    sprite.u0=Scalar::div(sprite.left,sprite.texture_width);sprite.u1=Scalar::div(sprite.right,sprite.texture_width);
    sprite.v0=Scalar::div(sprite.top,sprite.texture_height);sprite.v1=Scalar::div(sprite.bottom,sprite.texture_height);
    sprite.width=((number(sprite.right)-number(sprite.left))/number(source.scale_x)).to_float();sprite.height=((number(sprite.bottom)-number(sprite.top))/number(source.scale_y)).to_float();
}
}
