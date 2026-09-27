
#include "pch.h"

#include "gmResourceModifierInternal.h"

#include <vector>
#include <unordered_map>

#include <ios>
#include <fstream>
#include <sstream>
#include <cstdint>
#include <stdexcept>
#include <array>
#include <wincrypt.h>


// static variables:

static std::vector<Replacer> replacerVector{};

static std::vector<std::tuple<int, int, int, int>> preRequests{}; // contains replace requests made before all variables were initialized

// variables received from the header:
static Gm1Header* shcHeaderStart{};
static int shcGmDataAddr{};
static BOOL shcOffsetModifierFlag{};

// Native IDs are available only after the loader admits the complete batch.
struct SheetReservation
{
  int baseGm;
  Gm1Resource* resource;
  int slot{ -1 };
};
static std::vector<SheetReservation> sheetReservations{};

static bool AdmitReservedSheets(ColorAdapter* renderer)
{
  if (sheetReservations.empty()) return true;
  if (!shcFirstImageStart || !shcGmCount) return false;

  // Verified renderer fields; retain native 240-sheet / 66000-image capacities.
  auto& nextImage = *reinterpret_cast<int*>(reinterpret_cast<char*>(renderer) + 0x48);
  auto& nextGm = *reinterpret_cast<int*>(reinterpret_cast<char*>(renderer) + 0x4c);
  if (nextGm < 1 || nextGm > 240 || *shcGmCount != nextGm ||
      sheetReservations.size() > static_cast<size_t>(240 - nextGm)) return false;
  if (nextImage < 1 || nextImage > 66000) return false;

  size_t images{ 1 };
  for (int gm = 0; gm < 240; ++gm)
  {
    const size_t count = shcHeaderStart[gm].numberOfPicturesInFile;
    if (count > 66000 - images || (gm >= nextGm && count != 0)) return false;
    if (count && shcFirstImageStart[gm] != static_cast<int>(images)) return false;
    images += count;
  }
  if (images != static_cast<size_t>(nextImage)) return false;

  for (const auto& reservation : sheetReservations)
  {
    if (reservation.baseGm >= nextGm) return false;
    const auto& base = shcHeaderStart[reservation.baseGm];
    const auto& custom = *reservation.resource->gm1Header;
    const size_t count = base.numberOfPicturesInFile;
    if (!count || count != custom.numberOfPicturesInFile || base.gm1Type != custom.gm1Type ||
        count > 66000 - images) return false;
    images += count;
  }

  // All layout/capacity checks precede the first native write. Replacers snapshot
  // these inherited originals and retain SetGm's reset/free semantics.
  for (auto& reservation : sheetReservations)
  {
    const auto& base = shcHeaderStart[reservation.baseGm];
    const size_t count = base.numberOfPicturesInFile;
    const int source = shcFirstImageStart[reservation.baseGm];
    shcHeaderStart[nextGm] = base;
    std::memcpy(&shcImageHeaderStart[nextImage], &shcImageHeaderStart[source], count * sizeof(ImageHeader));
    std::memcpy(&shcSizesStart[nextImage], &shcSizesStart[source], count * sizeof(int));
    std::memcpy(&shcOffsetStart[nextImage], &shcOffsetStart[source], count * sizeof(int));
    shcFirstImageStart[nextGm] = nextImage;
    reservation.slot = nextGm++;
    nextImage += static_cast<int>(count);
  }
  *shcGmCount = nextGm;
  return true;
}

