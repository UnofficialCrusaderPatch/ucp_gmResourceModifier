
#include "pch.h"

#include <wincodec.h>

#include "gmResourceModifierInternal.h"

#include "IUnknownWrapper.h"


enum TgxStreamHeader : unsigned char
{
  STREAM_OF_PIXELS = 0x0,
  TGX_PIXEL_LENGTH = 0x1f,
  TRANSPARENT_PIXELS = 0x20,
  REPEATING_PIXELS = 0x40,
  NEWLINE = 0x80,
  TGX_PIXEL_HEADER = 0xe0,
};


static IUnknownWrapper<IWICImagingFactory> imageFactory;

// hopefully moves
static std::vector<WCHAR> GetWideCharFilepathFromUTF8(const char* utf8Str)
{
  std::vector<WCHAR> wCharBuffer{};

  int sizeAndRes{ MultiByteToWideChar(CP_UTF8, 0, utf8Str, -1, 0, 0) };
  if (!sizeAndRes)
  {
    return {};
  }

  wCharBuffer.resize(sizeAndRes);

  sizeAndRes = MultiByteToWideChar(CP_UTF8, 0, utf8Str, -1, wCharBuffer.data(), sizeAndRes);
  if (!sizeAndRes)
  {
    return {};
  }

  return wCharBuffer;
}


// sources:
// https://docs.microsoft.com/en-us/windows/win32/wic/-wic-api
// https://docs.microsoft.com/de-de/windows/win32/wic/-wic-creating-decoder
static bool OpenImageDecoder(const char* filepath, IUnknownWrapper<IWICBitmapDecoder>& decoderInterface)
{
  if (!imageFactory)
  {
    if (CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(imageFactory.expose())) != S_OK)
    {
      Log(LOG_WARNING, "[gmResourceModifier]: LoadResourceFromImage: Was unable to obtain image factory interface.");
      return false;
    }
  }

  std::vector<WCHAR> filepathWideStr{ GetWideCharFilepathFromUTF8(filepath) };
  if (filepathWideStr.empty())
  {
    Log(LOG_WARNING, "[gmResourceModifier]: LoadResourceFromImage: Was unable to decode filepath (UTF-8).");
    return false;
  }

  if (FAILED(imageFactory->CreateDecoderFromFilename(
    filepathWideStr.data(),             // Image to be decoded
    NULL,                               // Do not prefer a particular vendor
    GENERIC_READ,                       // Desired read access to the file
    WICDecodeMetadataCacheOnDemand,     // Cache metadata when needed
    decoderInterface.expose()           // Pointer to the decoder
  )))
  {
    Log(LOG_WARNING, "[gmResourceModifier]: LoadResourceFromImage: Was unable to obtain decoder. File invalid or missing.");
    return false;
  }
  return true;
}


// Pixel is the element type of the requested WIC format; scale resizes with WIC's Fant interpolation.
template <typename Pixel>
static std::vector<Pixel> GetFrameData(IUnknownWrapper<IWICBitmapDecoder>& decoderInterface,
  size_t frameIndex, REFWICPixelFormatGUID format, float scale, size_t& outWidth, size_t& outHeight)
{
  std::vector<Pixel> rawPixel{};
  UINT width{};
  UINT height{};

  // Retrieve the first frame of the image from the decoder
  IUnknownWrapper<IWICBitmapFrameDecode> frame;
  if (FAILED(decoderInterface->GetFrame(frameIndex, frame.expose())))
  {
    Log(LOG_WARNING, "[gmResourceModifier]: LoadResourceFromImage: Was unable to obtain frame.");
    return {};
  }

  IUnknownWrapper<IWICBitmapSource> converted;
  if (FAILED(WICConvertBitmapSource(format, frame.get(), converted.expose())))
  {
    Log(LOG_WARNING, "[gmResourceModifier]: LoadResourceFromImage: Was unable to transform an image into intermediate format.");
    return {};
  }

  if (FAILED(converted->GetSize(&width, &height)))
  {
    Log(LOG_WARNING, "[gmResourceModifier]: LoadResourceFromImage: Was unable to obtain frame size.");
    return {};
  }

  IWICBitmapSource* source{ converted.get() };
  IUnknownWrapper<IWICBitmapScaler> scaler;
  if (scale != 1.0f)
  {
    width = max(1u, static_cast<UINT>(width * scale + 0.5f));
    height = max(1u, static_cast<UINT>(height * scale + 0.5f));
    if (FAILED(imageFactory->CreateBitmapScaler(scaler.expose())) ||
        FAILED(scaler->Initialize(converted.get(), width, height, WICBitmapInterpolationModeFant)))
    {
      Log(LOG_WARNING, "[gmResourceModifier]: LoadResourceFromImage: Was unable to scale the image.");
      return {};
    }
    source = scaler.get();
  }

  rawPixel.resize(width * height);
  if (FAILED(source->CopyPixels(nullptr, width * sizeof(Pixel), width * height * sizeof(Pixel), (BYTE*)rawPixel.data())))
  {
    Log(LOG_WARNING, "[gmResourceModifier]: LoadResourceFromImage: Was unable to obtain raw pixels.");
    return {};
  }

  outWidth = width;
  outHeight = height;
  return rawPixel;
}


