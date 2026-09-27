#include "pch.h"
#include "gmResourceModifierInternal.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

extern "C" void ucp_log(ucp_NamedVerbosity, const char* message) { std::puts(message); }
extern "C" void* ucp_getProcAddressFromLibraryInModule(const char*, const char*, const char*) { return nullptr; }

static_assert(sizeof(Gm1Header) == 5208);
static_assert(sizeof(ImageHeader) == 16);

static int resource(const char* file, int count, int color)
{
  Gm1Header header{};
  header.numberOfPicturesInFile=count;
  header.gm1Type=Gm1Type::ANIMATIONS;
  header.dataSize=count*4;
  header.colorPalette[0][0]=static_cast<unsigned short>(color);
  std::ofstream out(file,std::ios::binary);
  out.write(reinterpret_cast<char*>(&header),sizeof(header));
  for(int i=0;i<count;++i) { int value=i*4; out.write(reinterpret_cast<char*>(&value),4); }
  for(int i=0;i<count;++i) { int value=4; out.write(reinterpret_cast<char*>(&value),4); }
  for(int i=0;i<count;++i) { ImageHeader image{}; image.width=1;image.height=1;out.write(reinterpret_cast<char*>(&image),16); }
  for(int i=0;i<count;++i) out.write("\0\0\x80\x80",4);
  out.close();
  return LoadGm1Resource(file);
}

// Only the native disk-loading boundary is replaced. Production admission,
// color preparation, SetGm, snapshots and resource reference counts execute.
struct RendererFixture : ColorAdapter
{
  void load(char*) {}
};

int main(int argc,char** argv)
{
  assert(argc==2);
  const bool reject=std::strcmp(argv[1],"success")!=0;
  std::vector<unsigned char> renderer(0x51c+240*sizeof(Gm1Header));
  std::vector<ImageHeader> headers(66000);
  std::vector<int> sizes(66000),offsets(66000),first(240);
  int gmCount=3;
  auto native=reinterpret_cast<Gm1Header*>(renderer.data()+0x51c);
  native[1].numberOfPicturesInFile=2;native[1].gm1Type=Gm1Type::ANIMATIONS;
  native[2].numberOfPicturesInFile=3;native[2].gm1Type=Gm1Type::ANIMATIONS;
  first[1]=1;first[2]=3;
  for(int i=1;i<6;++i) { headers[i].width=static_cast<unsigned short>(i);headers[i].height=1;offsets[i]=100+i;sizes[i]=4; }
  *reinterpret_cast<int*>(renderer.data()+0x48)=6;
  *reinterpret_cast<int*>(renderer.data()+0x4c)=gmCount;
  shcImageHeaderStart=headers.data();shcSizesStart=sizes.data();shcOffsetStart=offsets.data();
  shcFirstImageStart=first.data();shcGmCount=&gmCount;
  ColorAdapter::PixelFormat pixel=ColorAdapter::PixelFormat::ARGB_1555;
  ColorAdapter::gamePixelFormat=&pixel;
  ColorAdapter::actualLoadGmsFunc=static_cast<ColorAdapter::ActualLoadGmsFunc>(&RendererFixture::load);
  int a=resource("a.gm1",2,31),b=resource("b.gm1",3,62),texture=resource("texture.gm1",2,93);
  assert(a>=0 && b>=0 && texture>=0);
  assert(ReserveGm(-1,a)==-1 && ReserveGm(240,a)==-1 && ReserveGm(1,999)==-1);
  int tokenA=ReserveGm(1,a),tokenB=ReserveGm(2,b),tokenC=ReserveGm(1,a);
  assert(tokenA==0 && tokenB==1 && tokenC==2);
  assert(GetReservedGm(tokenA)==-1 && !FreeGm1Resource(a) && !FreeGm1Resource(b));
  assert(SetGm(1,-1,texture,-1)); // existing queued texture replacement
  if(std::strcmp(argv[1],"mismatch")==0) native[2].gm1Type=Gm1Type::FONT;
  if(std::strcmp(argv[1],"slot-capacity")==0) { gmCount=239;*reinterpret_cast<int*>(renderer.data()+0x4c)=239; }
  if(std::strcmp(argv[1],"image-capacity")==0) {
    native[2].numberOfPicturesInFile=65997;
    *reinterpret_cast<int*>(renderer.data()+0x48)=66000;
  }
  if(std::strcmp(argv[1],"occupied")==0) native[4].numberOfPicturesInFile=1;
  if(std::strcmp(argv[1],"bad-offset")==0) first[2]=99;
  auto originalRenderer=renderer;auto originalHeaders=headers;auto originalFirst=first;
  reinterpret_cast<ColorAdapter*>(renderer.data())->detouredLoadGmFiles(nullptr);
  assert(initDone && ReserveGm(1,a)==-1 && GetReservedGm(-1)==-1 && GetReservedGm(240)==-1);
  assert(native[1].colorPalette[0][0]==93); // old queue still executes
  assert(!FreeGm1Resource(texture));
  if(reject) {
    assert(GetReservedGm(tokenA)==-1 && GetReservedGm(tokenB)==-1 && GetReservedGm(tokenC)==-1);
    assert(first==originalFirst);
    assert(std::memcmp(renderer.data()+0x48,originalRenderer.data()+0x48,8)==0);
    assert(std::memcmp(&native[3],originalRenderer.data()+0x51c+3*sizeof(Gm1Header),237*sizeof(Gm1Header))==0);
    assert(std::memcmp(&headers[6],&originalHeaders[6],(66000-6)*sizeof(ImageHeader))==0);
    assert(FreeGm1Resource(a) && FreeGm1Resource(b));
  } else {
    assert(GetReservedGm(tokenA)==3 && GetReservedGm(tokenB)==4 && GetReservedGm(tokenC)==5);
    assert(gmCount==6 && first[3]==6 && first[4]==8 && first[5]==11);
    assert(*reinterpret_cast<int*>(renderer.data()+0x48)==13);
    assert(native[3].colorPalette[0][0]==31 && native[4].colorPalette[0][0]==62);
    assert(!FreeGm1Resource(a) && !FreeGm1Resource(b));
    assert(SetGm(3,-1,-1,-1) && !FreeGm1Resource(a));
    assert(SetGm(5,-1,-1,-1) && FreeGm1Resource(a));
    assert(SetGm(4,-1,-1,-1) && FreeGm1Resource(b));
    assert(native[3].colorPalette[0][0]==0 && headers[6].width==1 && offsets[6]==101);
    assert(headers[8].width==3 && offsets[8]==103);
  }
  assert(SetGm(1,-1,-1,-1) && FreeGm1Resource(texture));
  std::puts("reservation lifecycle passed");
}