// used for init
void __thiscall ColorAdapter::detouredLoadGmFiles(char* fileNameArray)
{
  if (initDone)
  {
    Log(LOG_WARNING, "[gmResourceModifier] Called init func again. Ignored.");
    return;
  }

  // call actual load gms func:
  shcTransformStruct = this;  // set ptr
  (*shcTransformStruct.*actualLoadGmsFunc)(fileNameArray);  // actual init everything

  // get gmheader, because they seem to be a member variable (currently from SHC 1.41)
  shcHeaderStart = (Gm1Header*)(((unsigned char*) shcTransformStruct) + 1308);

  // get the memory address of the processed data, because they seem to be a member variable (currently from SHC 1.41)
  shcGmDataAddr = *(int*)(((unsigned char*)shcTransformStruct) + 120);

  // needed to adjust image offset and sizes (although, the whole data is still kept in memory) (currently from SHC 1.41)
  shcOffsetModifierFlag = *(BOOL*)(((unsigned char*)shcTransformStruct) + 80);

  replacerVector.reserve(240);
  const bool reservationsAdmitted = AdmitReservedSheets(this);
  if (!reservationsAdmitted)
    Log(LOG_WARNING, "[gmResourceModifier]: Additional GM sheets could not be admitted; no reserved layout was installed.");

  initDone = true;  // needed here to ready resources

  // ready everything:

  // hardcoded to 240 headers currently
  size_t numberOfImages{ 1 };
  for (size_t i{ 0 }; i < 240; i++)
  {
    int pictureIndex{ static_cast<int>(shcHeaderStart[i].numberOfPicturesInFile < 1 ? -1 : numberOfImages) };
    replacerVector.emplace_back(i, pictureIndex);
    numberOfImages += shcHeaderStart[i].numberOfPicturesInFile;
  }

  Gm1ResourceManager::ReadyAllResources();

  for (const auto& [gmId, imageInGm, resourceId, imageInResource] : preRequests)
  {
    SetGm(gmId, imageInGm, resourceId, imageInResource); // need to call replace in order of definition
  }
  preRequests.clear();

  for (auto& reservation : sheetReservations)
  {
    if (reservationsAdmitted && !SetGm(reservation.slot, -1, reservation.resource->resourceId, -1))
      reservation.slot = -1;
    --reservation.resource->refCounter; // release the pending reservation pin
    reservation.resource = nullptr;
  }
}


void ColorAdapter::TransformRGB555ToRGB565(unsigned short* data, size_t byteSize, TransformType transformType)
{
  size_t loopCount{ byteSize / 2 };
  
  switch (transformType)
  {
    case TransformType::UNCOMPRESSED_2:
    {
      for (size_t i{ 0 }; i < loopCount; ++i)
      {
        if (data[i] != 0b1111100000011111)
        {
          data[i] = (data[i] & 0x1f) + ((data[i] & 0x3e0) >> 5) * 0x40 + ((data[i] & 0x7c00) >> 10) * 0x800;
        }
      }
      return;
    }

    case TransformType::COLOR_PALETTE:
    {
      for (size_t i{ 0 }; i < loopCount; ++i)
      {
        data[i] = (data[i] & 0x1f) + ((data[i] & 0xfc00) + (data[i] & 0x3e0)) * 2;
      }
      return;
    }

    default:
    {
      for (size_t i{ 0 }; i < loopCount; ++i)
      {
        data[i] = (data[i] & 0x1f) + ((data[i] & 0x3e0) >> 5) * 0x40 + ((data[i] & 0x7c00) >> 10) * 0x800;
      }
      return;
    }
  }
}

// TODO: test!!
void ColorAdapter::AdaptGm1Resource(Gm1Resource& resource)
{
  if (resource.done)
  {
    Log(LOG_WARNING, "[gmResourceModifier] Color adapt received already done resource. Skipping transform.");
  }

  if (*gamePixelFormat != PixelFormat::RGB_565) {
    return;
  }

  switch (resource.gm1Header->gm1Type)
  {
    case Gm1Type::ANIMATIONS:
      TransformRGB555ToRGB565((unsigned short*) resource.gm1Header->colorPalette, sizeof(resource.gm1Header->colorPalette), TransformType::COLOR_PALETTE);
      break;

    case Gm1Type::INTERFACE:
    case Gm1Type::FONT:
    case Gm1Type::TGX_CONST_SIZE:
      (*shcTransformStruct.*transformTgxFromRGB555ToRGB565)(resource.imageData.data(), resource.gm1Header->dataSize);
      break;

    case Gm1Type::NO_COMPRESSION_1:
      TransformRGB555ToRGB565((unsigned short*) resource.imageData.data(), resource.gm1Header->dataSize, TransformType::NORMAL);
      break;

    case Gm1Type::NO_COMPRESSION_2:
      TransformRGB555ToRGB565((unsigned short*) resource.imageData.data(), resource.gm1Header->dataSize, TransformType::UNCOMPRESSED_2);
      break;

    case Gm1Type::TILES_OBJECT:
    {
      for (size_t i{ 0 }; i < resource.gm1Header->numberOfPicturesInFile; ++i)
      {
        // offset needs to be relative to file for this, so offset adjust needs to be last step
        unsigned short* dataPtr{ (unsigned short*) (resource.imageData.data() + resource.imageOffset[i]) };
        TransformRGB555ToRGB565(dataPtr, 512, TransformType::NORMAL);
        if (resource.imageHeader[i].direction != 0)
        {
          // adjusted ptr value add for short
          (*shcTransformStruct.*transformTgxFromRGB555ToRGB565)(dataPtr + 256, resource.imageSizes[i] - 512);
        }
      }
      break;
    }

    default:
      Log(LOG_WARNING, "[gmResourceModifier] Color adapt received unknown resource type. Skipping transform.");
      break;
  }
}