// Decodes a TGX image into colour words and an opacity mask. Returns false on malformed data.
static bool DecodeTGX(const unsigned char* data, size_t size, size_t width, size_t height,
  std::vector<unsigned short>& rawPixel, std::vector<bool>& opaque)
{
  if (!width || !height)
  {
    return false;
  }
  rawPixel.assign(width * height, 0);
  opaque.assign(width * height, false);

  size_t x{ 0 };
  size_t y{ 0 };
  size_t pos{ 0 };
  while (pos < size && y < height)
  {
    const unsigned char token{ data[pos++] };
    const size_t length{ static_cast<size_t>(token & TGX_PIXEL_LENGTH) + 1 };
    const unsigned char type{ static_cast<unsigned char>(token & TGX_PIXEL_HEADER) };

    if (type == NEWLINE)
    {
      x = 0;
      ++y;
      continue;
    }
    if (x + length > width)
    {
      return false;
    }

    switch (type)
    {
      case TRANSPARENT_PIXELS:
        break;

      case REPEATING_PIXELS:
      {
        if (pos + sizeof(unsigned short) > size)
        {
          return false;
        }
        unsigned short pixel;
        std::memcpy(&pixel, &data[pos], sizeof(unsigned short));
        pos += sizeof(unsigned short);
        for (size_t i{ 0 }; i < length; ++i)
        {
          rawPixel[y * width + x + i] = pixel;
          opaque[y * width + x + i] = true;
        }
        break;
      }

      case STREAM_OF_PIXELS:
      {
        if (pos + length * sizeof(unsigned short) > size)
        {
          return false;
        }
        std::memcpy(&rawPixel[y * width + x], &data[pos], length * sizeof(unsigned short));
        pos += length * sizeof(unsigned short);
        for (size_t i{ 0 }; i < length; ++i)
        {
          opaque[y * width + x + i] = true;
        }
        break;
      }

      default:
        return false;
    }
    x += length;
  }
  return true;
}


