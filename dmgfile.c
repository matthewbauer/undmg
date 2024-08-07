/*
 *  dmgfile.c
 *  libdmg-hfsplus
 */

#include <bzlib.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include <lzfse.h>
#include <lzma.h>

#include "dmg.h"
#include "dmgfile.h"

static void cacheRun(DMG *dmg, BLKXTable *blkx, int run) {
  size_t bufferSize;
  z_stream zstrm;
  bz_stream bzstrm;
  lzma_stream lzmastrm = LZMA_STREAM_INIT;
  void *inBuffer;
  int ret;
  size_t have;

  if (dmg->runData) {
    free(dmg->runData);
  }

  bufferSize = SECTOR_SIZE * blkx->runs[run].sectorCount;

  dmg->runData = (void *)malloc(bufferSize);
  inBuffer = (void *)malloc(bufferSize);
  memset(dmg->runData, 0, bufferSize);

  ASSERT(dmg->dmg->seek(dmg->dmg,
                        blkx->dataStart + blkx->runs[run].compOffset) == 0,
         "fseeko");

  switch (blkx->runs[run].type) {
  case BLOCK_ZLIB:
    zstrm.zalloc = Z_NULL;
    zstrm.zfree = Z_NULL;
    zstrm.opaque = Z_NULL;
    zstrm.avail_in = 0;
    zstrm.next_in = Z_NULL;

    ASSERT(inflateInit(&zstrm) == Z_OK, "inflateInit");

    ASSERT((zstrm.avail_in = dmg->dmg->read(dmg->dmg, inBuffer,
                                            blkx->runs[run].compLength)) ==
               blkx->runs[run].compLength,
           "fread");
    zstrm.next_in = (unsigned char *)inBuffer;

    do {
      zstrm.avail_out = bufferSize;
      zstrm.next_out = (unsigned char *)dmg->runData;
      ASSERT((ret = inflate(&zstrm, Z_NO_FLUSH)) != Z_STREAM_ERROR,
             "inflate/Z_STREAM_ERROR");
      if (ret != Z_OK && ret != Z_BUF_ERROR && ret != Z_STREAM_END) {
        ASSERT(FALSE, "inflate");
      }
      have = bufferSize - zstrm.avail_out;
    } while (zstrm.avail_out == 0);

    ASSERT(inflateEnd(&zstrm) == Z_OK, "inflateEnd");
    break;
  case BLOCK_BZ2:
    bzstrm.bzalloc = NULL;
    bzstrm.bzfree = NULL;
    bzstrm.opaque = NULL;
    bzstrm.avail_in = 0;
    bzstrm.next_in = NULL;

    ASSERT(BZ2_bzDecompressInit(&bzstrm, 0, 0) == BZ_OK, "bzDecompressInit");

    ASSERT((zstrm.avail_in = dmg->dmg->read(dmg->dmg, inBuffer,
                                            blkx->runs[run].compLength)) ==
               blkx->runs[run].compLength,
           "fread");
    bzstrm.next_in = inBuffer;

    do {
      bzstrm.avail_out = bufferSize;
      bzstrm.next_out = dmg->runData;
      ASSERT((ret = BZ2_bzDecompress(&bzstrm)) != BZ_DATA_ERROR,
             "bzDecompress/Z_STREAM_ERROR");
      if (ret != BZ_OK && ret != BZ_DATA_ERROR && ret != BZ_STREAM_END) {
        ASSERT(FALSE, "decompress");
      }
      have = bufferSize - bzstrm.avail_out;
    } while (bzstrm.avail_out == 0);

    ASSERT(BZ2_bzDecompressEnd(&bzstrm) == BZ_OK, "bzDecompressEnd");
    break;
  case BLOCK_LZFSE:
    have = lzfse_decode_buffer(dmg->runData, bufferSize, inBuffer, bufferSize, NULL);

    break;
  case BLOCK_LZMA:
    ASSERT(lzma_stream_decoder(&lzmastrm, UINT64_MAX, 0) == LZMA_OK, "lzma_stream_decoder");

    ASSERT(
        (lzmastrm.avail_in = dmg->dmg->read(dmg->dmg, inBuffer,
                                            blkx->runs[run].compLength)) ==
            blkx->runs[run].compLength,
        "fread");
    lzmastrm.next_in = inBuffer;

    do {
      lzmastrm.avail_out = bufferSize;
      lzmastrm.next_out = dmg->runData;
      ret = lzma_code(&lzmastrm, LZMA_RUN);
      if (ret != LZMA_OK && ret != LZMA_STREAM_END) {
        ASSERT(FALSE, "lzma_code");
      }
      have = bufferSize - lzmastrm.avail_out;
    } while (lzmastrm.avail_out == 0);

    lzma_end(&lzmastrm);
    break;
  case BLOCK_RAW:
    ASSERT((have = dmg->dmg->read(dmg->dmg, dmg->runData,
                                  blkx->runs[run].compLength)) ==
               blkx->runs[run].compLength,
           "fread");
    break;
  case BLOCK_IGNORE:
    break;
  case BLOCK_COMMENT:
    break;
  case BLOCK_TERMINATOR:
    break;
  default:
    break;
  }

  dmg->runStart =
      (blkx->runs[run].sectorStart + blkx->firstSectorNumber) * SECTOR_SIZE;
  dmg->runEnd = dmg->runStart + (blkx->runs[run].sectorCount * SECTOR_SIZE);
}