void Gm1ResourceManager::LogHelper(const LogLevel level, const char* start, const char* filename, const char* error)
{
  static std::stringstream stream{};
  stream.clear();
  stream << start << "'" << filename << "': " << error;
  Log(level, stream.str().c_str());
}

// This is the owner's strict admission path for complete inherited artwork.
// Ordinary LoadGm1Resource keeps its established partial/replacement semantics.
static bool ValidateCompleteSheet(const Gm1Header& header,
  const std::vector<int>& offsets, const std::vector<int>& sizes,
  const std::vector<ImageHeader>& images, const std::vector<unsigned char>& data)
{
  if (header.dataSize > 16777216u) return false;
  const int pixelSize = header.gm1Type == Gm1Type::ANIMATIONS ? 1 : 2;
  for (size_t i = 0; i < images.size(); ++i)
  {
    const auto& image = images[i];
    if (!image.width || !image.height || image.width > 2048 || image.height > 2048 ||
        !sizes[i] || image.relativeDataPos || image.tileOffset || image.direction ||
        image.horizontalOffsetOfImage || image.buildingWidth || image.animatedColor)
      return false;
    size_t position = static_cast<size_t>(offsets[i]);
    const size_t end = position + static_cast<size_t>(sizes[i]);
    unsigned x = 0, y = 0;
    while (position < end)
    {
      const unsigned token = data[position++];
      const unsigned flag = token / 32, run = token % 32 + 1;
      if (flag == 4)
      {
        if (y >= image.height && !(token == 128 && end - position <= 2)) return false;
        ++y; x = 0;
        if (y > static_cast<unsigned>(image.height) + 3u) return false;
      }
      else
      {
        if (flag > 2 || y >= image.height || x + run > image.width) return false;
        x += run;
        if (flag == 0) position += run * pixelSize;
        else if (flag == 2) position += pixelSize;
        if (position > end) return false;
      }
    }
    if (y < image.height && !(y == image.height - 1 && x == image.width)) return false;
  }
  return true;
}

static bool HashCompleteSheet(const Gm1Header& header,
  const std::vector<int>& offsets, const std::vector<int>& sizes,
  const std::vector<ImageHeader>& images, const std::vector<unsigned char>& data,
  std::string& result)
{
  HCRYPTPROV provider = 0;
  HCRYPTHASH hash = 0;
  if (!CryptAcquireContext(&provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) return false;
  bool ok = CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash) != 0;
  auto feed = [&](const void* bytes, size_t length) {
    if (ok && length) ok = CryptHashData(hash, static_cast<const BYTE*>(bytes),
                                        static_cast<DWORD>(length), 0) != 0;
  };
  feed(&header, sizeof(header));
  feed(offsets.data(), offsets.size() * sizeof(int));
  feed(sizes.data(), sizes.size() * sizeof(int));
  feed(images.data(), images.size() * sizeof(ImageHeader));
  feed(data.data(), data.size());
  std::array<BYTE, 32> digest{};
  DWORD digestLength = static_cast<DWORD>(digest.size());
  if (ok) ok = CryptGetHashParam(hash, HP_HASHVAL, digest.data(), &digestLength, 0) != 0 &&
               digestLength == digest.size();
  if (hash) CryptDestroyHash(hash);
  CryptReleaseContext(provider, 0);
  if (!ok) return false;
  static constexpr char hex[] = "0123456789abcdef";
  result.clear(); result.reserve(64);
  for (BYTE byte : digest) { result.push_back(hex[byte >> 4]); result.push_back(hex[byte & 15]); }
  return true;
}