// hopefully at least moves
static std::vector<unsigned char> TransformToTGX(const std::vector<unsigned short>& rawPixel, const std::vector<bool>& opaque,
  size_t width, size_t height)
{
  std::vector<unsigned char> rawTgx{};

  // worst size is one control + one pixel + one newline at every vertical line end
  // -> number of pixels * size of ushort * 1.5 + height (ushort * 1.5 = 3 char)
  rawTgx.resize(rawPixel.size() * sizeof(char[3]) + height);

  size_t tgxSize{ 0 };
  size_t lineCounter{};
  for (size_t i{ 0 }; i < rawPixel.size(); ++i)
  {
    size_t counter{ 0 };

    ++lineCounter;
    unsigned short pixel = rawPixel[i];
    if (!opaque[i])
    {
      ++counter;
      while (lineCounter < width && counter < 32)
      {
        if (opaque[i + 1])
        {
          break;
        }
        ++counter;

        ++i;  // manipulate loop
        ++lineCounter;
      }
      rawTgx[tgxSize] = (unsigned char)(TRANSPARENT_PIXELS | (counter - 1));
      ++tgxSize;
    }
    else
    {
      unsigned short stateArray[32];
      stateArray[0] = pixel;
      ++counter;

      while (lineCounter < width && counter < 32)
      {
        pixel = rawPixel[i + 1];
        if (!opaque[i + 1] || pixel != stateArray[counter - 1])  // not same color anymore
        {
          break;
        }
        stateArray[counter] = pixel;
        ++counter;

        ++i;  // manipulate loop
        ++lineCounter;
      }

      if (counter > 1)  // currently two pixel provoke same color
      {
        rawTgx[tgxSize] = (unsigned char)(REPEATING_PIXELS | (counter - 1));
        *((unsigned short*)&rawTgx[tgxSize + 1]) = stateArray[counter - 1];
        tgxSize += 3;
      }
      else
      {
        size_t sameCounter{};

        while (lineCounter < width && counter < 32)
        {
          if (!opaque[i + 1])
          {
            break;
          }
          pixel = rawPixel[i + 1];
          if (pixel == stateArray[counter - 1])  // same color
          {
            ++sameCounter;
            if (sameCounter > 0) // currently one repeating pixel provokes same color
            {
              // resetting index (one pixel at the moment!)
              counter -= 1;

              i -= 1;
              lineCounter -= 1;
              break;
            }
          }
          stateArray[counter] = pixel;
          ++counter;

          ++i;  // manipulate loop
          ++lineCounter;
        }

        rawTgx[tgxSize] = (unsigned char)(STREAM_OF_PIXELS | (counter - 1));
        ++tgxSize;
        std::memcpy(&rawTgx[tgxSize], stateArray, counter * sizeof(unsigned short));
        tgxSize += counter * sizeof(unsigned short);
      }
    }


    if (lineCounter >= width)
    {
      rawTgx[tgxSize] = NEWLINE;
      ++tgxSize;
      lineCounter = 0;
    }
  }

  rawTgx.resize(tgxSize); // reduce to actual size
  return rawTgx;
}



int Gm1ResourceManager::CreateGm1ResourceFromImage(const char* filepath)
{
  IUnknownWrapper<IWICBitmapDecoder> decoderInterface;
  if (!OpenImageDecoder(filepath, decoderInterface))
  {
    return -1;
  }

  size_t frameCount{};
  if (FAILED(decoderInterface->GetFrameCount(&frameCount)))
  {
    Log(LOG_WARNING, "[gmResourceModifier]: LoadResourceFromImage: Was unable to frame count.");
    return -1;
  }

  std::vector<ImageHeader> resourceImageHeaders{};
  std::vector<int> resourceOffsets{};
  std::vector<int> resourceSizes{};
  std::vector<std::vector<unsigned char>> resourcesDataVectors{};

  for (size_t i{ 0 }; i < frameCount; ++i)
  {
    size_t width{ 0 };
    size_t height{ 0 };
    std::vector<unsigned short> rawPixel{ GetFrameData<unsigned short>(decoderInterface, 0, GUID_WICPixelFormat16bppBGRA5551, 1.0f, width, height) };
    if (rawPixel.empty())
    {
      return -1;
    }

    std::vector<bool> opaque(rawPixel.size());
    for (size_t p{ 0 }; p < rawPixel.size(); ++p)
    {
      opaque[p] = (rawPixel[p] & 0x8000) != 0;
    }

    resourcesDataVectors.push_back(TransformToTGX(rawPixel, opaque, width, height));
    if (resourcesDataVectors.back().empty())
    {
      return -1;
    }

    resourceOffsets.push_back( i > 0 ? resourceOffsets.back() + resourceSizes.back() : 0 );
    resourceSizes.push_back(resourcesDataVectors.back().size());
    ImageHeader& header{ resourceImageHeaders.emplace_back() };

    // for now only set dimensions?
    header.width = static_cast<unsigned short>(width);
    header.height = static_cast<unsigned short>(height);
  }

  Gm1Resource resource{};

  // create full array
  resource.imageData.swap(resourcesDataVectors.at(0));
  if (resourcesDataVectors.size() > 1)
  {
    size_t totalSize{ resource.imageData.size() };
    size_t currentIndex{ totalSize };

    for (size_t i{ 1 }; i < resourcesDataVectors.size(); i++)
    {
      totalSize += resourcesDataVectors.at(i).size();
    }
    resource.imageData.resize(totalSize);

    for (size_t i{ 1 }; i < resourcesDataVectors.size(); i++)
    {
      auto& dataVec{ resourcesDataVectors.at(i) };
      std::memcpy(&resource.imageData[currentIndex], dataVec.data(), dataVec.size());
      currentIndex += dataVec.size();
    }
  }

  resource.gm1Header = std::make_unique<Gm1Header>();
  resource.gm1Header->gm1Type = Gm1Type::INTERFACE; // testing with interface
  resource.gm1Header->numberOfPicturesInFile = resourceImageHeaders.size();
  resource.gm1Header->dataSize = resource.imageData.size();

  resource.imageHeader.swap(resourceImageHeaders);
  resource.imageSizes.swap(resourceSizes);
  resource.imageOffset.swap(resourceOffsets);

  return AddResource(std::move(resource), filepath, true);
}