static void cacheOffset(DMG *dmg, off_t location) {
  int i;
  int j;
  uint64_t sector;

  sector = (uint64_t)(location / SECTOR_SIZE);

  for (i = 0; i < dmg->numBLKX; i++) {
    if (sector >= dmg->blkx[i]->firstSectorNumber &&
        sector <
            (dmg->blkx[i]->firstSectorNumber + dmg->blkx[i]->sectorCount)) {
      for (j = 0; j < dmg->blkx[i]->blocksRunCount; j++) {
        if (sector >= (dmg->blkx[i]->firstSectorNumber +
                       dmg->blkx[i]->runs[j].sectorStart) &&
            sector < (dmg->blkx[i]->firstSectorNumber +
                      dmg->blkx[i]->runs[j].sectorStart +
                      dmg->blkx[i]->runs[j].sectorCount)) {
          cacheRun(dmg, dmg->blkx[i], j);
        }
      }
    }
  }
}

static int dmgFileRead(io_func *io, off_t location, size_t size, void *buffer) {
  DMG *dmg;
  size_t toRead;

  dmg = (DMG *)io->data;

  location += dmg->offset;

  if (size == 0) {
    return TRUE;
  }

  if (location < dmg->runStart || location >= dmg->runEnd) {
    cacheOffset(dmg, location);
  }

  if ((location + size) > dmg->runEnd) {
    toRead = dmg->runEnd - location;
  } else {
    toRead = size;
  }

  memcpy(buffer, (void *)((uint8_t *)dmg->runData +
                          (uint32_t)(location - dmg->runStart)),
         toRead);
  size -= toRead;
  location += toRead;
  buffer = (void *)((uint8_t *)buffer + toRead);

  if (size > 0) {
    return dmgFileRead(io, location - dmg->offset, size, buffer);
  } else {
    return TRUE;
  }
}

static int dmgFileWrite(io_func *io, off_t location, size_t size,
                        void *buffer) {
  fprintf(stderr, "Error: writing to DMGs is not supported (impossible to "
                  "achieve with compressed images and retain asr multicast "
                  "ordering).\n");
  return FALSE;
}

static void closeDmgFile(io_func *io) {
  DMG *dmg;

  dmg = (DMG *)io->data;

  if (dmg->runData) {
    free(dmg->runData);
  }

  free(dmg->blkx);
  releaseResources(dmg->resources);
  dmg->dmg->close(dmg->dmg);
  free(dmg);
  free(io);
}

io_func *openDmgFile(AbstractFile *abstractIn) {
  off_t fileLength;
  UDIFResourceFile resourceFile;
  DMG *dmg;
  ResourceData *blkx;
  ResourceData *curData;
  int i;

  io_func *toReturn;

  if (abstractIn == NULL) {
    return NULL;
  }

  fileLength = abstractIn->getLength(abstractIn);
  abstractIn->seek(abstractIn, fileLength - sizeof(UDIFResourceFile));
  readUDIFResourceFile(abstractIn, &resourceFile);

  dmg = (DMG *)malloc(sizeof(DMG));
  dmg->dmg = abstractIn;
  dmg->resources = readResources(abstractIn, &resourceFile);
  dmg->numBLKX = 0;

  blkx = (getResourceByKey(dmg->resources, "blkx"))->data;

  curData = blkx;
  while (curData != NULL) {
    dmg->numBLKX++;
    curData = curData->next;
  }

  dmg->blkx = (BLKXTable **)malloc(sizeof(BLKXTable *) * dmg->numBLKX);

  i = 0;
  while (blkx != NULL) {
    dmg->blkx[i] = (BLKXTable *)(blkx->data);
    i++;
    blkx = blkx->next;
  }

  dmg->offset = 0;

  dmg->runData = NULL;
  cacheOffset(dmg, 0);

  toReturn = (io_func *)malloc(sizeof(io_func));

  toReturn->data = dmg;
  toReturn->read = &dmgFileRead;
  toReturn->write = &dmgFileWrite;
  toReturn->close = &closeDmgFile;

  return toReturn;
}

io_func *openDmgFilePartition(AbstractFile *abstractIn, int partition) {
  io_func *toReturn;
  Partition *partitions;
  uint8_t ddmBuffer[SECTOR_SIZE];
  DriverDescriptorRecord *ddm;
  int numPartitions;
  int i;
  unsigned int BlockSize;

  toReturn = openDmgFile(abstractIn);

  if (toReturn == NULL) {
    return NULL;
  }

  toReturn->read(toReturn, 0, SECTOR_SIZE, ddmBuffer);
  ddm = (DriverDescriptorRecord *)ddmBuffer;
  flipDriverDescriptorRecord(ddm, FALSE);
  BlockSize = ddm->sbBlkSize;

  partitions = (Partition *)malloc(BlockSize);
  toReturn->read(toReturn, BlockSize, BlockSize, partitions);
  flipPartitionMultiple(partitions, FALSE, FALSE, BlockSize);
  numPartitions = partitions->pmMapBlkCnt;
  partitions = (Partition *)realloc(partitions, numPartitions * BlockSize);
  toReturn->read(toReturn, BlockSize, numPartitions * BlockSize, partitions);
  flipPartition(partitions, FALSE, BlockSize);

  if (partition >= 0) {
    ((DMG *)toReturn->data)->offset =
        partitions[partition].pmPyPartStart * BlockSize;
  } else {
    for (i = 0; i < numPartitions; i++) {
      if (strcmp((char *)partitions->pmParType, "Apple_HFSX") == 0 ||
          strcmp((char *)partitions->pmParType, "Apple_HFS") == 0) {
        ((DMG *)toReturn->data)->offset = partitions->pmPyPartStart * BlockSize;
        break;
      }
      partitions = (Partition *)((uint8_t *)partitions + BlockSize);
    }
  }

  return toReturn;
}