int Gm1ResourceManager::CreateGm1Resource(const char* filename,
  int expectedCount, int expectedType, std::string* sha256)
{
  
  int newId{ GetId() };
  if (newId < 0)
  {
    LogHelper(LOG_WARNING, "[gmResourceModifier]: Error while loading resource ", filename, "All ids used.");
    return -1;
  }

  // create temp
  std::unique_ptr<Gm1Header> gm1HeaderTemp;
  std::vector<int> imageOffsetTemp{};
  std::vector<int> imageSizesTemp{};
  std::vector<ImageHeader> imageHeaderTemp{};
  std::vector<unsigned char> imageDataTemp{};

  // source: https://stackoverflow.com/a/17338119
  std::ifstream ifs;
  //prepare ifs to throw if failbit gets set
  ifs.exceptions(std::ios::failbit | std::ios::badbit);
  try
  {
    ifs.open(filename, std::ios::binary);

    ifs.seekg(0, std::ios::end);
    const auto fileLength = ifs.tellg();
    ifs.seekg(0, std::ios::beg);
    if (fileLength < static_cast<std::streamoff>(sizeof(Gm1Header)))
      throw std::runtime_error("truncated GM1 header");

    gm1HeaderTemp = std::make_unique<Gm1Header>();
    ifs.read((char*) gm1HeaderTemp.get(), sizeof(Gm1Header));

    const size_t count = gm1HeaderTemp->numberOfPicturesInFile;
    // The game's image table has 66,000 entries. Check the file before any
    // count- or dataSize-controlled allocation, including wrapped counts.
    const uint64_t required = sizeof(Gm1Header) +
      static_cast<uint64_t>(count) * (2 * sizeof(int) + sizeof(ImageHeader)) +
      gm1HeaderTemp->dataSize;
    if (!count || count > 66000 || required > static_cast<uint64_t>(fileLength) ||
        (sha256 && (expectedCount <= 0 || expectedType <= 0 ||
                    count != static_cast<size_t>(expectedCount) ||
                    static_cast<int>(gm1HeaderTemp->gm1Type) != expectedType ||
                    required != static_cast<uint64_t>(fileLength))))
      throw std::runtime_error("invalid GM1 count or payload length");
    const int numberOfPictures{ static_cast<int>(count) };

    imageOffsetTemp.resize(numberOfPictures);
    ifs.read((char*) imageOffsetTemp.data(), numberOfPictures * sizeof(int));

    imageSizesTemp.resize(numberOfPictures);
    ifs.read((char*) imageSizesTemp.data(), numberOfPictures * sizeof(int));

    imageHeaderTemp.resize(numberOfPictures);
    ifs.read((char*) imageHeaderTemp.data(), numberOfPictures * sizeof(ImageHeader));

    for (int i = 0; i < numberOfPictures; ++i)
    {
      if (imageOffsetTemp[i] < 0 || imageSizesTemp[i] < 0 ||
          static_cast<uint64_t>(imageOffsetTemp[i]) + imageSizesTemp[i] > gm1HeaderTemp->dataSize)
        throw std::runtime_error("GM1 image data lies outside its payload");
    }

    imageDataTemp.resize(gm1HeaderTemp->dataSize);
    ifs.read((char*) imageDataTemp.data(), gm1HeaderTemp->dataSize);

    if (sha256 && (!ValidateCompleteSheet(*gm1HeaderTemp, imageOffsetTemp,
                                          imageSizesTemp, imageHeaderTemp, imageDataTemp) ||
                   !HashCompleteSheet(*gm1HeaderTemp, imageOffsetTemp,
                                      imageSizesTemp, imageHeaderTemp, imageDataTemp, *sha256)))
      throw std::runtime_error("invalid complete GM1 animation or content digest");

    ifs.close();  // closing manually to trigger error here and not later
  }
  catch (const std::exception& e)
  {
    LogHelper(LOG_WARNING, "[gmResourceModifier]: Error while loading resource ", filename, e.what());
    ReturnId(newId);
    return -1;
  }

  auto [it, success]{ resources.try_emplace(newId) };
  if (!success)
  {
    LogHelper(LOG_WARNING, "[gmResourceModifier]: Error while loading resource ", filename, "Failed to place resource in map.");
    ReturnId(newId);
    return -1;
  }
  
  Gm1Resource& resource{ it->second };
  resource.gm1Header.swap(gm1HeaderTemp);
  resource.imageHeader.swap(imageHeaderTemp);
  resource.imageSizes.swap(imageSizesTemp);
  resource.imageOffset.swap(imageOffsetTemp);
  resource.imageData.swap(imageDataTemp);

  if (initDone && !Gm1ResourceManager::ReadyResource(resource))
  {
    LogHelper(LOG_WARNING, "[gmResourceModifier]: Error while loading resource ", filename, "Failed to ready resource.");
    ReturnId(newId);
    resources.erase(newId);
    return -1;
  }

  resource.resourceId = newId;
  return newId;
}