static unsigned int ExpandChannel(unsigned int value, unsigned int maxValue)
{
  return (value * 255 + maxValue / 2) / maxValue;
}

static unsigned int ReduceChannel(unsigned int value, unsigned int maxValue)
{
  return (value * maxValue + 127) / 255;
}

// Builds a single interface image: the game's original image with the image file alpha-blended over it.
// Only covered pixels change; they are quantised once into the game's pixel format.
int Gm1ResourceManager::CreateOverlayResource(int gmID, int imageInGm, const char* filepath, int x, int y, float scale)
{
  const Gm1Resource* original{ GetOriginalGm(gmID) };
  if (!original || imageInGm < 0 || static_cast<size_t>(imageInGm) >= original->gm1Header->numberOfPicturesInFile)
  {
    Log(LOG_WARNING, "[gmResourceModifier]: CreateOverlayResource: Invalid GM image, or the game has not loaded its gm files yet.");
    return -1;
  }
  if (original->gm1Header->gm1Type != Gm1Type::INTERFACE)
  {
    Log(LOG_WARNING, "[gmResourceModifier]: CreateOverlayResource: Only interface gm files are supported.");
    return -1;
  }
  if (!(scale > 0.0f && scale <= 16.0f))
  {
    Log(LOG_WARNING, "[gmResourceModifier]: CreateOverlayResource: Scale must be above 0 and at most 16.");
    return -1;
  }

  const ImageHeader& header{ original->imageHeader[imageInGm] };
  const size_t width{ header.width };
  const size_t height{ header.height };
  std::vector<unsigned short> rawPixel{};
  std::vector<bool> opaque{};
  if (!DecodeTGX(GetGameImageData(original->imageOffset[imageInGm]), original->imageSizes[imageInGm],
    width, height, rawPixel, opaque))
  {
    Log(LOG_WARNING, "[gmResourceModifier]: CreateOverlayResource: The original image data could not be decoded.");
    return -1;
  }

  IUnknownWrapper<IWICBitmapDecoder> decoderInterface;
  if (!OpenImageDecoder(filepath, decoderInterface))
  {
    return -1;
  }
  size_t overlayWidth{ 0 };
  size_t overlayHeight{ 0 };
  // premultiplied BGRA bytes, read as 0xAARRGGBB
  const std::vector<unsigned int> overlay{ GetFrameData<unsigned int>(decoderInterface, 0, GUID_WICPixelFormat32bppPBGRA, scale,
    overlayWidth, overlayHeight) };
  if (overlay.empty())
  {
    return -1;
  }

  const bool rgb565{ *ColorAdapter::gamePixelFormat == ColorAdapter::PixelFormat::RGB_565 };
  const unsigned int greenMax{ rgb565 ? 63u : 31u };
  const unsigned int redShift{ rgb565 ? 11u : 10u };
  for (size_t overlayY{ 0 }; overlayY < overlayHeight; ++overlayY)
  {
    for (size_t overlayX{ 0 }; overlayX < overlayWidth; ++overlayX)
    {
      const int pixelX{ x + static_cast<int>(overlayX) };
      const int pixelY{ y + static_cast<int>(overlayY) };
      if (pixelX < 0 || pixelY < 0 || static_cast<size_t>(pixelX) >= width || static_cast<size_t>(pixelY) >= height)
      {
        continue;
      }

      const unsigned int color{ overlay[overlayY * overlayWidth + overlayX] };
      const unsigned int alpha{ color >> 24 };
      if (alpha == 0)
      {
        continue;
      }
      unsigned int red{ (color >> 16) & 0xff };
      unsigned int green{ (color >> 8) & 0xff };
      unsigned int blue{ color & 0xff };

      const size_t index{ pixelY * width + pixelX };
      if (opaque[index])
      {
        const unsigned short base{ rawPixel[index] };
        red += (ExpandChannel((base >> redShift) & 0x1f, 31) * (255 - alpha) + 127) / 255;
        green += (ExpandChannel((base >> 5) & greenMax, greenMax) * (255 - alpha) + 127) / 255;
        blue += (ExpandChannel(base & 0x1f, 31) * (255 - alpha) + 127) / 255;
      }
      else if (alpha >= 128) // interface images have one-bit transparency
      {
        red = (red * 255 + alpha / 2) / alpha;
        green = (green * 255 + alpha / 2) / alpha;
        blue = (blue * 255 + alpha / 2) / alpha;
        opaque[index] = true;
      }
      else
      {
        continue;
      }

      rawPixel[index] = static_cast<unsigned short>((rgb565 ? 0 : 0x8000) | (ReduceChannel(min(red, 255u), 31) << redShift) |
        (ReduceChannel(min(green, 255u), greenMax) << 5) | ReduceChannel(min(blue, 255u), 31));
    }
  }

  Gm1Resource resource{};
  resource.imageData = TransformToTGX(rawPixel, opaque, width, height);
  resource.gm1Header = std::make_unique<Gm1Header>();
  resource.gm1Header->gm1Type = Gm1Type::INTERFACE;
  resource.gm1Header->numberOfPicturesInFile = 1;
  resource.gm1Header->dataSize = resource.imageData.size();
  resource.imageHeader = { header };
  resource.imageOffset = { 0 };
  resource.imageSizes = { static_cast<int>(resource.imageData.size()) };

  // the data is already in the game's pixel format
  return AddResource(std::move(resource), filepath, false);
}