bool Gm1ResourceManager::ReadyResource(Gm1Resource& resource)
{
  if (!initDone)
  {
    Log(LOG_WARNING, "[gmResourceModifier]: Tried to ready resource before init.");
    return false;
  }

  if (resource.done)
  {
    Log(LOG_WARNING, "[gmResourceModifier]: Tried to ready resource twice.");
    return false;
  }

  ColorAdapter::AdaptGm1Resource(resource); // needs relative to object offsets

  // adjust offsets to SHC memory
  for (size_t i{ 0 }; i < resource.gm1Header->numberOfPicturesInFile; ++i)
  {
    resource.imageOffset[i] = (int)resource.imageData.data() + resource.imageOffset[i] - shcGmDataAddr;

    /* 
    // TODO -> still broken
    // tries to recreate stuff used by the game, but without resource copy -> it will not save memory space
    // I do not really know what this does... maybe it really is intended to save memory space
    // for now, it seems to work without, and as long as the copied data is not reduced to the important parts, it does not save space anyway
    if (shcOffsetModifierFlag == 0)
    {
      if ((resource.imageHeader[i].animatedColor & 4) != 0)
      {
        resource.imageOffset[i] = resource.imageOffset[i - 1];
        resource.imageSizes[i] = resource.imageSizes[resource.imageHeader[i].relativeDataPos + i];
      }
    }
    else if (resource.imageHeader[i].animatedColor != 0)
    {
      resource.imageOffset[i] = (resource.imageHeader[i].animatedColor & 4) != 0 ? resource.imageOffset[i - 1] 
        : resource.imageOffset[resource.imageHeader[i].relativeDataPos + i];
      resource.imageSizes[i] = resource.imageSizes[resource.imageHeader[i].relativeDataPos + i];
    }
    */
  }


  resource.done = true;
  return true;
}


bool Gm1ResourceManager::FreeGm1Resource(int resourceId)
{
  Gm1Resource* resPtr{ Gm1ResourceManager::GetResource(resourceId) };
  if (!resPtr)
  {
    return false;
  }
  Gm1Resource& resource{ *resPtr };

  if (resource.refCounter > 0)
  {
    Log(LOG_WARNING, "[gmResourceModifier]: FreeGm1Resource: Resource in use. Can not free.");
    return false;
  }

  resources.erase(resourceId);
  ReturnId(resourceId);
  return true;
}

int Gm1ResourceManager::GetId()
{
  int newId{ 0 };
  if (!freeIds.empty())
  {
    newId = freeIds.back();
    freeIds.pop_back();
  }
  else if (idGiver != INT_MAX)
  {
    newId = idGiver++;
  }
  else
  {
    newId = -1;
  }

  return newId;
}

void Gm1ResourceManager::ReturnId(int id)
{
  freeIds.push_back(id);
}

Gm1Resource* Gm1ResourceManager::GetResource(int resourceId)
{
  auto iter{ resources.find(resourceId) };
  if (iter == resources.end())
  {
    Log(LOG_WARNING, "[gmResourceModifier]: GetResource: Invalid ResourceID given.");
    return nullptr;
  }
  return &iter->second;
}

void Gm1ResourceManager::ReadyAllResources()
{
  for (auto& [resourceId, resource] : resources)
  {
    Gm1ResourceManager::ReadyResource(resource);
  }
}


void Replacer::readyOrigResource()
{
  if (origResource)
  {
    return; // already done
  }

  origResource = std::make_unique<Gm1Resource>();
  origResource->gm1Header = std::make_unique<Gm1Header>();
  std::memcpy(origResource->gm1Header.get(), &shcHeaderStart[gmIndex], sizeof(Gm1Header));

  int numberOfPictures{ static_cast<int>(origResource->gm1Header->numberOfPicturesInFile) };

  if (numberOfPictures < 1)
  {
    return; // we are done here, there is nothing in this index
  }

  // copy data
  origResource->imageOffset.resize(numberOfPictures);
  std::memcpy(origResource->imageOffset.data(), &shcOffsetStart[firstImageIndex], numberOfPictures * sizeof(int));

  origResource->imageSizes.resize(numberOfPictures);
  std::memcpy(origResource->imageSizes.data(), &shcSizesStart[firstImageIndex], numberOfPictures * sizeof(int));

  origResource->imageHeader.resize(numberOfPictures);
  std::memcpy(origResource->imageHeader.data(), &shcImageHeaderStart[firstImageIndex], numberOfPictures * sizeof(ImageHeader));

  origResource->done = true;
}


// no error checks here
void Replacer::copyToShc(Gm1Resource& resource, int imageIndex, int resourceImageIndex)
{
  if (imageIndex == -1)
  {
    int numberOfPictures{ static_cast<int>(resource.gm1Header->numberOfPicturesInFile) };

    std::memcpy(&shcHeaderStart[gmIndex], resource.gm1Header.get(), sizeof(Gm1Header));

    int otherImagesReplacedCounter{ 0 };
    for (const auto& [imageI, resourcePair] : imageReplaced)
    {
      // index of image
      --(Gm1ResourceManager::GetResource(resourcePair.first)->refCounter);
      ++otherImagesReplacedCounter;
    }
    imageReplaced.clear();

    if (mainReplacingResource)
    {
      mainReplacingResource->refCounter -= (numberOfPictures + 1 - otherImagesReplacedCounter); // + 1 for header
    }

    if (origResource.get() != &resource)
    {
      resource.refCounter += (numberOfPictures + 1);  // + 1 for header
      mainReplacingResource = &resource;
    }
    else
    {
      mainReplacingResource = nullptr;
    }
    
    std::memcpy(&shcOffsetStart[firstImageIndex], resource.imageOffset.data(), numberOfPictures * sizeof(int));
    std::memcpy(&shcSizesStart[firstImageIndex], resource.imageSizes.data(), numberOfPictures * sizeof(int));
    std::memcpy(&shcImageHeaderStart[firstImageIndex], resource.imageHeader.data(), numberOfPictures * sizeof(ImageHeader));

    return;
  }

  auto iter{ imageReplaced.find(imageIndex) };
  if (iter == imageReplaced.end())
  {
    if (mainReplacingResource)
    {
      --(mainReplacingResource->refCounter);
    }

    if (origResource.get() != &resource)
    {
      imageReplaced.try_emplace(imageIndex, resource.resourceId, resourceImageIndex);
    }
  }
  else
  {
    --(Gm1ResourceManager::GetResource(iter->second.first)->refCounter);
    if (origResource.get() != &resource)
    {
      iter->second = std::pair<int, int>(resource.resourceId, resourceImageIndex);
    }
    else
    {
      imageReplaced.erase(iter);
    }
  }

  if (origResource.get() != &resource)
  {
    ++(resource.refCounter);
  }

  shcOffsetStart[firstImageIndex + imageIndex] = resource.imageOffset[resourceImageIndex];
  shcSizesStart[firstImageIndex + imageIndex] = resource.imageSizes[resourceImageIndex];
  shcImageHeaderStart[firstImageIndex + imageIndex] = resource.imageHeader[resourceImageIndex];
}


/* export C */

extern "C" __declspec(dllexport) int __stdcall ReserveGm(int baseGm, int resourceId)
{
  if (initDone || baseGm < 0 || baseGm >= 240 || sheetReservations.size() >= 240) return -1;
  Gm1Resource* resource = Gm1ResourceManager::GetResource(resourceId);
  if (!resource || !resource->gm1Header->numberOfPicturesInFile ||
      resource->gm1Header->numberOfPicturesInFile > 66000) return -1;
  sheetReservations.push_back({ baseGm, resource });
  ++resource->refCounter;
  return static_cast<int>(sheetReservations.size() - 1);
}