extern "C" __declspec(dllexport) int __stdcall LoadResourceFromImage(const char* filepath)
{
  return Gm1ResourceManager::CreateGm1ResourceFromImage(filepath);
}

extern "C" __declspec(dllexport) int __stdcall CreateOverlayResource(int gmID, int imageInGm, const char* filepath, int x, int y, float scale)
{
  return Gm1ResourceManager::CreateOverlayResource(gmID, imageInGm, filepath, x, y, scale);
}


// lua

extern "C" __declspec(dllexport) int __cdecl lua_LoadResourceFromImage(lua_State * L)
{
  int n{ lua_gettop(L) };    /* number of arguments */
  if (n != 1)
  {
    luaL_error(L, "[gmResourceModifier]: lua_LoadResourceFromImage: Invalid number of args.");
  }

  if (!lua_isstring(L, 1))
  {
    luaL_error(L, "[gmResourceModifier]: lua_LoadResourceFromImage: Wrong input fields.");
  }

  int res{ LoadResourceFromImage(lua_tostring(L, 1)) };
  lua_pushinteger(L, res);
  return 1;
}

extern "C" __declspec(dllexport) int __cdecl lua_CreateOverlayResource(lua_State * L)
{
  const lua_Integer gmID{ luaL_checkinteger(L, 1) };
  const lua_Integer imageInGm{ luaL_checkinteger(L, 2) };
  const char* filepath{ luaL_checkstring(L, 3) };
  const lua_Integer x{ luaL_checkinteger(L, 4) };
  const lua_Integer y{ luaL_checkinteger(L, 5) };
  const lua_Number scale{ luaL_optnumber(L, 6, 1.0) };
  if (gmID < 0 || gmID > INT_MAX || imageInGm < 0 || imageInGm > INT_MAX ||
      x < INT_MIN || x > INT_MAX || y < INT_MIN || y > INT_MAX)
  {
    lua_pushinteger(L, -1);
    return 1;
  }
  lua_pushinteger(L, CreateOverlayResource(static_cast<int>(gmID), static_cast<int>(imageInGm), filepath,
    static_cast<int>(x), static_cast<int>(y), static_cast<float>(scale)));
  return 1;
}