extern "C" __declspec(dllexport) int __stdcall GetReservedGm(int reservation)
{
  if (!initDone || reservation < 0 || static_cast<size_t>(reservation) >= sheetReservations.size()) return -1;
  return sheetReservations[reservation].slot;
}

extern "C" __declspec(dllexport) int __stdcall LoadGm1Resource(const char* filepath)
{
  return Gm1ResourceManager::CreateGm1Resource(filepath);
}


extern "C" __declspec(dllexport) bool __stdcall SetGm(int gmID, int imageInGm, int resourceId, int imageInResource)
{
  if (!initDone)
  {
    static bool warnPrinted{ false };
    if (!warnPrinted)
    {
      Log(LOG_WARNING, "[gmResourceModifier]: Important: 'SetGm' Requests before the first Init will always return true.");
      warnPrinted = true;
    }

    preRequests.emplace_back(gmID, imageInGm, resourceId, imageInResource);
    return true;
  }

  if (gmID < 0 || gmID > 239) // hardcoded currently
  {
    Log(LOG_WARNING, "[gmResourceModifier]: SetGm: Invalid GmID given.");
    return false;
  }

  Replacer& currentReplacer{ replacerVector[gmID] };
  currentReplacer.readyOrigResource();

  if (imageInGm >= 0 && imageInGm > currentReplacer.origResource->gm1Header->numberOfPicturesInFile - 1)
  {
    Log(LOG_WARNING, "[gmResourceModifier]: SetGm: Invalid SHC GM1 picture number given.");
    return false;
  }

  if (resourceId < 0) // indicator for resetting -> imageInResource ignored
  {
    if (imageInGm < 0)
    {
      currentReplacer.copyToShc(*currentReplacer.origResource, -1, -1); // reset everything
    }
    else
    {
      currentReplacer.copyToShc(*currentReplacer.origResource, imageInGm, imageInGm); // reset an image
    }
    return true;
  }

  Gm1Resource* resPtr{ Gm1ResourceManager::GetResource(resourceId) };
  if (!resPtr)
  {
    return false;
  }
  Gm1Resource& resource{ *resPtr };

  if (currentReplacer.origResource->gm1Header->gm1Type != resource.gm1Header->gm1Type)
  {
    Gm1Type origType{ currentReplacer.origResource->gm1Header->gm1Type };
    Gm1Type resourceType{ resource.gm1Header->gm1Type };
    bool origIsTgxType{ origType == Gm1Type::INTERFACE || origType == Gm1Type::TGX_CONST_SIZE || origType == Gm1Type::FONT };
    bool resourceIsTgxType{ resourceType == Gm1Type::INTERFACE || resourceType == Gm1Type::TGX_CONST_SIZE || resourceType == Gm1Type::FONT };

    if (!(origIsTgxType && resourceIsTgxType))
    {
      Log(LOG_WARNING, "[gmResourceModifier]: SetGm: Resource can not be replaced. Incompatible types.");
      return false;
    }
    Log(LOG_DEBUG, "[gmResourceModifier]: SetGm: Replacing TGX data of gm with resource of different type.");
  }

  if (imageInResource < 0)
  {
    if (currentReplacer.origResource->gm1Header->numberOfPicturesInFile != resource.gm1Header->numberOfPicturesInFile)
    {
      Log(LOG_WARNING, "[gmResourceModifier]: SetGm: Complete resource replace can only happen if number of images equal.");
      return false;
    }

    currentReplacer.copyToShc(resource, -1, -1);
    return true;
  }

  if (imageInResource > resource.gm1Header->numberOfPicturesInFile - 1)
  {
    Log(LOG_WARNING, "[gmResourceModifier]: SetGm: Invalid resource number given.");
    return false;
  }
  if (imageInGm < 0)
  {
    Log(LOG_WARNING, "[gmResourceModifier]: SetGm: Missing number of image to replace.");
    return false;
  }

  currentReplacer.copyToShc(resource, imageInGm, imageInResource);
  return true;
}


extern "C" __declspec(dllexport) bool __stdcall FreeGm1Resource(int resourceId)
{
  return Gm1ResourceManager::FreeGm1Resource(resourceId);
}


/* export LUA */

extern "C" __declspec(dllexport) int __cdecl lua_ReserveGm(lua_State* L)
{
  lua_Integer base = luaL_checkinteger(L, 1);
  lua_Integer resource = luaL_checkinteger(L, 2);
  // Lua integers are wider than this native ABI. Reject instead of wrapping IDs.
  if (base < 0 || base >= 240 || resource < 0 || resource > INT_MAX)
    lua_pushinteger(L, -1);
  else
    lua_pushinteger(L, ReserveGm(static_cast<int>(base), static_cast<int>(resource)));
  return 1;
}

extern "C" __declspec(dllexport) int __cdecl lua_GetReservedGm(lua_State* L)
{
  lua_Integer token = luaL_checkinteger(L, 1);
  lua_pushinteger(L, token < 0 || token >= 240 ? -1 : GetReservedGm(static_cast<int>(token)));
  return 1;
}

extern "C" __declspec(dllexport) int __cdecl lua_LoadGm1Resource(lua_State * L)
{
  int n{ lua_gettop(L) };    /* number of arguments */
  if (n != 1)
  {
    luaL_error(L, "[gmResourceModifier]: lua_LoadGm1Resource: Invalid number of args.");
  }

  if (!lua_isstring(L, 1))
  {
    luaL_error(L, "[gmResourceModifier]: lua_LoadGm1Resource: Wrong input fields.");
  }

  int res{ Gm1ResourceManager::CreateGm1Resource(lua_tostring(L, 1)) };
  lua_pushinteger(L, res);
  return 1;
}


extern "C" __declspec(dllexport) int __cdecl lua_SetGm(lua_State * L)
{
  int n{ lua_gettop(L) };    /* number of arguments */
  if (n != 4)
  {
    luaL_error(L, "[gmResourceModifier]: lua_SetGm: Invalid number of args.");
  }

  if (!(lua_isinteger(L, 1) && lua_isinteger(L, 2) && lua_isinteger(L, 3) && lua_isinteger(L, 4)))
  {
    luaL_error(L, "[gmResourceModifier]: lua_SetGm: Wrong input fields.");
  }

  bool res{ SetGm(lua_tointeger(L, 1), lua_tointeger(L, 2), lua_tointeger(L, 3), lua_tointeger(L, 4)) };
  lua_pushboolean(L, res);
  return 1;
}


extern "C" __declspec(dllexport) int __cdecl lua_FreeGm1Resource(lua_State * L)
{
  int n{ lua_gettop(L) };    /* number of arguments */
  if (n != 1)
  {
    luaL_error(L, "[gmResourceModifier]: lua_FreeGm1Resource: Invalid number of args.");
  }

  if (!lua_isinteger(L, 1))
  {
    luaL_error(L, "[gmResourceModifier]: lua_FreeGm1Resource: Wrong input fields.");
  }

  bool res{ FreeGm1Resource(lua_tointeger(L, 1)) };
  lua_pushboolean(L, res);
  return 1;
}

extern "C" __declspec(dllexport) int __cdecl lua_LoadCompleteGm1Resource(lua_State* L)
{
  if (lua_gettop(L) != 3 || lua_type(L, 1) != LUA_TSTRING ||
      !lua_isinteger(L, 2) || !lua_isinteger(L, 3))
    return luaL_error(L, "[gmResourceModifier]: LoadCompleteGm1Resource expects path, image count and GM1 type");
  const lua_Integer count = lua_tointeger(L, 2), type = lua_tointeger(L, 3);
  if (count < 1 || count > 66000 || type < 1 || type > 7)
    return luaL_error(L, "[gmResourceModifier]: invalid complete GM1 layout");
  std::string digest;
  const int id = Gm1ResourceManager::CreateGm1Resource(lua_tostring(L, 1),
    static_cast<int>(count), static_cast<int>(type), &digest);
  lua_pushinteger(L, id);
  if (id >= 0) lua_pushlstring(L, digest.data(), digest.size());
  else lua_pushnil(L);
  return 2;
}
